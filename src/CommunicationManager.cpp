#include "CommunicationManager.h"
#include "CommunicationNodeBase.h"
#include "SerialCommNode.h"
#include "UdpCommNode.h"
#include <QTimer>
#include "TcpCommNode.h"
#include "ModbusNode.h" // provides ModbusRegisterItem
#include "PlcCommNode.h"
#include "FinsCommNode.h" // VM 4.4 B5-1：Omron FINS/TCP + FINS/UDP（寄存器表同 Modbus 口径）
#include "ReceiveEvent.h"
#include "SendEvent.h"
#include "GlobalTriggerManager.h"
#include "HeartbeatManager.h"
#include "AppLog.h"
#include <QJsonDocument>
#include <QFile>
#include <QSaveFile>
#include <QJsonArray>
#include <QDataStream>
#include <QMutexLocker>

CommunicationManager *CommunicationManager::instance()
{
    // 静态局部变量初始化在 C++11 后线程安全，且指针常驻避免析构顺序问题
    static CommunicationManager *inst = new CommunicationManager();
    return inst;
}

CommunicationManager::CommunicationManager(QObject *parent)
    : QObject(parent)
{
}

CommunicationManager::~CommunicationManager()
{
    // 同 fromJson：锁内只清容器，节点的 closeConnection 放到锁外
    QList<CommunicationNodeBase *> nodes;
    QList<ReceiveEvent *> recvEvents;
    QList<SendEvent *> sendEvents;
    {
        QMutexLocker locker(&m_mutex);
        nodes = m_devices.values();
        m_devices.clear();
        recvEvents = m_receiveEvents.values();
        m_receiveEvents.clear();
        sendEvents = m_sendEvents.values();
        m_sendEvents.clear();
    }

    for (auto *node : nodes) {
        node->closeConnection();
        delete node;
    }
    for (auto *ev : recvEvents) delete ev;
    for (auto *ev : sendEvents) delete ev;
}

// ---- Device Management ----

QStringList CommunicationManager::deviceNames() const
{
    QMutexLocker locker(&m_mutex);
    return m_deviceInfos.keys();
}

CommDeviceInfo CommunicationManager::deviceInfo(const QString &name) const
{
    QMutexLocker locker(&m_mutex);
    return m_deviceInfos.value(name);
}

bool CommunicationManager::hasDevice(const QString &name) const
{
    QMutexLocker locker(&m_mutex);
    return m_deviceInfos.contains(name);
}

bool CommunicationManager::addDevice(const QString &name, const QString &type, const QJsonObject &config)
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_deviceInfos.contains(name)) {
            VFP_DEBUG << "Device already exists:" << name;
            return false;
        }

        CommDeviceInfo info;
        info.name = name;
        info.type = type;
        info.config = config;
        info.isConnected = false;
        m_deviceInfos[name] = info;
    }

    // P3-12（2026-10-25 复核）：未知设备类型时 createDeviceNode 什么都不建，而这里照样
    // emit + return true ⇒ 方案里留下一个永远打不开的"幽灵设备"（列表里有、点开报错）。
    // 建不出节点就回滚登记并返回 false，让调用方看见失败。
    if (!createDeviceNode(name, type, config)) {
        {
            QMutexLocker locker(&m_mutex);
            m_deviceInfos.remove(name);
        }
        VFP_DEBUG << "Communication device NOT added (unknown type or node create failed):"
                  << name << "type:" << type;
        return false;
    }
    emit deviceAdded(name, type);

    VFP_DEBUG << "Communication device added:" << name << "type:" << type;
    return true;
}

bool CommunicationManager::updateDeviceConfig(const QString &name, const QJsonObject &config)
{
    QMutexLocker locker(&m_mutex);
    auto it = m_deviceInfos.find(name);
    if (it == m_deviceInfos.end()) return false;
    it.value().config = config;
    return true;
}

bool CommunicationManager::removeDevice(const QString &name, bool cleanupReferences)
{
    CommunicationNodeBase *node = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_deviceInfos.contains(name)) return false;
        node = m_devices.value(name, nullptr);
        m_devices.remove(name);
        m_deviceInfos.remove(name);
    }

    // 锁外关闭节点：closeConnection 会同步 emit connectionClosed，
    // 其槽函数需要重新获取 m_mutex（见头文件中的约束说明）
    if (node) {
        node->closeConnection();
        node->deleteLater();
    }

    // 清理该设备的帧组装缓冲与定时器
    if (QTimer *t = m_frameTimers.take(name)) {
        t->stop();
        t->deleteLater();
    }
    m_frameBuffers.remove(name);

    // 仅「用户主动删除」才清理按设备名索引的引用（2026-11-10 审核）。
    // 改配置时的 remove+add 同名重建走 cleanupReferences=false，否则每改一次
    // 设备参数都会连带删光该设备的事件/心跳/响应配置。
    if (cleanupReferences) {
        HeartbeatManager::instance()->unregisterHeartbeat(name);

        QStringList rxIds, txIds;
        {
            QMutexLocker locker(&m_mutex);
            for (auto it = m_receiveEvents.constBegin(); it != m_receiveEvents.constEnd(); ++it)
                if (it.value() && it.value()->deviceName() == name) rxIds << it.key();
            for (auto it = m_sendEvents.constBegin(); it != m_sendEvents.constEnd(); ++it)
                if (it.value() && it.value()->deviceName() == name) txIds << it.key();
        }
        // 锁外调用：removeXxxEvent 内部会再次取锁
        for (const QString &id : rxIds) removeReceiveEvent(id);
        for (const QString &id : txIds) removeSendEvent(id);

        // 响应配置：只把指向该设备的 device 清空，保留内容/延时/使能等其余字段，
        // 用户只需重选设备，不必重填整页。
        QJsonObject resp = m_responseConfig;
        const QStringList sections = {QStringLiteral("schemeLoad"),
                                      QStringLiteral("flowControl"),
                                      QStringLiteral("cameraResponse")};
        for (const QString &key : sections) {
            QJsonObject sec = resp.value(key).toObject();
            if (sec.value(QStringLiteral("device")).toString() == name) {
                sec[QStringLiteral("device")] = QString();
                resp[key] = sec;
            }
        }
        m_responseConfig = resp;
    }

    emit deviceRemoved(name);
    return true;
}

