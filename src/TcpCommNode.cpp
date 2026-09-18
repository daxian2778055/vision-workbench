#include "TcpCommNode.h"
#include "AppLog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
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
    m_params[QStringLiteral("autoReconnect")] = true;
    m_params[QStringLiteral("reconnectInterval")] = 3000;

    // 断线自动重连定时器（对齐 Modbus/PLC 的做法）
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (!m_connected && !m_isServer && m_autoReconnect && !m_userClosed) {
            // 后台异步重连：connectToHost 立即返回，成功/失败由信号驱动。
            // 同步 waitForConnected 会让"设备离线"变成"UI 周期性卡死"。
            m_asyncConnect = true;
            openConnection();
        }
    });
}

void TcpCommNode::setParam(const QString &name, const QVariant &value)
{
    if (name == QStringLiteral("autoReconnect")) {
        m_autoReconnect = value.toBool();
    } else if (name == QStringLiteral("reconnectInterval")) {
        m_reconnectInterval = qMax(500, value.toInt());
    }
    HalconNode::setParam(name, value);   // 写入 m_params → 随方案自动序列化
}

void TcpCommNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);
    // m_params 恢复后同步成员（否则存了 false 的"自动重连"重启后仍按默认 true 跑）
    m_autoReconnect = m_params.value(QStringLiteral("autoReconnect"), true).toBool();
    m_reconnectInterval =
        qMax(500, m_params.value(QStringLiteral("reconnectInterval"), 3000).toInt());
}

void TcpCommNode::scheduleReconnect()
{
    if (m_isServer || !m_autoReconnect || m_userClosed) return;
    if (m_reconnectTimer && !m_reconnectTimer->isActive())
        m_reconnectTimer->start(m_reconnectInterval);
}

bool TcpCommNode::openConnection()
{
    closeConnection();          // 内部会置 m_userClosed=true（防止关连触发重连）
    m_userClosed = false;       // 本次是主动建立连接，允许后续断线自动重连
    if (m_reconnectTimer) m_reconnectTimer->stop();

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
        const QString ip = m_params.value(QStringLiteral("serverIp")).toString();
        const int port = m_params.value(QStringLiteral("port"), 502).toInt();
        if (m_asyncConnect) {
            // 后台重连路径：发起即返回（结果由 connected/errorOccurred 信号驱动），绝不阻塞 UI 线程
            m_asyncConnect = false;
            m_socket->connectToHost(ip, port);
            return true;
        }
        m_socket->connectToHost(ip, port);
        if (!m_socket->waitForConnected(1500)) {   // 手动连接：短等待（局域网连接通常 <100ms）
            emit communicationError(QStringLiteral("\u8FDE\u63A5TCP\u670D\u52A1\u5668\u5931\u8D25: %1").arg(m_socket->errorString()));
            m_connected = false;
            m_params[QStringLiteral("connected")] = false;
            scheduleReconnect();   // 首次连接失败也自动重试（现场上电顺序不定）
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
    // 所有信号都校验 sender 是"当前 socket"：重连会替换 socket，旧 socket 的延迟信号
    // （disconnected/errorOccurred）若被当作当前连接处理，会把新连接误判为断开 →
    // 触发下一轮重连 → closeConnection 又切断健康连接，形成"反复 online/offline"死循环。
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
        if (socket != m_socket) return;
        onReadyRead();
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
        if (socket != m_socket) return;
        onSocketDisconnected();
    });
    // 异步连接成功（同步路径已在 openConnection 里置位，此判断会挡住重复处理）
    connect(socket, &QAbstractSocket::connected, this, [this, socket]() {
        if (socket != m_socket) return;
        if (!m_connected) {
            m_connected = true;
            m_params[QStringLiteral("connected")] = true;
            emit connectionOpened();
        }
        if (m_reconnectTimer) m_reconnectTimer->stop();   // 连上即停重连（防御）
    });
    // 连接失败（拒绝/不可达/超时）：异步路径靠这里安排下一次重连；
    // 同步路径的返回值分支也会排，scheduleReconnect 自带判重不会重复排。
    connect(socket, &QAbstractSocket::errorOccurred, this,
            [this, socket](QAbstractSocket::SocketError) {
        if (socket != m_socket) return;
        if (!m_connected)
            scheduleReconnect();
    });
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
    scheduleReconnect();   // 断线自动重连（历史缺陷：只报警、永久失联）
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
    m_userClosed = true;                              // 主动关闭：不触发自动重连
    if (m_reconnectTimer) m_reconnectTimer->stop();
    QTcpSocket *old = m_socket;
    m_socket = nullptr;   // 先解除引用：旧 socket 的延迟信号会被归属校验忽略，不影响新连接
    if (old) {
        old->disconnectFromHost();
        old->deleteLater();
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

    // 自动重连（客户端模式生效）
    auto *reconnectCheck = new QCheckBox(QStringLiteral("断线自动重连"));
    reconnectCheck->setObjectName(QStringLiteral("tcpAutoReconnect"));
    reconnectCheck->setChecked(m_autoReconnect);
    connect(reconnectCheck, &QCheckBox::toggled, this, [this](bool on) {
        setParam(QStringLiteral("autoReconnect"), on);
    });
    layout->addWidget(reconnectCheck);

    auto *intervalSpin = new QSpinBox();
    intervalSpin->setObjectName(QStringLiteral("tcpReconnectInterval"));
    intervalSpin->setRange(500, 60000);
    intervalSpin->setSingleStep(500);
    intervalSpin->setValue(m_reconnectInterval);
    connect(intervalSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) {
        setParam(QStringLiteral("reconnectInterval"), v);
    });
    layout->addWidget(new QLabel(QStringLiteral("重连间隔(ms):")));
    layout->addWidget(intervalSpin);

    auto *stateLabel = new QLabel(m_connected ? QStringLiteral("状态: 已连接")
                                              : QStringLiteral("状态: 未连接"));
    stateLabel->setObjectName(QStringLiteral("tcpState"));
    layout->addWidget(stateLabel);

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
    if (auto *cb = panel->findChild<QCheckBox *>(QStringLiteral("tcpAutoReconnect"))) {
        QSignalBlocker b(cb);
        cb->setChecked(m_autoReconnect);
    }
    if (auto *sp = panel->findChild<QSpinBox *>(QStringLiteral("tcpReconnectInterval"))) {
        QSignalBlocker b(sp);
        sp->setValue(m_reconnectInterval);
    }
    if (auto *lb = panel->findChild<QLabel *>(QStringLiteral("tcpState"))) {
        lb->setText(m_connected ? QStringLiteral("状态: 已连接")
                                : QStringLiteral("状态: 未连接"));
    }
    if (auto *btn = panel->findChild<QPushButton *>(QStringLiteral("tcpConnect"))) {
        btn->setText(m_connected ? QStringLiteral("\u65AD\u5F00") : QStringLiteral("\u8FDE\u63A5"));
    }
}
