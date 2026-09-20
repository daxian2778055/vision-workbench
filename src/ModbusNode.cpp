#include "ModbusNode.h"
#include "AppLog.h"
#include <QModbusDataUnit>
#include <QModbusReply>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QSignalBlocker>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDataStream>

namespace {
/// 把寄存器的 16 位字序列按配置的字节序组装成原始字节（轮询读取与服务器写入共用同一约定，
/// 避免两处各写一份、改一处漏一处）：ABCD=原序；CDAB=交换两字；BADC=字内字节互换；其余=完全反转。
QByteArray assembleRegisterBytes(const QVector<quint16> &values, const QString &byteOrder)
{
    QByteArray raw;
    QDataStream stream(&raw, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);   // Modbus 是 Big Endian

    // 归一化约定（与 32 位路径一致）：先把寄存器按 byteOrder 组装成最终字节序列，
    // parseRawToValue 一律按大端解释。单寄存器（16 位）语义：
    //   - ABCD / CDAB：只有 1 个字，无"换字"可言 → 原样（CDAB 单寄存器 = ABCD）；
    //   - BADC / DCBA：字内字节互换（单寄存器 = 高低字节颠倒）。
    // 旧实现用 `|| values.size() == 1` 强制走 ABCD，导致 BADC/DCBA 单寄存器未交换（16/32 位
    // 逻辑不一致）；而 CDAB 单寄存器也未被交换（正确）。此处 CDAB 单寄存器显式走原样。
    if (byteOrder == QStringLiteral("ABCD")) {
        for (quint16 v : values)
            stream << v;
    } else if (byteOrder == QStringLiteral("CDAB")) {
        if (values.size() >= 2)
            stream << values[1] << values[0];           // 32 位：交换两字
        else
            for (quint16 v : values) stream << v;       // 16 位单寄存器无"换字"，CDAB=ABCD 原样
    } else if (byteOrder == QStringLiteral("BADC")) {
        for (quint16 v : values)
            stream << quint16(((v & 0xFF) << 8) | ((v >> 8) & 0xFF));
    } else {
        for (int i = values.size() - 1; i >= 0; --i) {
            const quint16 v = values[i];
            stream << quint16(((v & 0xFF) << 8) | ((v >> 8) & 0xFF));
        }
    }
    return raw;
}
}   // namespace

ModbusNode::ModbusNode(QObject *parent)
    : CommunicationNodeBase(parent)
{
    setName(QStringLiteral("Modbus\u901A\u4FE1"));
    m_commType = CommunicationNodeBase::MODBUS_MASTER;
    m_type = OUTPUT;
}

void ModbusNode::init()
{
    // 不调用 HalconNode::init() / CommunicationNodeBase::init() — Modbus 不处理图像
    // 添加输入和输出端口
    addInputPort(QStringLiteral("data"), PortDataType::String);
    addOutputPort(QStringLiteral("\u5BC4\u5B58\u5668\u6570\u636E"), PortDataType::String);

    // 参数初始化
    m_params[QStringLiteral("role")] = QStringLiteral("\u5BA2\u6237\u7AEF"); // 客户端 / 服务器
    m_params[QStringLiteral("slaveAddress")] = 1;
    m_params[QStringLiteral("connectionType")] = QStringLiteral("TCP");
    m_params[QStringLiteral("host")] = QStringLiteral("127.0.0.1");
    m_params[QStringLiteral("port")] = 502;
    m_params[QStringLiteral("autoReconnect")] = true;
    m_params[QStringLiteral("reconnectInterval")] = 3000;
    m_params[QStringLiteral("pollInterval")] = 100;
    setParamDirect(QStringLiteral("connected"), false);

    // 自动重连定时器
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (!m_connected && m_autoReconnect && m_role == MODBUS_CLIENT) {
            VFP_DEBUG << "Modbus auto-reconnecting...";
            openConnection();
        }
    });

    // 轮询定时器
    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &ModbusNode::onPollTimeout);
}

void ModbusNode::setRole(Role role)
{
    if (m_role == role) return;
    bool wasConnected = m_connected;
    closeConnection();
    m_role = role;
    m_params[QStringLiteral("role")] = (role == MODBUS_SERVER)
        ? QStringLiteral("\u670D\u52A1\u5668") : QStringLiteral("\u5BA2\u6237\u7AEF");
    if (wasConnected) openConnection();
}

