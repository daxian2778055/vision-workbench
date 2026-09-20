#include "ModbusConfigDialog.h"
#include "ModbusNode.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QInputDialog>
#include <QTimer>
#include <QSerialPortInfo>

ModbusConfigDialog::ModbusConfigDialog(const QString &title,
                                       ModbusNode *node,
                                       QWidget *parent)
    : QDialog(parent), m_modbusNode(node)
{
    setWindowTitle(title);
    setMinimumSize(850, 600);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    // 只阻塞父窗口（通信管理），不阻塞主界面——exec() 会尊重已设置的 WindowModal
    setWindowModality(Qt::WindowModal);
    setupUI();
    connectToNodeLiveUpdates();
    refreshRoleUi();
    refreshToggleSwitch();
}

void ModbusConfigDialog::setupUI()
{
    auto *mainLayout = new QVBoxLayout(this);

    // ===== 连接角色（客户端/服务器）=====
    auto *roleGroup = new QGroupBox(QStringLiteral("\u901A\u4FE1\u89D2\u8272"));
    auto *roleLayout = new QHBoxLayout(roleGroup);
    m_roleCombo = new QComboBox();
    m_roleCombo->addItems({
        QStringLiteral("\u5BA2\u6237\u7AEF"),   // 主站：连接服务器
        QStringLiteral("\u670D\u52A1\u5668")    // 从站：对外提供寄存器
    });
    m_roleCombo->setToolTip(QStringLiteral(
        "\u5BA2\u6237\u7AEF: \u4E3B\u52A8\u8FDE\u63A5\u5916\u90E8 Modbus \u670D\u52A1\u5668\u5E76\u8F6E\u8BE2\u8BFB\u5199\u3002\n"
        "\u670D\u52A1\u5668: \u76D1\u542C\u672C\u5730\u7AEF\u53E3\uFF0C\u5411\u5916\u90E8\u5BA2\u6237\u63D0\u4F9B\u5BC4\u5B58\u5668\u8BFB\u5199\u3002"));
    roleLayout->addWidget(new QLabel(QStringLiteral("\u89D2\u8272:")));
    roleLayout->addWidget(m_roleCombo);
    roleLayout->addStretch();
    mainLayout->addWidget(roleGroup);

    // 连接切换开关
    m_toggleSwitch = new QPushButton();
    m_toggleSwitch->setCheckable(true);
    m_toggleSwitch->setMinimumWidth(130);
    m_toggleSwitch->setMaximumHeight(30);
    connect(m_toggleSwitch, &QPushButton::clicked, this, [this]() {
        if (!m_modbusNode) {
            QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"),
                QStringLiteral("\u8BE5\u8BBE\u5907\u5C1A\u672A\u5173\u8054\u901A\u4FE1\u8282\u70B9\uFF0C\u65E0\u6CD5\u8FDE\u63A5\u3002"));
            m_toggleSwitch->setChecked(!m_toggleSwitch->isChecked());
            return;
        }
        if (m_modbusNode->isConnected()) {
            m_modbusNode->closeConnection();
        } else {
            m_modbusNode->openConnection();
        }
        refreshToggleSwitch();
    });

    // ===== 基本连接参数 =====
    auto *basicGroup = new QGroupBox(QStringLiteral("\u57FA\u672C\u8FDE\u63A5\u53C2\u6570"));
    auto *formLayout = new QFormLayout(basicGroup);

    formLayout->addRow(QStringLiteral("\u8FDE\u63A5\u72B6\u6001:"), m_toggleSwitch);

    m_connType = new QComboBox();
    m_connType->addItems({QStringLiteral("TCP"), QStringLiteral("RTU")});
    m_connType->setToolTip(QStringLiteral("TCP: \u7F51\u53E3\u8FDE\u63A5 | RTU: \u4E32\u53E3\u8FDE\u63A5"));
    formLayout->addRow(QStringLiteral("\u8FDE\u63A5\u7C7B\u578B:"), m_connType);

    m_hostLabel = new QLabel(QStringLiteral("\u4E3B\u673A\u5730\u5740:"));
    m_host = new QLineEdit();
    m_host->setPlaceholderText(QStringLiteral("\u4F8B: 192.168.1.100"));
    formLayout->addRow(m_hostLabel, m_host);

    m_portLabel = new QLabel(QStringLiteral("端口:"));
    m_port = new QSpinBox();
    m_port->setRange(1, 65535);
    m_port->setValue(502);
    formLayout->addRow(m_portLabel, m_port);

    // RTU（RS485）串口参数：连接类型选 RTU 时必须能配串口——
    // 历史缺陷：有 RTU 选项但没有任何串口字段，节点侧也漏设参数，选了 RTU 永远连不上
    m_serialPortLabel = new QLabel(QStringLiteral("串口号:"));
    m_serialPortName = new QComboBox();
    m_serialPortName->setEditable(true);
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts())
        m_serialPortName->addItem(info.portName());
    formLayout->addRow(m_serialPortLabel, m_serialPortName);

    m_serialBaudLabel = new QLabel(QStringLiteral("波特率:"));
    m_serialBaudRate = new QComboBox();
    m_serialBaudRate->setEditable(true);
    for (int b : { 9600, 19200, 38400, 57600, 115200 })
        m_serialBaudRate->addItem(QString::number(b));
    m_serialBaudRate->setCurrentText(QStringLiteral("9600"));
    formLayout->addRow(m_serialBaudLabel, m_serialBaudRate);

    // 数据位/停止位/校验：ModbusNode 会读这三个键，但 UI 此前根本不产出 → 8E1/8O1 设备连不上。
    // 停止位存 QSerialPort::StopBits 枚举值（1=1位 / 3=2位 / 2=1.5位），因为 QModbus
    // 把 SerialStopBitsParameter 的值直接当该枚举使用（与 SerialCommNode 的约定不同）。
    m_serialDataBitsLabel = new QLabel(QStringLiteral("数据位:"));
    m_serialDataBits = new QComboBox();
    for (int b : { 5, 6, 7, 8 })
        m_serialDataBits->addItem(QString::number(b), b);
    {
        const int idx = m_serialDataBits->findData(8);
        m_serialDataBits->setCurrentIndex(idx >= 0 ? idx : m_serialDataBits->count() - 1);
    }
    formLayout->addRow(m_serialDataBitsLabel, m_serialDataBits);

    m_serialStopBitsLabel = new QLabel(QStringLiteral("停止位:"));
    m_serialStopBits = new QComboBox();
    m_serialStopBits->addItem(QStringLiteral("1"), 1);     // QSerialPort::OneStop
    m_serialStopBits->addItem(QStringLiteral("2"), 3);     // QSerialPort::TwoStop
    m_serialStopBits->addItem(QStringLiteral("1.5"), 2);   // QSerialPort::OneAndHalfStop
    formLayout->addRow(m_serialStopBitsLabel, m_serialStopBits);

    m_serialParityLabel = new QLabel(QStringLiteral("校验:"));
    m_serialParity = new QComboBox();
    m_serialParity->addItem(QStringLiteral("无"), QStringLiteral("None"));
    m_serialParity->addItem(QStringLiteral("偶"), QStringLiteral("Even"));
    m_serialParity->addItem(QStringLiteral("奇"), QStringLiteral("Odd"));
    formLayout->addRow(m_serialParityLabel, m_serialParity);

    connect(m_connType, &QComboBox::currentTextChanged, this,
            [this](const QString &) { refreshConnTypeUi(); });
    refreshConnTypeUi();

    m_slaveAddress = new QSpinBox();
    m_slaveAddress->setRange(1, 247);
    m_slaveAddress->setValue(1);
    m_slaveAddress->setToolTip(QStringLiteral("\u4ECE\u7AD9\u5730\u5740\uFF08\u670D\u52A1\u5668\u6A21\u5F0F\u4E0B\u4E3A\u672C\u673A\u5730\u5740\uFF09"));
    formLayout->addRow(QStringLiteral("\u4ECE\u7AD9\u5730\u5740:"), m_slaveAddress);

    mainLayout->addWidget(basicGroup);

    // ===== 自动重连与轮询 =====
    auto *reconnPollGroup = new QGroupBox(QStringLiteral("\u81EA\u52A8\u91CD\u8FDE\u4E0E\u8F6E\u8BE2"));
    auto *reconnPollLayout = new QVBoxLayout(reconnPollGroup);

    m_autoReconnect = new QCheckBox(QStringLiteral("\u542F\u7528\u81EA\u52A8\u91CD\u8FDE"));
    reconnPollLayout->addWidget(m_autoReconnect);

    auto *rpForm = new QFormLayout();
    m_reconnectInterval = new QSpinBox();
    m_reconnectInterval->setRange(500, 60000);
    m_reconnectInterval->setValue(3000);
    m_reconnectInterval->setSuffix(QStringLiteral(" ms"));
    rpForm->addRow(QStringLiteral("\u91CD\u8FDE\u95F4\u9694:"), m_reconnectInterval);

    m_pollInterval = new QSpinBox();
    m_pollInterval->setRange(10, 10000);
    m_pollInterval->setValue(100);
    m_pollInterval->setSuffix(QStringLiteral(" ms"));
    m_pollInterval->setToolTip(QStringLiteral("\u5BC4\u5B58\u5668\u8F6E\u8BE2\u95F4\u9694\uFF0C\u9ED8\u8BA4 100ms"));
    rpForm->addRow(QStringLiteral("\u8F6E\u8BE2\u5468\u671F:"), m_pollInterval);

    reconnPollLayout->addLayout(rpForm);
    mainLayout->addWidget(reconnPollGroup);

    // ===== 寄存器表格 — 7 列 =====
    auto *regGroup = new QGroupBox(QStringLiteral("\u5BC4\u5B58\u5668\u914D\u7F6E"));
    auto *regLayout = new QVBoxLayout(regGroup);

    auto *infoLabel = new QLabel(QStringLiteral(
        "\u25B2 \u53CC\u51FB\"\u5F53\u524D\u503C\"\u5217\u53EF\u76F4\u63A5\u4FEE\u6539\u5E76\u5199\u5165\u5230\u5916\u90E8\u8BBE\u5907\uFF08\u4EC5\u9650\u8BBF\u95EE\u6A21\u5F0F\u4E3A\u53EF\u5199\u65F6\uFF09\u3002\n"
        "\u25B2 \"\u8BBF\u95EE\u6A21\u5F0F\"\u4E3A\u53EA\u8BFB\u65F6\uFF0C\u4EC5\u4ECE\u5916\u90E8\u8BBE\u5907\u83B7\u53D6\u503C\uFF1B\u4E3A\u53EF\u5199\u65F6\uFF0C\u53EF\u5411\u5916\u90E8\u8BBE\u5907\u5199\u5165\u503C\u3002"
    ));
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: gray; font-size: 11px; padding-bottom: 4px;");
    regLayout->addWidget(infoLabel);

    m_registerTable = new QTableWidget();
    m_registerTable->setColumnCount(7);
    m_registerTable->setHorizontalHeaderLabels({
        QStringLiteral("\u5BC4\u5B58\u5668\u5730\u5740"),
        QStringLiteral("\u6570\u636E\u7C7B\u578B"),
        QStringLiteral("\u5B57\u8282\u987A\u5E8F"),
        QStringLiteral("\u8BBF\u95EE\u6A21\u5F0F"),
        QStringLiteral("\u5F53\u524D\u503C"),      // 实时显示
        QStringLiteral("\u63CF\u8FF0"),
        QStringLiteral("\u542F\u7528")
    });
    m_registerTable->horizontalHeader()->setStretchLastSection(false);
    m_registerTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_registerTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_registerTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_registerTable->setMinimumHeight(180);

    // 双击当前值列可写入
    connect(m_registerTable, &QTableWidget::cellDoubleClicked,
            this, &ModbusConfigDialog::onCellDoubleClicked);

    regLayout->addWidget(m_registerTable);

    // 按钮行
    auto *btnLayout = new QHBoxLayout();
    auto *addBtn = new QPushButton(QStringLiteral("\u6DFB\u52A0\u5BC4\u5B58\u5668"));
    auto *removeBtn = new QPushButton(QStringLiteral("\u5220\u9664"));
    m_writeBtn = new QPushButton(QStringLiteral("\u5199\u5165\u503C"));
    m_writeBtn->setStyleSheet("QPushButton { color: #0066cc; font-weight: bold; }");
    btnLayout->addWidget(addBtn);
    btnLayout->addWidget(removeBtn);
    btnLayout->addWidget(m_writeBtn);
    btnLayout->addStretch();
    regLayout->addLayout(btnLayout);

    connect(addBtn, &QPushButton::clicked, this, &ModbusConfigDialog::onAddRegister);
    connect(removeBtn, &QPushButton::clicked, this, &ModbusConfigDialog::onRemoveRegister);
    connect(m_writeBtn, &QPushButton::clicked, this, [this]() {
        int row = m_registerTable->currentRow();
        if (row >= 0) onWriteValueToRegister(row);
    });

    mainLayout->addWidget(regGroup);

    // ===== 确认/取消 =====
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &ModbusConfigDialog::onAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    // 角色切换联动
    connect(m_roleCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ModbusConfigDialog::onRoleChanged);
}

