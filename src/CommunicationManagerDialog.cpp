#include "CommunicationManagerDialog.h"
#include "CommunicationManager.h"
#include "CommunicationNodeBase.h"
#include "ModbusNode.h"
#include "ModbusConfigDialog.h"
#include "PlcCommNode.h"
#include "PlcConfigDialog.h"
#include "ReceiveEvent.h"
#include "SendEvent.h"
#include "CommEventEditDialogs.h"
#include "CommDeviceConfigDialog.h"
#include "HeartbeatManager.h"
#include "AppLog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFormLayout>
#include <QMessageBox>
#include <QInputDialog>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QDateTime>
#include <QColor>
#include <QTimer>

CommunicationManagerDialog::CommunicationManagerDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    refreshDeviceTable();
    refreshReceiveEventTable();
    refreshSendEventTable();
    refreshHeartbeatTable();
}

CommunicationManagerDialog::~CommunicationManagerDialog()
{
}

void CommunicationManagerDialog::setupUI()
{
    setWindowTitle(QStringLiteral("\u901A\u4FE1\u7BA1\u7406"));
    setMinimumSize(750, 500);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *mainLayout = new QVBoxLayout(this);

    auto *tabs = new QTabWidget();
    setupDeviceTab(tabs);
    setupReceiveEventTab(tabs);
    setupSendEventTab(tabs);
    setupHeartbeatTab(tabs);

    mainLayout->addWidget(tabs);

    auto *btnLayout = new QHBoxLayout();
    m_closeBtn = new QPushButton(QStringLiteral("\u5173\u95ED"));
    m_closeBtn->setMinimumHeight(30);
    btnLayout->addStretch();
    btnLayout->addWidget(m_closeBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    QTabWidget *t = tabs; Q_UNUSED(t)
}

// ==================== Device Tab ====================

void CommunicationManagerDialog::setupDeviceTab(QTabWidget *tabs)
{
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);

    auto *label = new QLabel(QStringLiteral("<b>\u8BBE\u5907\u7BA1\u7406</b>"));
    label->setStyleSheet("font-size: 13px;");
    layout->addWidget(label);

    // 设备表格 — 5 列：名称 / 类型 / 连接开关 / 配置 / 寄存器实时值
    m_deviceTable = new QTableWidget();
    m_deviceTable->setColumnCount(5);
    m_deviceTable->setHorizontalHeaderLabels({
        QStringLiteral("\u8BBE\u5907\u540D\u79F0"),
        QStringLiteral("\u7C7B\u578B"),
        QStringLiteral("\u8FDE\u63A5\u72B6\u6001"),
        QStringLiteral("\u914D\u7F6E"),
        QStringLiteral("\u5BC4\u5B58\u5668\u503C")
    });
    m_deviceTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_deviceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_deviceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_deviceTable->horizontalHeader()->setStretchLastSection(true);
    m_deviceTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    layout->addWidget(m_deviceTable);

    auto *btnLayout = new QHBoxLayout();
    m_addDeviceBtn = new QPushButton(QStringLiteral("\u6DFB\u52A0\u8BBE\u5907"));
    m_removeDeviceBtn = new QPushButton(QStringLiteral("\u5220\u9664\u8BBE\u5907"));
    m_configBtn = new QPushButton(QStringLiteral("\u914D\u7F6E"));
    m_readBtn = new QPushButton(QStringLiteral("\u5237\u65B0\u5BC4\u5B58\u5668"));

    btnLayout->addWidget(m_addDeviceBtn);
    btnLayout->addWidget(m_removeDeviceBtn);
    btnLayout->addWidget(m_configBtn);
    btnLayout->addWidget(m_readBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    connect(m_addDeviceBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onAddDevice);
    connect(m_removeDeviceBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onRemoveDevice);
    connect(m_configBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onConfigDevice);
    connect(m_readBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onReadRegisters);

    // 连接状态实时刷新：自动重连/对端断开/重连成功都会更新表格。
    // 关键：必须**异步合并**刷新——openDevice/closeDevice 会同步 emit
    // deviceConnected/deviceDisconnected，若在信号里同步重建表格，
    // 发起操作的 onToggleConnection 栈上仍持有该行「连接开关」按钮裸指针，
    // 表格重建销毁按钮后再访问即为 use-after-free（点"连接开关"即崩的根因）。
    connect(CommunicationManager::instance(), &CommunicationManager::deviceConnected, this,
            [this](const QString &) { scheduleDeviceTableRefresh(); });
    connect(CommunicationManager::instance(), &CommunicationManager::deviceDisconnected, this,
            [this](const QString &) { scheduleDeviceTableRefresh(); });

    tabs->addTab(tab, QStringLiteral("\u8BBE\u5907\u7BA1\u7406"));
}

void CommunicationManagerDialog::refreshDeviceTable()
{
    // 保留当前选中设备：连接状态实时刷新（自动重连/对端断开）时避免选中行跳动
    QString selectedName;
    {
        const int selRow = m_deviceTable->currentRow();
        if (selRow >= 0 && m_deviceTable->item(selRow, 0))
            selectedName = m_deviceTable->item(selRow, 0)->text();
    }
    m_deviceTable->setRowCount(0);
    auto *cm = CommunicationManager::instance();

    for (const QString &name : cm->deviceNames()) {
        CommDeviceInfo info = cm->deviceInfo(name);
        int row = m_deviceTable->rowCount();
        m_deviceTable->insertRow(row);
        m_deviceTable->setItem(row, 0, new QTableWidgetItem(name));
        m_deviceTable->setItem(row, 1, new QTableWidgetItem(info.type));

        // 列 2: 连接切换开关（QPushButton 风格化为开关样式）
        auto *toggleBtn = new QPushButton();
        toggleBtn->setCheckable(true);
        toggleBtn->setMinimumWidth(80);
        toggleBtn->setMaximumHeight(28);

        // 判断是否为 Modbus 服务器角色
        bool isModbusServer = false;
        if (info.type == QStringLiteral("Modbus")) {
            auto *mn = qobject_cast<ModbusNode *>(cm->deviceNode(name));
            if (mn) isModbusServer = (mn->role() == ModbusNode::MODBUS_SERVER);
        }
        QString onText = isModbusServer ? QStringLiteral("\u2714 \u670D\u52A1\u5668\u5DF2\u542F\u52A8")
                                        : QStringLiteral("\u2714 \u5DF2\u8FDE\u63A5");
        QString offText = isModbusServer ? QStringLiteral("\u25B6 \u542F\u52A8\u670D\u52A1\u5668")
                                         : QStringLiteral("\u25B6 \u672A\u8FDE\u63A5");

        if (info.isConnected) {
            toggleBtn->setChecked(true);
            toggleBtn->setText(onText);
            toggleBtn->setStyleSheet(
                "QPushButton { background-color: #4CAF50; color: white; border: none; "
                "border-radius: 4px; padding: 4px 12px; font-weight: bold; }"
                "QPushButton:hover { background-color: #45a049; }");
        } else {
            toggleBtn->setChecked(false);
            toggleBtn->setText(offText);
            toggleBtn->setStyleSheet(
                "QPushButton { background-color: #f44336; color: white; border: none; "
                "border-radius: 4px; padding: 4px 12px; font-weight: bold; }"
                "QPushButton:hover { background-color: #da190b; }"
                "QPushButton:checked { background-color: #4CAF50; }");
        }
        connect(toggleBtn, &QPushButton::clicked, this, [this, toggleBtn, name, row]() {
            onToggleConnection(row);
        });
        m_deviceTable->setCellWidget(row, 2, toggleBtn);

        // 列 3: 配置摘要
        QString configStr;
        if (info.config.contains(QStringLiteral("portName")))
            configStr = info.config[QStringLiteral("portName")].toString();
        else if (info.config.contains(QStringLiteral("serverIp")))
            configStr = info.config[QStringLiteral("serverIp")].toString() + QStringLiteral(":") +
                       QString::number(info.config[QStringLiteral("port")].toInt());
        else if (info.config.contains(QStringLiteral("host")))
            configStr = info.config[QStringLiteral("host")].toString();
        m_deviceTable->setItem(row, 3, new QTableWidgetItem(configStr));

        // 列 4: 寄存器实时值（仅 Modbus 显示）
        auto *regValueWidget = new QWidget();
        auto *regLayout = new QHBoxLayout(regValueWidget);
        regLayout->setContentsMargins(2, 2, 2, 2);
        auto *regLabel = new QLabel(QStringLiteral("--"));
        regLabel->setStyleSheet("color: gray; font-size: 11px;");
        regLayout->addWidget(regLabel);
        regLayout->addStretch();
        m_deviceTable->setCellWidget(row, 4, regValueWidget);

        if (info.type == QStringLiteral("Modbus")) {
            auto *modbusNode = qobject_cast<ModbusNode *>(cm->deviceNode(name));
            if (modbusNode) {
                // 连接实时寄存器值更新。
                // context 必须是 regLabel（而不是 this）：表格重建会销毁行内 QLabel，
                // 连接随接收者销毁自动断开；若挂在 this 上，每次刷新都会累积一条
                // 指向已销毁 QLabel 的活跃连接（自动重连抖动下每 3s 刷新一次 → 悬空写必崩）
                connect(modbusNode, &ModbusNode::registerCurrentValueChanged,
                        regLabel, [regLabel](int addr, double val, const QString &displayText) {
                    Q_UNUSED(val)
                    QString current = regLabel->text();
                    if (current == QStringLiteral("--")) current.clear();
                    else if (current.length() > 80) current.clear();

                    if (!current.isEmpty()) current += QStringLiteral(" | ");
                    current += QStringLiteral("D%1=%2").arg(addr).arg(displayText);

                    // 仅保留最近几个
                    QStringList parts = current.split(QStringLiteral(" | "));
                    while (parts.size() > 6) parts.removeFirst();
                    regLabel->setText(parts.join(QStringLiteral(" | ")));
                    regLabel->setStyleSheet("color: #0066cc; font-size: 11px;");
                }, Qt::QueuedConnection);
            }
        }
    }
    m_deviceTable->resizeColumnsToContents();
    // 保证连接开关列和寄存器列有合适宽度
    m_deviceTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_deviceTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);

    // 恢复选中设备
    if (!selectedName.isEmpty()) {
        for (int r = 0; r < m_deviceTable->rowCount(); ++r) {
            if (m_deviceTable->item(r, 0) && m_deviceTable->item(r, 0)->text() == selectedName) {
                m_deviceTable->setCurrentCell(r, 0);
                break;
            }
        }
    }
}