void ModbusNode::applyRoleParam(const QString &mode)
{
    if (mode == QStringLiteral("\u670D\u52A1\u5668") || mode == QStringLiteral("Server")) {
        if (m_role != MODBUS_SERVER) {
            bool wasConnected = m_connected;
            closeConnection();
            m_role = MODBUS_SERVER;
            if (wasConnected) openConnection();
        }
    } else {
        if (m_role != MODBUS_CLIENT) {
            bool wasConnected = m_connected;
            closeConnection();
            m_role = MODBUS_CLIENT;
            if (wasConnected) openConnection();
        }
    }
}

bool ModbusNode::openConnection()
{
    closeConnection();

    // ===== 服务器模式：监听端口，对外提供寄存器 =====
    if (m_role == MODBUS_SERVER) {
        m_modbusServer = new QModbusTcpServer(this);
        m_modbusServer->setServerAddress(static_cast<quint8>(m_slaveAddress));
        m_modbusServer->setConnectionParameter(
            QModbusDevice::NetworkPortParameter,
            m_params.value(QStringLiteral("port"), 502).toInt());

        // 必须先 setMap：QModbusServer 默认寄存器表为空，未映射地址的写入会被直接拒绝——
        // 既不会改值、也不会触发 dataWritten（表现为"客户端写成功、服务器毫无反应"）。
        // 这里按寄存器表格把保持寄存器区映射出来，并至少保留一个可写寄存器。
        {
            int maxAddr = -1;
            for (const auto &r : m_registers) {
                if (r.address >= 0 && r.address <= 65535)
                    maxAddr = qMax(maxAddr, r.address);
            }
            const quint16 count = static_cast<quint16>(maxAddr + 1 > 0 ? maxAddr + 1 : 1);
            QModbusDataUnitMap map;
            map.insert(QModbusDataUnit::HoldingRegisters,
                       QModbusDataUnit(QModbusDataUnit::HoldingRegisters, 0, count));
            m_modbusServer->setMap(map);
        }

        // 用寄存器表格初始化服务器的数据单元
        syncServerRegisters();

        connect(m_modbusServer, &QModbusServer::dataWritten,
                this, &ModbusNode::onServerDataWritten);
        connect(m_modbusServer, &QModbusDevice::stateChanged,
                this, &ModbusNode::onServerStateChanged);

        if (!m_modbusServer->connectDevice()) {
            m_connected = false;
            setParamDirect(QStringLiteral("connected"), false);
            emit communicationError(
                QStringLiteral("Modbus\u670D\u52A1\u5668\u542F\u52A8\u5931\u8D25: %1")
                    .arg(m_modbusServer->errorString()));
            return false;
        }

        m_connected = true;
        setParamDirect(QStringLiteral("connected"), true);
        emit connectionOpened();
        VFP_DEBUG << "Modbus server listening on port"
                  << m_params.value(QStringLiteral("port"), 502).toInt();
        return true;
    }

    // ===== 客户端模式：连接外部设备 =====
    QString connType = m_params.value(QStringLiteral("connectionType")).toString();
    if (connType == QStringLiteral("TCP")) {
        m_modbus = new QModbusTcpClient(this);
    } else {
        m_modbus = new QModbusRtuSerialMaster(this);
    }

    if (!m_modbus) return false;

    // 连接参数
    if (connType == QStringLiteral("TCP")) {
        m_modbus->setConnectionParameter(QModbusDevice::NetworkPortParameter,
                                          m_params.value(QStringLiteral("port"), 502).toInt());
        m_modbus->setConnectionParameter(QModbusDevice::NetworkAddressParameter,
                                          m_params.value(QStringLiteral("host")).toString());
    } else {
        // RTU（RS485）：必须先下发串口参数，否则 QModbusRtuSerialMaster 无从连接
        // （历史缺陷：这里一个参数都不设，配了 RTU 只能无限重连循环，功能不可用）
        m_modbus->setConnectionParameter(QModbusDevice::SerialPortNameParameter,
                                          m_params.value(QStringLiteral("portName")).toString());
        m_modbus->setConnectionParameter(QModbusDevice::SerialBaudRateParameter,
                                          m_params.value(QStringLiteral("baudRate"), 9600).toInt());
        m_modbus->setConnectionParameter(QModbusDevice::SerialDataBitsParameter,
                                          m_params.value(QStringLiteral("dataBits"), 8).toInt());
        m_modbus->setConnectionParameter(QModbusDevice::SerialStopBitsParameter,
                                          m_params.value(QStringLiteral("stopBits"), 1).toInt());
        // 校验：None / Even / Odd → QSerialPort::Parity 枚举值 0 / 2 / 3
        const QString parity =
            m_params.value(QStringLiteral("parity"), QStringLiteral("None")).toString().toLower();
        int parityValue = 0;
        if (parity == QStringLiteral("even") || parity == QStringLiteral("偶"))
            parityValue = 2;
        else if (parity == QStringLiteral("odd") || parity == QStringLiteral("奇"))
            parityValue = 3;
        m_modbus->setConnectionParameter(QModbusDevice::SerialParityParameter, parityValue);
    }

    // 状态变更信号
    connect(m_modbus, &QModbusClient::stateChanged,
            this, &ModbusNode::onModbusStateChanged);

    if (!m_modbus->connectDevice()) {
        m_connected = false;
        setParamDirect(QStringLiteral("connected"), false);
        emit communicationError(QStringLiteral("Modbus\u8FDE\u63A5\u5931\u8D25"));

        // 自动重连
        if (m_autoReconnect && m_reconnectTimer) {
            m_reconnectTimer->start(m_reconnectInterval);
        }
        return false;
    }

    m_connected = true;
    setParamDirect(QStringLiteral("connected"), true);
    m_slaveAddress = m_params.value(QStringLiteral("slaveAddress"), 1).toInt();
    emit connectionOpened();

    // 启动轮询
    startPolling();
    return true;
}

