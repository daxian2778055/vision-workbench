#include "UdpCommNode.h"
#include "AppLog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QHostAddress>
#ifdef Q_OS_WIN
#include <winsock2.h>
#endif

UdpCommNode::UdpCommNode(QObject *parent) : CommunicationNodeBase(parent)
{
    setName(QStringLiteral("UDP通信"));
    m_commType = UDP;
    m_type = OUTPUT;
}

void UdpCommNode::init()
{
    CommunicationNodeBase::init();
    m_params[QStringLiteral("localPort")] = 8000;      /// 本地绑定端口
    m_params[QStringLiteral("remoteIp")] = QStringLiteral("127.0.0.1"); /// 发送目标（空=仅接收）
    m_params[QStringLiteral("remotePort")] = 8000;     /// 发送目标端口
    m_params[QStringLiteral("bindAddress")] = QString(); /// U-202：留空 = 未配置（接收面拒绑定；全网卡写 0.0.0.0）
    setParamDirect(QStringLiteral("connected"), false);
    // 帧组装（UDP 一般天然分包，保留配置项以统一行为）：默认关闭
    m_params[QStringLiteral("frameTimeoutMs")] = 0;
    m_params[QStringLiteral("frameTerminator")] = QString();
}

bool UdpCommNode::openConnection()
{
    if (m_socket) closeConnection();

    m_socket = new QUdpSocket(this);
    // N4 判空：端口越界若直接 static_cast<quint16> 会静默回绕（如 99999→34463）绑定到意外端口，
    // 收发"看似正常"却全错——显式拒绝非法配置。
    const int rawLocalPort = m_params.value(QStringLiteral("localPort"), 8000).toInt();
    if (rawLocalPort < 0 || rawLocalPort > 65535) {
        emit communicationError(QStringLiteral("UDP 本地端口 %1 非法（应在 0~65535）").arg(rawLocalPort));
        m_socket->deleteLater();
        m_socket = nullptr;
        m_connected = false;
        setParamDirect(QStringLiteral("connected"), false);
        return false;
    }
    const quint16 localPort = static_cast<quint16>(rawLocalPort);
    // 绑定地址（U-202 口径）：解析见表 CommunicationNodeBase.h——未配置（留空）在**接收面**
    // （本地端口 > 0，即真的有监听面）拒绑定并留可执行提示；显式 IP 字面量 = 只绑该网卡；
    // 无效 = 拒绑定不静默回落。端口 0（仅发送场景）没有监听面，绑定只是源地址选择，维持旧行为。
    const CommBind::BindChoice bind = CommBind::resolveListenAddress(
        m_params.value(QStringLiteral("bindAddress")).toString());
    if (bind.kind == CommBind::BindKind::Missing && localPort > 0) {
        emit communicationError(QStringLiteral(
            "UDP 绑定地址未配置：请显式填写要绑定的 IP 字面量（如需全网卡请写 0.0.0.0）"));
        m_socket->deleteLater();
        m_socket = nullptr;
        m_connected = false;
        setParamDirect(QStringLiteral("connected"), false);
        return false;
    }
    if (bind.kind == CommBind::BindKind::Invalid) {
        emit communicationError(QStringLiteral(
            "UDP 绑定地址配置无效（应填 IP 字面量，如需全网卡请写 0.0.0.0）"));
        m_socket->deleteLater();
        m_socket = nullptr;
        m_connected = false;
        setParamDirect(QStringLiteral("connected"), false);
        return false;
    }
    const QHostAddress localAddr = (bind.kind == CommBind::BindKind::Explicit)
                                       ? bind.address
                                       : QHostAddress::AnyIPv4;
    // 端口 0 表示由系统分配（仅发送场景可用）
    if (!m_socket->bind(localAddr, localPort)) {
        emit communicationError(QStringLiteral("UDP 绑定端口 %1 失败: %2")
                                    .arg(localPort).arg(m_socket->errorString()));
        m_socket->deleteLater();
        m_socket = nullptr;
        m_connected = false;
        setParamDirect(QStringLiteral("connected"), false);
        return false;
    }
    VFP_DEBUG << "UDP socket bound on" << localAddr.toString()
              << "port" << localPort;
    connect(m_socket, &QUdpSocket::readyRead, this, &UdpCommNode::onReadyRead);
#ifdef Q_OS_WIN
    // 允许发送广播（现场常用 UDP 广播触发；Windows 下必须显式 SO_BROADCAST）
    {
        const qintptr fd = m_socket->socketDescriptor();
        if (fd != -1) {
            int on = 1;
            ::setsockopt(SOCKET(fd), SOL_SOCKET, SO_BROADCAST,
                         reinterpret_cast<const char *>(&on), sizeof(on));
        }
    }
#endif
    m_connected = true;
    setParamDirect(QStringLiteral("connected"), true);
    emit connectionOpened();
    return true;
}