bool CommunicationManager::renameDevice(const QString &oldName, const QString &newName)
{
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty() || trimmed == oldName) return false;

    CommunicationNodeBase *node = nullptr;
    QList<ReceiveEvent *> rxEvents;
    QList<SendEvent *> txEvents;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_deviceInfos.contains(oldName) || m_deviceInfos.contains(trimmed))
            return false;
        node = m_devices.take(oldName);
        m_devices[trimmed] = node;
        CommDeviceInfo info = m_deviceInfos.take(oldName);
        info.name = trimmed;
        m_deviceInfos[trimmed] = info;
        // 锁内只取指针快照；事件字段的写入放到锁外（与 setEnabled 同款既有口径）
        for (ReceiveEvent *ev : m_receiveEvents)
            if (ev && ev->deviceName() == oldName) rxEvents.append(ev);
        for (SendEvent *ev : m_sendEvents)
            if (ev && ev->deviceName() == oldName) txEvents.append(ev);
    }

    if (node) node->setDeviceName(trimmed);
    for (ReceiveEvent *ev : rxEvents) ev->setDeviceName(trimmed);
    for (SendEvent *ev : txEvents) ev->setDeviceName(trimmed);
    HeartbeatManager::instance()->renameDevice(oldName, trimmed);

    // 响应配置里的设备选择同样跟随改名（2026-11-10 审核）：原实现只跟了
    // 节点/事件/心跳/帧缓冲，schemeLoad/flowControl/cameraResponse 仍指旧名，
    // 重命名后响应发送会静默失败（sendResponse 查不到设备只留 debug 痕）。
    {
        QJsonObject resp = m_responseConfig;
        const QStringList sections = {QStringLiteral("schemeLoad"),
                                      QStringLiteral("flowControl"),
                                      QStringLiteral("cameraResponse")};
        for (const QString &key : sections) {
            QJsonObject sec = resp.value(key).toObject();
            if (sec.value(QStringLiteral("device")).toString() == oldName) {
                sec[QStringLiteral("device")] = trimmed;
                resp[key] = sec;
            }
        }
        m_responseConfig = resp;
    }

    // 组帧缓冲随名迁移；旧定时器回调捕获的是旧名，停旧建新（剩余半帧继续按新名超时出帧）
    if (m_frameBuffers.contains(oldName))
        m_frameBuffers[trimmed] = m_frameBuffers.take(oldName);
    if (QTimer *t = m_frameTimers.take(oldName)) {
        t->stop();
        t->deleteLater();
    }
    if (node && !m_frameBuffers.value(trimmed).isEmpty()) {
        const int timeoutMs = node->getParam(QStringLiteral("frameTimeoutMs")).toInt();
        if (timeoutMs > 0)
            startFrameTimer(trimmed, timeoutMs);
    }

    emit deviceRenamed(oldName, trimmed);
    VFP_DEBUG << "Communication device renamed:" << oldName << "->" << trimmed;
    return true;
}

