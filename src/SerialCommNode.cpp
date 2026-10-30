#include "SerialCommNode.h"
#include "AppLog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QPushButton>
#include <QSerialPortInfo>
#include <QSignalBlocker>

SerialCommNode::SerialCommNode(QObject *parent) : CommunicationNodeBase(parent)
{
    setName(QStringLiteral("\u4E32\u53E3\u901A\u4FE1"));
    m_commType = SERIAL;
    m_type = OUTPUT;
    // Port-owned IO thread (r19 P1-12): has its own event loop so readyRead/error
    // are delivered, and it never blocks on the GUI join (P1-13).
    m_ioThread = new QThread(this);
    m_ioThread->setObjectName(QStringLiteral("serial-io"));
    m_ioThread->start();
}

SerialCommNode::~SerialCommNode()
{
    closeConnection();
    if (m_ioThread) {
        m_ioThread->quit();
        m_ioThread->wait(3000);
    }
}

void SerialCommNode::init()
{
    CommunicationNodeBase::init();
    m_params[QStringLiteral("portName")] = QStringLiteral("COM1");
    m_params[QStringLiteral("baudRate")] = 9600;
    m_params[QStringLiteral("dataBits")] = 8;
    m_params[QStringLiteral("stopBits")] = 1;
    m_params[QStringLiteral("parity")] = QStringLiteral("None");
    m_params[QStringLiteral("autoReconnect")] = true;
    m_params[QStringLiteral("reconnectInterval")] = 3000;
    // 帧组装（粘包/半包治理）：默认关闭
    m_params[QStringLiteral("frameTimeoutMs")] = 0;
    m_params[QStringLiteral("frameTerminator")] = QString();

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
        // 参数唯一来源：参数表（原先读成员，与界面线程写构成无保护竞态）
        if (!m_connected && getParam(QStringLiteral("autoReconnect")).toBool() && !m_userClosed)
            openConnection();
    });
}

void SerialCommNode::setParam(const QString &name, const QVariant &value)
{
    // 只写参数表（基类加锁 + 校验）。钳制放**写侧**（interval 下限 500ms），
    // 让重连排程与面板共用同一份值（旧实现：成员=钳后、参数表=原值、面板=读成员）。
    if (name == QStringLiteral("reconnectInterval")) {
        HalconNode::setParam(name, qMax(500, value.toInt()));
        return;
    }
    HalconNode::setParam(name, value);
}

void SerialCommNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);   // autoReconnect / reconnectInterval 由基类从 params 恢复（唯一来源）
    // 兼容更早方案：这两个键若只存在顶层（无 params 段）则补写；
    // 旧实现对缺失键不做处理 ⇒ **缺键保留原值**，故用 contains 守卫。
    if (!json.contains(QStringLiteral("params"))) {
        if (json.contains(QStringLiteral("autoReconnect"))) {
            setParam(QStringLiteral("autoReconnect"),
                     json.value(QStringLiteral("autoReconnect")).toVariant());
        }
        if (json.contains(QStringLiteral("reconnectInterval"))) {
            setParam(QStringLiteral("reconnectInterval"),
                     json.value(QStringLiteral("reconnectInterval")).toVariant());
        }
    }
}

void SerialCommNode::scheduleReconnect()
{
    // 参数唯一来源：参数表（本轮各取一次；interval 已由写侧钳到 ≥500ms）
    const bool autoReconnect = getParam(QStringLiteral("autoReconnect")).toBool();
    if (!autoReconnect || m_userClosed) return;
    if (m_reconnectTimer && !m_reconnectTimer->isActive())
        m_reconnectTimer->start(getParam(QStringLiteral("reconnectInterval")).toInt());
}

