#include "PlcConfigDialog.h"
#include "PlcCommNode.h"
#include "ModbusNode.h"  // ModbusRegisterItem
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

PlcConfigDialog::PlcConfigDialog(const QString &title,
                                 PlcCommNode *node,
                                 QWidget *parent)
    : QDialog(parent), m_plcNode(node)
{
    setWindowTitle(title);
    setMinimumSize(850, 600);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    // 只阻塞父窗口（通信管理），不阻塞主界面——exec() 会尊重已设置的 WindowModal
    setWindowModality(Qt::WindowModal);
    setupUI();
    connectToNodeLiveUpdates();
    refreshToggleSwitch();
}

void PlcConfigDialog::setupUI()
{
    auto *mainLayout = new QVBoxLayout(this);

    // ===== 连接切换开关 =====
    auto *toggleGroup = new QGroupBox(QStringLiteral("\u8FDE\u63A5\u72B6\u6001"));
    auto *toggleLayout = new QHBoxLayout(toggleGroup);
    m_toggleSwitch = new QPushButton();
    m_toggleSwitch->setCheckable(true);
    m_toggleSwitch->setMinimumWidth(130);
    m_toggleSwitch->setMaximumHeight(30);
    connect(m_toggleSwitch, &QPushButton::clicked, this, &PlcConfigDialog::onToggleConnection);
    toggleLayout->addWidget(m_toggleSwitch);
    toggleLayout->addStretch();
    mainLayout->addWidget(toggleGroup);

    // ===== 基本连接参数 =====
    auto *basicGroup = new QGroupBox(QStringLiteral("\u57FA\u672C\u8FDE\u63A5\u53C2\u6570"));
    auto *formLayout = new QFormLayout(basicGroup);

    m_brandCombo = new QComboBox();
    m_brandCombo->addItems({QStringLiteral("Siemens"), QStringLiteral("Mitsubishi"),
                            QStringLiteral("Omron"), QStringLiteral("Keyence"),
                            QStringLiteral("Panasonic"), QStringLiteral("\u901A\u7528")});
    m_brandCombo->setToolTip(QStringLiteral("\u4E0D\u540C\u54C1\u724C PLC \u5747\u652F\u6301 Modbus TCP\uFF0C"
        "\u901A\u8FC7\u53E3\u7684\u4ECE\u7AD9\u5730\u5740\u533A\u5206\u3002"));
    formLayout->addRow(QStringLiteral("\u54C1\u724C:"), m_brandCombo);

    m_host = new QLineEdit();
    m_host->setPlaceholderText(QStringLiteral("\u4F8B: 192.168.0.1"));
    formLayout->addRow(QStringLiteral("\u4E3B\u673A\u5730\u5740:"), m_host);

    m_port = new QSpinBox();
    m_port->setRange(1, 65535);
    m_port->setValue(502);
    formLayout->addRow(QStringLiteral("\u7AEF\u53E3:"), m_port);

    m_slaveAddress = new QSpinBox();
    m_slaveAddress->setRange(1, 247);
    m_slaveAddress->setValue(1);
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
    rpForm->addRow(QStringLiteral("\u8F6E\u8BE2\u5468\u671F:"), m_pollInterval);

    reconnPollLayout->addLayout(rpForm);
    mainLayout->addWidget(reconnPollGroup);

    // ===== 寄存器表格 — 7 列 =====
    auto *regGroup = new QGroupBox(QStringLiteral("\u5BC4\u5B58\u5668\u914D\u7F6E"));
    auto *regLayout = new QVBoxLayout(regGroup);

    auto *infoLabel = new QLabel(QStringLiteral(
        "\u25B2 \u53CC\u51FB\"\u5F53\u524D\u503C\"\u5217\u53EF\u76F4\u63A5\u4FEE\u6539\u5E76\u5199\u5165\u5230 PLC\u3002\n"
        "\u25B2 \u8BBF\u95EE\u6A21\u5F0F\u53EA\u8BFB: \u4EC5\u4ECE PLC \u83B7\u53D6\u503C\uFF1B\u53EF\u8BFB\u53EF\u5199: \u53EF\u5411 PLC \u5199\u5165\u503C\u3002"));
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
        QStringLiteral("\u5F53\u524D\u503C"),
        QStringLiteral("\u63CF\u8FF0"),
        QStringLiteral("\u542F\u7528")
    });
    m_registerTable->horizontalHeader()->setStretchLastSection(false);
    m_registerTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_registerTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_registerTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_registerTable->setMinimumHeight(180);

    connect(m_registerTable, &QTableWidget::cellDoubleClicked,
            this, &PlcConfigDialog::onCellDoubleClicked);

    regLayout->addWidget(m_registerTable);

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

    connect(addBtn, &QPushButton::clicked, this, &PlcConfigDialog::onAddRegister);
    connect(removeBtn, &QPushButton::clicked, this, &PlcConfigDialog::onRemoveRegister);
    connect(m_writeBtn, &QPushButton::clicked, this, [this]() {
        int row = m_registerTable->currentRow();
        if (row >= 0) onWriteValueToRegister(row);
    });

    mainLayout->addWidget(regGroup);

    // ===== 确认/取消 =====
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &PlcConfigDialog::onAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