bool CommunicationManager::createDeviceNode(const QString &name, const QString &type, const QJsonObject &config)
{
    // 节点构造 / init() / 参数下发一律在锁外进行（init 等实现可能触发信号，持锁调用有重入风险，
    // 见 CommunicationManager.h 中的约束说明）；锁只保护最终的注册与信号连接。
    CommunicationNodeBase *node = nullptr;

    if (type == QStringLiteral("Serial") || type == QStringLiteral("串口")) {
        SerialCommNode *serial = new SerialCommNode(this);
        serial->init();
        if (config.contains(QStringLiteral("portName")))
            serial->setParam(QStringLiteral("portName"), config[QStringLiteral("portName")].toString());
        if (config.contains(QStringLiteral("baudRate")))
            serial->setParam(QStringLiteral("baudRate"), config[QStringLiteral("baudRate")].toInt());
        // 数据位/停止位/校验/重连：此前不传，用户在配置里改了也不生效
        if (config.contains(QStringLiteral("dataBits")))
            serial->setParam(QStringLiteral("dataBits"), config[QStringLiteral("dataBits")].toInt());
        if (config.contains(QStringLiteral("stopBits")))
            serial->setParam(QStringLiteral("stopBits"), config[QStringLiteral("stopBits")].toInt());
        if (config.contains(QStringLiteral("parity")))
            serial->setParam(QStringLiteral("parity"), config[QStringLiteral("parity")].toString());
        if (config.contains(QStringLiteral("autoReconnect")))
            serial->setParam(QStringLiteral("autoReconnect"), config[QStringLiteral("autoReconnect")].toBool());
        if (config.contains(QStringLiteral("reconnectInterval")))
            serial->setParam(QStringLiteral("reconnectInterval"), config[QStringLiteral("reconnectInterval")].toInt());
        if (config.contains(QStringLiteral("frameTimeoutMs")))
            serial->setParam(QStringLiteral("frameTimeoutMs"), config[QStringLiteral("frameTimeoutMs")].toInt());
        if (config.contains(QStringLiteral("frameTerminator")))
            serial->setParam(QStringLiteral("frameTerminator"), config[QStringLiteral("frameTerminator")].toString());
        node = serial;
    } else if (type == QStringLiteral("TCP")) {
        TcpCommNode *tcp = new TcpCommNode(this);
        tcp->init();
        if (config.contains(QStringLiteral("serverIp")))
            tcp->setParam(QStringLiteral("serverIp"), config[QStringLiteral("serverIp")].toString());
        if (config.contains(QStringLiteral("port")))
            tcp->setParam(QStringLiteral("port"), config[QStringLiteral("port")].toInt());
        if (config.contains(QStringLiteral("mode")))
            tcp->setParam(QStringLiteral("mode"), config[QStringLiteral("mode")].toString());
        // U-202：监听地址键此前不投递——经通信管理器建的 TCP 服务端设备根本没法填监听地址，
        // 收紧默认面（留空＝未配置＝拒启动）后这就是死路，必须先补这条投递。
        if (config.contains(QStringLiteral("bindAddress")))
            tcp->setParam(QStringLiteral("bindAddress"), config[QStringLiteral("bindAddress")].toString());
        // 重连参数：此前不传，配置里改的重连开关/间隔对新建连接无效
        if (config.contains(QStringLiteral("autoReconnect")))
            tcp->setParam(QStringLiteral("autoReconnect"), config[QStringLiteral("autoReconnect")].toBool());
        if (config.contains(QStringLiteral("reconnectInterval")))
            tcp->setParam(QStringLiteral("reconnectInterval"), config[QStringLiteral("reconnectInterval")].toInt());
        if (config.contains(QStringLiteral("frameTimeoutMs")))
            tcp->setParam(QStringLiteral("frameTimeoutMs"), config[QStringLiteral("frameTimeoutMs")].toInt());
        if (config.contains(QStringLiteral("frameTerminator")))
            tcp->setParam(QStringLiteral("frameTerminator"), config[QStringLiteral("frameTerminator")].toString());
        node = tcp;
    } else if (type == QStringLiteral("UDP")) {
        UdpCommNode *udp = new UdpCommNode(this);
        udp->init();
        if (config.contains(QStringLiteral("localPort")))
            udp->setParam(QStringLiteral("localPort"), config[QStringLiteral("localPort")].toInt());
        if (config.contains(QStringLiteral("remoteIp")))
            udp->setParam(QStringLiteral("remoteIp"), config[QStringLiteral("remoteIp")].toString());
        if (config.contains(QStringLiteral("remotePort")))
            udp->setParam(QStringLiteral("remotePort"), config[QStringLiteral("remotePort")].toInt());
        // U-202：本地绑定地址键此前不投递——同 TCP 服务端，收紧默认面后必须能填（见上条注）。
        if (config.contains(QStringLiteral("bindAddress")))
            udp->setParam(QStringLiteral("bindAddress"), config[QStringLiteral("bindAddress")].toString());
        if (config.contains(QStringLiteral("frameTimeoutMs")))
            udp->setParam(QStringLiteral("frameTimeoutMs"), config[QStringLiteral("frameTimeoutMs")].toInt());
        if (config.contains(QStringLiteral("frameTerminator")))
            udp->setParam(QStringLiteral("frameTerminator"), config[QStringLiteral("frameTerminator")].toString());
        node = udp;
    } else if (type == QStringLiteral("Modbus")) {
        ModbusNode *modbus = new ModbusNode(this);
        modbus->init();
        if (config.contains(QStringLiteral("role")))
            modbus->setParam(QStringLiteral("role"), config[QStringLiteral("role")].toString());
        if (config.contains(QStringLiteral("connectionType")))
            modbus->setParam(QStringLiteral("connectionType"), config[QStringLiteral("connectionType")].toString());
        // RTU 串口参数：不下发则 QModbusRtuSerialMaster 无参数可连（RTU 完全不可用）
        if (config.contains(QStringLiteral("portName")))
            modbus->setParam(QStringLiteral("portName"), config[QStringLiteral("portName")].toString());
        if (config.contains(QStringLiteral("baudRate")))
            modbus->setParam(QStringLiteral("baudRate"), config[QStringLiteral("baudRate")].toInt());
        // 数据位/停止位/校验：ModbusNode 的 RTU 分支会读这三个键（默认 8/1/None），
        // 但工厂此前从不投递 → 对话框里配的校验位/数据位/停止位全部落空，
        // 8E1/8O1 设备永远连不上（TCP 时这些键无意义，投递也无害）。
        if (config.contains(QStringLiteral("dataBits")))
            modbus->setParam(QStringLiteral("dataBits"), config[QStringLiteral("dataBits")].toInt());
        if (config.contains(QStringLiteral("stopBits")))
            modbus->setParam(QStringLiteral("stopBits"), config[QStringLiteral("stopBits")].toInt());
        if (config.contains(QStringLiteral("parity")))
            modbus->setParam(QStringLiteral("parity"), config[QStringLiteral("parity")].toString());
        if (config.contains(QStringLiteral("host")))
            modbus->setParam(QStringLiteral("host"), config[QStringLiteral("host")].toString());
        if (config.contains(QStringLiteral("port")))
            modbus->setParam(QStringLiteral("port"), config[QStringLiteral("port")].toInt());
        if (config.contains(QStringLiteral("slaveAddress")))
            modbus->setParam(QStringLiteral("slaveAddress"), config[QStringLiteral("slaveAddress")].toInt());
        // U-202：从站监听地址键此前不投递——通信管理器建的 Modbus 从站设备没法填监听地址，
        // 收紧默认面（留空＝未配置＝从站拒启动）后这就是死路，必须先补这条投递。
        if (config.contains(QStringLiteral("bindAddress")))
            modbus->setParam(QStringLiteral("bindAddress"), config[QStringLiteral("bindAddress")].toString());
        if (config.contains(QStringLiteral("autoReconnect")))
            modbus->setParam(QStringLiteral("autoReconnect"), config[QStringLiteral("autoReconnect")].toBool());
        if (config.contains(QStringLiteral("reconnectInterval")))
            modbus->setParam(QStringLiteral("reconnectInterval"), config[QStringLiteral("reconnectInterval")].toInt());
        if (config.contains(QStringLiteral("pollInterval")))
            modbus->setParam(QStringLiteral("pollInterval"), config[QStringLiteral("pollInterval")].toInt());
        // S4：回写三段确认（默认关闭）——配置里开了但不下发就等于没开（与上面历次"参数未投递"同类）
        if (config.contains(QStringLiteral("writeVerify")))
            modbus->setParam(QStringLiteral("writeVerify"), config[QStringLiteral("writeVerify")].toBool());
        // 加载寄存器表格
        if (config.contains(QStringLiteral("registers"))) {
            QList<ModbusRegisterItem> regs;
            QJsonArray regsArr = config[QStringLiteral("registers")].toArray();
            for (const auto &v : regsArr) {
                QJsonObject ro = v.toObject();
                ModbusRegisterItem r;
                r.address = ro[QStringLiteral("address")].toInt();
                r.dataType = ro[QStringLiteral("dataType")].toString(QStringLiteral("int16"));
                r.byteOrder = ro[QStringLiteral("byteOrder")].toString(QStringLiteral("ABCD"));
                r.accessMode = ro[QStringLiteral("accessMode")].toString(QStringLiteral("Read"));
                r.enabled = ro[QStringLiteral("enabled")].toBool(true);
                r.description = ro[QStringLiteral("description")].toString();
                regs.append(r);
            }
            modbus->setRegisters(regs);
        }
        node = modbus;
    } else if (type == QStringLiteral("PLC")) {   // 原为 "PLC" || "PLC"（复制粘贴的无效判断）
        PlcCommNode *plc = new PlcCommNode(this);
        plc->init();
        if (config.contains(QStringLiteral("plcBrand")))
            plc->setParam(QStringLiteral("plcBrand"), config[QStringLiteral("plcBrand")].toString());
        if (config.contains(QStringLiteral("host")))
            plc->setParam(QStringLiteral("host"), config[QStringLiteral("host")].toString());
        if (config.contains(QStringLiteral("port")))
            plc->setParam(QStringLiteral("port"), config[QStringLiteral("port")].toInt());
        if (config.contains(QStringLiteral("slaveAddress")))
            plc->setParam(QStringLiteral("slaveAddress"), config[QStringLiteral("slaveAddress")].toInt());
        if (config.contains(QStringLiteral("autoReconnect")))
            plc->setParam(QStringLiteral("autoReconnect"), config[QStringLiteral("autoReconnect")].toBool());
        if (config.contains(QStringLiteral("reconnectInterval")))
            plc->setParam(QStringLiteral("reconnectInterval"), config[QStringLiteral("reconnectInterval")].toInt());
        if (config.contains(QStringLiteral("pollInterval")))
            plc->setParam(QStringLiteral("pollInterval"), config[QStringLiteral("pollInterval")].toInt());
        // S4：回写三段确认（默认关闭）——同上，必须透传才生效
        if (config.contains(QStringLiteral("writeVerify")))
            plc->setParam(QStringLiteral("writeVerify"), config[QStringLiteral("writeVerify")].toBool());
        // 加载寄存器表格
        if (config.contains(QStringLiteral("registers"))) {
            QList<ModbusRegisterItem> regs;
            QJsonArray regsArr = config[QStringLiteral("registers")].toArray();
            for (const auto &v : regsArr) {
                QJsonObject ro = v.toObject();
                ModbusRegisterItem r;
                r.address = ro[QStringLiteral("address")].toInt();
                r.dataType = ro[QStringLiteral("dataType")].toString(QStringLiteral("int16"));
                r.byteOrder = ro[QStringLiteral("byteOrder")].toString(QStringLiteral("ABCD"));
                r.accessMode = ro[QStringLiteral("accessMode")].toString(QStringLiteral("Read"));
                r.enabled = ro[QStringLiteral("enabled")].toBool(true);
                r.description = ro[QStringLiteral("description")].toString();
                regs.append(r);
            }
            plc->setRegisters(regs);
        }
        node = plc;
    } else if (type.compare(QStringLiteral("FINS TCP"), Qt::CaseInsensitive) == 0
               || type.compare(QStringLiteral("FINS UDP"), Qt::CaseInsensitive) == 0) {
        // VM 4.4 B5-1：Omron FINS 通信。类型串大小写不敏感（"FINS TCP" / "Fins TCP" 同义）；
        // 两个传输面共用同一套参数与寄存器表，仅构造参数不同。
        const FinsCommNode::Transport transport =
            (type.compare(QStringLiteral("FINS UDP"), Qt::CaseInsensitive) == 0)
                ? FinsCommNode::FinsUdp
                : FinsCommNode::FinsTcp;
        FinsCommNode *fins = new FinsCommNode(transport, this);
        fins->init();
        if (config.contains(QStringLiteral("targetIp")))
            fins->setParam(QStringLiteral("targetIp"), config[QStringLiteral("targetIp")].toString());
        if (config.contains(QStringLiteral("targetPort")))
            fins->setParam(QStringLiteral("targetPort"), config[QStringLiteral("targetPort")].toInt());
        if (config.contains(QStringLiteral("pollInterval")))
            fins->setParam(QStringLiteral("pollInterval"), config[QStringLiteral("pollInterval")].toInt());
        if (config.contains(QStringLiteral("timeoutMs")))
            fins->setParam(QStringLiteral("timeoutMs"), config[QStringLiteral("timeoutMs")].toInt());
        if (config.contains(QStringLiteral("autoReconnect")))
            fins->setParam(QStringLiteral("autoReconnect"), config[QStringLiteral("autoReconnect")].toBool());
        if (config.contains(QStringLiteral("reconnectInterval")))
            fins->setParam(QStringLiteral("reconnectInterval"), config[QStringLiteral("reconnectInterval")].toInt());
        if (config.contains(QStringLiteral("localNode")))
            fins->setParam(QStringLiteral("localNode"), config[QStringLiteral("localNode")].toInt());
        if (config.contains(QStringLiteral("remoteNode")))
            fins->setParam(QStringLiteral("remoteNode"), config[QStringLiteral("remoteNode")].toInt());
        // 加载寄存器表格（area/address/dataType/accessMode/enabled/description，与 Modbus 同形）
        if (config.contains(QStringLiteral("registers"))) {
            QList<FinsRegisterItem> regs;
            const QJsonArray regsArr = config[QStringLiteral("registers")].toArray();
            for (const auto &v : regsArr) {
                const QJsonObject ro = v.toObject();
                FinsRegisterItem r;
                r.area = ro[QStringLiteral("area")].toString(QStringLiteral("DM"));
                r.address = ro[QStringLiteral("address")].toInt();
                r.dataType = ro[QStringLiteral("dataType")].toString(QStringLiteral("int16"));
                r.accessMode = ro[QStringLiteral("accessMode")].toString(QStringLiteral("Read"));
                r.enabled = ro[QStringLiteral("enabled")].toBool(true);
                r.description = ro[QStringLiteral("description")].toString();
                regs.append(r);
            }
            fins->setRegisters(regs);
        }
        node = fins;
    }

    if (!node)
        return false;   // 未知类型：调用方（addDevice）据此回滚登记，不留幽灵设备（P3-12）

    // 统一透传（根治"配了不生效"）：上面逐键下发的键保持"类型收敛"（如 baudRate 取 int），
    // 其余**未声明**的键一律按原始类型透传。历史缺陷：这里曾是 fail-closed 白名单——新增配置键
    // 若忘了在分支里补一行，用户在配置里改了却静默不生效（S4 的 writeVerify 正是这样漏掉的；源码里
    // 还留着"数据位/停止位/校验/重连此前不传"等多处注释）。
    // 口径（评审指正后修正）：**未声明的键会存入参数表，但节点不会去读它**——不是"忽略"。
    //   HalconNode::setParam 对未声明键就是原样入库（用例 getParam(unmanagedProbeKey)==4242 正是
    //   以"存储"为判别基础）。即可被外部读取/回显，但不影响节点行为。
    // 排除集用"节点已声明的参数名"动态判定，而不是手工清单：
    //   · 分支里显式下发的键在 setParam 之后必然已存在于参数表 → 自动排除 → **不存在二次覆盖**
    //     （手工清单漏一项就是类型回退：int 截断、qMax 下限这类键会被原始类型再写一遍）；
    //   · 顺带挡住"配置里手写与节点内部运行参数重名的键"（connected / moduleStatus / lastReceived
    //     等 init 已预置）——旧写法会在创建期把该值写进参数表、覆盖内部状态。
    {
        const QList<QString> declared = node->getAllParamNames();
        // 预留（内部状态/派生）键：由节点自己写入，配置里手写同名键不得改写它们。
        // 这**不同于**上面的"已声明"排除集：comm 节点覆写 init() 时不调 HalconNode::init()，
        // 故 moduleStatus 这类派生键并不在"已声明"集合里（实测：不加这层就会被写进参数表）。
        // 判定方式力求**规则化**，避免再出现漏项（上一版按名字逐个点名，就漏了 lastSent / registers，
        // 评审两轮指出）：
        //  · 精确名：moduleStatus（run() 派生的模块状态）、connected（连接状态）、
        //    registers（Modbus/PLC 的寄存器表走 setRegisters()、**不经 setParam** → 不在"已声明"集合，
        //    不挡住就会把创建期的原始 QVariantList 塞进基类参数表）；
        //  · 前缀规则：**所有 "last" 开头**的键 —— 该族全部由节点自己写入（本轮查全：
        //    lastSent / lastReceived / lastSender（TCP/Serial/UDP）、lastOutput（ScriptNode）、
        //    lastSavedPath（写文件类）、lastError（多个节点）、lastInput / lastData（流程节点））。
        // 该规则的边界已核对：现有**配置键**里没有任何一个以 "last" 开头（键集见各设备对话框 build*Form），
        // 因此不会误挡用户可配的项。
        const auto isReservedInternal = [](const QString &key) {
            if (key == QLatin1String("moduleStatus") || key == QLatin1String("connected")
                || key == QLatin1String("registers"))
                return true;
            return key.startsWith(QLatin1String("last"));
        };
        for (auto it = config.constBegin(); it != config.constEnd(); ++it) {
            if (declared.contains(it.key()))
                continue;   // 已由上面的分支按类型收敛下发，或属节点内部参数（init 预置）
            if (isReservedInternal(it.key()))
                continue;
            node->setParam(it.key(), it.value().toVariant());
        }
    }

    QMutexLocker locker(&m_mutex);
    if (m_devices.contains(name)) {
        // 同名设备已存在：原实现会把刚创建的节点直接丢弃（泄漏），此处显式回收
        locker.unlock();
        node->closeConnection();
        node->deleteLater();
        VFP_DEBUG << "Device already exists, drop duplicate node:" << name;
        return true;   // 重复节点已显式回收；登记已存在，对调用方算成功
    }
    m_devices[name] = node;
    node->setDeviceName(name);

    {
        // Forward data received signals（经帧组装：配置了组帧则按帧转发，未配置则原样转发）
        // 捕获 node 而非建节点时的名字：重命名后 deviceName() 即新名，转发不挂旧名。
        // U-20（2026-11-10）：context 用 node 而非长寿命单例 this —— removeDevice 里
        // node 会 deleteLater，context=node 时连接与已排队的回调都随节点一起取消，
        // 否则排队中的 lambda 仍会解引用裸 node。
        connect(node, &CommunicationNodeBase::dataReceived, node, [this, node](const QByteArray &data) {
            feedFrameAssembler(node->deviceName(), data);
        });

        // 注意：这两个槽会在 openDevice/closeDevice（可能仍处于外层调用栈）中被同步触发，
        // 因此锁内只做状态更新，emit 放到锁外；且用 find 而非 operator[]，
        // 避免设备已被 removeDevice 移除后又被 [] 重新插入“复活”。
        connect(node, &CommunicationNodeBase::connectionOpened, node, [this, node]() {
            const QString name = node->deviceName();
            {
                QMutexLocker locker(&m_mutex);
                auto it = m_deviceInfos.find(name);
                if (it == m_deviceInfos.end())
                    return;
                it->isConnected = true;
            }
            emit deviceConnected(name);
        });

        connect(node, &CommunicationNodeBase::connectionClosed, node, [this, node]() {
            const QString name = node->deviceName();
            {
                QMutexLocker locker(&m_mutex);
                auto it = m_deviceInfos.find(name);
                if (it == m_deviceInfos.end())
                    return;   // 设备已被 removeDevice 移除：清理由 removeDevice 负责
                it->isConnected = false;
            }
            // 断开即丢弃半帧缓冲并停表：否则"掉线 → 自动重连"后，新链路首帧会与
            // 旧链路的残缺数据拼在一起（脏半帧污染真实报文，解析全错还不报错）
            m_frameBuffers.remove(name);
            if (QTimer *t = m_frameTimers.value(name, nullptr))
                t->stop();
            emit deviceDisconnected(name);
        });

        // Forward register value changes
        connect(node, &CommunicationNodeBase::registerValueChanged, this,
                [this, node](int address, const QByteArray &value, const QString &dataType) {
            Q_UNUSED(dataType)
            // 发出通信设备收到的原始数据（供字符串触发和接收事件系统使用）
            QByteArray fullData;
            QDataStream stream(&fullData, QIODevice::WriteOnly);
            stream << address;
            fullData.append(value);
            emit dataReceived(node->deviceName(), fullData);
        });

        // Forward PLC register changes
        if (auto *plc = qobject_cast<PlcCommNode *>(node)) {
            connect(plc, &PlcCommNode::plcRegisterChanged, this,
                    [this, node](const QString &, int address,
                                 const QByteArray &value, const QString &dataType) {
                Q_UNUSED(dataType)
                QByteArray fullData;
                QDataStream stream(&fullData, QIODevice::WriteOnly);
                stream << address;
                fullData.append(value);
                emit dataReceived(node->deviceName(), fullData);
            });
        }
    }
    return true;   // 节点已注册（P3-12：调用方据此决定是否 emit/保留登记）
}