void ModbusConfigDialog::setConfig(const QJsonObject &config)
{
    loadConfigToForm(config);
}

QJsonObject ModbusConfigDialog::config() const
{
    return buildConfigFromForm();
}

void ModbusConfigDialog::onRoleChanged(int /*index*/)
{
    refreshRoleUi();
    refreshToggleSwitch();
}

void ModbusConfigDialog::refreshRoleUi()
{
    if (!m_roleCombo) return;
    bool isServer = (m_roleCombo->currentIndex() == 1);

    // 服务器模式隐藏主机地址，端口语义为"监听端口"
    if (m_portLabel) m_portLabel->setText(isServer
        ? QStringLiteral("监听端口:")
        : QStringLiteral("端口:"));
    // 主机/端口 vs 串口参数（RTU）的可见性统一由 refreshConnTypeUi 决定
    refreshConnTypeUi();
    if (m_autoReconnect) m_autoReconnect->setEnabled(!isServer);
    if (m_reconnectInterval) m_reconnectInterval->setEnabled(!isServer);
    if (m_pollInterval) m_pollInterval->setEnabled(!isServer);
    if (m_pollInterval && m_pollInterval->toolTip().isEmpty()) {
        // no-op
    }
}

void ModbusConfigDialog::refreshConnTypeUi()
{
    const bool isRtu = (m_connType && m_connType->currentText() == QStringLiteral("RTU"));
    const bool isServer = (m_roleCombo && m_roleCombo->currentIndex() == 1);

    // TCP：主机地址（客户端）+ 端口；RTU：串口号 + 波特率
    if (m_host) m_host->setVisible(!isRtu && !isServer);
    if (m_hostLabel) m_hostLabel->setVisible(!isRtu && !isServer);
    if (m_port) m_port->setVisible(!isRtu || isServer);       // 服务器固定 TCP 监听端口
    if (m_portLabel) m_portLabel->setVisible(!isRtu || isServer);
    if (m_serialPortLabel) m_serialPortLabel->setVisible(isRtu);
    if (m_serialPortName) m_serialPortName->setVisible(isRtu);
    if (m_serialBaudLabel) m_serialBaudLabel->setVisible(isRtu);
    if (m_serialBaudRate) m_serialBaudRate->setVisible(isRtu);
    if (m_serialDataBitsLabel) m_serialDataBitsLabel->setVisible(isRtu);
    if (m_serialDataBits) m_serialDataBits->setVisible(isRtu);
    if (m_serialStopBitsLabel) m_serialStopBitsLabel->setVisible(isRtu);
    if (m_serialStopBits) m_serialStopBits->setVisible(isRtu);
    if (m_serialParityLabel) m_serialParityLabel->setVisible(isRtu);
    if (m_serialParity) m_serialParity->setVisible(isRtu);
}