void ModbusNode::closeConnection()
{
    // 状态守卫（N2 同款）：以"曾连接"为唯一判据，与 TCP/串口/PLC 一致——照常拆解设备，
    // 仅抑制 emit。旧实现按"对象全空"判据，而客户端连接失败后 m_modbus 残留非空（openConnection
    // 失败分支不置空），于是每 3s 重连后仍发一次假 connectionClosed → CM 清帧缓冲+刷 UI
    // （恰是 N2 要消灭的形态）。
    const bool was = m_connected;
    stopPolling();
    m_pendingQueue.clear();

    if (m_reconnectTimer) m_reconnectTimer->stop();

    if (m_modbus) {
        m_modbus->disconnectDevice();
        m_modbus->deleteLater();
        m_modbus = nullptr;
    }
    if (m_modbusServer) {
        m_modbusServer->disconnectDevice();
        m_modbusServer->deleteLater();
        m_modbusServer = nullptr;
    }
    m_connected = false;
    setParamDirect(QStringLiteral("connected"), false);
    if (was)
        emit connectionClosed();
}

bool ModbusNode::isConnected() const
{
    return m_connected;
}

bool ModbusNode::isServerListening() const
{
    return m_role == MODBUS_SERVER && m_modbusServer
           && m_modbusServer->state() == QModbusDevice::ConnectedState;
}

void ModbusNode::syncServerRegisters()
{
    if (!m_modbusServer) return;

    // 将每个寄存器写入服务器数据单元（Hold 寄存器）
    for (const auto &r : m_registers) {
        if (r.address < 0 || r.address > 65535) continue;
        m_modbusServer->setData(
            QModbusDataUnit::HoldingRegisters,
            static_cast<quint16>(r.address),
            static_cast<quint16>(static_cast<int>(r.currentValue)));
    }
}

void ModbusNode::setRegisters(const QList<ModbusRegisterItem> &regs)
{
    m_registers = regs;
    m_currentRegIdx = 0;
    if (m_role == MODBUS_SERVER && m_modbusServer) {
        syncServerRegisters();
    }
}

bool ModbusNode::setLocalRegisterValue(int address, quint16 value)
{
    // 更新本地寄存器表
    for (auto &r : m_registers) {
        if (r.address == address) {
            r.currentValue = static_cast<double>(value);
            r.displayValue = QString::number(value);
            break;
        }
    }

    if (m_role == MODBUS_SERVER && m_modbusServer) {
        bool ok = m_modbusServer->setData(
            QModbusDataUnit::HoldingRegisters,
            static_cast<quint16>(address), value);
        if (ok) {
            emit registerCurrentValueChanged(address, static_cast<double>(value),
                                             QString::number(value));
        }
        return ok;
    }
    // 客户端模式下作为写请求发送给外部设备
    return writeRegister(address, value);
}

