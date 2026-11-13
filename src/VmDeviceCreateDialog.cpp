#include "VmDeviceCreateDialog.h"
#include "VmWidgets.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QIntValidator>
#include <QMouseEvent>
#include <QJsonArray>
#include <QStringList>

namespace {

struct TypeEntry {
    const char *display;
    const char *internal;
    const char *defaultName;
    int defaultPort;
};

/// 顺序与 CommunicationManagerDialog::onAddDevice 旧下拉一致，
/// 显示名按 VM 截图用词（TCP客户端 / ModBus通信）。
const TypeEntry kTypes[] = {
    { "TCP\u5BA2\u6237\u7AEF",  "TCP",      "TCP\u5BA2\u6237\u7AEF",  7920 },
    { "\u4E32\u53E3",            "\u4E32\u53E3", "\u4E32\u53E3",           0 },
    { "ModBus\u901A\u4FE1",      "Modbus",   "ModBus\u901A\u4FE1",     502 },
    { "PLC",                     "PLC",      "PLC",                    502 },
    { "UDP",                     "UDP",      "UDP",                    5000 },
    { "FINS TCP",                "FINS TCP", "FINS TCP",               9600 },
    { "FINS UDP",                "FINS UDP", "FINS UDP",               9600 },
};

constexpr int kTypeCount = int(sizeof(kTypes) / sizeof(kTypes[0]));

QString fieldLabelStyle()
{
    return QStringLiteral("color: #555555; font-size: 12px;");
}

QLabel *makeFieldLabel(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setMinimumWidth(76);
    l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    l->setStyleSheet(fieldLabelStyle());
    return l;
}

}   // namespace

VmDeviceCreateDialog::VmDeviceCreateDialog(QWidget *parent)
    : QDialog(parent)
{
    buildUi();
}

