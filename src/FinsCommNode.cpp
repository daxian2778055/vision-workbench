#include "FinsCommNode.h"
#include "FinsFrame.h"
#include "AppLog.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>

namespace {

/// FINS/TCP 帧头命令（不是 FINS 命令码）：0 节点地址请求 / 1 节点地址响应 / 2 帧发送
constexpr quint32 kTcpCmdNodeAddressRequest = 0x00000000u;
constexpr quint32 kTcpCmdNodeAddressResponse = 0x00000001u;
constexpr quint32 kTcpCmdFrameSend = 0x00000002u;

/// FINS 命令码：0101 = Memory Area Read，0102 = Memory Area Write（W342 5-3）
constexpr quint16 kCmdMemoryRead = 0x0101u;
constexpr quint16 kCmdMemoryWrite = 0x0102u;

constexpr int kTcpHeaderBytes = 16;

}   // namespace

FinsCommNode::FinsCommNode(Transport t, QObject *parent)
    : CommunicationNodeBase(parent)
    , m_transport(t)
{
    setName(t == FinsTcp ? QStringLiteral("FINS/TCP\u901A\u4FE1")
                         : QStringLiteral("FINS/UDP\u901A\u4FE1"));
    // 传输层语义直接复用既有两类：TCP 客户端 / UDP
    m_commType = (t == FinsTcp) ? CommunicationNodeBase::TCP_CLIENT
                                : CommunicationNodeBase::UDP;
    m_type = OUTPUT;
}

void FinsCommNode::init()
{
    // 通信节点不处理图像：不调 HalconNode::init()（与 ModbusNode 同口径），只建端口与参数表
    addInputPort(QStringLiteral("data"), PortDataType::String);
    addOutputPort(QStringLiteral("\u5BC4\u5B58\u5668\u6570\u636E"), PortDataType::String);

    m_params[QStringLiteral("targetIp")] = QStringLiteral("127.0.0.1");
    // 7920 取自 VM 4.4 的 OmronFinsTcpModuleParam.xml 默认值。注意：Omron 设备出厂常见
    // FINS/TCP 端口是 9600，现场按 PLC 实际设置改本项即可（不做硬编码假设）。
    m_params[QStringLiteral("targetPort")] = 7920;
    m_params[QStringLiteral("pollInterval")] = 500;
    m_params[QStringLiteral("timeoutMs")] = 1000;
    m_params[QStringLiteral("autoReconnect")] = true;
    m_params[QStringLiteral("reconnectInterval")] = 3000;
    m_params[QStringLiteral("localNode")] = 0;    // 0 = 用握手响应里分配的本机节点号
    m_params[QStringLiteral("remoteNode")] = 0;   // 0 = 用握手响应里的 PLC 节点号
    setParamDirect(QStringLiteral("connected"), false);

    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &FinsCommNode::onPollTimeout);

    m_requestTimer = new QTimer(this);
    m_requestTimer->setSingleShot(true);
    connect(m_requestTimer, &QTimer::timeout, this, &FinsCommNode::onRequestTimeout);

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &FinsCommNode::onReconnectTimeout);
}

void FinsCommNode::setParam(const QString &name, const QVariant &value)
{
    // 钳制放写侧（参数表只存钳后值，运行期与序列化共用一个口径）
    if (name == QStringLiteral("pollInterval")) {
        const int v = qMax(10, value.toInt());
        CommunicationNodeBase::setParam(name, v);
        if (m_pollTimer && m_pollTimer->isActive())
            m_pollTimer->setInterval(v);
        return;
    }
    if (name == QStringLiteral("reconnectInterval")) {
        CommunicationNodeBase::setParam(name, qMax(500, value.toInt()));
        return;
    }
    if (name == QStringLiteral("timeoutMs")) {
        CommunicationNodeBase::setParam(name, qMax(100, value.toInt()));
        return;
    }
    CommunicationNodeBase::setParam(name, value);
}

