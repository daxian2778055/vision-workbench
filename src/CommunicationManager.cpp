#include "CommunicationManager.h"
#include "CommunicationNodeBase.h"
#include "SerialCommNode.h"
#include "TcpCommNode.h"
#include "ModbusNode.h" // provides ModbusRegisterItem
#include "PlcCommNode.h"
#include "ReceiveEvent.h"
#include "SendEvent.h"
#include "AppLog.h"
#include <QJsonDocument>
#include <QFile>
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
        node = tcp;
    } else if (type == QStringLiteral("Modbus")) {
        ModbusNode *modbus = new ModbusNode(this);
        modbus->init();
        if (config.contains(QStringLiteral("role")))
            modbus->setParam(QStringLiteral("role"), config[QStringLiteral("role")].toString());
        if (config.contains(QStringLiteral("connectionType")))
            modbus->setParam(QStringLiteral("connectionType"), config[QStringLiteral("connectionType")].toString());
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
    } else if (type == QStringLiteral("PLC") || type == QStringLiteral("PLC")) {
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
        // Forward data received signals
        connect(node, &CommunicationNodeBase::dataReceived, this, [this, name](const QByteArray &data) {
            emit dataReceived(name, data);
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
                    return;
                it->isConnected = false;
            }
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
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(toJson()).toJson());
    file.close();
    return true;
}
