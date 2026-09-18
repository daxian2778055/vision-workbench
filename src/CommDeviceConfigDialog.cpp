#include "CommDeviceConfigDialog.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QSerialPortInfo>

CommDeviceConfigDialog::CommDeviceConfigDialog(const QString &type, const QJsonObject &initial,
                                               QWidget *parent)
    : QDialog(parent), m_type(type)
{
    setWindowTitle(QStringLiteral("设备配置 — %1").arg(type));
    resize(420, 260);

    auto *root = new QVBoxLayout(this);
    auto *gb = new QGroupBox(QStringLiteral("连接参数"), this);
    root->addWidget(gb);

    if (type == QStringLiteral("TCP")) {
        buildTcpForm(initial);
    } else if (type == QStringLiteral("UDP")) {
        buildUdpForm(initial);
    } else {
        buildSerialForm(initial);   // "串口"/"Serial"
    }

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &CommDeviceConfigDialog::onAccept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(box);
}

void CommDeviceConfigDialog::buildTcpForm(const QJsonObject &initial)
{
    auto *group = qobject_cast<QGroupBox *>(layout()->itemAt(0)->widget());
    auto *form = new QFormLayout(group);

    m_tcpModeCombo = new QComboBox(group);
    m_tcpModeCombo->addItems({ QStringLiteral("Client"), QStringLiteral("Server") });
    m_tcpModeCombo->setCurrentText(
        initial.value(QStringLiteral("mode")).toString(QStringLiteral("Client")));

    m_tcpIpEdit = new QLineEdit(group);
    m_tcpIpEdit->setText(
        initial.value(QStringLiteral("serverIp")).toString(QStringLiteral("127.0.0.1")));
    m_tcpIpEdit->setPlaceholderText(QStringLiteral("客户端模式：目标服务器 IP"));

    m_tcpPortSpin = new QSpinBox(group);
    m_tcpPortSpin->setRange(1, 65535);
    m_tcpPortSpin->setValue(initial.value(QStringLiteral("port")).toInt(502));

    form->addRow(QStringLiteral("模式"), m_tcpModeCombo);
    form->addRow(QStringLiteral("服务器 IP"), m_tcpIpEdit);
    form->addRow(QStringLiteral("端口"), m_tcpPortSpin);

    auto *hint = new QLabel(QStringLiteral(
        "Client：主动连接外部服务器（支持断线自动重连）。\n"
        "Server：本机监听端口，等待外部连接。"), group);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: gray; font-size: 11px;");
    form->addRow(hint);
}

void CommDeviceConfigDialog::buildSerialForm(const QJsonObject &initial)
{
    auto *group = qobject_cast<QGroupBox *>(layout()->itemAt(0)->widget());
    auto *form = new QFormLayout(group);

    m_serialPortCombo = new QComboBox(group);
    m_serialPortCombo->setEditable(true);
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts())
        m_serialPortCombo->addItem(info.portName());
    {
        const QString cur =
            initial.value(QStringLiteral("portName")).toString(QStringLiteral("COM1"));
        if (m_serialPortCombo->findText(cur) < 0)
            m_serialPortCombo->addItem(cur);
        m_serialPortCombo->setCurrentText(cur);
    }

    m_serialBaudCombo = new QComboBox(group);
    m_serialBaudCombo->setEditable(true);
    for (int b : { 9600, 19200, 38400, 57600, 115200, 230400 })
        m_serialBaudCombo->addItem(QString::number(b), b);
    {
        const int curBaud = initial.value(QStringLiteral("baudRate")).toInt(9600);
        const int idx = m_serialBaudCombo->findData(curBaud);
        if (idx >= 0)
            m_serialBaudCombo->setCurrentIndex(idx);
        else
            m_serialBaudCombo->setCurrentText(QString::number(curBaud));
    }

    form->addRow(QStringLiteral("串口号"), m_serialPortCombo);
    form->addRow(QStringLiteral("波特率"), m_serialBaudCombo);
}

void CommDeviceConfigDialog::buildUdpForm(const QJsonObject &initial)
{
    auto *group = qobject_cast<QGroupBox *>(layout()->itemAt(0)->widget());
    auto *form = new QFormLayout(group);

    m_udpLocalPortSpin = new QSpinBox(group);
    m_udpLocalPortSpin->setRange(0, 65535);
    m_udpLocalPortSpin->setValue(initial.value(QStringLiteral("localPort")).toInt(8000));

    m_udpRemoteIpEdit = new QLineEdit(group);
    m_udpRemoteIpEdit->setText(
        initial.value(QStringLiteral("remoteIp")).toString(QStringLiteral("127.0.0.1")));
    m_udpRemoteIpEdit->setPlaceholderText(QStringLiteral("留空 = 仅接收，不发送"));

    m_udpRemotePortSpin = new QSpinBox(group);
    m_udpRemotePortSpin->setRange(1, 65535);
    m_udpRemotePortSpin->setValue(initial.value(QStringLiteral("remotePort")).toInt(8000));

    form->addRow(QStringLiteral("本地接收端口"), m_udpLocalPortSpin);
    form->addRow(QStringLiteral("目标 IP"), m_udpRemoteIpEdit);
    form->addRow(QStringLiteral("目标端口"), m_udpRemotePortSpin);

    auto *hint = new QLabel(QStringLiteral(
        "本地端口 0 = 由系统随机分配（仅发送场景可用）。\n"
        "目标 IP 留空时只接收、不发送。"), group);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: gray; font-size: 11px;");
    form->addRow(hint);
}

void CommDeviceConfigDialog::onAccept()
{
    if (m_type == QStringLiteral("TCP")) {
        const QString mode = m_tcpModeCombo->currentText();
        const QString ip = m_tcpIpEdit->text().trimmed();
        if (mode == QStringLiteral("Client") && ip.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("参数不完整"),
                                 QStringLiteral("客户端模式必须填写服务器 IP"));
            return;
        }
        m_config[QStringLiteral("mode")] = mode;
        m_config[QStringLiteral("serverIp")] = ip;
        m_config[QStringLiteral("port")] = m_tcpPortSpin->value();
    } else if (m_type == QStringLiteral("UDP")) {
        m_config[QStringLiteral("localPort")] = m_udpLocalPortSpin->value();
        m_config[QStringLiteral("remoteIp")] = m_udpRemoteIpEdit->text().trimmed();
        m_config[QStringLiteral("remotePort")] = m_udpRemotePortSpin->value();
    } else {
        const QString port = m_serialPortCombo->currentText().trimmed();
        if (port.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("参数不完整"),
                                 QStringLiteral("请选择或填写串口号"));
            return;
        }
        m_config[QStringLiteral("portName")] = port;
        bool ok = false;
        const int baud = m_serialBaudCombo->currentText().toInt(&ok);
        m_config[QStringLiteral("baudRate")] = ok ? baud : 9600;
    }
    accept();
}