QByteArray CommunicationManager::decodeEscapes(const QString &text)
{
    QByteArray out;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c != QLatin1Char('\\') || i + 1 >= text.size()) {
            out.append(c.toLatin1());
            continue;
        }
        const QChar n = text.at(++i);
        if (n == QLatin1Char('r'))      out.append('\r');
        else if (n == QLatin1Char('n')) out.append('\n');
        else if (n == QLatin1Char('t')) out.append('\t');
        else if (n == QLatin1Char('0')) out.append('\0');
        else if (n == QLatin1Char('\\'))out.append('\\');
        else if ((n == QLatin1Char('x') || n == QLatin1Char('X')) && i + 2 < text.size()) {
            bool ok = false;
            const int v = text.mid(i + 1, 2).toInt(&ok, 16);
            if (ok) { out.append(static_cast<char>(v)); i += 2; }
            else    { out.append('x'); }
        } else {
            out.append(n.toLatin1());
        }
    }
    return out;
}

void CommunicationManager::feedFrameAssembler(const QString &deviceName, const QByteArray &data)
{
    // 数据帧率与原样转发路径一致：无组帧配置时零开销（不建缓冲、不加定时器）
    CommunicationNodeBase *node = deviceNode(deviceName);
    const int timeoutMs = node ? node->getParam(QStringLiteral("frameTimeoutMs")).toInt() : 0;
    const QString termText =
        node ? node->getParam(QStringLiteral("frameTerminator")).toString() : QString();

    if (timeoutMs <= 0 && termText.isEmpty()) {
        emit dataReceived(deviceName, data);   // 未启用组帧：原样转发（与历史行为一致）
        return;
    }

    // 全程不持有 m_frameBuffers 的引用/迭代器跨 emit：下游槽可能 removeDevice/新增设备，
    // 任何一次插入都会触发 QHash 重哈希 → 旧引用悬空（重入下写已释放内存）。
    QByteArray buf = m_frameBuffers.value(deviceName);
    buf.append(data);
    if (buf.size() > 1024 * 1024) {   // 兜底：规则长期不匹配时防止无界增长
        VFP_DEBUG << "Frame assembler overflow, drop buffer for device:" << deviceName;
        m_frameBuffers.remove(deviceName);
        return;
    }

    // ① 结束符切帧（保留结束符，便于解析与监视查看原始帧）——先在本地切好，最后统一发
    QList<QByteArray> frames;
    if (!termText.isEmpty()) {
        const QByteArray term = decodeEscapes(termText);
        if (!term.isEmpty()) {
            int idx = -1;
            while ((idx = buf.indexOf(term)) >= 0) {
                const int end = idx + term.size();
                frames.append(buf.left(end));
                buf.remove(0, end);
            }
        }
    }
    m_frameBuffers[deviceName] = buf;   // 先落缓冲（此时状态已是最终态），后发帧

    // ② 帧超时兜底：静默 timeoutMs 后把剩余数据整体当一帧（对端不发结束符时使用）
    if (timeoutMs > 0)
        startFrameTimer(deviceName, timeoutMs);

    for (const QByteArray &frame : frames)
        emit dataReceived(deviceName, frame);
}