bool FinsCommNode::openConnection()
{
    closeConnection();          // 清理旧链路（内部置 m_userClosed=true，下面复位）
    m_userClosed = false;
    m_handshakeDone = false;
    m_rxBuffer.clear();
    m_pollIndex = 0;
    m_requestPending = false;
    m_pendingIndex = -1;
    if (m_reconnectTimer)
        m_reconnectTimer->stop();

    const QString ip = getParam(QStringLiteral("targetIp")).toString().trimmed();
    const int port = getParam(QStringLiteral("targetPort")).toInt();
    QHostAddress addr;
    if (!addr.setAddress(ip)) {
        emit communicationError(QStringLiteral("FINS 目标地址无效（需 IP 字面量）: %1").arg(ip));
        m_connected = false;
        setParamDirect(QStringLiteral("connected"), false);
        return false;
    }
    if (port < 1 || port > 65535) {
        emit communicationError(QStringLiteral("FINS 目标端口 %1 非法（应在 1~65535）").arg(port));
        m_connected = false;
        setParamDirect(QStringLiteral("connected"), false);
        return false;
    }

    if (m_transport == FinsTcp) {
        m_tcp = new QTcpSocket(this);
        QTcpSocket *socket = m_tcp;
        // 所有信号都校验 sender 是"当前 socket"：重连会替换 socket，旧 socket 的延迟信号
        // 若被当作当前连接处理，会把健康链路误判为断开（TcpCommNode 同款防抖）。
        connect(socket, &QTcpSocket::connected, this, [this, socket]() {
            if (socket == m_tcp)
                onTcpConnected();
        });
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            if (socket == m_tcp)
                onTcpReadyRead();
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            if (socket == m_tcp)
                onTcpDisconnected();
        });
        connect(socket, &QAbstractSocket::errorOccurred, this,
                [this, socket](QAbstractSocket::SocketError) {
            if (socket != m_tcp)
                return;
            // 已连上之后的错误交给 disconnected 分支收口（避免两条路径各拆一次链路）
            if (m_connected)
                return;
            emit communicationError(QStringLiteral("FINS/TCP 连接失败: %1").arg(socket->errorString()));
            m_tcp = nullptr;
            socket->deleteLater();
            m_handshakeDone = false;
            setParamDirect(QStringLiteral("connected"), false);
            if (!m_userClosed && getParam(QStringLiteral("autoReconnect")).toBool()
                && m_reconnectTimer)
                m_reconnectTimer->start(getParam(QStringLiteral("reconnectInterval")).toInt());
        });
        // 异步连接：不阻塞调用方（界面/流程线程）
        socket->connectToHost(addr, quint16(port));
        return true;
    }

    // FINS/UDP：绑定临时本地端口（响应按源地址回送），每个数据报即一帧裸 FINS 帧
    m_udp = new QUdpSocket(this);
    QUdpSocket *udp = m_udp;
    if (!udp->bind(QHostAddress::AnyIPv4, 0)) {
        emit communicationError(QStringLiteral("FINS/UDP 本地端口绑定失败: %1").arg(udp->errorString()));
        m_udp = nullptr;
        udp->deleteLater();
        m_connected = false;
        setParamDirect(QStringLiteral("connected"), false);
        return false;
    }
    connect(udp, &QUdpSocket::readyRead, this, [this, udp]() {
        if (udp == m_udp)
            onUdpReadyRead();
    });
    m_connected = true;
    setParamDirect(QStringLiteral("connected"), true);
    emit connectionOpened();
    if (m_pollTimer) {
        m_pollTimer->setInterval(getParam(QStringLiteral("pollInterval")).toInt());
        m_pollTimer->start();
    }
    return true;
}

void FinsCommNode::closeConnection()
{
    m_userClosed = true;   // 主动关闭：不触发自动重连
    if (m_pollTimer)
        m_pollTimer->stop();
    if (m_requestTimer)
        m_requestTimer->stop();
    if (m_reconnectTimer)
        m_reconnectTimer->stop();
    m_requestPending = false;
    m_pendingIndex = -1;
    m_handshakeDone = false;
    m_rxBuffer.clear();

    const bool wasConnected = m_connected;
    QTcpSocket *tcp = m_tcp;
    m_tcp = nullptr;       // 先解除引用：旧 socket 的延迟信号会被归属校验忽略
    if (tcp) {
        tcp->disconnectFromHost();
        tcp->deleteLater();
    }
    QUdpSocket *udp = m_udp;
    m_udp = nullptr;
    if (udp) {
        udp->close();
        udp->deleteLater();
    }
    m_connected = false;
    setParamDirect(QStringLiteral("connected"), false);   // 参数照写（不 emit），界面数值保持真实
    if (wasConnected)
        emit connectionClosed();
}

bool FinsCommNode::isConnected() const
{
    if (m_transport == FinsTcp)
        return m_connected && m_tcp
               && m_tcp->state() == QAbstractSocket::ConnectedState;
    return m_connected && m_udp != nullptr;
}

