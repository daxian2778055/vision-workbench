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
    // 播种现有配置：表单只覆盖它自己管理的键，其余键（autoReconnect / reconnectInterval /
    // 组帧之外的扩展键）原样保留。历史缺陷：不播种 + onAccept 从零构造 → 热更新后这些键
    // 被静默丢弃（存盘再打开"断线自动重连"就没了）。
    m_config = initial;
    setWindowTitle(QStringLiteral("设备配置 — %1").arg(type));
    resize(420, 260);
    // 只阻塞父窗口（通信管理），不阻塞主界面
    setWindowModality(Qt::WindowModal);

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

    addFrameRows(form, initial);
}

void CommDeviceConfigDialog::addFrameRows(QFormLayout *form, const QJsonObject &initial)
{
    QWidget *host = form->parentWidget();
    m_frameTimeoutSpin = new QSpinBox(host);
    m_frameTimeoutSpin->setRange(0, 60000);
    m_frameTimeoutSpin->setSingleStep(10);
    m_frameTimeoutSpin->setValue(initial.value(QStringLiteral("frameTimeoutMs")).toInt(0));
    m_frameTimeoutSpin->setToolTip(QStringLiteral(
        "0 = 不启用。静默超过该毫秒数即认为一帧结束（治理粘包/半包：\n"
        "对端连续发多帧或一帧被拆开时，保证按帧触发而不是按到达块触发）"));
    m_frameTermEdit = new QLineEdit(host);
    m_frameTermEdit->setText(initial.value(QStringLiteral("frameTerminator")).toString());
    m_frameTermEdit->setPlaceholderText(QStringLiteral("如 \\r\\n（留空 = 不按结束符切帧）"));
    m_frameTermEdit->setToolTip(QStringLiteral("帧结束符，支持转义：\\r \\n \\t \\xHH"));
    form->addRow(QStringLiteral("组帧超时(ms)"), m_frameTimeoutSpin);
    form->addRow(QStringLiteral("帧结束符"), m_frameTermEdit);
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

    m_dataBitsCombo = new QComboBox(group);
    for (int b : { 5, 6, 7, 8 })
        m_dataBitsCombo->addItem(QString::number(b), b);
    {
        const int idx = m_dataBitsCombo->findData(
            initial.value(QStringLiteral("dataBits")).toInt(8));
        m_dataBitsCombo->setCurrentIndex(idx >= 0 ? idx : 3);
    }

    m_stopBitsCombo = new QComboBox(group);
    m_stopBitsCombo->addItem(QStringLiteral("1"), 1);
    m_stopBitsCombo->addItem(QStringLiteral("2"), 2);
    m_stopBitsCombo->addItem(QStringLiteral("1.5"), 3);
    {
        const int idx = m_stopBitsCombo->findData(
            initial.value(QStringLiteral("stopBits")).toInt(1));
        m_stopBitsCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    m_parityCombo = new QComboBox(group);
    m_parityCombo->addItem(QStringLiteral("\u65E0"), QStringLiteral("None"));
    m_parityCombo->addItem(QStringLiteral("\u5076"), QStringLiteral("Even"));
    m_parityCombo->addItem(QStringLiteral("\u5947"), QStringLiteral("Odd"));
    {
        const int idx = m_parityCombo->findData(
            initial.value(QStringLiteral("parity")).toString(QStringLiteral("None")));
        m_parityCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    form->addRow(QStringLiteral("串口号"), m_serialPortCombo);
    form->addRow(QStringLiteral("波特率"), m_serialBaudCombo);
    form->addRow(QStringLiteral("\u6570\u636E\u4F4D"), m_dataBitsCombo);
    form->addRow(QStringLiteral("\u505C\u6B62\u4F4D"), m_stopBitsCombo);
    form->addRow(QStringLiteral("\u6821\u9A8C"), m_parityCombo);

    addFrameRows(form, initial);
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
        "目标 IP 留空时只接收、不发送。\n"
        "目标 IP 填 255.255.255.255 即广播（已自动开启广播权限）。"), group);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: gray; font-size: 11px;");
    form->addRow(hint);

    addFrameRows(form, initial);
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
        m_config[QStringLiteral("dataBits")] = m_dataBitsCombo->currentData().toInt();
        m_config[QStringLiteral("stopBits")] = m_stopBitsCombo->currentData().toInt();
        m_config[QStringLiteral("parity")] = m_parityCombo->currentData().toString();
    }

    // 组帧参数（三类通用）：0/空 = 不启用（行为与历史一致）
    m_config[QStringLiteral("frameTimeoutMs")] =
        m_frameTimeoutSpin ? m_frameTimeoutSpin->value() : 0;
    m_config[QStringLiteral("frameTerminator")] =
        m_frameTermEdit ? m_frameTermEdit->text() : QString();
    accept();
}