void ModbusNode::onServerDataWritten(QModbusDataUnit::RegisterType table, int address, int size)
{
    if (table != QModbusDataUnit::HoldingRegisters || !m_modbusServer) return;

    for (int i = 0; i < size; ++i) {
        const int regAddr = address + i;

        // 高字折叠：本地址若是某个宽类型寄存器（int32/uint32/float）的"高字"
        // （其基础地址 regAddr-1 配置为宽类型），该值已在基础地址那次迭代组装进同一笔
        // 逻辑值——此处直接跳过，避免同一次写向事件链投两帧（旧实现会多投一帧错误高字）。
        const int base = regAddr - 1;
        bool isHighWordOfWide = false;
        for (int k = 0; k < m_registers.size(); ++k) {
            if (m_registers[k].address == base) {
                const QString dt = m_registers[k].dataType;
                if (dt == QStringLiteral("int32") || dt == QStringLiteral("uint32")
                    || dt == QStringLiteral("float")) {
                    isHighWordOfWide = true;
                }
                break;
            }
        }
        if (isHighWordOfWide)
            continue;

        if (regAddr < 0 || regAddr > 65535)
            continue;

        quint16 first = 0;
        if (!m_modbusServer->data(QModbusDataUnit::HoldingRegisters,
                                  static_cast<quint16>(regAddr), &first)) {
            continue;
        }

        // 只处理"已配置"的寄存器：服务器映射表（setMap 连续覆盖到 maxAddr）允许写入任意地址，
        // 但监控表不应被外部客户端无限撑大——未配置地址直接忽略，不再无脑补行。
        int idx = -1;
        for (int k = 0; k < m_registers.size(); ++k) {
            if (m_registers[k].address == regAddr) { idx = k; break; }
        }
        if (idx < 0)
            continue;

        // 32 位类型跨 2 个寄存器：按该行配置的字节序组装 4 字节；否则服务器模式下
        // int32/uint32/float 永远只有 2 字节 → 接收事件按长度不符丢弃（换了新死的半边）
        const QString dataType = m_registers[idx].dataType;
        const QString byteOrder = m_registers[idx].byteOrder;
        const bool wide = (dataType == QStringLiteral("int32")
                           || dataType == QStringLiteral("uint32")
                           || dataType == QStringLiteral("float"));
        QVector<quint16> words;
        words.append(first);
        if (wide) {
            // 边界：基础地址已是 65535 时，高字地址 65536 会回绕到 0 读到错误值，必须截断
            if (regAddr + 1 <= 65535) {
                quint16 second = 0;
                if (m_modbusServer->data(QModbusDataUnit::HoldingRegisters,
                                         static_cast<quint16>(regAddr + 1), &second)) {
                    words.append(second);
                }
            }
        }
        const QByteArray rawBytes = assembleRegisterBytes(words, byteOrder);

        // 与轮询路径对齐：currentValue 存解析值（此前服务器模式存 16 位原始字，int32/float 行显示半个值）
        m_registers[idx].currentValue = parseRawToValue(rawBytes, dataType, byteOrder);
        m_registers[idx].displayValue = QString::number(
            m_registers[idx].currentValue, 'f', dataType == QStringLiteral("float") ? 4 : 0);
        emit registerCurrentValueChanged(regAddr, m_registers[idx].currentValue,
                                        m_registers[idx].displayValue);
        emit registerWrittenByClient(regAddr, first);

        // 关键修复：服务器模式此前只发上面两个 UI 信号、**从不发 registerValueChanged** ——
        // 接收事件/触发链路（CM 转发 → GlobalTriggerManager → 流程）对"客户端写服务器"
        // 完全失聪，等于整条死。按与轮询路径同一「原始字节」约定补齐，
        // 且只在值变化时发（与轮询一致，避免同值重复写反复触发）。
        if (!m_registers[idx].hasLastValue || rawBytes != m_registers[idx].lastRaw) {
            m_registers[idx].lastRaw = rawBytes;
            m_registers[idx].hasLastValue = true;
            emit registerValueChanged(regAddr, rawBytes, dataType);
        }
    }
}

void ModbusNode::onServerStateChanged(int state)
{
    if (state == QModbusDevice::ConnectedState) {
        VFP_DEBUG << "Modbus server client connected";
    } else if (state == QModbusDevice::UnconnectedState) {
        VFP_DEBUG << "Modbus server client disconnected";
    }
}