void CommunicationManager::startFrameTimer(const QString &deviceName, int timeoutMs)
{
    QTimer *t = m_frameTimers.value(deviceName, nullptr);
    if (!t) {
        t = new QTimer(this);
        t->setSingleShot(true);
        m_frameTimers.insert(deviceName, t);
        connect(t, &QTimer::timeout, this, [this, deviceName]() {
            const QByteArray frame = m_frameBuffers.value(deviceName);
            if (frame.isEmpty())
                return;
            m_frameBuffers.remove(deviceName);
            emit dataReceived(deviceName, frame);
        });
    }
    t->start(timeoutMs);   // 每来一块数据重置静默计时
}

CommunicationNodeBase *CommunicationManager::deviceNode(const QString &name) const
{
    QMutexLocker locker(&m_mutex);
    return m_devices.value(name, nullptr);
}

bool CommunicationManager::isDataUploadEnabled(const QString &deviceName) const
{
    CommunicationNodeBase *node = deviceNode(deviceName);
    // 设备不存在时视为启用：调用方多为"数据到达后的消费判定"，
    // 未知设备不该被静默改写为“不上传”（事件路径也不依赖本判定）。
    if (!node) return true;
    const QVariant v = node->getParam(QStringLiteral("dataUpload"));
    return v.isValid() ? v.toBool() : true;
}