void SerialCommNode::applyPortSettings(QSerialPort *port)
{
    if (!port) return;
    // 数据位 / 停止位 / 校验：真正使用配置（历史实现硬编码 8/1/None，用户在界面上
    // 改校验位、停止位完全不生效，通讯不上还查不出原因）
    switch (m_params.value(QStringLiteral("dataBits"), 8).toInt()) {
    case 5:  port->setDataBits(QSerialPort::Data5); break;
    case 6:  port->setDataBits(QSerialPort::Data6); break;
    case 7:  port->setDataBits(QSerialPort::Data7); break;
    default: port->setDataBits(QSerialPort::Data8); break;
    }
    switch (m_params.value(QStringLiteral("stopBits"), 1).toInt()) {
    case 2:  port->setStopBits(QSerialPort::TwoStop); break;
    case 3:  port->setStopBits(QSerialPort::OneAndHalfStop); break;   // 约定 3=1.5 位
    default: port->setStopBits(QSerialPort::OneStop); break;
    }
    const QString parity =
        m_params.value(QStringLiteral("parity"), QStringLiteral("None")).toString().trimmed().toLower();
    if (parity == QStringLiteral("even") || parity == QStringLiteral("\u5076"))
        port->setParity(QSerialPort::EvenParity);
    else if (parity == QStringLiteral("odd") || parity == QStringLiteral("\u5947"))
        port->setParity(QSerialPort::OddParity);
    else
        port->setParity(QSerialPort::NoParity);
}

bool SerialCommNode::openConnection()
{
    if (m_serial) closeConnection();
    m_userClosed = false;
    if (m_reconnectTimer) m_reconnectTimer->stop();

    const QString portName = m_params.value(QStringLiteral("portName")).toString().trimmed();
    if (portName.isEmpty()) {
        emit communicationError(QStringLiteral("no serial port configured"));
        m_connected = false;
        setParamDirect(QStringLiteral("connected"), false);
        return false;
    }
    const int baud = m_params.value(QStringLiteral("baudRate"), 9600).toInt();

    // r19 P1-12: create/configure/open the port in the dedicated IO thread. QIODevice
    // must not be touched from other threads; open() may come from the executor thread
    // while write() comes from the node thread and readyRead must arrive on the port's
    // own thread. The IO thread always has a running event loop and never waits on the
    // GUI, so no deadlock with joinForDestroy (P1-13).
    bool ok = false;
    bool retryable = false;
    QString err;
    QSerialPort *created = nullptr;
    QMetaObject::invokeMethod(
        m_ioThread,
        [this, &ok, &err, &retryable, &created, portName, baud]() {
            auto *port = new QSerialPort();   // no parent: owned by the IO thread
            port->setPortName(portName);
            if (!port->setBaudRate(baud)) {
                err = QStringLiteral("baud rate %1 unsupported").arg(baud);
                delete port;
                return;
            }
            applyPortSettings(port);
            if (!port->open(QIODevice::ReadWrite)) {
                err = QStringLiteral("open failed: %1").arg(port->errorString());
                retryable = true;
                delete port;
                return;
            }
            connect(port, &QSerialPort::readyRead, port, [this, port]() {
                const QByteArray data = port->readAll();
                emit dataReceived(data);
                setParamDirect(QStringLiteral("lastReceived"), QString::fromLatin1(data));
            });
            connect(port, &QSerialPort::errorOccurred, this, &SerialCommNode::onSerialError);
            created = port;
            ok = true;
        },
        Qt::BlockingQueuedConnection);

    if (!ok) {
        emit communicationError(err.isEmpty() ? QStringLiteral("serial open failed") : err);
        m_connected = false;
        setParamDirect(QStringLiteral("connected"), false);
        if (retryable)
            scheduleReconnect();
        return false;
    }
    {
        QMutexLocker<QMutex> locker(&m_stateMutex);
        m_serial = created;
        m_connected = true;
    }
    setParamDirect(QStringLiteral("connected"), true);
    emit connectionOpened();
    return true;
}