void ModbusNode::setParam(const QString &name, const QVariant &value)
{
    if (name == QStringLiteral("role")) {
        applyRoleParam(value.toString());
    } else if (name == QStringLiteral("autoReconnect")) {
        m_autoReconnect = value.toBool();
    } else if (name == QStringLiteral("reconnectInterval")) {
        m_reconnectInterval = qMax(500, value.toInt());
    } else if (name == QStringLiteral("pollInterval")) {
        m_pollInterval = qMax(10, value.toInt());
        if (m_pollTimer) m_pollTimer->setInterval(m_pollInterval);
    } else if (name == QStringLiteral("slaveAddress")) {
        m_slaveAddress = value.toInt();
    }
    CommunicationNodeBase::setParam(name, value);
}

QVariant ModbusNode::getParam(const QString &name) const
{
    return CommunicationNodeBase::getParam(name);
}

// ---- 轮询 ----

void ModbusNode::startPolling()
{
    if (!m_pollTimer) return;
    m_currentRegIdx = 0;
    m_pollTimer->setInterval(m_pollInterval);
    m_pollTimer->start();
}

void ModbusNode::stopPolling()
{
    if (m_pollTimer) m_pollTimer->stop();
}

void ModbusNode::onPollTimeout()
{
    if (!m_connected || !m_modbus || m_registers.isEmpty()) return;

    // 按寄存器表格逐一读取（一次读一个，避免冲突）
    // 先处理队列中的残留请求
    if (!m_pendingQueue.isEmpty()) return;

    // 找到下一个启用的寄存器
    int startIdx = m_currentRegIdx;
    for (int i = 0; i < m_registers.size(); ++i) {
        int idx = (startIdx + i) % m_registers.size();
        const auto &reg = m_registers[idx];
        if (!reg.enabled) continue;

        int byteCount = 2;
        if (reg.dataType == QStringLiteral("int32") || reg.dataType == QStringLiteral("float"))
            byteCount = 4;

        int regCount = byteCount / 2; // 每个寄存器 2 字节

        PendingRead pr;
        pr.slaveAddr = m_slaveAddress;
        pr.regAddr = reg.address;
        pr.count = regCount;
        pr.regIndex = idx;

        m_pendingQueue.append(pr);
        if (!readRegister(m_slaveAddress, reg.address, regCount)) {
            // 请求未能发出（离线/忙）：立刻回滚队列条目——否则 pendingQueue 非空会让
            // 后续每次轮询都在入口处直接 return（轮询永久冻结且无任何报警）
            m_pendingQueue.removeLast();
            VFP_DEBUG << "Modbus read dispatch failed at address" << reg.address;
        }
        // 只推进一步。历史缺陷：此处推进后函数末尾又无条件推进一次 → 偶数个寄存器时
        // 每逢一个跳过一个，一半寄存器永久不刷新，界面/日志却"看起来正常"。
        m_currentRegIdx = (idx + 1) % m_registers.size();
        break;
    }
}

bool ModbusNode::readRegister(int slaveAddr, int regAddr, int count)
{
    if (!m_modbus) return false;

    QModbusDataUnit readUnit(QModbusDataUnit::HoldingRegisters, regAddr, count);
    QModbusReply *reply = m_modbus->sendReadRequest(readUnit, slaveAddr);
    if (!reply) {
        // 请求未发出（设备离线/忙）：返回失败，调用方回滚 pendingQueue 条目。
        // 否则该条目永远无人消费 → 轮询入口 !isEmpty 检查让轮询永久冻结且无报警。
        return false;
    }

    auto handler = [this, reply, regAddr]() {
                // 查找对应的待处理项
                int regIndex = -1;
                for (int i = 0; i < m_pendingQueue.size(); ++i) {
                    if (m_pendingQueue[i].regAddr == regAddr) {
                        regIndex = m_pendingQueue[i].regIndex;
                        m_pendingQueue.removeAt(i);
                        break;
                    }
                }

                if (reply->error() == QModbusDevice::NoError) {
                    const QModbusDataUnit unit = reply->result();
                    const QVector<quint16> values = unit.values();

                    if (!values.isEmpty() && regIndex >= 0 && regIndex < m_registers.size()) {
                        auto &reg = m_registers[regIndex];

                        // 将 values 转为原始字节（与服务器写入路径共用同一字节序约定）
                        const QByteArray raw = assembleRegisterBytes(values, reg.byteOrder);

                        // 更新当前解析值
                        double parsed = parseRawToValue(raw, reg.dataType, reg.byteOrder);
                        reg.currentValue = parsed;
                        reg.displayValue = QString::number(parsed, 'f',
                            reg.dataType == QStringLiteral("float") ? 4 : 0);
                        emit registerCurrentValueChanged(reg.address, parsed, reg.displayValue);

                        // 检查值是否变化
                        if (!reg.hasLastValue || raw != reg.lastRaw) {
                            reg.lastRaw = raw;
                            reg.hasLastValue = true;

                            emit registerValueChanged(
                                reg.address, raw, reg.dataType);
                        }
                    }
                } else {
                    VFP_DEBUG << "Modbus read error at address" << regAddr << ":" << reply->errorString();
                }
                reply->deleteLater();
    };
    if (reply->isFinished())
        handler();   // 同步完成（罕见）：必须立即消费，否则 pendingQueue 条目永久滞留
    else
        connect(reply, &QModbusReply::finished, this, handler);
    return true;
}