void FinsCommNode::onTcpConnected()
{
    if (!m_tcp)
        return;
    if (m_reconnectTimer)
        m_reconnectTimer->stop();
    m_rxBuffer.clear();
    m_handshakeDone = false;
    // 先做节点地址握手：PLC 在响应里分配本机节点号，同时给出自己的节点号
    const QByteArray req = Fins::nodeAddressRequest();
    const qint64 written = m_tcp->write(req);
    if (written != req.size()) {
        emit communicationError(QStringLiteral("FINS/TCP 节点地址请求发送失败: %1")
                                    .arg(m_tcp->errorString()));
        dropLink(QString());
        return;
    }
    m_tcp->flush();
}

void FinsCommNode::onTcpReadyRead()
{
    if (!m_tcp)
        return;
    m_rxBuffer.append(m_tcp->readAll());

    for (;;) {
        quint32 len = 0;
        quint32 cmd = 0;
        quint32 err = 0;
        int headerBytes = 0;
        if (!Fins::parseTcpHeader(m_rxBuffer, &len, &cmd, &err, &headerBytes)) {
            // 不足 16 字节 = 正常半包，保留缓冲等后续数据；
            // 已有完整帧头却拆不开（magic 错 / 长度非法）才是脏帧，需丢弃整段。
            if (m_rxBuffer.size() >= kTcpHeaderBytes) {
                emit communicationError(QStringLiteral(
                    "FINS/TCP 帧头非法（magic 或长度不符），已丢弃 %1 字节").arg(m_rxBuffer.size()));
                m_rxBuffer.clear();
            }
            break;
        }
        if (headerBytes > m_rxBuffer.size())
            break;   // 半包：等后续字节

        const QByteArray data = m_rxBuffer.mid(kTcpHeaderBytes, int(len) - 8);
        m_rxBuffer.remove(0, headerBytes);

        if (err != 0) {
            emit communicationError(QStringLiteral("FINS/TCP 帧头错误码: 0x%1")
                                        .arg(err, 8, 16, QChar('0')));
            continue;
        }
        if (cmd == kTcpCmdNodeAddressResponse) {
            quint32 client = 0;
            quint32 server = 0;
            if (!Fins::parseNodeAddressResponse(data, &client, &server)) {
                emit communicationError(QStringLiteral("FINS/TCP 节点地址响应过短（%1 字节）")
                                            .arg(data.size()));
                continue;
            }
            m_clientNode = client;
            m_serverNode = server;
            m_handshakeDone = true;
            VFP_DEBUG << "FINS/TCP node address handshake: client" << client << "server" << server;
            if (!m_connected) {
                m_connected = true;
                setParamDirect(QStringLiteral("connected"), true);
                emit connectionOpened();
            }
            if (m_pollTimer) {
                m_pollTimer->setInterval(getParam(QStringLiteral("pollInterval")).toInt());
                m_pollTimer->start();
            }
        } else if (cmd == kTcpCmdFrameSend) {
            handleFinsResponse(data);
        } else {
            emit communicationError(QStringLiteral("FINS/TCP 未知帧命令: 0x%1")
                                        .arg(cmd, 8, 16, QChar('0')));
        }
    }
}

void FinsCommNode::onTcpDisconnected()
{
    if (m_pollTimer)
        m_pollTimer->stop();
    if (m_requestTimer)
        m_requestTimer->stop();
    m_requestPending = false;
    m_pendingIndex = -1;
    m_handshakeDone = false;
    m_rxBuffer.clear();

    const bool wasConnected = m_connected;
    m_connected = false;
    setParamDirect(QStringLiteral("connected"), false);
    QTcpSocket *tcp = m_tcp;
    m_tcp = nullptr;
    if (tcp)
        tcp->deleteLater();

    if (wasConnected) {
        emit communicationError(QStringLiteral("FINS/TCP 连接已断开"));
        emit connectionClosed();
    }
    if (!m_userClosed && getParam(QStringLiteral("autoReconnect")).toBool() && m_reconnectTimer)
        m_reconnectTimer->start(getParam(QStringLiteral("reconnectInterval")).toInt());
}

void FinsCommNode::onUdpReadyRead()
{
    if (!m_udp)
        return;
    while (m_udp->hasPendingDatagrams()) {
        const qint64 size = qMax<qint64>(0, m_udp->pendingDatagramSize());
        QByteArray dg;
        dg.resize(int(size));
        const qint64 n = m_udp->readDatagram(dg.data(), dg.size());
        if (n <= 0)
            break;
        dg.resize(int(n));
        // FINS/UDP 每个数据报就是一帧裸 FINS 帧（没有 FINS/TCP 帧头）
        handleFinsResponse(dg);
    }
}