bool CommunicationManager::openDevice(const QString &name)
{
    // 锁内只取指针；openConnection 会同步 emit connectionOpened，必须在锁外执行
    CommunicationNodeBase *node = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        node = m_devices.value(name, nullptr);
    }
    if (!node) return false;
    return node->openConnection();
}

bool CommunicationManager::closeDevice(const QString &name)
{
    CommunicationNodeBase *node = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        node = m_devices.value(name, nullptr);
    }
    if (!node) return false;
    node->closeConnection();
    return true;
}

bool CommunicationManager::sendData(const QString &deviceName, const QByteArray &data)
{
    CommunicationNodeBase *node = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        node = m_devices.value(deviceName, nullptr);
    }
    if (!node || !node->isConnected()) return false;

    // Modbus/PLC 是寄存器语义（写寄存器），没有「裸字节发送」。历史行为是一路投递到基类
    // onSendRequested 的空实现 → 静默 no-op：上层以为回写成功、设备其实什么都没收到。
    // 这里提前失败并留痕，让调用方（如「发送数据」算子）能正确上报失败。
    if (!node->supportsRawSend()) {
        qWarning() << QStringLiteral("CommunicationManager: 该设备不支持原始字节发送"
                                     "（Modbus/PLC 请使用写寄存器）：")
                   << deviceName;
        return false;
    }

    // 通过信号队列投递到节点线程执行实际 write（跨线程安全）
    node->requestSend(data);
    // 兼容旧逻辑：数据写入参数供节点/UI 读取
    node->setParam(QStringLiteral("sendData"), QString::fromLatin1(data));
    emit dataSent(deviceName, data);
    return true;
}