void ModbusNode::onModbusStateChanged(int state)
{
    if (state == QModbusDevice::ConnectedState) {
        if (!m_connected) {
            m_connected = true;
            setParamDirect(QStringLiteral("connected"), true);
            emit connectionOpened();
            startPolling();
        }
        if (m_reconnectTimer) m_reconnectTimer->stop();
    } else if (state == QModbusDevice::UnconnectedState) {
        if (m_connected) {
            m_connected = false;
            setParamDirect(QStringLiteral("connected"), false);
            emit connectionClosed();
            stopPolling();
        }

        // 自动重连
        if (m_autoReconnect && m_reconnectTimer) {
            m_reconnectTimer->start(m_reconnectInterval);
        }
    }
}

void ModbusNode::run(bool /*autoSwitch*/)
{
    // Modbus 运行时不执行图像处理
    // 轮询数据在 onPollTimeout 中异步完成
    // 输出会通过 registerValueChanged 信号 + receiveEvent 系统路由
}

double ModbusNode::parseRawToValue(const QByteArray &raw, const QString &dataType, const QString &byteOrder) const
{
    if (raw.isEmpty()) return 0.0;

    if (dataType == QStringLiteral("int16")) {
        // 16 位：assembleRegisterBytes 已按 byteOrder 归一化为最终字节序列，此处统一大端解释
        // （与 32 位路径一致；此前 16 位用 order 小端——单寄存器时与"归一化不交换"相互抵消看似
        // 正确，但 16/32 位逻辑不统一）。强制大端对单寄存器行为中性：ABCD 不变，BADC/DCBA 与
        // "归一化已交换"配大端结果完全一致。
        if (raw.size() < 2) return 0.0;
        qint16 val;
        QDataStream s(raw);
        s.setByteOrder(QDataStream::BigEndian);
        s >> val;
        return static_cast<double>(val);
    } else if (dataType == QStringLiteral("uint16")) {
        if (raw.size() < 2) return 0.0;
        quint16 val;
        QDataStream s(raw);
        s.setByteOrder(QDataStream::BigEndian);
        s >> val;
        return static_cast<double>(val);
    } else if (dataType == QStringLiteral("int32") || dataType == QStringLiteral("uint32")
               || dataType == QStringLiteral("float")) {
        // 32 位：assembleRegisterBytes 已按 byteOrder 把寄存器归一化为最终字节序列，
        // 此处统一大端解释（与 ReceiveEvent::extractValue 规范一致）；强制大端可同时修掉
        // BADC 双重交换与 DCBA 反向（旧实现按 order 小端再换一次 = S13 另一半）。
        // uint32 此前无分支 → 恒返回 0.0（uint32 恒 0 漏洞）。
        if (raw.size() < 4) return 0.0;
        QDataStream s(raw);
        s.setByteOrder(QDataStream::BigEndian);
        if (dataType == QStringLiteral("int32")) {
            qint32 val;
            s >> val;
            return static_cast<double>(val);
        } else if (dataType == QStringLiteral("uint32")) {
            quint32 val;
            s >> val;
            return static_cast<double>(val);
        } else {
            float val;
            s >> val;
            return static_cast<double>(val);
        }
    }
    return 0.0;
}