void PlcConfigDialog::setConfig(const QJsonObject &config)
{
    loadConfigToForm(config);
}

QJsonObject PlcConfigDialog::config() const
{
    return buildConfigFromForm();
}

void PlcConfigDialog::onToggleConnection()
{
    if (!m_plcNode) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("\u8BE5\u8BBE\u5907\u5C1A\u672A\u5173\u8054\u901A\u4FE1\u8282\u70B9\u3002"));
        m_toggleSwitch->setChecked(!m_toggleSwitch->isChecked());
        return;
    }
    if (m_plcNode->isConnected()) {
        m_plcNode->closeConnection();
    } else {
        m_plcNode->openConnection();
    }
    refreshToggleSwitch();
}

void PlcConfigDialog::refreshToggleSwitch()
{
    if (!m_toggleSwitch) return;
    bool on = (m_plcNode ? m_plcNode->isConnected() : false);
    m_toggleSwitch->setChecked(on);
    m_toggleSwitch->setText(on
        ? QStringLiteral("\u2714 \u5DF2\u8FDE\u63A5")
        : QStringLiteral("\u25B6 \u8FDE\u63A5 PLC"));

    m_toggleSwitch->setStyleSheet(on
        ? "QPushButton { background-color: #4CAF50; color: white; border: none; "
          "border-radius: 4px; padding: 4px 12px; font-weight: bold; }"
          "QPushButton:hover { background-color: #45a049; }"
        : "QPushButton { background-color: #f44336; color: white; border: none; "
          "border-radius: 4px; padding: 4px 12px; font-weight: bold; }"
          "QPushButton:hover { background-color: #da190b; }"
          "QPushButton:checked { background-color: #4CAF50; }");
}

void PlcConfigDialog::connectToNodeLiveUpdates()
{
    if (!m_plcNode) return;

    connect(m_plcNode, &PlcCommNode::registerCurrentValueChanged,
            this, [this](int address, double value, const QString &displayText) {
        updateRegisterValue(address, value, displayText);
    }, Qt::QueuedConnection);

    connect(m_plcNode, &PlcCommNode::connectionOpened, this, [this]() { refreshToggleSwitch(); });
    connect(m_plcNode, &PlcCommNode::connectionClosed, this, [this]() { refreshToggleSwitch(); });

    QList<ModbusRegisterItem> regs = m_plcNode->registers();
    for (const auto &r : regs) {
        if (r.hasLastValue) {
            updateRegisterValue(r.address, r.currentValue, r.displayValue);
        }
    }
}

