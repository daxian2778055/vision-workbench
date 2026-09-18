#include "UdpCommNode.h"
#include "AppLog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QHostAddress>

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
    m_params[QStringLiteral("connected")] = false;
}

bool UdpCommNode::openConnection()
{
    if (m_socket) closeConnection();

    m_socket = new QUdpSocket(this);
    const quint16 localPort =
        static_cast<quint16>(m_params.value(QStringLiteral("localPort"), 8000).toInt());
    // 绑定任意本机地址（不限定 IP）；端口 0 表示由系统分配（仅发送场景可用）
    if (!m_socket->bind(QHostAddress::AnyIPv4, localPort)) {
        emit communicationError(QStringLiteral("UDP 绑定端口 %1 失败: %2")
                                    .arg(localPort).arg(m_socket->errorString()));
        m_socket->deleteLater();
        m_socket = nullptr;
        m_connected = false;
        m_params[QStringLiteral("connected")] = false;
        return false;
    }
    connect(m_socket, &QUdpSocket::readyRead, this, &UdpCommNode::onReadyRead);
    m_connected = true;
    m_params[QStringLiteral("connected")] = true;
    emit connectionOpened();
    return true;
}

void UdpCommNode::closeConnection()
{
    if (m_socket) {
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_connected = false;
    m_params[QStringLiteral("connected")] = false;
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
        m_params[QStringLiteral("lastReceived")] = QString::fromLatin1(buf);
        m_params[QStringLiteral("lastSender")] = m_lastSender;
        emit dataReceived(buf);
    }
}

void UdpCommNode::onSendRequested(const QByteArray &data)
{
    if (!m_connected || !m_socket) return;
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
    m_params[QStringLiteral("lastSent")] = QString::fromLatin1(data);
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
