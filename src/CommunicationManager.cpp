#include "CommunicationManager.h"
#include "CommunicationNodeBase.h"
#include "SerialCommNode.h"
#include "UdpCommNode.h"
#include <QTimer>
#include "TcpCommNode.h"
#include "ModbusNode.h" // provides ModbusRegisterItem
#include "PlcCommNode.h"
#include "ReceiveEvent.h"
#include "SendEvent.h"
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

    createDeviceNode(name, type, config);
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

bool CommunicationManager::removeDevice(const QString &name)
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

    emit deviceRemoved(name);
    return true;
}

void CommunicationManager::createDeviceNode(const QString &name, const QString &type, const QJsonObject &config)
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
    }

    if (!node)
        return;

    // 统一透传（根治"配了不生效"）：上面逐键下发的键保持"类型收敛"（如 baudRate 取 int），
    // 其余**未显式处理**的键一律按原始类型透传。历史缺陷：这里是 fail-closed 白名单——新增配置键
    // 若忘了在分支里补一行，用户在配置里改了却静默不生效（S4 的 writeVerify 正是这样漏掉的；源码里
    // 还留着"数据位/停止位/校验/重连此前不传"等多处注释）。
    // 现在默认 fail-open：未知键直接到节点（各节点 setParam 对不认识的键本就安全忽略）。
    // 维护提示：将来在分支里新增"需要类型收敛"的键，请同步加入下表；加漏了也不会失效——
    // 只是会被这里按原始类型（toVariant）再透传一遍。
    {
        static const char *const kExplicitlyHandled[] = {
            "portName", "baudRate", "dataBits", "stopBits", "parity",
            "autoReconnect", "reconnectInterval", "frameTimeoutMs", "frameTerminator",
            "serverIp", "port", "mode",
            "localPort", "remoteIp", "remotePort",
            "role", "connectionType", "host", "slaveAddress", "pollInterval",
            "writeVerify", "registers", "plcBrand"
        };
        for (auto it = config.constBegin(); it != config.constEnd(); ++it) {
            bool explicitlyHandled = false;
            for (const char *k : kExplicitlyHandled) {
                if (it.key() == QLatin1String(k)) {
                    explicitlyHandled = true;
                    break;
                }
            }
            if (!explicitlyHandled)
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
        return;
    }
    m_devices[name] = node;
    node->setDeviceName(name);

    {
        // Forward data received signals（经帧组装：配置了组帧则按帧转发，未配置则原样转发）
        connect(node, &CommunicationNodeBase::dataReceived, this, [this, name](const QByteArray &data) {
            feedFrameAssembler(name, data);
        });

        // 注意：这两个槽会在 openDevice/closeDevice（可能仍处于外层调用栈）中被同步触发，
        // 因此锁内只做状态更新，emit 放到锁外；且用 find 而非 operator[]，
        // 避免设备已被 removeDevice 移除后又被 [] 重新插入“复活”。
        connect(node, &CommunicationNodeBase::connectionOpened, this, [this, name]() {
            {
                QMutexLocker locker(&m_mutex);
                auto it = m_deviceInfos.find(name);
                if (it == m_deviceInfos.end())
                    return;
                it->isConnected = true;
            }
            emit deviceConnected(name);
        });

        connect(node, &CommunicationNodeBase::connectionClosed, this, [this, name]() {
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
                [this, name](int address, const QByteArray &value, const QString &dataType) {
            // 发出通信设备收到的原始数据（供字符串触发和接收事件系统使用）
            QByteArray fullData;
            QDataStream stream(&fullData, QIODevice::WriteOnly);
            stream << address;
            fullData.append(value);
            emit dataReceived(name, fullData);
        });

        // Forward PLC register changes
        if (auto *plc = qobject_cast<PlcCommNode *>(node)) {
            connect(plc, &PlcCommNode::plcRegisterChanged, this,
                    [this, name](const QString &, int address,
                                 const QByteArray &value, const QString &dataType) {
                Q_UNUSED(dataType);
                QByteArray fullData;
                QDataStream stream(&fullData, QIODevice::WriteOnly);
                stream << address;
                fullData.append(value);
                emit dataReceived(name, fullData);
            });
        }
    }
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
    if (timeoutMs > 0) {
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

    for (const QByteArray &frame : frames)
        emit dataReceived(deviceName, frame);
}

CommunicationNodeBase *CommunicationManager::deviceNode(const QString &name) const
{
    QMutexLocker locker(&m_mutex);
    return m_devices.value(name, nullptr);
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

    return root;
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

    // 加载设备：addDevice 每次自行加锁，避免嵌套死锁
    QJsonArray devicesArr = json[QStringLiteral("devices")].toArray();
    for (const auto &v : devicesArr) {
        QJsonObject dObj = v.toObject();
        addDevice(dObj[QStringLiteral("name")].toString(),
                  dObj[QStringLiteral("type")].toString(),
                  dObj[QStringLiteral("config")].toObject());
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