void FinsCommNode::onRequestTimeout()
{
    if (!m_requestPending)
        return;
    m_requestPending = false;
    m_pendingIndex = -1;
    emit communicationError(QStringLiteral("FINS 请求超时（%1 ms 内未收到响应）")
                                .arg(getParam(QStringLiteral("timeoutMs")).toInt()));
    if (m_transport == FinsTcp) {
        // 超时视为链路不健康：断开并按配置重连（UDP 无连接，清 pending 后继续轮询即可）
        dropLink(QString());
    }
}

void FinsCommNode::onReconnectTimeout()
{
    if (m_userClosed || m_connected)
        return;
    if (!getParam(QStringLiteral("autoReconnect")).toBool())
        return;
    openConnection();
}

void FinsCommNode::dropLink(const QString &why)
{
    if (m_pollTimer)
        m_pollTimer->stop();
    if (m_requestTimer)
        m_requestTimer->stop();
    m_requestPending = false;
    m_pendingIndex = -1;
    m_handshakeDone = false;
    m_rxBuffer.clear();

    if (!why.isEmpty())
        emit communicationError(why);

    const bool wasConnected = m_connected;
    QTcpSocket *tcp = m_tcp;
    m_tcp = nullptr;
    if (tcp) {
        tcp->disconnectFromHost();
        tcp->deleteLater();
    }
    m_connected = false;
    setParamDirect(QStringLiteral("connected"), false);
    if (wasConnected)
        emit connectionClosed();
    if (!m_userClosed && getParam(QStringLiteral("autoReconnect")).toBool() && m_reconnectTimer)
        m_reconnectTimer->start(getParam(QStringLiteral("reconnectInterval")).toInt());
}

void FinsCommNode::onPollTimeout()
{
    if (!m_connected)
        return;
    if (m_transport == FinsTcp && !m_handshakeDone)
        return;
    if (m_requestPending)
        return;   // 单请求在途（有超时兜底），不叠加请求
    if (m_registers.isEmpty())
        return;

    const int n = m_registers.size();
    for (int i = 0; i < n; ++i) {
        const int idx = (m_pollIndex + i) % n;
        const FinsRegisterItem &reg = m_registers.at(idx);
        if (!reg.enabled)
            continue;
        if (reg.accessMode.compare(QStringLiteral("Read"), Qt::CaseInsensitive) != 0
            && reg.accessMode.compare(QStringLiteral("ReadWrite"), Qt::CaseInsensitive) != 0)
            continue;   // 只写项不轮询
        const quint16 area = Fins::areaCode(reg.area);
        const int wc = Fins::wordCount(reg.dataType);
        if (area == 0 || wc <= 0 || reg.address < 0 || reg.address > 0xFFFF)
            continue;   // 配置不支持：跳过（写路径会把原因经 communicationError 报出）
        if (sendReadForIndex(idx)) {
            m_pollIndex = (idx + 1) % n;
            return;
        }
    }
}

bool FinsCommNode::sendReadForIndex(int index)
{
    if (index < 0 || index >= m_registers.size())
        return false;
    const FinsRegisterItem &reg = m_registers.at(index);
    const quint16 area = Fins::areaCode(reg.area);
    const int wc = Fins::wordCount(reg.dataType);
    if (area == 0 || wc <= 0)
        return false;

    const QByteArray frame = Fins::command(kCmdMemoryRead, destinationNode(), sourceNode(),
                                           Fins::readMemoryParams(area, quint16(reg.address),
                                                                  quint16(wc)));
    if (!sendFinsFrame(frame))
        return false;
    m_requestPending = true;
    m_pendingIndex = index;
    if (m_requestTimer)
        m_requestTimer->start(getParam(QStringLiteral("timeoutMs")).toInt());
    return true;
}