void SerialCommNode::onSerialError(QSerialPort::SerialPortError error)
{
    // USB 转串口掉线/被拔出：ResourceError（或权限错误）代表物理链路已断。
    // 历史实现不处理任何错误信号 → 界面永远"已连接"、不会自愈也不会报警。
    if (error == QSerialPort::NoError)
        return;
    if (error != QSerialPort::ResourceError && error != QSerialPort::PermissionError
        && error != QSerialPort::DeviceNotFoundError)
        return;

    // 先取出错误描述再清理：close()/deleteLater() 之后 errorString() 已无效。
    // 历史实现先置空 m_serial 再取 → 诊断信息永远丢失（只知道"断线"，不知"为什么"）。
    const QString errorText = m_serial ? m_serial->errorString() : QString();
    if (m_serial) {
        m_serial->close();
        m_serial->deleteLater();
        m_serial = nullptr;
    }
    if (m_connected) {
        m_connected = false;
        setParamDirect(QStringLiteral("connected"), false);
        emit communicationError(QStringLiteral("\u4E32\u53E3\u8FDE\u63A5\u4E2D\u65AD: %1")
                                    .arg(errorText.isEmpty() ? QStringLiteral("设备移除") : errorText));
        emit connectionClosed();
    }
    scheduleReconnect();
}

void SerialCommNode::closeConnection()
{
    m_userClosed = true;
    if (m_reconnectTimer) m_reconnectTimer->stop();
    const bool wasConnected = m_connected;
    QSerialPort *port = nullptr;
    {
        QMutexLocker<QMutex> locker(&m_stateMutex);
        port = m_serial;
        m_serial = nullptr;
        m_connected = false;
    }
    if (port) {
        // close/disconnect/delete all happen in the IO thread (r19 P1-12)
        QMetaObject::invokeMethod(
            m_ioThread,
            [port]() {
                port->close();
                port->disconnect();
                port->deleteLater();
            },
            Qt::BlockingQueuedConnection);
    }
    setParamDirect(QStringLiteral("connected"), false);
    if (!wasConnected)
        return;
    emit connectionClosed();
}

bool SerialCommNode::isConnected() const
{
    return m_connected;
}

void SerialCommNode::run(bool autoSwitch)
{
    CommunicationNodeBase::run(autoSwitch);
    // In auto-switch mode, ensure connection is maintained
    if (!m_connected && autoSwitch) {
        openConnection();
    }
}

void SerialCommNode::onDataReceived()
{
    if (m_serial) {
        QByteArray data = m_serial->readAll();
        emit dataReceived(data);
        setParamDirect(QStringLiteral("lastReceived"), QString::fromLatin1(data));
    }
}

void SerialCommNode::onSendRequested(const QByteArray &data)
{
    QSerialPort *port = nullptr;
    bool connected = false;
    {
        QMutexLocker<QMutex> locker(&m_stateMutex);
        port = m_serial;
        connected = m_connected;
    }
    if (!connected || !port) {
        noteSendWriteFailure(QStringLiteral("serial send failed: not connected"));
        return;
    }
    // write() also runs in the IO thread (r19 P1-12)
    qint64 written = -1;
    QMetaObject::invokeMethod(
        m_ioThread,
        [port, data, &written]() {
            written = port->write(data);
            if (written == data.size())
                port->flush();
        },
        Qt::BlockingQueuedConnection);
    if (written != data.size()) {
        noteSendWriteFailure(QStringLiteral("serial send failed: %1").arg(port->errorString()));
        return;
    }
    setParamDirect(QStringLiteral("lastSent"), QString::fromLatin1(data));
}