void CommunicationManagerDialog::scheduleDeviceTableRefresh()
{
    if (m_deviceTableRefreshPending) return;
    m_deviceTableRefreshPending = true;
    // 0ms 单发：推迟到当前调用栈展开后再重建，且把连续多个状态信号合并为一次刷新
    // （context 为 this：对话框销毁后自动取消，不会打到已析构对象）
    QTimer::singleShot(0, this, [this]() {
        m_deviceTableRefreshPending = false;
        refreshDeviceTable();
    });
}

void CommunicationManagerDialog::onToggleConnection(int row)
{
    if (row < 0 || row >= m_deviceTable->rowCount()) return;
    QString name = m_deviceTable->item(row, 0)->text();
    auto *cm = CommunicationManager::instance();
    CommDeviceInfo info = cm->deviceInfo(name);

    // 判断是否为 Modbus 服务器角色
    bool isModbusServer = false;
    if (info.type == QStringLiteral("Modbus")) {
        auto *mn = qobject_cast<ModbusNode *>(cm->deviceNode(name));
        if (mn) isModbusServer = (mn->role() == ModbusNode::MODBUS_SERVER);
    }
    QString onText = isModbusServer ? QStringLiteral("\u2714 \u670D\u52A1\u5668\u5DF2\u542F\u52A8")
                                    : QStringLiteral("\u2714 \u5DF2\u8FDE\u63A5");
    QString offText = isModbusServer ? QStringLiteral("\u25B6 \u542F\u52A8\u670D\u52A1\u5668")
                                     : QStringLiteral("\u25B6 \u672A\u8FDE\u63A5");

    // 获取该行的开关按钮
    auto *toggleBtn = qobject_cast<QPushButton *>(m_deviceTable->cellWidget(row, 2));
    if (!toggleBtn) return;

    if (info.isConnected) {
        // 当前已连接，点击切换为断开
        cm->closeDevice(name);
        toggleBtn->setChecked(false);
        toggleBtn->setText(offText);
        toggleBtn->setStyleSheet(
            "QPushButton { background-color: #f44336; color: white; border: none; "
            "border-radius: 4px; padding: 4px 12px; font-weight: bold; }"
            "QPushButton:hover { background-color: #da190b; }"
            "QPushButton:checked { background-color: #4CAF50; }");
    } else {
        // 当前未连接，点击切换为连接
        toggleBtn->setText(QStringLiteral("\u8FDE\u63A5\u4E2D..."));
        toggleBtn->setStyleSheet(
            "QPushButton { background-color: #FF9800; color: white; border: none; "
            "border-radius: 4px; padding: 4px 12px; font-weight: bold; }");
        toggleBtn->setEnabled(false);

        bool ok = cm->openDevice(name);
        toggleBtn->setEnabled(true);

        if (ok) {
            toggleBtn->setChecked(true);
            toggleBtn->setText(onText);
            toggleBtn->setStyleSheet(
                "QPushButton { background-color: #4CAF50; color: white; border: none; "
                "border-radius: 4px; padding: 4px 12px; font-weight: bold; }"
                "QPushButton:hover { background-color: #45a049; }");
        } else {
            toggleBtn->setChecked(false);
            toggleBtn->setText(QStringLiteral("\u2716 \u8FDE\u63A5\u5931\u8D25"));
            toggleBtn->setStyleSheet(
                "QPushButton { background-color: #f44336; color: white; border: none; "
                "border-radius: 4px; padding: 4px 12px; font-weight: bold; }"
                "QPushButton:hover { background-color: #da190b; }"
                "QPushButton:checked { background-color: #4CAF50; }");
        }
    }
    refreshDeviceTable();
}