// ---- Receive Event Management ----

QStringList CommunicationManager::receiveEventIds() const
{
    QMutexLocker locker(&m_mutex);
    return m_receiveEvents.keys();
}

ReceiveEvent *CommunicationManager::receiveEvent(const QString &id) const
{
    QMutexLocker locker(&m_mutex);
    return m_receiveEvents.value(id, nullptr);
}

bool CommunicationManager::addReceiveEvent(ReceiveEvent *event)
{
    if (!event) return false;

    const QString id = event->eventId();
    {
        QMutexLocker locker(&m_mutex);
        if (m_receiveEvents.contains(id)) return false;
        m_receiveEvents[id] = event;
    }
    emit receiveEventAdded(id);
    return true;
}

bool CommunicationManager::removeReceiveEvent(const QString &id)
{
    ReceiveEvent *ev = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_receiveEvents.contains(id)) return false;
        ev = m_receiveEvents.take(id);
    }
    if (ev) ev->deleteLater();
    emit receiveEventRemoved(id);
    return true;
}

bool CommunicationManager::renameReceiveEvent(const QString &oldId, const QString &newId)
{
    const QString trimmed = newId.trimmed();
    if (trimmed.isEmpty() || trimmed == oldId) return false;
    ReceiveEvent *ev = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_receiveEvents.contains(oldId) || m_receiveEvents.contains(trimmed))
            return false;
        ev = m_receiveEvents.take(oldId);
        m_receiveEvents[trimmed] = ev;
    }
    ev->setEventId(trimmed);
    GlobalTriggerManager::instance()->renameEventTrigger(oldId, trimmed);
    emit receiveEventRenamed(oldId, trimmed);
    return true;
}

// ---- Send Event Management ----

QStringList CommunicationManager::sendEventIds() const
{
    QMutexLocker locker(&m_mutex);
    return m_sendEvents.keys();
}

SendEvent *CommunicationManager::sendEvent(const QString &id) const
{
    QMutexLocker locker(&m_mutex);
    return m_sendEvents.value(id, nullptr);
}

bool CommunicationManager::fireSendEvent(const QString &id, const QVariant &data)
{
    // 注意：必须在锁外调用 send()——send 内部会回调 sendData，而 sendData 会重新进入
    // 本锁（非递归锁），持锁调用会自死锁。
    SendEvent *ev = sendEvent(id);
    if (!ev)
        return false;
    return ev->send(data);
}

int CommunicationManager::fireEnabledSendEvents(const QVariant &data)
{
    // 「每轮结束自动上报」的落点。先取 ID 快照（sendEventIds 内部短暂持锁），
    // 再在锁外逐个调用 fireSendEvent——原因同 fireSendEvent 的注释：
    // send() 会回调 sendData 并重新进入本锁，持锁调用会自死锁。
    const QStringList ids = sendEventIds();
    int sent = 0;
    for (const QString &id : ids) {
        if (fireSendEvent(id, data)) {
            ++sent;
        }
    }
    return sent;
}

bool CommunicationManager::addSendEvent(SendEvent *event)
{
    if (!event) return false;

    const QString id = event->eventId();
    {
        QMutexLocker locker(&m_mutex);
        if (m_sendEvents.contains(id)) return false;
        m_sendEvents[id] = event;
    }
    emit sendEventAdded(id);
    return true;
}

bool CommunicationManager::removeSendEvent(const QString &id)
{
    SendEvent *ev = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_sendEvents.contains(id)) return false;
        ev = m_sendEvents.take(id);
    }
    if (ev) ev->deleteLater();
    emit sendEventRemoved(id);
    return true;
}

bool CommunicationManager::renameSendEvent(const QString &oldId, const QString &newId)
{
    const QString trimmed = newId.trimmed();
    if (trimmed.isEmpty() || trimmed == oldId) return false;
    SendEvent *ev = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_sendEvents.contains(oldId) || m_sendEvents.contains(trimmed))
            return false;
        ev = m_sendEvents.take(oldId);
        m_sendEvents[trimmed] = ev;
    }
    ev->setEventId(trimmed);
    emit sendEventRenamed(oldId, trimmed);
    return true;
}

// ---- Serialization ----