void ModbusConfigDialog::refreshToggleSwitch()
{
    if (!m_toggleSwitch) return;
    bool isServer = (m_roleCombo && m_roleCombo->currentIndex() == 1);
    bool on = (m_modbusNode ? m_modbusNode->isConnected() : false);

    m_toggleSwitch->setChecked(on);
    m_toggleSwitch->setText(isServer
        ? (on ? QStringLiteral("\u25B6 \u670D\u52A1\u5668\u5DF2\u542F\u52A8") : QStringLiteral("\u25A0 \u542F\u52A8\u670D\u52A1\u5668"))
        : (on ? QStringLiteral("\u2714 \u5DF2\u8FDE\u63A5") : QStringLiteral("\u25B6 \u8FDE\u63A5\u670D\u52A1\u5668")));

    QString style = on
        ? "QPushButton { background-color: #4CAF50; color: white; border: none; "
          "border-radius: 4px; padding: 4px 12px; font-weight: bold; }"
          "QPushButton:hover { background-color: #45a049; }"
        : "QPushButton { background-color: #f44336; color: white; border: none; "
          "border-radius: 4px; padding: 4px 12px; font-weight: bold; }"
          "QPushButton:hover { background-color: #da190b; }"
          "QPushButton:checked { background-color: #4CAF50; }";
    m_toggleSwitch->setStyleSheet(style);
}