void CommunicationManagerDialog::onAddDevice()
{
    // Device type selection
    QStringList types = {
        QStringLiteral("TCP"),
        QStringLiteral("\u4E32\u53E3"),
        QStringLiteral("Modbus"),
        QStringLiteral("PLC"),
        QStringLiteral("UDP")
    };

    bool ok = false;
    QString type = QInputDialog::getItem(this,
        QStringLiteral("\u6DFB\u52A0\u8BBE\u5907"),
        QStringLiteral("\u8BBE\u5907\u7C7B\u578B:"),
        types, 0, false, &ok);
    if (!ok) return;

    QString name = QInputDialog::getText(this,
        QStringLiteral("\u8BBE\u5907\u540D\u79F0"),
        QStringLiteral("\u8BF7\u8F93\u5165\u8BBE\u5907\u540D\u79F0:"),
        QLineEdit::Normal,
        type + QStringLiteral("_") + QString::number(QDateTime::currentMSecsSinceEpoch() % 10000),
        &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    QJsonObject config;

    if (type == QStringLiteral("TCP")) {
        CommDeviceConfigDialog dlg(type, config, this);
        if (dlg.exec() != QDialog::Accepted) return;
        config = dlg.config();

    } else if (type == QStringLiteral("\u4E32\u53E3")) {
        CommDeviceConfigDialog dlg(QStringLiteral("Serial"), config, this);
        if (dlg.exec() != QDialog::Accepted) return;
        config = dlg.config();

    } else if (type == QStringLiteral("Modbus")) {
        // 新增 Modbus 设备时，先创建设备再打开配置
        ModbusConfigDialog dlg(QStringLiteral("Modbus\u914D\u7F6E - %1").arg(name), nullptr, this);
        dlg.setConfig(config);
        if (dlg.exec() != QDialog::Accepted) return;
        config = dlg.config();
    } else if (type == QStringLiteral("UDP")) {
        CommDeviceConfigDialog dlg(type, config, this);
        if (dlg.exec() != QDialog::Accepted) return;
        config = dlg.config();
    } else if (type == QStringLiteral("PLC")) {
        PlcConfigDialog dlg(QStringLiteral("PLC\u914D\u7F6E - %1").arg(name), nullptr, this);
        dlg.setConfig(config);
        if (dlg.exec() != QDialog::Accepted) return;
        config = dlg.config();
    }

    if (CommunicationManager::instance()->addDevice(name.trimmed(), type, config)) {
        refreshDeviceTable();
    } else {
        QMessageBox::warning(this, QStringLiteral("\u5931\u8D25"),
            QStringLiteral("\u8BBE\u5907\u540D\u79F0\u5DF2\u5B58\u5728"));
    }
}

void CommunicationManagerDialog::onRemoveDevice()
{
    int row = m_deviceTable->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("\u8BF7\u9009\u62E9\u8981\u5220\u9664\u7684\u8BBE\u5907"));
        return;
    }
    QString name = m_deviceTable->item(row, 0)->text();
    if (QMessageBox::question(this, QStringLiteral("\u786E\u8BA4"),
        QStringLiteral("\u786E\u5B9A\u8981\u5220\u9664\u8BBE\u5907 %1 \u5417\uFF1F").arg(name))
        == QMessageBox::Yes) {
        CommunicationManager::instance()->removeDevice(name);
        refreshDeviceTable();
    }
}