QJsonObject CommunicationManager::toJson() const
{
    QMutexLocker locker(&m_mutex);

    QJsonObject root;

    // Devices
    QJsonArray devicesArr;
    for (auto it = m_deviceInfos.constBegin(); it != m_deviceInfos.constEnd(); ++it) {
        QJsonObject dObj;
        dObj[QStringLiteral("name")] = it.key();
        dObj[QStringLiteral("type")] = it.value().type;
        dObj[QStringLiteral("config")] = it.value().config;
        devicesArr.append(dObj);
    }
    root[QStringLiteral("devices")] = devicesArr;

    // Receive events
    QJsonArray recvArr;
    for (auto it = m_receiveEvents.constBegin(); it != m_receiveEvents.constEnd(); ++it) {
        recvArr.append(it.value()->toJson());
    }
    root[QStringLiteral("receiveEvents")] = recvArr;

    // Send events
    QJsonArray sendArr;
    for (auto it = m_sendEvents.constBegin(); it != m_sendEvents.constEnd(); ++it) {
        sendArr.append(it.value()->toJson());
    }
    root[QStringLiteral("sendEvents")] = sendArr;

    // 响应配置（VM 5.2.5）：随方案保存；空对象不落键（老方案字节不变）
    if (!m_responseConfig.isEmpty())
        root[QStringLiteral("responseConfig")] = m_responseConfig;

    return root;
}

QJsonObject CommunicationManager::responseConfig() const
{
    QMutexLocker locker(&m_mutex);
    return m_responseConfig;
}

void CommunicationManager::setResponseConfig(const QJsonObject &cfg)
{
    QMutexLocker locker(&m_mutex);
    m_responseConfig = cfg;
}

void CommunicationManager::fromJson(const QJsonObject &json)
{
    // 清空现有数据：锁内只做容器操作，节点的关闭放到锁外
    // （closeConnection 会同步 emit connectionClosed → 其槽函数需重新进入 m_mutex）
    QList<CommunicationNodeBase *> oldNodes;
    {
        QMutexLocker locker(&m_mutex);
        oldNodes = m_devices.values();
        m_devices.clear();
        m_deviceInfos.clear();
        for (auto *e : m_receiveEvents) delete e;
        m_receiveEvents.clear();
        for (auto *e : m_sendEvents) delete e;
        m_sendEvents.clear();
    }
    for (auto *n : oldNodes) {
        n->closeConnection();
        n->deleteLater();
    }

    // 设备已整体清空：同步丢弃帧组装缓冲与成帧定时器。
    // 否则"切方案"后旧半帧/旧定时器残留，新方案同名设备的首帧会被旧数据污染。
    m_frameBuffers.clear();
    for (QTimer *t : m_frameTimers) {
        t->stop();
        t->deleteLater();
    }
    m_frameTimers.clear();

    // 加载设备：addDevice 每次自行加锁，避免嵌套死锁。
    // 建不出来（未知类型）必须留痕（2026-10-25 复核 P3-12 残留）：静默丢设备的后果是
    // "方案里的通信设备打开后不见了"，而且下一次保存会把它整条抹掉——现场根本查不到原因。
    QJsonArray devicesArr = json[QStringLiteral("devices")].toArray();
    for (const auto &v : devicesArr) {
        QJsonObject dObj = v.toObject();
        const QString devName = dObj[QStringLiteral("name")].toString();
        const QString devType = dObj[QStringLiteral("type")].toString();
        if (!addDevice(devName, devType, dObj[QStringLiteral("config")].toObject())) {
            const QString reason = QStringLiteral("类型未知或节点构造失败");
            VFP_DEBUG << "方案载入：通信设备未能建立，保留原始条目待人工确认:"
                      << devName << devType << reason;
            emit deviceLoadFailed(devName, devType, reason);   // 载入面据此汇总提示
        }
    }

    // 加载接收事件
    {
        QMutexLocker locker(&m_mutex);
        QJsonArray recvArr = json[QStringLiteral("receiveEvents")].toArray();
        for (const auto &v : recvArr) {
            QJsonObject eObj = v.toObject();
            int type = eObj[QStringLiteral("type")].toInt();
            QString id = eObj[QStringLiteral("id")].toString();
            QString devName = eObj[QStringLiteral("deviceName")].toString();

            ReceiveEvent *ev = nullptr;
            if (type == 0) { // TEXT_PROTOCOL
                ev = new TextProtocolReceiveEvent(id, devName, this);
            } else { // BYTE_MATCH
                ev = new ByteMatchReceiveEvent(id, devName, this);
            }
            if (ev) {
                ev->fromJson(eObj);
                m_receiveEvents[id] = ev;
            }
        }
    }

    // 加载发送事件：此前 toJson 写了、fromJson 不读 → 保存/重启后
    // PLC 回写配置（模板/字段表/启停）全部"消失"，只能重新手配
    {
        QMutexLocker locker(&m_mutex);
        QJsonArray sendArr = json[QStringLiteral("sendEvents")].toArray();
        for (const auto &v : sendArr) {
            QJsonObject eObj = v.toObject();
            const int type = eObj[QStringLiteral("type")].toInt();
            const QString id = eObj[QStringLiteral("id")].toString();
            const QString devName = eObj[QStringLiteral("deviceName")].toString();
            if (id.isEmpty()) continue;

            SendEvent *ev = nullptr;
            if (type == int(SendEvent::BYTE_PACK))
                ev = new BytePackSendEvent(id, devName, this);
            else
                ev = new TextDirectSendEvent(id, devName, this);
            if (ev) {
                ev->fromJson(eObj);
                m_sendEvents[id] = ev;
            }
        }
    }

    // 响应配置（VM 5.2.5）：老方案无此键 → 空对象（各子项按未启用处理）
    {
        QMutexLocker locker(&m_mutex);
        m_responseConfig = json.value(QStringLiteral("responseConfig")).toObject();
    }
}

bool CommunicationManager::loadFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) return false;
    fromJson(doc.object());
    return true;
}

bool CommunicationManager::saveToFile(const QString &path)
{
    // 原子写（QSaveFile）：避免写入中途崩溃留下半份配置（重启后设备/事件全丢）
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QByteArray payload = QJsonDocument(toJson()).toJson();
    if (file.write(payload) != payload.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}