void ModbusConfigDialog::connectToNodeLiveUpdates()
{
    if (!m_modbusNode) return;

    // 连接 ModbusNode 的实时寄存器值更新信号
    connect(m_modbusNode, &ModbusNode::registerCurrentValueChanged,
            this, [this](int address, double value, const QString &displayText) {
        updateRegisterValue(address, value, displayText);
    }, Qt::QueuedConnection);

    // 节点连接/断开状态变化时刷新切换开关
    connect(m_modbusNode, &ModbusNode::connectionOpened, this, [this]() {
        refreshToggleSwitch();
    });
    connect(m_modbusNode, &ModbusNode::connectionClosed, this, [this]() {
        refreshToggleSwitch();
    });

    // 如果节点已连接且有数据，刷新一次
    // 读取现有寄存器值
    QList<ModbusRegisterItem> regs = m_modbusNode->registers();
    for (int row = 0; row < regs.size(); ++row) {
        const auto &r = regs[row];
        if (r.hasLastValue) {
            updateRegisterValue(r.address, r.currentValue, r.displayValue);
        }
    }
}

void ModbusConfigDialog::updateRegisterValue(int address, double value, const QString &displayText)
{
    for (int row = 0; row < m_registerTable->rowCount(); ++row) {
        if (registerAddressForRow(row) == address) {
            QTableWidgetItem *valueItem = m_registerTable->item(row, 4);
            if (valueItem) {
                valueItem->setText(displayText);
            }
            // 根据值变化高亮闪烁提示
            valueItem->setBackground(QColor(220, 255, 220)); // 浅绿色闪烁
            // 用定时器恢复背景色
            QTimer::singleShot(300, this, [this, row]() {
                QTableWidgetItem *vi = m_registerTable->item(row, 4);
                if (vi) vi->setBackground(QColor(255, 255, 255));
            });
            break;
        }
    }
}