void CommunicationManagerDialog::onConfigDevice()
{
    int row = m_deviceTable->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("\u8BF7\u9009\u62E9\u8981\u914D\u7F6E\u7684\u8BBE\u5907"));
        return;
    }
    QString name = m_deviceTable->item(row, 0)->text();
    auto *cm = CommunicationManager::instance();
    CommDeviceInfo info = cm->deviceInfo(name);
    const bool wasConnected = info.isConnected;   // TCP/串口/UDP 热更新后恢复连接用

    if (info.type == QStringLiteral("Modbus")) {
        if (wasConnected) cm->closeDevice(name);   // 删重建前必须先断开
        auto *modbusNode = qobject_cast<ModbusNode *>(cm->deviceNode(name));
        ModbusConfigDialog dlg(QStringLiteral("Modbus\u914D\u7F6E - %1").arg(name),
                               modbusNode, this);
        dlg.setConfig(info.config);
        if (dlg.exec() == QDialog::Accepted) {
            QJsonObject newConfig = dlg.config();
            // 更新设备配置
            cm->removeDevice(name);
            cm->addDevice(name, info.type, newConfig);
            // 移除旧的 + 重新创建设备节点
        }
    } else if (info.type == QStringLiteral("TCP") || info.type == QStringLiteral("UDP")
               || info.type == QStringLiteral("\u4E32\u53E3") || info.type == QStringLiteral("Serial")) {
        // 单页表单 + 热更新：直接改现节点参数后重开连接，不再"删设备→重建"（避免连接被无谓打断）
        const QString formType = (info.type == QStringLiteral("\u4E32\u53E3")
                                  || info.type == QStringLiteral("Serial"))
                                     ? QStringLiteral("Serial") : info.type;
        CommDeviceConfigDialog dlg(formType, info.config, this);
        if (dlg.exec() != QDialog::Accepted) { refreshDeviceTable(); return; }
        const QJsonObject newConfig = dlg.config();
        auto *node = cm->deviceNode(name);
        if (!node) { refreshDeviceTable(); return; }
        if (wasConnected) node->closeConnection();
        for (auto it = newConfig.constBegin(); it != newConfig.constEnd(); ++it)
            node->setParam(it.key(), it.value().toVariant());
        if (wasConnected) node->openConnection();
        cm->updateDeviceConfig(name, newConfig);   // 方案保存时写的是新配置
    } else if (info.type == QStringLiteral("PLC")) {
        if (wasConnected) cm->closeDevice(name);
        auto *plcNode = qobject_cast<PlcCommNode *>(cm->deviceNode(name));
        PlcConfigDialog dlg(QStringLiteral("PLC\u914D\u7F6E - %1").arg(name),
                            plcNode, this);
        dlg.setConfig(info.config);
        if (dlg.exec() == QDialog::Accepted) {
            QJsonObject newConfig = dlg.config();
            cm->removeDevice(name);
            cm->addDevice(name, info.type, newConfig);
        }
    } else {
        QMessageBox::information(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("%1 \u8BBE\u5907\u914D\u7F6E\u4E0D\u652F\u6301\u6B64\u64CD\u4F5C\u3002").arg(info.type));
    }
    refreshDeviceTable();
}

