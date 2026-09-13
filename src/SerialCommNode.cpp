#include "SerialCommNode.h"
#include "AppLog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
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
}

bool SerialCommNode::openConnection()
{
    if (m_serial) closeConnection();

    m_serial = new QSerialPort(this);
    m_serial->setPortName(m_params.value(QStringLiteral("portName")).toString());
    m_serial->setBaudRate(m_params.value(QStringLiteral("baudRate"), 9600).toInt());
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setParity(QSerialPort::NoParity);

    if (!m_serial->open(QIODevice::ReadWrite)) {
        emit communicationError(QStringLiteral("\u6253\u5F00\u4E32\u53E3\u5931\u8D25: %1").arg(m_serial->errorString()));
        m_connected = false;
        m_params[QStringLiteral("connected")] = false;
        return false;
    }

    connect(m_serial, &QSerialPort::readyRead, this, &SerialCommNode::onDataReceived);
    m_connected = true;
    m_params[QStringLiteral("connected")] = true;
    emit connectionOpened();
    return true;
}

void SerialCommNode::closeConnection()
{
    if (m_serial) {
        m_serial->close();
        m_serial->deleteLater();
        m_serial = nullptr;
    }
    m_connected = false;
    m_params[QStringLiteral("connected")] = false;
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
        m_params[QStringLiteral("lastReceived")] = QString::fromLatin1(data);
    }
}

void SerialCommNode::onSendRequested(const QByteArray &data)
{
    if (!m_connected || !m_serial) return;
    qint64 written = m_serial->write(data);
    if (written < 0) {
        emit communicationError(QStringLiteral("\u4E32\u53E3\u53D1\u9001\u5931\u8D25: %1").arg(m_serial->errorString()));
        return;
    }
    m_serial->flush();
    m_params[QStringLiteral("lastSent")] = QString::fromLatin1(data);
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
    if (auto *btn = panel->findChild<QPushButton *>(QStringLiteral("serialConnect"))) {
        btn->setText(m_connected ? QStringLiteral("\u65AD\u5F00\u8FDE\u63A5") : QStringLiteral("\u6253\u5F00\u8FDE\u63A5"));
    }
}