int ModbusConfigDialog::registerAddressForRow(int row) const
{
    QTableWidgetItem *addrItem = m_registerTable->item(row, 0);
    if (!addrItem) return -1;
    bool ok = false;
    int addr = addrItem->text().toInt(&ok);
    return ok ? addr : -1;
}

void ModbusConfigDialog::loadConfigToForm(const QJsonObject &config)
{
    if (config.contains(QStringLiteral("role"))) {
        QString role = config[QStringLiteral("role")].toString();
        int idx = (role == QStringLiteral("\u670D\u52A1\u5668") || role == QStringLiteral("Server")) ? 1 : 0;
        m_roleCombo->setCurrentIndex(idx);
    }
    if (config.contains(QStringLiteral("connectionType")))
        m_connType->setCurrentText(config[QStringLiteral("connectionType")].toString());
    if (config.contains(QStringLiteral("host")))
        m_host->setText(config[QStringLiteral("host")].toString());
    if (config.contains(QStringLiteral("port")))
        m_port->setValue(config[QStringLiteral("port")].toInt());
    if (config.contains(QStringLiteral("portName")) && m_serialPortName) {
        const QString pn = config[QStringLiteral("portName")].toString();
        if (!pn.isEmpty() && m_serialPortName->findText(pn) < 0)
            m_serialPortName->addItem(pn);   // 当前机器上不存在的串口也允许显示（现场换机常见）
        m_serialPortName->setCurrentText(pn);
    }
    if (config.contains(QStringLiteral("baudRate")) && m_serialBaudRate)
        m_serialBaudRate->setCurrentText(QString::number(config[QStringLiteral("baudRate")].toInt()));
    if (config.contains(QStringLiteral("dataBits")) && m_serialDataBits) {
        const int idx = m_serialDataBits->findData(config[QStringLiteral("dataBits")].toInt());
        if (idx >= 0) m_serialDataBits->setCurrentIndex(idx);
    }
    if (config.contains(QStringLiteral("stopBits")) && m_serialStopBits) {
        const int idx = m_serialStopBits->findData(config[QStringLiteral("stopBits")].toInt());
        if (idx >= 0) m_serialStopBits->setCurrentIndex(idx);
    }
    if (config.contains(QStringLiteral("parity")) && m_serialParity) {
        const int idx = m_serialParity->findData(config[QStringLiteral("parity")].toString());
        if (idx >= 0) m_serialParity->setCurrentIndex(idx);
    }
    if (config.contains(QStringLiteral("slaveAddress")))
        m_slaveAddress->setValue(config[QStringLiteral("slaveAddress")].toInt());
    if (config.contains(QStringLiteral("autoReconnect")))
        m_autoReconnect->setChecked(config[QStringLiteral("autoReconnect")].toBool());
    if (config.contains(QStringLiteral("reconnectInterval")))
        m_reconnectInterval->setValue(config[QStringLiteral("reconnectInterval")].toInt());
    if (config.contains(QStringLiteral("pollInterval")))
        m_pollInterval->setValue(config[QStringLiteral("pollInterval")].toInt());

    // 加载寄存器
    m_registerTable->setRowCount(0);
    QJsonArray regsArr = config[QStringLiteral("registers")].toArray();
    for (const auto &v : regsArr) {
        QJsonObject ro = v.toObject();
        int row = m_registerTable->rowCount();
        m_registerTable->insertRow(row);

        // 列 0: 寄存器地址
        auto *addrItem = new QTableWidgetItem(QString::number(ro[QStringLiteral("address")].toInt()));
        m_registerTable->setItem(row, 0, addrItem);

        // 列 1: 数据类型
        auto *typeCombo = new QComboBox();
        typeCombo->addItems({QStringLiteral("int16"), QStringLiteral("uint16"),
                             QStringLiteral("int32"), QStringLiteral("float")});
        typeCombo->setCurrentText(ro[QStringLiteral("dataType")].toString(QStringLiteral("int16")));
        m_registerTable->setCellWidget(row, 1, typeCombo);

        // 列 2: 字节顺序
        auto *orderCombo = new QComboBox();
        orderCombo->addItems({QStringLiteral("ABCD"), QStringLiteral("CDAB"),
                              QStringLiteral("BADC"), QStringLiteral("DCBA")});
        orderCombo->setCurrentText(ro[QStringLiteral("byteOrder")].toString(QStringLiteral("ABCD")));
        m_registerTable->setCellWidget(row, 2, orderCombo);

        // 列 3: 访问模式
        auto *accessCombo = new QComboBox();
        accessCombo->addItems({
            QStringLiteral("\u53EA\u8BFB"),       // Read
            QStringLiteral("\u53EF\u8BFB\u53EF\u5199"), // ReadWrite
            QStringLiteral("\u53EA\u5199")        // Write
        });
        QString accessMode = ro[QStringLiteral("accessMode")].toString(QStringLiteral("Read"));
        if (accessMode == QStringLiteral("ReadWrite"))
            accessCombo->setCurrentIndex(1);
        else if (accessMode == QStringLiteral("Write"))
            accessCombo->setCurrentIndex(2);
        else
            accessCombo->setCurrentIndex(0);
        m_registerTable->setCellWidget(row, 3, accessCombo);

        // 列 4: 当前值（实时显示，初始为空）
        auto *valueItem = new QTableWidgetItem(QStringLiteral("--"));
        valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable); // 默认不可编辑
        valueItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_registerTable->setItem(row, 4, valueItem);

        // 列 5: 描述
        m_registerTable->setItem(row, 5, new QTableWidgetItem(ro[QStringLiteral("description")].toString()));

        // 列 6: 启用
        auto *enableItem = new QTableWidgetItem();
        enableItem->setCheckState(ro[QStringLiteral("enabled")].toBool(true) ? Qt::Checked : Qt::Unchecked);
        m_registerTable->setItem(row, 6, enableItem);
    }

    refreshRoleUi();
    refreshToggleSwitch();
}