void CommunicationManagerDialog::onReadRegisters()
{
    int row = m_deviceTable->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("\u8BF7\u9009\u62E9\u4E00\u4E2A Modbus \u8BBE\u5907\u3002"));
        return;
    }
    QString name = m_deviceTable->item(row, 0)->text();
    auto *cm = CommunicationManager::instance();
    auto *modbusNode = qobject_cast<ModbusNode *>(cm->deviceNode(name));
    if (!modbusNode) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("\u8BE5\u8BBE\u5907\u4E0D\u662F Modbus \u8BBE\u5907\u3002"));
        return;
    }

    if (!modbusNode->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("Modbus \u8BBE\u5907\u672A\u8FDE\u63A5\uFF0C\u8BF7\u5148\u6253\u5F00\u8FDE\u63A5\u3002"));
        return;
    }

    // 打开 ModbusConfigDialog 查询实时值（只读模式）
    CommDeviceInfo info = cm->deviceInfo(name);
    ModbusConfigDialog dlg(QStringLiteral("Modbus\u5BC4\u5B58\u5668\u76D1\u63A7 - %1").arg(name),
                           modbusNode, this);
    dlg.setConfig(info.config);
    dlg.exec();
    // 用户关闭对话框即可
}

// ==================== Receive Event Tab ====================

void CommunicationManagerDialog::setupReceiveEventTab(QTabWidget *tabs)
{
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);

    auto *label = new QLabel(QStringLiteral("<b>\u63A5\u6536\u4E8B\u4EF6</b>"));
    label->setStyleSheet("font-size: 13px;");
    layout->addWidget(label);

    auto *infoLabel = new QLabel(QStringLiteral(
        "\u63A5\u6536\u4E8B\u4EF6\u53EF\u5C06\u901A\u4FE1\u63A5\u6536\u5230\u7684\u6570\u636E\u8FDB\u884C\u89E3\u6790,\u4ECE\u800C\u5C06\u4E00\u6BB5\u6570\u636E\u89E3\u6790\u4E3A\u9700\u8981\u7684\u503C\u3002\n"
        "\u652F\u6301\u4E24\u79CD\u6A21\u5F0F: \u6587\u672C-\u534F\u8BAE\u89E3\u6790(\u6309\u5206\u9694\u7B26\u62C6\u5206) / \u5B57\u8282\u5339\u914D-\u534F\u8BAE\u7EC4\u88C5(\u5BC4\u5B58\u5668\u503C\u5339\u914D)"
    ));
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(infoLabel);

    m_receiveEventTable = new QTableWidget();
    m_receiveEventTable->setColumnCount(5);
    m_receiveEventTable->setHorizontalHeaderLabels({
        QStringLiteral("\u4E8B\u4EF6ID"),
        QStringLiteral("\u7C7B\u578B"),
        QStringLiteral("\u8BBE\u5907"),
        QStringLiteral("\u914D\u7F6E"),
        QStringLiteral("\u72B6\u6001")
    });
    m_receiveEventTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_receiveEventTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_receiveEventTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_receiveEventTable->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_receiveEventTable);

    auto *btnLayout = new QHBoxLayout();
    m_addReceiveEventBtn = new QPushButton(QStringLiteral("\u6DFB\u52A0\u63A5\u6536\u4E8B\u4EF6"));
    m_removeReceiveEventBtn = new QPushButton(QStringLiteral("\u5220\u9664"));
    m_editReceiveEventBtn = new QPushButton(QStringLiteral("\u7F16\u8F91"));
    btnLayout->addWidget(m_addReceiveEventBtn);
    btnLayout->addWidget(m_removeReceiveEventBtn);
    btnLayout->addWidget(m_editReceiveEventBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    connect(m_addReceiveEventBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onAddReceiveEvent);
    connect(m_removeReceiveEventBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onRemoveReceiveEvent);
    connect(m_editReceiveEventBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onEditReceiveEvent);

    tabs->addTab(tab, QStringLiteral("\u63A5\u6536\u4E8B\u4EF6"));
}

void CommunicationManagerDialog::refreshReceiveEventTable()
{
    m_receiveEventTable->setRowCount(0);
    auto *cm = CommunicationManager::instance();

    for (const QString &id : cm->receiveEventIds()) {
        auto *ev = cm->receiveEvent(id);
        if (!ev) continue;

        int row = m_receiveEventTable->rowCount();
        m_receiveEventTable->insertRow(row);
        m_receiveEventTable->setItem(row, 0, new QTableWidgetItem(id));
        m_receiveEventTable->setItem(row, 1, new QTableWidgetItem(
            ev->eventType() == ReceiveEvent::TEXT_PROTOCOL
                ? QStringLiteral("\u6587\u672C-\u534F\u8BAE\u89E3\u6790")
                : QStringLiteral("\u5B57\u8282\u5339\u914D-\u534F\u8BAE\u7EC4\u88C5")));
        m_receiveEventTable->setItem(row, 2, new QTableWidgetItem(ev->deviceName()));

        QTableWidgetItem *statusItem = new QTableWidgetItem(
            ev->enabled() ? QStringLiteral("\u542F\u7528") : QStringLiteral("\u7981\u7528"));
        statusItem->setForeground(ev->enabled() ? QColor(0, 128, 0) : Qt::gray);
        m_receiveEventTable->setItem(row, 4, statusItem);
    }
    m_receiveEventTable->resizeColumnsToContents();
}