bool ModbusNode::writeRegister(int address, quint16 value)
{
    if (!m_connected || !m_modbus) return false;

    QModbusDataUnit writeUnit(QModbusDataUnit::HoldingRegisters, address, 1);
    writeUnit.setValue(0, value);

    QModbusReply *reply = m_modbus->sendWriteRequest(writeUnit, m_slaveAddress);
    if (reply) {
        if (!reply->isFinished()) {
            connect(reply, &QModbusReply::finished, this, [this, reply, address]() {
                if (reply->error() == QModbusDevice::NoError) {
                    VFP_DEBUG << "Modbus write success at address" << address;
                } else {
                    VFP_DEBUG << "Modbus write error at address" << address << ":" << reply->errorString();
                    emit communicationError(
                        QStringLiteral("Modbus写寄存器失败(地址%1):%2")
                            .arg(address).arg(reply->errorString()));
                }
                reply->deleteLater();
            });
        } else {
            reply->deleteLater();
        }
        return true;
    }
    return false;
}

double ModbusNode::registerCurrentValue(int address) const
{
    for (const auto &r : m_registers) {
        if (r.address == address) return r.currentValue;
    }
    return 0.0;
}

QString ModbusNode::registerDisplayValue(int address) const
{
    for (const auto &r : m_registers) {
        if (r.address == address) return r.displayValue;
    }
    return QString();
}

QJsonObject ModbusNode::toJson() const
{
    QJsonObject obj = CommunicationNodeBase::toJson();
    obj[QStringLiteral("role")] = (m_role == MODBUS_SERVER)
        ? QStringLiteral("\u670D\u52A1\u5668") : QStringLiteral("\u5BA2\u6237\u7AEF");
    obj[QStringLiteral("autoReconnect")] = m_autoReconnect;
    obj[QStringLiteral("reconnectInterval")] = m_reconnectInterval;
    obj[QStringLiteral("pollInterval")] = m_pollInterval;
    obj[QStringLiteral("slaveAddress")] = m_slaveAddress;

    QJsonArray regsArr;
    for (const auto &r : m_registers) {
        QJsonObject ro;
        ro[QStringLiteral("address")] = r.address;
        ro[QStringLiteral("dataType")] = r.dataType;
        ro[QStringLiteral("byteOrder")] = r.byteOrder;
        ro[QStringLiteral("accessMode")] = r.accessMode;
        ro[QStringLiteral("enabled")] = r.enabled;
        ro[QStringLiteral("description")] = r.description;
        regsArr.append(ro);
    }
    obj[QStringLiteral("registers")] = regsArr;
    return obj;
}

void ModbusNode::fromJson(const QJsonObject &json)
{
    CommunicationNodeBase::fromJson(json);
    QString role = json[QStringLiteral("role")].toString(QStringLiteral("\u5BA2\u6237\u7AEF"));
    m_role = (role == QStringLiteral("\u670D\u52A1\u5668") || role == QStringLiteral("Server"))
        ? MODBUS_SERVER : MODBUS_CLIENT;
    m_params[QStringLiteral("role")] = (m_role == MODBUS_SERVER)
        ? QStringLiteral("\u670D\u52A1\u5668") : QStringLiteral("\u5BA2\u6237\u7AEF");
    m_autoReconnect = json[QStringLiteral("autoReconnect")].toBool(true);
    m_reconnectInterval = json[QStringLiteral("reconnectInterval")].toInt(3000);
    m_pollInterval = json[QStringLiteral("pollInterval")].toInt(100);
    m_slaveAddress = json[QStringLiteral("slaveAddress")].toInt(1);

    m_registers.clear();
    QJsonArray regsArr = json[QStringLiteral("registers")].toArray();
    for (const auto &v : regsArr) {
        QJsonObject ro = v.toObject();
        ModbusRegisterItem r;
        r.address = ro[QStringLiteral("address")].toInt();
        r.dataType = ro[QStringLiteral("dataType")].toString(QStringLiteral("int16"));
        r.byteOrder = ro[QStringLiteral("byteOrder")].toString(QStringLiteral("ABCD"));
        r.accessMode = ro[QStringLiteral("accessMode")].toString(QStringLiteral("Read"));
        r.enabled = ro[QStringLiteral("enabled")].toBool(true);
        r.description = ro[QStringLiteral("description")].toString();
        m_registers.append(r);
    }

    m_params[QStringLiteral("autoReconnect")] = m_autoReconnect;
    m_params[QStringLiteral("reconnectInterval")] = m_reconnectInterval;
    m_params[QStringLiteral("pollInterval")] = m_pollInterval;
    m_params[QStringLiteral("slaveAddress")] = m_slaveAddress;
}