void VmDeviceCreateDialog::buildUi()
{
    setWindowTitle(QStringLiteral("\u8BBE\u5907\u7BA1\u7406"));
    setFixedSize(430, 470);
    // VM 截图：深灰自定义标题栏 + 下沿一条橙色线，没有系统边框。
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setStyleSheet(QStringLiteral(
        "QDialog { background-color: #f5f5f5; }"
        "QGroupBox { background-color: #ffffff; border: 1px solid #d8d8d8; "
        "border-radius: 2px; margin-top: 14px; font-size: 12px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; "
        "padding: 0 4px; color: #333333; }"
        "QLineEdit, QComboBox, QSpinBox { border: 1px solid #cfcfcf; "
        "background: #ffffff; padding: 4px 6px; min-height: 22px; }"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ───────── 自定义标题栏 ─────────
    m_titleBar = new QWidget(this);
    m_titleBar->setObjectName(QStringLiteral("vmDevTitleBar"));
    m_titleBar->setFixedHeight(34);
    m_titleBar->setStyleSheet(QStringLiteral(
        "QWidget#vmDevTitleBar { background-color: #2b2b2b; "
        "border-bottom: 2px solid #f26b21; }"));
    auto *titleRow = new QHBoxLayout(m_titleBar);
    titleRow->setContentsMargins(12, 0, 6, 0);
    auto *title = new QLabel(QStringLiteral("\u8BBE\u5907\u7BA1\u7406"), m_titleBar);
    title->setStyleSheet(QStringLiteral(
        "color: #ffffff; font-size: 13px; font-weight: bold;"));
    titleRow->addWidget(title);
    titleRow->addStretch();
    auto *closeBtn = new QPushButton(QStringLiteral("\u00D7"), m_titleBar);
    closeBtn->setObjectName(QStringLiteral("vmDevCloseBtn"));
    closeBtn->setFixedSize(30, 26);
    closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton { color: #d8d8d8; background: transparent; border: none; font-size: 16px; }"
        "QPushButton:hover { color: #ffffff; background-color: #444444; }"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    titleRow->addWidget(closeBtn);
    root->addWidget(m_titleBar);

    auto *content = new QWidget(this);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(16, 12, 16, 12);
    contentLayout->setSpacing(12);
    root->addWidget(content, 1);

    // ───────── 通信协议 ─────────
    m_protocolGroup = new QGroupBox(QStringLiteral("\u901A\u4FE1\u534F\u8BAE"), content);
    m_protocolGroup->setObjectName(QStringLiteral("vmDevProtocolGroup"));
    auto *protoGrid = new QGridLayout(m_protocolGroup);
    protoGrid->setContentsMargins(14, 16, 14, 12);
    protoGrid->setHorizontalSpacing(12);
    protoGrid->setVerticalSpacing(12);

    m_typeCombo = new QComboBox(m_protocolGroup);
    m_typeCombo->setObjectName(QStringLiteral("vmDevTypeCombo"));
    for (int i = 0; i < kTypeCount; ++i) {
        m_typeCombo->addItem(QString::fromUtf8(kTypes[i].display),
                             QString::fromUtf8(kTypes[i].internal));
    }
    m_nameEdit = new QLineEdit(m_protocolGroup);
    m_nameEdit->setObjectName(QStringLiteral("vmDevNameEdit"));

    protoGrid->addWidget(makeFieldLabel(QStringLiteral("\u534F\u8BAE\u7C7B\u578B"), m_protocolGroup), 0, 0);
    protoGrid->addWidget(m_typeCombo, 0, 1);
    protoGrid->addWidget(makeFieldLabel(QStringLiteral("\u8BBE\u5907\u540D\u79F0"), m_protocolGroup), 1, 0);
    protoGrid->addWidget(m_nameEdit, 1, 1);
    protoGrid->setColumnStretch(1, 1);
    contentLayout->addWidget(m_protocolGroup);

    // ───────── 通信参数 ─────────
    m_paramGroup = new QGroupBox(QStringLiteral("\u901A\u4FE1\u53C2\u6570"), content);
    m_paramGroup->setObjectName(QStringLiteral("vmDevParamGroup"));
    auto *paramLayout = new QVBoxLayout(m_paramGroup);
    paramLayout->setContentsMargins(14, 16, 14, 12);
    paramLayout->setSpacing(10);

    auto makeRow = [this, paramLayout](const QString &objectName) {
        auto *row = new QWidget(m_paramGroup);
        row->setObjectName(objectName);
        auto *grid = new QGridLayout(row);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(12);
        grid->setVerticalSpacing(0);
        grid->setColumnMinimumWidth(0, 76);
        paramLayout->addWidget(row);
        m_paramRows.append(row);
        return grid;
    };

    // 行 0：通信方式（仅 Modbus）
    auto *g0 = makeRow(QStringLiteral("vmRowCommMode"));
    m_commMode = new QComboBox(m_paramGroup);
    m_commMode->setObjectName(QStringLiteral("vmDevCommMode"));
    m_commMode->addItem(QStringLiteral("TcpClient"), QStringLiteral("TcpClient"));
    m_commMode->addItem(QStringLiteral("TcpServer"), QStringLiteral("TcpServer"));
    m_commMode->addItem(QStringLiteral("Rtu"), QStringLiteral("Rtu"));
    g0->addWidget(makeFieldLabel(QStringLiteral("\u901A\u4FE1\u65B9\u5F0F"), m_paramGroup), 0, 0);
    auto *commCell = new QWidget(m_paramGroup);
    auto *commCellLayout = new QHBoxLayout(commCell);
    commCellLayout->setContentsMargins(0, 0, 0, 0);
    commCellLayout->addWidget(m_commMode, 1);
    g0->addWidget(commCell, 0, 1);

    // 行 1：目标 IP（四段）
    auto *g1 = makeRow(QStringLiteral("vmRowIp"));
    auto *ipHost = new QWidget(m_paramGroup);
    auto *ipRow = new QHBoxLayout(ipHost);
    ipRow->setContentsMargins(0, 0, 0, 0);
    ipRow->setSpacing(0);
    for (int i = 0; i < 4; ++i) {
        m_ipEdits[i] = new QLineEdit(ipHost);
        m_ipEdits[i]->setObjectName(QStringLiteral("vmDevIp%1").arg(i));
        m_ipEdits[i]->setFixedWidth(46);
        m_ipEdits[i]->setMaxLength(3);
        m_ipEdits[i]->setAlignment(Qt::AlignCenter);
        m_ipEdits[i]->setValidator(new QIntValidator(0, 255, m_ipEdits[i]));
        ipRow->addWidget(m_ipEdits[i]);
        if (i < 3) {
            auto *dot = new QLabel(QStringLiteral("."), ipHost);
            dot->setFixedWidth(9);
            dot->setAlignment(Qt::AlignCenter);
            ipRow->addWidget(dot);
        }
    }
    ipRow->addStretch();
    g1->addWidget(makeFieldLabel(QStringLiteral("\u76EE\u6807IP"), m_paramGroup), 0, 0);
    g1->addWidget(ipHost, 0, 1);

    // 行 2：目标端口 / 轮询间隔（各自显隐）
    auto *g2 = makeRow(QStringLiteral("vmRowPort"));
    m_port = new QSpinBox(m_paramGroup);
    m_port->setObjectName(QStringLiteral("vmDevPort"));
    m_port->setRange(1, 65535);
    m_port->setFixedWidth(110);
    m_pollInterval = new QSpinBox(m_paramGroup);
    m_pollInterval->setObjectName(QStringLiteral("vmDevPollInterval"));
    m_pollInterval->setRange(1, 60000);
    m_pollInterval->setFixedWidth(110);
    m_pollInterval->setValue(10);

    auto *portCell = new QWidget(m_paramGroup);
    portCell->setObjectName(QStringLiteral("vmCellPort"));
    auto *portCellLayout = new QHBoxLayout(portCell);
    portCellLayout->setContentsMargins(0, 0, 0, 0);
    portCellLayout->addWidget(makeFieldLabel(QStringLiteral("\u76EE\u6807\u7AEF\u53E3"), portCell));
    portCellLayout->addWidget(m_port);
    portCellLayout->addStretch();
    auto *pollCell = new QWidget(m_paramGroup);
    pollCell->setObjectName(QStringLiteral("vmCellPoll"));
    auto *pollCellLayout = new QHBoxLayout(pollCell);
    pollCellLayout->setContentsMargins(0, 0, 0, 0);
    pollCellLayout->addWidget(makeFieldLabel(QStringLiteral("\u8F6E\u8BE2\u95F4\u9694(ms)"), pollCell));
    pollCellLayout->addWidget(m_pollInterval);
    pollCellLayout->addStretch();
    g2->addWidget(portCell, 0, 1);
    g2->addWidget(pollCell, 0, 2);

    // 行 3：串口 / 波特率（仅串口）
    auto *g3 = makeRow(QStringLiteral("vmRowSerial"));
    m_serialPort = new QComboBox(m_paramGroup);
    m_serialPort->setObjectName(QStringLiteral("vmDevSerialPort"));
    m_serialPort->setEditable(true);
    m_serialPort->addItems({ QStringLiteral("COM1"), QStringLiteral("COM2"),
                             QStringLiteral("COM3"), QStringLiteral("COM4") });
    m_baud = new QSpinBox(m_paramGroup);
    m_baud->setObjectName(QStringLiteral("vmDevBaud"));
    m_baud->setRange(1200, 921600);
    m_baud->setValue(9600);
    m_baud->setFixedWidth(110);
    g3->addWidget(makeFieldLabel(QStringLiteral("\u4E32\u53E3"), m_paramGroup), 0, 0);
    g3->addWidget(m_serialPort, 0, 1);
    auto *baudCell = new QWidget(m_paramGroup);
    auto *baudLayout = new QHBoxLayout(baudCell);
    baudLayout->setContentsMargins(0, 0, 0, 0);
    baudLayout->addWidget(makeFieldLabel(QStringLiteral("\u6CE2\u7279\u7387"), baudCell));
    baudLayout->addWidget(m_baud);
    g3->addWidget(baudCell, 0, 2);

    // 行 4：数据上传 / 自动重连
    auto *g4 = makeRow(QStringLiteral("vmRowFlags"));
    m_dataUpload = new VmSwitch(m_paramGroup);
    m_dataUpload->setObjectName(QStringLiteral("vmDevDataUpload"));
    m_dataUpload->setChecked(true);
    m_autoReconnect = new VmSwitch(m_paramGroup);
    m_autoReconnect->setObjectName(QStringLiteral("vmDevAutoReconnect"));
    m_autoReconnect->setChecked(true);
    g4->addWidget(makeFieldLabel(QStringLiteral("\u6570\u636E\u4E0A\u4F20"), m_paramGroup), 0, 0);
    g4->addWidget(m_dataUpload, 0, 1, Qt::AlignLeft);
    auto *reconnCell = new QWidget(m_paramGroup);
    reconnCell->setObjectName(QStringLiteral("vmCellReconnect"));
    auto *reconnLayout = new QHBoxLayout(reconnCell);
    reconnLayout->setContentsMargins(0, 0, 0, 0);
    reconnLayout->addWidget(makeFieldLabel(QStringLiteral("\u81EA\u52A8\u91CD\u8FDE"), reconnCell));
    reconnLayout->addWidget(m_autoReconnect);
    reconnLayout->addStretch();
    g4->addWidget(reconnCell, 0, 2);

    // 行 5：接收结束符
    auto *g5 = makeRow(QStringLiteral("vmRowRxTerm"));
    m_rxTerm = new VmSwitch(m_paramGroup);
    m_rxTerm->setObjectName(QStringLiteral("vmDevRxTerm"));
    g5->addWidget(makeFieldLabel(QStringLiteral("\u63A5\u6536\u7ED3\u675F\u7B26"), m_paramGroup), 0, 0);
    g5->addWidget(m_rxTerm, 0, 1, Qt::AlignLeft);

    m_hint = new QLabel(m_paramGroup);
    m_hint->setObjectName(QStringLiteral("vmDevHint"));
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet(QStringLiteral("color: #999999; font-size: 11px;"));
    paramLayout->addWidget(m_hint);
    paramLayout->addStretch();
    contentLayout->addWidget(m_paramGroup, 1);

    // ───────── 右下「创建」 ─────────
    auto *buttonRow = new QHBoxLayout();
    buttonRow->addStretch();
    m_createBtn = new QPushButton(QStringLiteral("\u521B\u5EFA"), this);
    m_createBtn->setObjectName(QStringLiteral("vmDevCreateBtn"));
    m_createBtn->setFixedSize(72, 28);
    m_createBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: #f26b21; color: #ffffff; border: none; border-radius: 2px; }"
        "QPushButton:hover { background: #e05e15; }"
        "QPushButton:disabled { background: #d8d8d8; color: #999999; }"));
    buttonRow->addWidget(m_createBtn);
    contentLayout->addLayout(buttonRow);

    for (int i = 0; i < 4; ++i)
        m_ipEdits[i]->setText(i == 0 ? QStringLiteral("127") : QStringLiteral("0"));
    m_ipEdits[3]->setText(QStringLiteral("1"));

    connect(m_typeCombo, &QComboBox::currentIndexChanged,
            this, &VmDeviceCreateDialog::onTypeChanged);
    connect(m_nameEdit, &QLineEdit::textChanged,
            this, &VmDeviceCreateDialog::updateCreateButton);
    connect(m_createBtn, &QPushButton::clicked, this, &QDialog::accept);

    onTypeChanged(m_typeCombo->currentIndex());
    updateCreateButton();
}

QString VmDeviceCreateDialog::deviceType() const
{
    return m_typeCombo ? m_typeCombo->currentData().toString() : QString();
}

QString VmDeviceCreateDialog::deviceName() const
{
    return m_nameEdit ? m_nameEdit->text().trimmed() : QString();
}

QWidget *VmDeviceCreateDialog::protocolGroupForTest() const
{
    return m_protocolGroup;
}

QWidget *VmDeviceCreateDialog::paramGroupForTest() const
{
    return m_paramGroup;
}

void VmDeviceCreateDialog::onTypeChanged(int index)
{
    if (index < 0 || index >= kTypeCount) return;
    const TypeEntry &e = kTypes[index];
    const QString autoName = QString::fromUtf8(e.defaultName);

    // 名称为空、或仍是上一次自动填的名字时，跟着协议类型更新（VM 默认名行为）。
    // 用户一旦手改过，就不再覆盖。
    if (m_nameEdit
        && (m_nameEdit->text().trimmed().isEmpty()
            || m_nameEdit->text() == m_lastAutoName)) {
        m_nameEdit->setText(autoName);
        m_lastAutoName = autoName;
    }
    m_port->setValue(e.defaultPort > 0 ? e.defaultPort : 1);
    applyTypeVisibility();
    updateCreateButton();
}

void VmDeviceCreateDialog::updateCreateButton()
{
    if (m_createBtn)
        m_createBtn->setEnabled(!deviceName().isEmpty());
}

void VmDeviceCreateDialog::applyTypeVisibility()
{
    const QString t = deviceType();
    const bool serial = (t == QStringLiteral("\u4E32\u53E3"));
    const bool modbus = (t == QStringLiteral("Modbus"));
    const bool polled = (modbus || t == QStringLiteral("PLC")
                         || t.startsWith(QStringLiteral("FINS")));
    const bool network = !serial;

    // 行索引与 buildUi 中 makeRow 的顺序严格对应
    if (m_paramRows.size() >= 6) {
        m_paramRows[0]->setVisible(modbus);                 // 通信方式
        m_paramRows[1]->setVisible(network);                // 目标 IP
        m_paramRows[2]->setVisible(true);                   // 端口 / 轮询
        m_paramRows[3]->setVisible(serial);                 // 串口 / 波特率
        m_paramRows[4]->setVisible(true);                   // 数据上传 / 自动重连
        m_paramRows[5]->setVisible(!polled);                // 接收结束符
    }
    if (auto *cell = findChild<QWidget *>(QStringLiteral("vmCellPort")))
        cell->setVisible(network);
    if (auto *cell = findChild<QWidget *>(QStringLiteral("vmCellPoll")))
        cell->setVisible(polled);
    if (m_dataUpload)
        m_dataUpload->setVisible(!polled || modbus);

    if (m_hint) {
        m_hint->setText(polled
            ? QStringLiteral("\u5BC4\u5B58\u5668\u7B49\u9AD8\u7EA7\u53C2\u6570\u8BF7\u5728\u521B\u5EFA\u540E\u70B9\u3010\u914D\u7F6E\u3011\u5B8C\u5584")
            : QString());
    }
}

QString VmDeviceCreateDialog::ipString() const
{
    QStringList segs;
    for (int i = 0; i < 4; ++i)
        segs << (m_ipEdits[i] ? m_ipEdits[i]->text().trimmed() : QStringLiteral("0"));
    return segs.join(QLatin1Char('.'));
}

QJsonObject VmDeviceCreateDialog::config() const
{
    QJsonObject c;
    const QString t = deviceType();
    const QString ip = ipString();
    const int port = m_port ? m_port->value() : 502;
    const bool autoReconnect = m_autoReconnect && m_autoReconnect->isChecked();
    const bool dataUpload = m_dataUpload && m_dataUpload->isChecked();
    const bool rxTerm = m_rxTerm && m_rxTerm->isChecked();

    if (t == QStringLiteral("TCP")) {
        c[QStringLiteral("mode")] = QStringLiteral("Client");
        c[QStringLiteral("serverIp")] = ip;
        c[QStringLiteral("port")] = port;
        c[QStringLiteral("dataUpload")] = dataUpload;
        c[QStringLiteral("autoReconnect")] = autoReconnect;
        c[QStringLiteral("frameTerminator")] = rxTerm ? QStringLiteral("\r\n") : QString();
    } else if (t == QStringLiteral("UDP")) {
        c[QStringLiteral("localPort")] = port;
        c[QStringLiteral("remoteIp")] = ip;
        c[QStringLiteral("remotePort")] = port;
        c[QStringLiteral("dataUpload")] = dataUpload;
        c[QStringLiteral("autoReconnect")] = autoReconnect;
    } else if (t == QStringLiteral("\u4E32\u53E3")) {
        c[QStringLiteral("portName")] = m_serialPort->currentText().trimmed();
        c[QStringLiteral("baudRate")] = m_baud->value();
        c[QStringLiteral("dataBits")] = 8;
        c[QStringLiteral("stopBits")] = 1;
        c[QStringLiteral("parity")] = QStringLiteral("None");
        c[QStringLiteral("autoReconnect")] = autoReconnect;
        c[QStringLiteral("frameTerminator")] = rxTerm ? QStringLiteral("\r\n") : QString();
    } else if (t == QStringLiteral("Modbus")) {
        c[QStringLiteral("role")] = QStringLiteral("Client");
        c[QStringLiteral("connectionType")] = m_commMode->currentData().toString();
        c[QStringLiteral("host")] = ip;
        c[QStringLiteral("port")] = port;
        c[QStringLiteral("pollInterval")] = m_pollInterval->value();
        c[QStringLiteral("slaveAddress")] = 1;
        c[QStringLiteral("autoReconnect")] = autoReconnect;
        c[QStringLiteral("registers")] = QJsonArray();
    } else if (t == QStringLiteral("PLC")) {
        c[QStringLiteral("plcBrand")] = QStringLiteral("Siemens");
        c[QStringLiteral("host")] = ip;
        c[QStringLiteral("port")] = port;
        c[QStringLiteral("pollInterval")] = m_pollInterval->value();
        c[QStringLiteral("slaveAddress")] = 1;
        c[QStringLiteral("autoReconnect")] = autoReconnect;
        c[QStringLiteral("registers")] = QJsonArray();
    } else if (t.startsWith(QStringLiteral("FINS"))) {
        c[QStringLiteral("targetIp")] = ip;
        c[QStringLiteral("targetPort")] = port;
        c[QStringLiteral("pollInterval")] = m_pollInterval->value();
        c[QStringLiteral("timeoutMs")] = 3000;
        c[QStringLiteral("autoReconnect")] = autoReconnect;
        c[QStringLiteral("localNode")] = 0;
        c[QStringLiteral("remoteNode")] = 0;
        c[QStringLiteral("registers")] = QJsonArray();
    }
    return c;
}

void VmDeviceCreateDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_titleBar
        && m_titleBar->geometry().contains(event->pos())) {
        m_dragPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QDialog::mousePressEvent(event);
}

void VmDeviceCreateDialog::mouseMoveEvent(QMouseEvent *event)
{
    if ((event->buttons() & Qt::LeftButton) && !m_dragPos.isNull()) {
        move(event->globalPosition().toPoint() - m_dragPos);
        event->accept();
        return;
    }
    QDialog::mouseMoveEvent(event);
}

void VmDeviceCreateDialog::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragPos = QPoint();
    QDialog::mouseReleaseEvent(event);
}