void UdpCommNode::closeConnection()
{
    // 只在"确实连过"时上报断开：重复 close / removeDevice 不得发假"断开"（同 TCP/串口的抖动修复）
    const bool wasConnected = m_connected;
    if (m_socket) {
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_connected = false;
    setParamDirect(QStringLiteral("connected"), false);   // 参数照旧写（不emit），保持界面数值真实
    if (!wasConnected)
        return;
    emit connectionClosed();
}

bool UdpCommNode::isConnected() const
{
    return m_connected;
}

void UdpCommNode::run(bool autoSwitch)
{
    CommunicationNodeBase::run(autoSwitch);
    if (!m_connected && autoSwitch)
        openConnection();
}

void UdpCommNode::onReadyRead()
{
    if (!m_socket) return;
    while (m_socket->hasPendingDatagrams()) {
        QByteArray buf;
        buf.resize(static_cast<int>(m_socket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        const qint64 n = m_socket->readDatagram(buf.data(), buf.size(), &sender, &senderPort);
        if (n < 0) continue;
        buf.resize(static_cast<int>(n));
        m_lastSender = QStringLiteral("%1:%2").arg(sender.toString()).arg(senderPort);
        setParamDirect(QStringLiteral("lastReceived"), QString::fromLatin1(buf));
        m_params[QStringLiteral("lastSender")] = m_lastSender;
        emit dataReceived(buf);
    }
}

void UdpCommNode::onSendRequested(const QByteArray &data)
{
    // 静默早退是历史缺陷：调用方已在 sendData() 拿到 true，这里的丢弃会让"发送成功"是假的
    if (!m_connected || !m_socket) {
        emit communicationError(QStringLiteral("UDP 发送失败: 未连接"));
        return;
    }
    const QString ip = m_params.value(QStringLiteral("remoteIp")).toString().trimmed();
    if (ip.isEmpty()) {
        emit communicationError(QStringLiteral("UDP 未配置目标 IP（仅接收模式不可发送）"));
        return;
    }
    const quint16 port =
        static_cast<quint16>(m_params.value(QStringLiteral("remotePort"), 8000).toInt());
    const qint64 written =
        m_socket->writeDatagram(data, QHostAddress(ip), port);
    if (written < 0) {
        emit communicationError(QStringLiteral("UDP 发送失败: %1").arg(m_socket->errorString()));
        return;
    }
    setParamDirect(QStringLiteral("lastSent"), QString::fromLatin1(data));
}

QWidget *UdpCommNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>UDP 通信</b>")));

    auto *hint = new QLabel(QStringLiteral(
        "本地端口：接收用（0=随机）。\n目标 IP/端口：发送用；目标 IP 留空 = 仅接收。"));
    hint->setStyleSheet("color: gray; font-size: 11px;");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *localSpin = new QSpinBox();
    localSpin->setObjectName(QStringLiteral("udpLocalPort"));
    localSpin->setRange(0, 65535);
    localSpin->setValue(m_params.value(QStringLiteral("localPort"), 8000).toInt());
    connect(localSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) {
        setParam(QStringLiteral("localPort"), v);
    });
    layout->addWidget(new QLabel(QStringLiteral("本地端口:")));
    layout->addWidget(localSpin);

    // 本地约束地址（U-202 口径）：接收面（本地端口 > 0）必填——留空即未配置，拒绑定；
    // 填 IP 字面量 = 只绑该网卡；无效值启动时拒绝（不静默回落）。
    auto *bindEdit = new QLineEdit();
    bindEdit->setObjectName(QStringLiteral("udpBindAddress"));
    bindEdit->setPlaceholderText(QStringLiteral("接收必填：如 127.0.0.1；全网卡写 0.0.0.0"));
    bindEdit->setText(m_params.value(QStringLiteral("bindAddress")).toString());
    connect(bindEdit, &QLineEdit::editingFinished, this, [this, bindEdit]() {
        setParam(QStringLiteral("bindAddress"), bindEdit->text().trimmed());
    });
    layout->addWidget(new QLabel(QStringLiteral("绑定地址:")));
    layout->addWidget(bindEdit);

    auto *ipEdit = new QLineEdit(m_params.value(QStringLiteral("remoteIp")).toString());
    ipEdit->setObjectName(QStringLiteral("udpRemoteIp"));
    ipEdit->setPlaceholderText(QStringLiteral("如 192.168.1.10（留空=仅接收）"));
    connect(ipEdit, &QLineEdit::editingFinished, this, [this, ipEdit]() {
        setParam(QStringLiteral("remoteIp"), ipEdit->text().trimmed());
    });
    layout->addWidget(new QLabel(QStringLiteral("目标 IP:")));
    layout->addWidget(ipEdit);

    auto *portSpin = new QSpinBox();
    portSpin->setObjectName(QStringLiteral("udpRemotePort"));
    portSpin->setRange(1, 65535);
    portSpin->setValue(m_params.value(QStringLiteral("remotePort"), 8000).toInt());
    connect(portSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) {
        setParam(QStringLiteral("remotePort"), v);
    });
    layout->addWidget(new QLabel(QStringLiteral("目标端口:")));
    layout->addWidget(portSpin);

    auto *connectBtn = new QPushButton(m_connected ? QStringLiteral("断开") : QStringLiteral("打开"));
    connectBtn->setObjectName(QStringLiteral("udpConnect"));
    connect(connectBtn, &QPushButton::clicked, this, [this, connectBtn]() {
        if (m_connected) {
            closeConnection();
            connectBtn->setText(QStringLiteral("打开"));
        } else {
            openConnection();
            connectBtn->setText(m_connected ? QStringLiteral("断开") : QStringLiteral("打开"));
        }
    });
    layout->addWidget(connectBtn);

    layout->addStretch();
    return panel;
}

void UdpCommNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    if (auto *sp = panel->findChild<QSpinBox *>(QStringLiteral("udpLocalPort"))) {
        QSignalBlocker b(sp);
        sp->setValue(m_params.value(QStringLiteral("localPort"), 8000).toInt());
    }
    if (auto *ed = panel->findChild<QLineEdit *>(QStringLiteral("udpBindAddress"))) {
        QSignalBlocker b(ed);
        ed->setText(m_params.value(QStringLiteral("bindAddress")).toString());
    }
    if (auto *ed = panel->findChild<QLineEdit *>(QStringLiteral("udpRemoteIp"))) {
        QSignalBlocker b(ed);
        ed->setText(m_params.value(QStringLiteral("remoteIp")).toString());
    }
    if (auto *sp = panel->findChild<QSpinBox *>(QStringLiteral("udpRemotePort"))) {
        QSignalBlocker b(sp);
        sp->setValue(m_params.value(QStringLiteral("remotePort"), 8000).toInt());
    }
    if (auto *btn = panel->findChild<QPushButton *>(QStringLiteral("udpConnect"))) {
        btn->setText(m_connected ? QStringLiteral("断开") : QStringLiteral("打开"));
    }
}