QWidget *SerialCommNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>\u4E32\u53E3\u901A\u4FE1</b>")));

    // Port combo
    auto *portCombo = new QComboBox();
    portCombo->setObjectName(QStringLiteral("serialPort"));
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        portCombo->addItem(info.portName());
    }
    QString curPort = m_params.value(QStringLiteral("portName")).toString();
    int idx = portCombo->findText(curPort);
    if (idx >= 0) portCombo->setCurrentIndex(idx);

    connect(portCombo, &QComboBox::currentTextChanged, this, [this](const QString &port) {
        setParam(QStringLiteral("portName"), port);
    });
    layout->addWidget(new QLabel(QStringLiteral("\u7AEF\u53E3:")));
    layout->addWidget(portCombo);

    // Baud rate
    auto *baudCombo = new QComboBox();
    baudCombo->setObjectName(QStringLiteral("serialBaud"));
    for (int b : {9600, 19200, 38400, 57600, 115200, 230400})
        baudCombo->addItem(QString::number(b), b);
    baudCombo->setCurrentIndex(baudCombo->findData(m_params.value(QStringLiteral("baudRate"), 9600).toInt()));
    connect(baudCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, baudCombo](int) {
        setParam(QStringLiteral("baudRate"), baudCombo->currentData().toInt());
    });
    layout->addWidget(new QLabel(QStringLiteral("\u6CE2\u7279\u7387:")));
    layout->addWidget(baudCombo);

    // 数据位 / 停止位 / 校验（此前硬编码 8/1/None，配置不生效；这里真正接入）
    auto *dataBitsCombo = new QComboBox();
    dataBitsCombo->setObjectName(QStringLiteral("serialDataBits"));
    for (int b : {5, 6, 7, 8})
        dataBitsCombo->addItem(QString::number(b), b);
    dataBitsCombo->setCurrentIndex(
        qMax(0, dataBitsCombo->findData(m_params.value(QStringLiteral("dataBits"), 8).toInt())));
    connect(dataBitsCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, dataBitsCombo](int) {
        setParam(QStringLiteral("dataBits"), dataBitsCombo->currentData().toInt());
    });
    layout->addWidget(new QLabel(QStringLiteral("\u6570\u636E\u4F4D:")));
    layout->addWidget(dataBitsCombo);

    auto *stopBitsCombo = new QComboBox();
    stopBitsCombo->setObjectName(QStringLiteral("serialStopBits"));
    stopBitsCombo->addItem(QStringLiteral("1"), 1);
    stopBitsCombo->addItem(QStringLiteral("2"), 2);
    stopBitsCombo->addItem(QStringLiteral("1.5"), 3);
    stopBitsCombo->setCurrentIndex(
        qMax(0, stopBitsCombo->findData(m_params.value(QStringLiteral("stopBits"), 1).toInt())));
    connect(stopBitsCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, stopBitsCombo](int) {
        setParam(QStringLiteral("stopBits"), stopBitsCombo->currentData().toInt());
    });
    layout->addWidget(new QLabel(QStringLiteral("\u505C\u6B62\u4F4D:")));
    layout->addWidget(stopBitsCombo);

    auto *parityCombo = new QComboBox();
    parityCombo->setObjectName(QStringLiteral("serialParity"));
    parityCombo->addItem(QStringLiteral("\u65E0"), QStringLiteral("None"));
    parityCombo->addItem(QStringLiteral("\u5076"), QStringLiteral("Even"));
    parityCombo->addItem(QStringLiteral("\u5947"), QStringLiteral("Odd"));
    {
        const QString curP = m_params.value(QStringLiteral("parity"), QStringLiteral("None")).toString();
        const int pi = parityCombo->findData(curP);
        parityCombo->setCurrentIndex(pi >= 0 ? pi : 0);
    }
    connect(parityCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, parityCombo](int) {
        setParam(QStringLiteral("parity"), parityCombo->currentData().toString());
    });
    layout->addWidget(new QLabel(QStringLiteral("\u6821\u9A8C:")));
    layout->addWidget(parityCombo);

    // 断线自动重连（USB 转串口掉线自愈）
    auto *reconnectCheck = new QCheckBox(QStringLiteral("\u65AD\u7EBF\u81EA\u52A8\u91CD\u8FDE"));
    reconnectCheck->setObjectName(QStringLiteral("serialAutoReconnect"));
    reconnectCheck->setChecked(getParam(QStringLiteral("autoReconnect")).toBool());
    connect(reconnectCheck, &QCheckBox::toggled, this, [this](bool on) {
        setParam(QStringLiteral("autoReconnect"), on);
    });
    layout->addWidget(reconnectCheck);

    auto *intervalSpin = new QSpinBox();
    intervalSpin->setObjectName(QStringLiteral("serialReconnectInterval"));
    intervalSpin->setRange(500, 60000);
    intervalSpin->setSingleStep(500);
    intervalSpin->setValue(getParam(QStringLiteral("reconnectInterval")).toInt());
    connect(intervalSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) {
        setParam(QStringLiteral("reconnectInterval"), v);
    });
    layout->addWidget(new QLabel(QStringLiteral("\u91CD\u8FDE\u95F4\u9694(ms):")));
    layout->addWidget(intervalSpin);

    // Connect button
    auto *connectBtn = new QPushButton(m_connected ? QStringLiteral("\u65AD\u5F00\u8FDE\u63A5") : QStringLiteral("\u6253\u5F00\u8FDE\u63A5"));
    connectBtn->setObjectName(QStringLiteral("serialConnect"));
    connect(connectBtn, &QPushButton::clicked, this, [this, connectBtn]() {
        if (m_connected) {
            closeConnection();
            connectBtn->setText(QStringLiteral("\u6253\u5F00\u8FDE\u63A5"));
        } else {
            openConnection();
            connectBtn->setText(m_connected ? QStringLiteral("\u65AD\u5F00\u8FDE\u63A5") : QStringLiteral("\u6253\u5F00\u8FDE\u63A5"));
        }
    });
    layout->addWidget(connectBtn);

    layout->addStretch();
    return panel;
}

void SerialCommNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    if (auto *cb = panel->findChild<QComboBox *>(QStringLiteral("serialPort"))) {
        QSignalBlocker b(cb);
        QString port = m_params.value(QStringLiteral("portName")).toString();
        int idx = cb->findText(port);
        if (idx >= 0) cb->setCurrentIndex(idx);
    }
    if (auto *cb = panel->findChild<QComboBox *>(QStringLiteral("serialBaud"))) {
        QSignalBlocker b(cb);
        int idx = cb->findData(m_params.value(QStringLiteral("baudRate"), 9600).toInt());
        if (idx >= 0) cb->setCurrentIndex(idx);
    }
    if (auto *cb = panel->findChild<QComboBox *>(QStringLiteral("serialDataBits"))) {
        QSignalBlocker b(cb);
        const int idx = cb->findData(m_params.value(QStringLiteral("dataBits"), 8).toInt());
        if (idx >= 0) cb->setCurrentIndex(idx);
    }
    if (auto *cb = panel->findChild<QComboBox *>(QStringLiteral("serialStopBits"))) {
        QSignalBlocker b(cb);
        const int idx = cb->findData(m_params.value(QStringLiteral("stopBits"), 1).toInt());
        if (idx >= 0) cb->setCurrentIndex(idx);
    }
    if (auto *cb = panel->findChild<QComboBox *>(QStringLiteral("serialParity"))) {
        QSignalBlocker b(cb);
        const int idx = cb->findData(
            m_params.value(QStringLiteral("parity"), QStringLiteral("None")).toString());
        if (idx >= 0) cb->setCurrentIndex(idx);
    }
    if (auto *ck = panel->findChild<QCheckBox *>(QStringLiteral("serialAutoReconnect"))) {
        QSignalBlocker b(ck);
        ck->setChecked(getParam(QStringLiteral("autoReconnect")).toBool());
    }
    if (auto *sp = panel->findChild<QSpinBox *>(QStringLiteral("serialReconnectInterval"))) {
        QSignalBlocker b(sp);
        sp->setValue(getParam(QStringLiteral("reconnectInterval")).toInt());
    }
    if (auto *btn = panel->findChild<QPushButton *>(QStringLiteral("serialConnect"))) {
        btn->setText(m_connected ? QStringLiteral("\u65AD\u5F00\u8FDE\u63A5") : QStringLiteral("\u6253\u5F00\u8FDE\u63A5"));
    }
}