QJsonObject ModbusConfigDialog::buildConfigFromForm() const
{
    QJsonObject cfg;
    cfg[QStringLiteral("role")] = (m_roleCombo->currentIndex() == 1)
        ? QStringLiteral("\u670D\u52A1\u5668") : QStringLiteral("\u5BA2\u6237\u7AEF");
    cfg[QStringLiteral("connectionType")] = m_connType->currentText();
    cfg[QStringLiteral("host")] = m_host->text();
    cfg[QStringLiteral("port")] = m_port->value();
    // RTU 串口参数（TCP 时为空/默认值，不影响既有配置）
    cfg[QStringLiteral("portName")] =
        m_serialPortName ? m_serialPortName->currentText().trimmed() : QString();
    cfg[QStringLiteral("baudRate")] =
        m_serialBaudRate ? m_serialBaudRate->currentText().toInt() : 9600;
    // 数据位/停止位/校验必须随配置落盘并投递到节点，否则 RTU 只能吃节点默认 8/1/None
    // （stopBits 存 QSerialPort::StopBits 枚举值：1=OneStop / 3=TwoStop / 2=OneAndHalfStop）
    cfg[QStringLiteral("dataBits")] =
        m_serialDataBits ? m_serialDataBits->currentData().toInt() : 8;
    cfg[QStringLiteral("stopBits")] =
        m_serialStopBits ? m_serialStopBits->currentData().toInt() : 1;
    cfg[QStringLiteral("parity")] =
        m_serialParity ? m_serialParity->currentData().toString() : QStringLiteral("None");
    cfg[QStringLiteral("slaveAddress")] = m_slaveAddress->value();
    cfg[QStringLiteral("autoReconnect")] = m_autoReconnect->isChecked();
    cfg[QStringLiteral("reconnectInterval")] = m_reconnectInterval->value();
    cfg[QStringLiteral("pollInterval")] = m_pollInterval->value();

    QJsonArray regsArr;
    for (int row = 0; row < m_registerTable->rowCount(); ++row) {
        QJsonObject ro;
        auto *addrItem = m_registerTable->item(row, 0);
        if (addrItem) ro[QStringLiteral("address")] = addrItem->text().toInt();

        auto *typeCombo = qobject_cast<QComboBox *>(m_registerTable->cellWidget(row, 1));
        ro[QStringLiteral("dataType")] = typeCombo ? typeCombo->currentText() : QStringLiteral("int16");

        auto *orderCombo = qobject_cast<QComboBox *>(m_registerTable->cellWidget(row, 2));
        ro[QStringLiteral("byteOrder")] = orderCombo ? orderCombo->currentText() : QStringLiteral("ABCD");

        auto *accessCombo = qobject_cast<QComboBox *>(m_registerTable->cellWidget(row, 3));
        QString accessMode = QStringLiteral("Read");
        if (accessCombo) {
            int idx = accessCombo->currentIndex();
            if (idx == 1) accessMode = QStringLiteral("ReadWrite");
            else if (idx == 2) accessMode = QStringLiteral("Write");
        }
        ro[QStringLiteral("accessMode")] = accessMode;

        auto *descItem = m_registerTable->item(row, 5);
        ro[QStringLiteral("description")] = descItem ? descItem->text() : QString();

        auto *enableItem = m_registerTable->item(row, 6);
        ro[QStringLiteral("enabled")] = enableItem ? (enableItem->checkState() == Qt::Checked) : true;

        regsArr.append(ro);
    }
    cfg[QStringLiteral("registers")] = regsArr;
    return cfg;
}

