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
        if (!m_connected && m_autoReconnect && !m_userClosed)
            openConnection();
    });
}

void SerialCommNode::setParam(const QString &name, const QVariant &value)
{
    if (name == QStringLiteral("autoReconnect")) {
        m_autoReconnect = value.toBool();
    } else if (name == QStringLiteral("reconnectInterval")) {
        m_reconnectInterval = qMax(500, value.toInt());
    }
    HalconNode::setParam(name, value);
}

void SerialCommNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);
    m_autoReconnect = m_params.value(QStringLiteral("autoReconnect"), true).toBool();
    m_reconnectInterval =
        qMax(500, m_params.value(QStringLiteral("reconnectInterval"), 3000).toInt());
}

void SerialCommNode::scheduleReconnect()
{
    if (!m_autoReconnect || m_userClosed) return;
    if (m_reconnectTimer && !m_reconnectTimer->isActive())
        m_reconnectTimer->start(m_reconnectInterval);
}

void SerialCommNode::applyPortSettings()
{
    if (!m_serial) return;
    // 数据位 / 停止位 / 校验：真正使用配置（历史实现硬编码 8/1/None，用户在界面上
    // 改校验位、停止位完全不生效，通讯不上还查不出原因）
    switch (m_params.value(QStringLiteral("dataBits"), 8).toInt()) {
    case 5:  m_serial->setDataBits(QSerialPort::Data5); break;
    case 6:  m_serial->setDataBits(QSerialPort::Data6); break;
    case 7:  m_serial->setDataBits(QSerialPort::Data7); break;
    default: m_serial->setDataBits(QSerialPort::Data8); break;
    }
    switch (m_params.value(QStringLiteral("stopBits"), 1).toInt()) {
    case 2:  m_serial->setStopBits(QSerialPort::TwoStop); break;
    case 3:  m_serial->setStopBits(QSerialPort::OneAndHalfStop); break;   // 约定 3=1.5 位
    default: m_serial->setStopBits(QSerialPort::OneStop); break;
    }
    const QString parity =
        m_params.value(QStringLiteral("parity"), QStringLiteral("None")).toString().trimmed().toLower();
    if (parity == QStringLiteral("even") || parity == QStringLiteral("\u5076"))
        m_serial->setParity(QSerialPort::EvenParity);
    else if (parity == QStringLiteral("odd") || parity == QStringLiteral("\u5947"))
        m_serial->setParity(QSerialPort::OddParity);
    else
        m_serial->setParity(QSerialPort::NoParity);
}

bool SerialCommNode::openConnection()
{
    if (m_serial) closeConnection();
    m_userClosed = false;   // 本次是主动建立连接
    if (m_reconnectTimer) m_reconnectTimer->stop();

    m_serial = new QSerialPort(this);
    // N4 判空：未配置串口号时 open("") 必然失败，且会被自动重连每 3s 刷屏——快速失败且不排重连
    // （与下方"波特率不支持"同一策略：配置错误重试无意义）。
    const QString portName = m_params.value(QStringLiteral("portName")).toString().trimmed();
    if (portName.isEmpty()) {
        emit communicationError(QStringLiteral("未配置串口号，串口未打开"));
        m_serial->deleteLater();
        m_serial = nullptr;
        m_connected = false;
        setParamDirect(QStringLiteral("connected"), false);
        return false;
    }
    m_serial->setPortName(portName);
    const int baud = m_params.value(QStringLiteral("baudRate"), 9600).toInt();
    if (!m_serial->setBaudRate(baud)) {
        // 波特率不受支持时继续打开只会以错误速率"永远通不上"——明确失败并提示，
        // 且不排自动重连（配置错误重试无意义，避免每 3s 刷屏）
        emit communicationError(QStringLiteral("波特率 %1 不受支持，串口未打开").arg(baud));
        m_serial->deleteLater();
        m_serial = nullptr;
        m_connected = false;
        setParamDirect(QStringLiteral("connected"), false);
        return false;
    }
    applyPortSettings();   // 数据位/停止位/校验按配置生效

    if (!m_serial->open(QIODevice::ReadWrite)) {
        emit communicationError(QStringLiteral("\u6253\u5F00\u4E32\u53E3\u5931\u8D25: %1").arg(m_serial->errorString()));
        m_connected = false;
        setParamDirect(QStringLiteral("connected"), false);
        scheduleReconnect();   // 串口暂不可用（设备未插/被占用）→ 稍后自动重试
        return false;
    }

    connect(m_serial, &QSerialPort::readyRead, this, &SerialCommNode::onDataReceived);
    connect(m_serial, &QSerialPort::errorOccurred, this, &SerialCommNode::onSerialError);
    m_connected = true;
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
    m_userClosed = true;   // 主动关闭：不触发自动重连
    if (m_reconnectTimer) m_reconnectTimer->stop();
    // 只在"确实连过"时上报断开：否则 openConnection() 开头的清理、每次自动重连尝试、
    // closeDevice/removeDevice 的无条件 close 都会各发一次假"串口连接中断"，
    // 下游（心跳/接收事件/界面状态）被反复的 closed/opened 抖动淹没。
    const bool wasConnected = m_connected;
    if (m_serial) {
        m_serial->close();
        m_serial->deleteLater();
        m_serial = nullptr;
    }
    m_connected = false;
    setParamDirect(QStringLiteral("connected"), false);   // 参数照旧写（不emit），保持界面数值真实
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
    // 静默早退是历史缺陷：sendData() 在调用瞬间判过 isConnected() 并已返回 true，
    // 到这里才真正 write——若中间串口已断（USB 转串口掉线），数据被静默丢弃。
    if (!m_connected || !m_serial) {
        emit communicationError(QStringLiteral("串口发送失败: 未连接"));
        return;
    }
    const qint64 written = m_serial->write(data);
    if (written != data.size()) {   // 短写/失败都必须报错，不能静默丢弃
        emit communicationError(QStringLiteral("\u4E32\u53E3\u53D1\u9001\u5931\u8D25: %1").arg(m_serial->errorString()));
        return;
    }
    m_serial->flush();
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
    reconnectCheck->setChecked(m_autoReconnect);
    connect(reconnectCheck, &QCheckBox::toggled, this, [this](bool on) {
        setParam(QStringLiteral("autoReconnect"), on);
    });
    layout->addWidget(reconnectCheck);

    auto *intervalSpin = new QSpinBox();
    intervalSpin->setObjectName(QStringLiteral("serialReconnectInterval"));
    intervalSpin->setRange(500, 60000);
    intervalSpin->setSingleStep(500);
    intervalSpin->setValue(m_reconnectInterval);
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
        ck->setChecked(m_autoReconnect);
    }
    if (auto *sp = panel->findChild<QSpinBox *>(QStringLiteral("serialReconnectInterval"))) {
        QSignalBlocker b(sp);
        sp->setValue(m_reconnectInterval);
    }
    if (auto *btn = panel->findChild<QPushButton *>(QStringLiteral("serialConnect"))) {
        btn->setText(m_connected ? QStringLiteral("\u65AD\u5F00\u8FDE\u63A5") : QStringLiteral("\u6253\u5F00\u8FDE\u63A5"));
    }
}
