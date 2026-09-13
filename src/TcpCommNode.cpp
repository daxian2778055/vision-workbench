#include "TcpCommNode.h"
#include "AppLog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QSignalBlocker>

TcpCommNode::TcpCommNode(QObject *parent) : CommunicationNodeBase(parent)
{
    setName(QStringLiteral("TCP\u901A\u4FE1"));
    m_commType = TCP_CLIENT;
    m_type = OUTPUT;
}

void TcpCommNode::init()
{
    CommunicationNodeBase::init();
    m_params[QStringLiteral("serverIp")] = QStringLiteral("127.0.0.1");
    m_params[QStringLiteral("port")] = 502;
    m_params[QStringLiteral("mode")] = QStringLiteral("Client");
}

bool TcpCommNode::openConnection()
{
    closeConnection();

    QString mode = m_params.value(QStringLiteral("mode")).toString();
    m_isServer = (mode == QStringLiteral("Server"));

    if (m_isServer) {
        m_server = new QTcpServer(this);
        if (!m_server->listen(QHostAddress::Any, m_params.value(QStringLiteral("port"), 502).toInt())) {
            emit communicationError(QStringLiteral("\u670D\u52A1\u7AEF\u542F\u52A8\u5931\u8D25: %1").arg(m_server->errorString()));
            m_connected = false;
            m_params[QStringLiteral("connected")] = false;
            return false;
        }
        connect(m_server, &QTcpServer::newConnection, this, [this]() {
            // 释放旧连接（同一节点只维护一个活动客户端）
            if (m_socket) {
                m_socket->disconnectFromHost();
                m_socket->deleteLater();
                m_socket = nullptr;
            }
            QTcpSocket *client = m_server->nextPendingConnection();
            if (!client) return;
            m_socket = client;
            hookSocket(client);
            m_connected = true;
            m_params[QStringLiteral("connected")] = true;
            emit connectionOpened();
        });
    } else {
        m_socket = new QTcpSocket(this);
        hookSocket(m_socket);
        m_socket->connectToHost(m_params.value(QStringLiteral("serverIp")).toString(),
                                 m_params.value(QStringLiteral("port"), 502).toInt());
        if (!m_socket->waitForConnected(3000)) {
            emit communicationError(QStringLiteral("\u8FDE\u63A5TCP\u670D\u52A1\u5668\u5931\u8D25: %1").arg(m_socket->errorString()));
            m_connected = false;
            m_params[QStringLiteral("connected")] = false;
            return false;
        }
        m_connected = true;
        m_params[QStringLiteral("connected")] = true;
        emit connectionOpened();
    }
    return true;
}

void TcpCommNode::hookSocket(QTcpSocket *socket)
{
    if (!socket) return;
    connect(socket, &QTcpSocket::readyRead, this, &TcpCommNode::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &TcpCommNode::onSocketDisconnected);
}

void TcpCommNode::onReadyRead()
{
    if (!m_socket) return;
    QByteArray data = m_socket->readAll();
    if (!data.isEmpty()) {
        emit dataReceived(data);
        m_params[QStringLiteral("lastReceived")] = QString::fromLatin1(data);
    }
}

void TcpCommNode::onSocketDisconnected()
{
    m_connected = false;
    m_params[QStringLiteral("connected")] = false;
    emit communicationError(QStringLiteral("TCP\u8FDE\u63A5\u5DF2\u65AD\u5F00"));
    emit connectionClosed();
}

void TcpCommNode::onSendRequested(const QByteArray &data)
{
    if (!m_connected || !m_socket) return;
    if (m_socket->state() != QAbstractSocket::ConnectedState) return;
    qint64 written = m_socket->write(data);
    if (written < 0) {
        emit communicationError(QStringLiteral("TCP\u53D1\u9001\u5931\u8D25: %1").arg(m_socket->errorString()));
        return;
    }
    m_socket->flush();
    m_params[QStringLiteral("lastSent")] = QString::fromLatin1(data);
}