bool FinsCommNode::sendFinsFrame(const QByteArray &finsFrame)
{
    if (m_transport == FinsTcp) {
        if (!m_tcp || m_tcp->state() != QAbstractSocket::ConnectedState) {
            emit communicationError(QStringLiteral("FINS/TCP 发送失败: 未连接"));
            return false;
        }
        const QByteArray wire = Fins::frameSend(finsFrame);
        const qint64 written = m_tcp->write(wire);
        if (written != wire.size()) {
            emit communicationError(QStringLiteral("FINS/TCP 发送失败: %1").arg(m_tcp->errorString()));
            // 写不进去往往意味着链路已死（半开）：断开并按配置重连，不等用户手动处理
            dropLink(QString());
            return false;
        }
        m_tcp->flush();   // best-effort：成败只看 write 是否吞下全部字节
        return true;
    }

    if (!m_udp) {
        emit communicationError(QStringLiteral("FINS/UDP 发送失败: 套接字未打开"));
        return false;
    }
    QHostAddress addr;
    if (!addr.setAddress(getParam(QStringLiteral("targetIp")).toString().trimmed())) {
        emit communicationError(QStringLiteral("FINS/UDP 发送失败: 目标地址无效"));
        return false;
    }
    const int port = getParam(QStringLiteral("targetPort")).toInt();
    if (port < 1 || port > 65535) {
        emit communicationError(QStringLiteral("FINS/UDP 发送失败: 目标端口非法（%1）").arg(port));
        return false;
    }
    const qint64 n = m_udp->writeDatagram(finsFrame, addr, quint16(port));
    if (n != finsFrame.size()) {
        emit communicationError(QStringLiteral("FINS/UDP 发送失败: %1").arg(m_udp->errorString()));
        return false;
    }
    return true;
}

void FinsCommNode::handleFinsResponse(const QByteArray &finsFrame)
{
    quint16 endCode = 0;
    QByteArray data;

    if (Fins::parseResponse(finsFrame, kCmdMemoryRead, &endCode, &data)) {
        if (m_requestTimer)
            m_requestTimer->stop();
        const bool hadPending = m_requestPending;
        const int idx = m_pendingIndex;
        m_requestPending = false;
        m_pendingIndex = -1;

        if (endCode != 0) {
            emit communicationError(QStringLiteral("FINS 读寄存器失败：endCode=0x%1")
                                        .arg(endCode, 4, 16, QChar('0')));
            return;
        }
        if (!hadPending || idx < 0 || idx >= m_registers.size())
            return;   // 迟到响应（超时已判失败）：丢弃，不把旧值当新值上报

        FinsRegisterItem &reg = m_registers[idx];
        const int wc = Fins::wordCount(reg.dataType);
        if (wc <= 0 || data.size() < wc * 2) {
            emit communicationError(QStringLiteral("FINS 读响应数据不足：地址 %1 需 %2 字节，实收 %3")
                                        .arg(reg.address).arg(wc * 2).arg(data.size()));
            return;
        }
        const QByteArray raw = data.left(wc * 2);
        double value = 0.0;
        if (Fins::decodeValue(reg.dataType, raw, &value)) {
            reg.currentValue = value;
            reg.hasValue = true;
            reg.displayValue = QString::number(
                value, 'f',
                reg.dataType.compare(QStringLiteral("float"), Qt::CaseInsensitive) == 0 ? 4 : 0);
        }
        emit registerValueChanged(reg.address, raw, reg.dataType);
        return;
    }

    if (Fins::parseResponse(finsFrame, kCmdMemoryWrite, &endCode, &data)) {
        if (endCode != 0)
            emit communicationError(QStringLiteral("FINS 写寄存器被拒：endCode=0x%1")
                                        .arg(endCode, 4, 16, QChar('0')));
        return;
    }

    emit communicationError(QStringLiteral("FINS 响应命令码不匹配或帧过短（%1 字节）")
                                .arg(finsFrame.size()));
}

quint8 FinsCommNode::destinationNode() const
{
    const int param = getParam(QStringLiteral("remoteNode")).toInt();
    if (param > 0)
        return quint8(param & 0xFF);
    return quint8(m_serverNode & 0xFF);   // 0 = 用握手/响应里的 PLC 节点号
}

quint8 FinsCommNode::sourceNode() const
{
    const int param = getParam(QStringLiteral("localNode")).toInt();
    if (param > 0)
        return quint8(param & 0xFF);
    return quint8(m_clientNode & 0xFF);   // 0 = 用握手响应里分配的本机节点号
}