void CommunicationManagerDialog::onAddReceiveEvent()
{
    QStringList types = {
        QStringLiteral("\u6587\u672C-\u534F\u8BAE\u89E3\u6790"),
        QStringLiteral("\u5B57\u8282\u5339\u914D-\u534F\u8BAE\u7EC4\u88C5")
    };
    bool ok = false;
    QString type = QInputDialog::getItem(this,
        QStringLiteral("\u6DFB\u52A0\u63A5\u6536\u4E8B\u4EF6"),
        QStringLiteral("\u4E8B\u4EF6\u7C7B\u578B:"),
        types, 0, false, &ok);
    if (!ok) return;

    QStringList devices = CommunicationManager::instance()->deviceNames();
    if (devices.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("\u8BF7\u5148\u6DFB\u52A0\u901A\u4FE1\u8BBE\u5907"));
        return;
    }

    QString device = QInputDialog::getItem(this,
        QStringLiteral("\u7ED1\u5B9A\u8BBE\u5907"),
        QStringLiteral("\u7ED1\u5B9A\u901A\u4FE1\u8BBE\u5907:"),
        devices, 0, false, &ok);
    if (!ok) return;

    QString id = QInputDialog::getText(this,
        QStringLiteral("\u4E8B\u4EF6ID"),
        QStringLiteral("\u4E8B\u4EF6\u540D\u79F0:"),
        QLineEdit::Normal,
        QStringLiteral("Event_") + QString::number(QDateTime::currentMSecsSinceEpoch() % 10000),
        &ok);
    if (!ok || id.trimmed().isEmpty()) return;

    ReceiveEvent *ev = nullptr;
    if (type == types[0]) {
        auto *tev = new TextProtocolReceiveEvent(id.trimmed(), device, CommunicationManager::instance());
        QStringList modes = { QStringLiteral("分隔符拆分"), QStringLiteral("正则表达式") };
        QString mode = QInputDialog::getItem(this, QStringLiteral("解析模式"),
            QStringLiteral("文本解析模式:"), modes, 0, false, &ok);
        if (!ok) return;
        if (mode == modes[1]) {
            QString re = QInputDialog::getText(this, QStringLiteral("正则表达式"),
                QStringLiteral("正则(捕获组作为字段，如 (-?\\d+\\.?\\d*)):"), QLineEdit::Normal,
                QStringLiteral("(-?\\d+\\.?\\d*)"), &ok);
            if (!ok) return;
            tev->setParseMode(TextProtocolReceiveEvent::Regex);
            tev->setRegex(re.trimmed());
        } else {
            QString delim = QInputDialog::getText(this, QStringLiteral("\u5206\u9694\u7B26"),
                QStringLiteral("\u534F\u8BAE\u5206\u9694\u7B26:"), QLineEdit::Normal,
                QStringLiteral(","), &ok);
            if (ok) tev->setDelimiter(delim);
        }
        ev = tev;
    } else {
        auto *bev = new ByteMatchReceiveEvent(id.trimmed(), device, CommunicationManager::instance());
        int addr = QInputDialog::getInt(this, QStringLiteral("\u5BC4\u5B58\u5668\u5730\u5740"),
            QStringLiteral("\u5BC4\u5B58\u5668\u5730\u5740:"), 0, 0, 65535, 1, &ok);
        if (ok) bev->setRegisterAddress(addr);
        ByteMatchRule rule;
        rule.byteOffset = 0;
        rule.byteLength = 2;
        ev = bev;
        bev->addRule(rule);
    }

    if (ev) {
        CommunicationManager::instance()->addReceiveEvent(ev);
        refreshReceiveEventTable();
    }
}

void CommunicationManagerDialog::onRemoveReceiveEvent()
{
    int row = m_receiveEventTable->currentRow();
    if (row < 0) return;
    QString id = m_receiveEventTable->item(row, 0)->text();
    CommunicationManager::instance()->removeReceiveEvent(id);
    refreshReceiveEventTable();
}

void CommunicationManagerDialog::onEditReceiveEvent()
{
    int row = m_receiveEventTable->currentRow();
    if (row < 0) return;
    QString id = m_receiveEventTable->item(row, 0)->text();
    auto *ev = CommunicationManager::instance()->receiveEvent(id);
    if (!ev) return;

    // 全参数可编辑（分隔符/正则/字节规则），不再"只能启停、改参数必须删重建"
    ReceiveEventEditDialog dlg(ev, this);
    if (dlg.exec() == QDialog::Accepted)
        refreshReceiveEventTable();
}

// ==================== Send Event Tab ====================