void ModbusConfigDialog::onAddRegister()
{
    int row = m_registerTable->rowCount();
    m_registerTable->insertRow(row);
    m_registerTable->setItem(row, 0, new QTableWidgetItem(QString::number(row * 10)));

    auto *typeCombo = new QComboBox();
    typeCombo->addItems({QStringLiteral("int16"), QStringLiteral("uint16"),
                         QStringLiteral("int32"), QStringLiteral("float")});
    m_registerTable->setCellWidget(row, 1, typeCombo);

    auto *orderCombo = new QComboBox();
    orderCombo->addItems({QStringLiteral("ABCD"), QStringLiteral("CDAB"),
                          QStringLiteral("BADC"), QStringLiteral("DCBA")});
    m_registerTable->setCellWidget(row, 2, orderCombo);

    auto *accessCombo = new QComboBox();
    accessCombo->addItems({
        QStringLiteral("\u53EA\u8BFB"),
        QStringLiteral("\u53EF\u8BFB\u53EF\u5199"),
        QStringLiteral("\u53EA\u5199")
    });
    m_registerTable->setCellWidget(row, 3, accessCombo);

    auto *valueItem = new QTableWidgetItem(QStringLiteral("--"));
    valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);
    valueItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_registerTable->setItem(row, 4, valueItem);

    m_registerTable->setItem(row, 5, new QTableWidgetItem(
        QStringLiteral("Register_%1").arg(row)));

    auto *enableItem = new QTableWidgetItem();
    enableItem->setCheckState(Qt::Checked);
    m_registerTable->setItem(row, 6, enableItem);
}