void PlcConfigDialog::updateRegisterValue(int address, double value, const QString &displayText)
{
    for (int row = 0; row < m_registerTable->rowCount(); ++row) {
        if (registerAddressForRow(row) == address) {
            QTableWidgetItem *valueItem = m_registerTable->item(row, 4);
            if (valueItem) valueItem->setText(displayText);
            valueItem->setBackground(QColor(220, 255, 220));
            QTimer::singleShot(300, this, [this, row]() {
                QTableWidgetItem *vi = m_registerTable->item(row, 4);
                if (vi) vi->setBackground(QColor(255, 255, 255));
            });
            break;
        }
    }
}

int PlcConfigDialog::registerAddressForRow(int row) const
{
    QTableWidgetItem *addrItem = m_registerTable->item(row, 0);
    if (!addrItem) return -1;
    bool ok = false;
    int addr = addrItem->text().toInt(&ok);
    return ok ? addr : -1;
}

void PlcConfigDialog::loadConfigToForm(const QJsonObject &config)
{
    if (config.contains(QStringLiteral("plcBrand")))
        m_brandCombo->setCurrentText(config[QStringLiteral("plcBrand")].toString());
    if (config.contains(QStringLiteral("host")))
        m_host->setText(config[QStringLiteral("host")].toString());
    if (config.contains(QStringLiteral("port")))
        m_port->setValue(config[QStringLiteral("port")].toInt());
    if (config.contains(QStringLiteral("slaveAddress")))
        m_slaveAddress->setValue(config[QStringLiteral("slaveAddress")].toInt());
    if (config.contains(QStringLiteral("autoReconnect")))
        m_autoReconnect->setChecked(config[QStringLiteral("autoReconnect")].toBool());
    if (config.contains(QStringLiteral("reconnectInterval")))
        m_reconnectInterval->setValue(config[QStringLiteral("reconnectInterval")].toInt());
    if (config.contains(QStringLiteral("pollInterval")))
        m_pollInterval->setValue(config[QStringLiteral("pollInterval")].toInt());

    m_registerTable->setRowCount(0);
    QJsonArray regsArr = config[QStringLiteral("registers")].toArray();
    for (const auto &v : regsArr) {
        QJsonObject ro = v.toObject();
        int row = m_registerTable->rowCount();
        m_registerTable->insertRow(row);

        m_registerTable->setItem(row, 0, new QTableWidgetItem(QString::number(ro[QStringLiteral("address")].toInt())));

        auto *typeCombo = new QComboBox();
        typeCombo->addItems({QStringLiteral("int16"), QStringLiteral("uint16"),
                             QStringLiteral("int32"), QStringLiteral("float")});
        typeCombo->setCurrentText(ro[QStringLiteral("dataType")].toString(QStringLiteral("int16")));
        m_registerTable->setCellWidget(row, 1, typeCombo);

        auto *orderCombo = new QComboBox();
        orderCombo->addItems({QStringLiteral("ABCD"), QStringLiteral("CDAB"),
                              QStringLiteral("BADC"), QStringLiteral("DCBA")});
        orderCombo->setCurrentText(ro[QStringLiteral("byteOrder")].toString(QStringLiteral("ABCD")));
        m_registerTable->setCellWidget(row, 2, orderCombo);

        auto *accessCombo = new QComboBox();
        accessCombo->addItems({
            QStringLiteral("\u53EA\u8BFB"),
            QStringLiteral("\u53EF\u8BFB\u53EF\u5199"),
            QStringLiteral("\u53EA\u5199")
        });
        QString accessMode = ro[QStringLiteral("accessMode")].toString(QStringLiteral("Read"));
        if (accessMode == QStringLiteral("ReadWrite")) accessCombo->setCurrentIndex(1);
        else if (accessMode == QStringLiteral("Write")) accessCombo->setCurrentIndex(2);
        else accessCombo->setCurrentIndex(0);
        m_registerTable->setCellWidget(row, 3, accessCombo);

        auto *valueItem = new QTableWidgetItem(QStringLiteral("--"));
        valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);
        valueItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_registerTable->setItem(row, 4, valueItem);

        m_registerTable->setItem(row, 5, new QTableWidgetItem(ro[QStringLiteral("description")].toString()));

        auto *enableItem = new QTableWidgetItem();
        enableItem->setCheckState(ro[QStringLiteral("enabled")].toBool(true) ? Qt::Checked : Qt::Unchecked);
        m_registerTable->setItem(row, 6, enableItem);
    }

    refreshToggleSwitch();
}