bool FinsCommNode::writeRegister(int address, double value)
{
    int idx = -1;
    for (int i = 0; i < m_registers.size(); ++i) {
        if (m_registers.at(i).address == address) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        emit communicationError(QStringLiteral(
            "FINS 写寄存器失败：地址 %1 未在寄存器表中登记（无法确定存储区/数据类型）").arg(address));
        return false;
    }
    if (!isConnected())
        return false;   // 未连接：调用方据返回值显式失败（不静默排队）
    if (m_transport == FinsTcp && !m_handshakeDone)
        return false;
    if (address < 0 || address > 0xFFFF)
        return false;

    const FinsRegisterItem &reg = m_registers.at(idx);
    const quint16 area = Fins::areaCode(reg.area);
    const int wc = Fins::wordCount(reg.dataType);
    if (area == 0 || wc <= 0) {
        emit communicationError(QStringLiteral(
            "FINS 写寄存器失败：地址 %1 的存储区/数据类型不受支持（%2/%3）")
            .arg(address).arg(reg.area, reg.dataType));
        return false;
    }
    const QByteArray payload = Fins::encodeValue(reg.dataType, value);
    if (payload.size() != wc * 2) {
        emit communicationError(QStringLiteral("FINS 写寄存器失败：值编码长度异常（%1）")
                                    .arg(payload.size()));
        return false;
    }

    const QByteArray frame = Fins::command(kCmdMemoryWrite, destinationNode(), sourceNode(),
                                           Fins::writeMemoryParams(area, quint16(address),
                                                                   quint16(wc), payload));
    // 返回值只表示"请求已发出"；PLC 回执 endCode 非 0 经 communicationError 上报
    return sendFinsFrame(frame);
}

void FinsCommNode::setRegisters(const QList<FinsRegisterItem> &regs)
{
    m_registers = regs;
    m_pollIndex = 0;
}

double FinsCommNode::registerCurrentValue(int address) const
{
    for (const auto &r : m_registers) {
        if (r.address == address)
            return r.currentValue;
    }
    return 0.0;
}

QString FinsCommNode::registerDisplayValue(int address) const
{
    for (const auto &r : m_registers) {
        if (r.address == address)
            return r.displayValue;
    }
    return QString();
}

QJsonObject FinsCommNode::toJson() const
{
    QJsonObject obj = CommunicationNodeBase::toJson();
    obj[QStringLiteral("transport")]
        = (m_transport == FinsTcp) ? QStringLiteral("FINS TCP") : QStringLiteral("FINS UDP");
    // 顶层键保留（老读取方/设备配置对话框兼容），值一律取自参数表（唯一来源）
    const char *const keys[] = {"targetIp", "targetPort", "pollInterval", "timeoutMs",
                                "autoReconnect", "reconnectInterval", "localNode", "remoteNode"};
    for (const char *k : keys) {
        const QString key = QString::fromLatin1(k);
        obj[key] = QJsonValue::fromVariant(getParam(key));
    }

    QJsonArray regsArr;
    for (const auto &r : m_registers) {
        QJsonObject ro;
        ro[QStringLiteral("area")] = r.area;
        ro[QStringLiteral("address")] = r.address;
        ro[QStringLiteral("dataType")] = r.dataType;
        ro[QStringLiteral("accessMode")] = r.accessMode;
        ro[QStringLiteral("enabled")] = r.enabled;
        ro[QStringLiteral("description")] = r.description;
        regsArr.append(ro);
    }
    obj[QStringLiteral("registers")] = regsArr;
    return obj;
}

void FinsCommNode::fromJson(const QJsonObject &json)
{
    CommunicationNodeBase::fromJson(json);   // 参数由基类从 params 段恢复（唯一来源）

    // 兼容更早方案：这些键曾只存在顶层（无 params 段）。缺键保留原值（contains 守卫）。
    if (!json.contains(QStringLiteral("params"))) {
        const char *const keys[] = {"targetIp", "targetPort", "pollInterval", "timeoutMs",
                                    "autoReconnect", "reconnectInterval", "localNode", "remoteNode"};
        for (const char *k : keys) {
            const QString key = QString::fromLatin1(k);
            if (json.contains(key))
                setParam(key, json.value(key).toVariant());
        }
    }

    m_registers.clear();
    const QJsonArray regsArr = json[QStringLiteral("registers")].toArray();
    for (const auto &v : regsArr) {
        const QJsonObject ro = v.toObject();
        FinsRegisterItem r;
        r.area = ro[QStringLiteral("area")].toString(QStringLiteral("DM"));
        r.address = ro[QStringLiteral("address")].toInt();
        r.dataType = ro[QStringLiteral("dataType")].toString(QStringLiteral("int16"));
        r.accessMode = ro[QStringLiteral("accessMode")].toString(QStringLiteral("Read"));
        r.enabled = ro[QStringLiteral("enabled")].toBool(true);
        r.description = ro[QStringLiteral("description")].toString();
        m_registers.append(r);
    }
    m_pollIndex = 0;
}