void CommunicationManagerDialog::setupSendEventTab(QTabWidget *tabs)
{
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);

    auto *label = new QLabel(QStringLiteral("<b>\u53D1\u9001\u4E8B\u4EF6</b>"));
    label->setStyleSheet("font-size: 13px;");
    layout->addWidget(label);

    auto *tmplInfo = new QLabel(QStringLiteral(
        "发送事件把数据格式化为报文发给外部设备。\n"
        "文本模板支持命名占位符：{模块号.参数名} / {global.变量名}，"
        "例如 \"{1.结果},{global.计数}\"。\n"
        "流程每轮结束会把本轮结果自动填入占位符后上报（对标 VM 的每轮上报）。"));
    tmplInfo->setWordWrap(true);
    tmplInfo->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(tmplInfo);

    m_sendEventTable = new QTableWidget();
    m_sendEventTable->setColumnCount(4);
    m_sendEventTable->setHorizontalHeaderLabels({
        QStringLiteral("\u4E8B\u4EF6ID"),
        QStringLiteral("\u7C7B\u578B"),
        QStringLiteral("\u8BBE\u5907"),
        QStringLiteral("\u72B6\u6001")
    });
    m_sendEventTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_sendEventTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_sendEventTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_sendEventTable->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_sendEventTable);

    auto *btnLayout = new QHBoxLayout();
    m_addSendEventBtn = new QPushButton(QStringLiteral("\u6DFB\u52A0\u53D1\u9001\u4E8B\u4EF6"));
    m_removeSendEventBtn = new QPushButton(QStringLiteral("\u5220\u9664"));
    m_editSendEventBtn = new QPushButton(QStringLiteral("\u7F16\u8F91"));
    btnLayout->addWidget(m_addSendEventBtn);
    btnLayout->addWidget(m_removeSendEventBtn);
    btnLayout->addWidget(m_editSendEventBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    connect(m_addSendEventBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onAddSendEvent);
    connect(m_removeSendEventBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onRemoveSendEvent);
    connect(m_editSendEventBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onEditSendEvent);

    tabs->addTab(tab, QStringLiteral("\u53D1\u9001\u4E8B\u4EF6"));
}

void CommunicationManagerDialog::refreshSendEventTable()
{
    m_sendEventTable->setRowCount(0);
    auto *cm = CommunicationManager::instance();

    for (const QString &id : cm->sendEventIds()) {
        auto *ev = cm->sendEvent(id);
        if (!ev) continue;

        int row = m_sendEventTable->rowCount();
        m_sendEventTable->insertRow(row);
        m_sendEventTable->setItem(row, 0, new QTableWidgetItem(id));
        m_sendEventTable->setItem(row, 1, new QTableWidgetItem(
            ev->sendType() == SendEvent::TEXT_DIRECT
                ? QStringLiteral("\u6587\u672C-\u76F4\u63A5\u8F93\u51FA")
                : QStringLiteral("\u5B57\u8282\u7EC4\u5305")));
        m_sendEventTable->setItem(row, 2, new QTableWidgetItem(ev->deviceName()));

        auto *statusItem = new QTableWidgetItem(
            ev->enabled() ? QStringLiteral("\u542F\u7528") : QStringLiteral("\u7981\u7528"));
        statusItem->setForeground(ev->enabled() ? QColor(0, 128, 0) : Qt::gray);
        m_sendEventTable->setItem(row, 3, statusItem);
    }
    m_sendEventTable->resizeColumnsToContents();
}

void CommunicationManagerDialog::onAddSendEvent()
{
    QStringList types = {
        QStringLiteral("\u6587\u672C-\u76F4\u63A5\u8F93\u51FA"),
        QStringLiteral("\u5B57\u8282\u7EC4\u5305")
    };
    bool ok = false;
    QString type = QInputDialog::getItem(this,
        QStringLiteral("\u6DFB\u52A0\u53D1\u9001\u4E8B\u4EF6"),
        QStringLiteral("\u4E8B\u4EF6\u7C7B\u578B:"),
        types, 0, false, &ok);
    if (!ok) return;

    QStringList devices = CommunicationManager::instance()->deviceNames();
    if (devices.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("\u8BF7\u5148\u6DFB\u52A0\u901A\u4FE1\u8BBE\u5907"));
        return;
    }

    QString device = QInputDialog::getItem(this,
        QStringLiteral("\u7ED1\u5B9A\u8BBE\u5907"),
        QStringLiteral("\u7ED1\u5B9A\u901A\u4FE1\u8BBE\u5907:"),
        devices, 0, false, &ok);
    if (!ok) return;

    QString id = QInputDialog::getText(this,
        QStringLiteral("\u4E8B\u4EF6ID"),
        QStringLiteral("\u4E8B\u4EF6\u540D\u79F0:"),
        QLineEdit::Normal,
        QStringLiteral("Send_") + QString::number(QDateTime::currentMSecsSinceEpoch() % 10000),
        &ok);
    if (!ok || id.trimmed().isEmpty()) return;

    SendEvent *ev = nullptr;
    if (type == types[0]) {
        ev = new TextDirectSendEvent(id.trimmed(), device, CommunicationManager::instance());
    } else {
        ev = new BytePackSendEvent(id.trimmed(), device, CommunicationManager::instance());
    }

    if (ev) {
        CommunicationManager::instance()->addSendEvent(ev);
        refreshSendEventTable();
    }
}

void CommunicationManagerDialog::onRemoveSendEvent()
{
    int row = m_sendEventTable->currentRow();
    if (row < 0) return;
    QString id = m_sendEventTable->item(row, 0)->text();
    CommunicationManager::instance()->removeSendEvent(id);
    refreshSendEventTable();
}