QJsonObject PlcConfigDialog::buildConfigFromForm() const
{
    QJsonObject cfg;
    cfg[QStringLiteral("plcBrand")] = m_brandCombo->currentText();
    cfg[QStringLiteral("host")] = m_host->text();
    cfg[QStringLiteral("port")] = m_port->value();
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

void PlcConfigDialog::onAddRegister()
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
    accessCombo->addItems({QStringLiteral("\u53EA\u8BFB"),
                           QStringLiteral("\u53EF\u8BFB\u53EF\u5199"),
                           QStringLiteral("\u53EA\u5199")});
    m_registerTable->setCellWidget(row, 3, accessCombo);

    auto *valueItem = new QTableWidgetItem(QStringLiteral("--"));
    valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);
    valueItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_registerTable->setItem(row, 4, valueItem);

    m_registerTable->setItem(row, 5, new QTableWidgetItem(QStringLiteral("Register_%1").arg(row)));

    auto *enableItem = new QTableWidgetItem();
    enableItem->setCheckState(Qt::Checked);
    m_registerTable->setItem(row, 6, enableItem);
}

void PlcConfigDialog::onRemoveRegister()
{
    int row = m_registerTable->currentRow();
    if (row >= 0) m_registerTable->removeRow(row);
}

void PlcConfigDialog::onCellDoubleClicked(int row, int column)
{
    if (column != 4) return;
    if (!m_plcNode) return;

    auto *accessCombo = qobject_cast<QComboBox *>(m_registerTable->cellWidget(row, 3));
    int accessIdx = accessCombo ? accessCombo->currentIndex() : 0;
    if (accessIdx == 0) {
        QMessageBox::information(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("\u8BE5\u5BC4\u5B58\u5668\u4E3A\u53EA\u8BFB\u6A21\u5F0F\uFF0C\u65E0\u6CD5\u5199\u5165\u3002"));
        return;
    }
    onWriteValueToRegister(row);
}

void PlcConfigDialog::onWriteValueToRegister(int row)
{
    if (!m_plcNode || !m_plcNode->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("PLC \u672A\u8FDE\u63A5\uFF0C\u65E0\u6CD5\u5199\u5165\u3002"));
        return;
    }

    auto *addrItem = m_registerTable->item(row, 0);
    if (!addrItem) return;
    bool ok = false;
    int address = addrItem->text().toInt(&ok);
    if (!ok) return;

    auto *typeCombo = qobject_cast<QComboBox *>(m_registerTable->cellWidget(row, 1));
    QString dataType = typeCombo ? typeCombo->currentText() : QStringLiteral("int16");

    QString currentDisplay = m_registerTable->item(row, 4)
        ? m_registerTable->item(row, 4)->text() : QStringLiteral("0");

    double input = QInputDialog::getDouble(this,
        QStringLiteral("\u5199\u5165\u5BC4\u5B58\u5668 - \u5730\u5740%1").arg(address),
        QStringLiteral("\u8F93\u5165\u503C (\u6570\u636E\u7C7B\u578B: %1):").arg(dataType),
        currentDisplay.toDouble(), -1e12, 1e12, dataType == QStringLiteral("float") ? 4 : 0, &ok);
    if (!ok) return;

    quint16 regValue = static_cast<quint16>(static_cast<int>(input));
    if (m_plcNode->writeRegister(address, regValue)) {
        QTableWidgetItem *valueItem = m_registerTable->item(row, 4);
        if (valueItem) {
            valueItem->setText(QString::number(input, 'f', dataType == QStringLiteral("float") ? 4 : 0));
            valueItem->setBackground(QColor(200, 230, 255));
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

void PlcConfigDialog::onAccept()
{
    accept();
}