void ModbusConfigDialog::onRemoveRegister()
{
    int row = m_registerTable->currentRow();
    if (row >= 0) m_registerTable->removeRow(row);
}

void ModbusConfigDialog::onCellDoubleClicked(int row, int column)
{
    // 仅在当前值列(4)且已连接时才触发写入
    if (column != 4) return;
    if (!m_modbusNode) return;

    auto *accessCombo = qobject_cast<QComboBox *>(m_registerTable->cellWidget(row, 3));
    int accessIdx = accessCombo ? accessCombo->currentIndex() : 0;
    // 0=只读, 1=读写, 2=只写 — 只有读写和只写才能写入
    if (accessIdx == 0) {
        QMessageBox::information(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("\u8BE5\u5BC4\u5B58\u5668\u4E3A\u53EA\u8BFB\u6A21\u5F0F\uFF0C\u65E0\u6CD5\u5199\u5165\u3002"));
        return;
    }
    onWriteValueToRegister(row);
}

void ModbusConfigDialog::onWriteValueToRegister(int row)
{
    if (!m_modbusNode || !m_modbusNode->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("Modbus \u672A\u8FDE\u63A5\uFF0C\u65E0\u6CD5\u5199\u5165\u3002"));
        return;
    }

    auto *addrItem = m_registerTable->item(row, 0);
    if (!addrItem) return;
    bool ok = false;
    int address = addrItem->text().toInt(&ok);
    if (!ok) return;

    auto *typeCombo = qobject_cast<QComboBox *>(m_registerTable->cellWidget(row, 1));
    QString dataType = typeCombo ? typeCombo->currentText() : QStringLiteral("int16");

    // 弹出输入对话框让用户输入值
    QString currentDisplay = m_registerTable->item(row, 4)
        ? m_registerTable->item(row, 4)->text()
        : QStringLiteral("0");

    double input = QInputDialog::getDouble(this,
        QStringLiteral("\u5199\u5165\u5BC4\u5B58\u5668 - \u5730\u5740%1").arg(address),
        QStringLiteral("\u8F93\u5165\u503C (\u6570\u636E\u7C7B\u578B: %1):").arg(dataType),
        currentDisplay.toDouble(), -1e12, 1e12, dataType == QStringLiteral("float") ? 4 : 0, &ok);
    if (!ok) return;

    // 按该地址的 dataType/byteOrder 由节点内部逆变换拆字写入（含 int32/float 拆两寄存器），
    // 与读路径字节序往返对称（S3）。
    bool isServer = (m_roleCombo && m_roleCombo->currentIndex() == 1);
    bool okWrite;
    if (isServer) {
        // 服务器模式：直接更新本地寄存器（客户端可读到新值）
        okWrite = m_modbusNode->setLocalRegisterValue(address, input);
    } else {
        okWrite = m_modbusNode->writeRegister(address, input);
    }

    if (okWrite) {
        // 立即在UI上显示写入选定的值
        QTableWidgetItem *valueItem = m_registerTable->item(row, 4);
        if (valueItem) {
            valueItem->setText(QString::number(input, 'f', dataType == QStringLiteral("float") ? 4 : 0));
            valueItem->setBackground(QColor(200, 230, 255)); // 浅蓝色表示刚写入
            QTimer::singleShot(1000, this, [this, row]() {
                QTableWidgetItem *vi = m_registerTable->item(row, 4);
                if (vi) vi->setBackground(QColor(255, 255, 255));
            });
        }
        QMessageBox::information(this, QStringLiteral("\u6210\u529F"),
            QStringLiteral("\u5BC4\u5B58\u5668\u5730\u5740%1 \u5199\u5165\u6210\u529F\uFF1A%2")
                .arg(address).arg(input));
    } else {
        QMessageBox::warning(this, QStringLiteral("\u5931\u8D25"),
            QStringLiteral("\u5BC4\u5B58\u5668\u5730\u5740%1 \u5199\u5165\u5931\u8D25\u3002").arg(address));
    }
}

void ModbusConfigDialog::onAccept()
{
    accept();
}