void TcpCommNode::closeConnection()
{
    if (m_socket) {
        m_socket->disconnectFromHost();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    m_connected = false;
    m_params[QStringLiteral("connected")] = false;
    emit connectionClosed();
}

bool TcpCommNode::isConnected() const
{
    return m_connected;
}

void TcpCommNode::run(bool autoSwitch)
{
    CommunicationNodeBase::run(autoSwitch);
}

QWidget *TcpCommNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>TCP \u901A\u4FE1</b>")));

    // Mode selection
    auto *modeCombo = new QComboBox();
    modeCombo->setObjectName(QStringLiteral("tcpMode"));
    modeCombo->addItem(QStringLiteral("Client"));
    modeCombo->addItem(QStringLiteral("Server"));
    modeCombo->setCurrentText(m_params.value(QStringLiteral("mode")).toString());
    connect(modeCombo, &QComboBox::currentTextChanged, this, [this](const QString &mode) {
        setParam(QStringLiteral("mode"), mode);
    });
    layout->addWidget(new QLabel(QStringLiteral("\u6A21\u5F0F:")));
    layout->addWidget(modeCombo);

    // IP
    auto *ipEdit = new QLineEdit();
    ipEdit->setObjectName(QStringLiteral("tcpIp"));
    ipEdit->setText(m_params.value(QStringLiteral("serverIp")).toString());
    connect(ipEdit, &QLineEdit::editingFinished, this, [this, ipEdit]() {
        setParam(QStringLiteral("serverIp"), ipEdit->text());
    });
    layout->addWidget(new QLabel(QStringLiteral("IP \u5730\u5740:")));
    layout->addWidget(ipEdit);

    // Port
    auto *portEdit = new QLineEdit();
    portEdit->setObjectName(QStringLiteral("tcpPort"));
    portEdit->setText(QString::number(m_params.value(QStringLiteral("port"), 502).toInt()));
    connect(portEdit, &QLineEdit::editingFinished, this, [this, portEdit]() {
        setParam(QStringLiteral("port"), portEdit->text().toInt());
    });
    layout->addWidget(new QLabel(QStringLiteral("\u7AEF\u53E3:")));
    layout->addWidget(portEdit);

    // Connect button
    auto *connectBtn = new QPushButton(m_connected ? QStringLiteral("\u65AD\u5F00") : QStringLiteral("\u8FDE\u63A5"));
    connectBtn->setObjectName(QStringLiteral("tcpConnect"));
    connect(connectBtn, &QPushButton::clicked, this, [this, connectBtn]() {
        if (m_connected) {
            closeConnection();
            connectBtn->setText(QStringLiteral("\u8FDE\u63A5"));
        } else {
            openConnection();
            connectBtn->setText(m_connected ? QStringLiteral("\u65AD\u5F00") : QStringLiteral("\u8FDE\u63A5"));
        }
    });
    layout->addWidget(connectBtn);

    layout->addStretch();
    return panel;
}

void TcpCommNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    if (auto *w = panel->findChild<QComboBox *>(QStringLiteral("tcpMode"))) {
        QSignalBlocker b(w);
        w->setCurrentText(m_params.value(QStringLiteral("mode")).toString());
    }
    if (auto *w = panel->findChild<QLineEdit *>(QStringLiteral("tcpIp"))) {
        QSignalBlocker b(w);
        w->setText(m_params.value(QStringLiteral("serverIp")).toString());
    }
    if (auto *w = panel->findChild<QLineEdit *>(QStringLiteral("tcpPort"))) {
        QSignalBlocker b(w);
        w->setText(QString::number(m_params.value(QStringLiteral("port"), 502).toInt()));
    }
    if (auto *btn = panel->findChild<QPushButton *>(QStringLiteral("tcpConnect"))) {
        btn->setText(m_connected ? QStringLiteral("\u65AD\u5F00") : QStringLiteral("\u8FDE\u63A5"));
    }
}