void CommunicationManagerDialog::onEditSendEvent()
{
    int row = m_sendEventTable->currentRow();
    if (row < 0) return;
    QString id = m_sendEventTable->item(row, 0)->text();
    auto *ev = CommunicationManager::instance()->sendEvent(id);
    if (!ev) return;

    // 全参数可编辑（模板/后缀/字段表），不再"只能启停、改参数必须删重建"
    SendEventEditDialog dlg(ev, this);
    if (dlg.exec() == QDialog::Accepted)
        refreshSendEventTable();
}

// ==================== Heartbeat Tab ====================

void CommunicationManagerDialog::setupHeartbeatTab(QTabWidget *tabs)
{
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);

    auto *label = new QLabel(QStringLiteral("<b>\u5FC3\u8DF3\u7BA1\u7406</b>"));
    label->setStyleSheet("font-size: 13px;");
    layout->addWidget(label);

    auto *infoLabel = new QLabel(QStringLiteral(
        "\u5FC3\u8DF3\u7BA1\u7406\u7528\u4E8E\u76D1\u6D4B\u4E0E\u5916\u90E8\u8BBE\u5907\u7684\u8FDE\u63A5\u72B6\u6001\u3002\n"
        "\u7CFB\u7EDF\u5B9A\u65F6\u5411\u5916\u90E8\u8BBE\u5907\u53D1\u9001\u5FC3\u8DF3\u6570\u636E\uFF0C\u91C7\u7528\u4EA4\u66FF\u53D8\u5316\u7684\u6570\u636E\u6A21\u5F0F\u3002"
    ));
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(infoLabel);

    m_heartbeatTable = new QTableWidget();
    m_heartbeatTable->setColumnCount(5);
    m_heartbeatTable->setHorizontalHeaderLabels({
        QStringLiteral("\u8BBE\u5907"),
        QStringLiteral("\u95F4\u9694(ms)"),
        QStringLiteral("\u6A21\u5F0F0"),
        QStringLiteral("\u6A21\u5F0F1"),
        QStringLiteral("\u72B6\u6001")
    });
    m_heartbeatTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_heartbeatTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_heartbeatTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_heartbeatTable->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_heartbeatTable);

    auto *btnLayout = new QHBoxLayout();
    m_addHeartbeatBtn = new QPushButton(QStringLiteral("\u6DFB\u52A0\u5FC3\u8DF3"));
    m_removeHeartbeatBtn = new QPushButton(QStringLiteral("\u5220\u9664"));
    btnLayout->addWidget(m_addHeartbeatBtn);
    btnLayout->addWidget(m_removeHeartbeatBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    connect(m_addHeartbeatBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onAddHeartbeat);
    connect(m_removeHeartbeatBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onRemoveHeartbeat);

    tabs->addTab(tab, QStringLiteral("\u5FC3\u8DF3\u7BA1\u7406"));
}

void CommunicationManagerDialog::refreshHeartbeatTable()
{
    m_heartbeatTable->setRowCount(0);
    auto entries = HeartbeatManager::instance()->entries();
    for (const auto &e : entries) {
        int row = m_heartbeatTable->rowCount();
        m_heartbeatTable->insertRow(row);
        m_heartbeatTable->setItem(row, 0, new QTableWidgetItem(e.deviceName));
        m_heartbeatTable->setItem(row, 1, new QTableWidgetItem(QString::number(e.intervalMs)));
        m_heartbeatTable->setItem(row, 2, new QTableWidgetItem(e.pattern0));
        m_heartbeatTable->setItem(row, 3, new QTableWidgetItem(e.pattern1));

        auto *statusItem = new QTableWidgetItem(
            e.active ? QStringLiteral("\u542F\u52A8") : QStringLiteral("\u505C\u6B62"));
        statusItem->setForeground(e.active ? QColor(0, 128, 0) : Qt::gray);
        m_heartbeatTable->setItem(row, 4, statusItem);
    }
    m_heartbeatTable->resizeColumnsToContents();
}

void CommunicationManagerDialog::onAddHeartbeat()
{
    QStringList devices = CommunicationManager::instance()->deviceNames();
    if (devices.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("\u8BF7\u5148\u6DFB\u52A0\u901A\u4FE1\u8BBE\u5907"));
        return;
    }

    bool ok = false;
    QString device = QInputDialog::getItem(this,
        QStringLiteral("\u6DFB\u52A0\u5FC3\u8DF3"),
        QStringLiteral("\u9009\u62E9\u8BBE\u5907:"),
        devices, 0, false, &ok);
    if (!ok) return;

    int interval = QInputDialog::getInt(this,
        QStringLiteral("\u5FC3\u8DF3\u95F4\u9694"),
        QStringLiteral("\u95F4\u9694(ms):"), 1000, 500, 60000, 100, &ok);
    if (!ok) return;

    HeartbeatManager::instance()->registerHeartbeat(device, interval);
    HeartbeatManager::instance()->startHeartbeat(device);
    refreshHeartbeatTable();
}

void CommunicationManagerDialog::onRemoveHeartbeat()
{
    int row = m_heartbeatTable->currentRow();
    if (row < 0) return;
    QString device = m_heartbeatTable->item(row, 0)->text();
    HeartbeatManager::instance()->unregisterHeartbeat(device);
    refreshHeartbeatTable();
}
