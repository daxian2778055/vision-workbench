#include "FinsConfigDialog.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {

// 寄存器表列序（config()/appendRegisterRow 共用一处定义）
enum RegisterColumn {
    ColEnabled = 0,
    ColArea,
    ColAddress,
    ColDataType,
    ColAccess,
    ColDescription,
    RegisterColumnCount
};

const QStringList &areaNames()
{
    static const QStringList names = {QStringLiteral("DM"), QStringLiteral("CIO"),
                                      QStringLiteral("WR"), QStringLiteral("HR"),
                                      QStringLiteral("AR")};
    return names;
}

const QStringList &dataTypeNames()
{
    static const QStringList names = {QStringLiteral("int16"), QStringLiteral("uint16"),
                                      QStringLiteral("int32"), QStringLiteral("uint32"),
                                      QStringLiteral("float")};
    return names;
}

const QStringList &accessModeNames()
{
    static const QStringList names = {QStringLiteral("Read"), QStringLiteral("ReadWrite"),
                                      QStringLiteral("Write")};
    return names;
}

/// 非可编辑下拉框按文本选中；配置里出现未知取值时保持首项（= 默认值），不产出非法项。
void selectOrFirst(QComboBox *combo, const QString &text)
{
    const int idx = combo->findText(text);
    combo->setCurrentIndex(idx >= 0 ? idx : 0);
}

}   // namespace

FinsConfigDialog::FinsConfigDialog(const QString &deviceType, const QJsonObject &initial,
                                   QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(deviceType + QStringLiteral(" 配置"));
    setMinimumSize(720, 560);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowModality(Qt::WindowModal);
    setupUI();
    setConfig(initial);
}

void FinsConfigDialog::setupUI()
{
    auto *mainLayout = new QVBoxLayout(this);

    // ===== 连接参数 =====
    auto *connGroup = new QGroupBox(QStringLiteral("连接参数"), this);
    auto *form = new QFormLayout(connGroup);

    m_targetIp = new QLineEdit(connGroup);
    m_targetIp->setObjectName(QStringLiteral("finsTargetIpEdit"));
    m_targetIp->setPlaceholderText(QStringLiteral("例: 192.168.0.10"));
    form->addRow(QStringLiteral("目标IP:"), m_targetIp);

    m_targetPort = new QSpinBox(connGroup);
    m_targetPort->setObjectName(QStringLiteral("finsTargetPortSpin"));
    m_targetPort->setRange(1, 65535);
    m_targetPort->setValue(7920);
    // Omron 出厂常见 FINS/TCP 端口是 9600；VM 默认值 7920 只作初值，现场按 PLC 设置改。
    m_targetPort->setToolTip(QStringLiteral("FINS/TCP 默认 7920（Omron 出厂常见 9600，按 PLC 实际设置）"));
    form->addRow(QStringLiteral("目标端口:"), m_targetPort);

    m_pollInterval = new QSpinBox(connGroup);
    m_pollInterval->setObjectName(QStringLiteral("finsPollSpin"));
    m_pollInterval->setRange(50, 60000);
    m_pollInterval->setValue(500);
    m_pollInterval->setSuffix(QStringLiteral(" ms"));
    form->addRow(QStringLiteral("轮询周期(ms):"), m_pollInterval);

    m_timeoutMs = new QSpinBox(connGroup);
    m_timeoutMs->setObjectName(QStringLiteral("finsTimeoutSpin"));
    m_timeoutMs->setRange(100, 60000);
    m_timeoutMs->setValue(1000);
    m_timeoutMs->setSuffix(QStringLiteral(" ms"));
    form->addRow(QStringLiteral("超时(ms):"), m_timeoutMs);

    m_autoReconnect = new QCheckBox(QStringLiteral("断线后自动重连"), connGroup);
    m_autoReconnect->setObjectName(QStringLiteral("finsAutoReconnectCheck"));
    m_autoReconnect->setChecked(true);
    form->addRow(QStringLiteral("自动重连:"), m_autoReconnect);

    m_reconnectInterval = new QSpinBox(connGroup);
    m_reconnectInterval->setObjectName(QStringLiteral("finsReconnectSpin"));
    m_reconnectInterval->setRange(500, 60000);
    m_reconnectInterval->setValue(3000);
    m_reconnectInterval->setSuffix(QStringLiteral(" ms"));
    form->addRow(QStringLiteral("重连间隔(毫秒):"), m_reconnectInterval);

    m_localNode = new QSpinBox(connGroup);
    m_localNode->setObjectName(QStringLiteral("finsLocalNodeSpin"));
    m_localNode->setRange(0, 255);
    m_localNode->setValue(0);
    m_localNode->setToolTip(QStringLiteral("0 = 由握手分配"));
    form->addRow(QStringLiteral("本地节点号:"), m_localNode);

    m_remoteNode = new QSpinBox(connGroup);
    m_remoteNode->setObjectName(QStringLiteral("finsRemoteNodeSpin"));
    m_remoteNode->setRange(0, 255);
    m_remoteNode->setValue(0);
    m_remoteNode->setToolTip(QStringLiteral("0 = 由握手分配"));
    form->addRow(QStringLiteral("远端节点号:"), m_remoteNode);

    mainLayout->addWidget(connGroup);

    // ===== 寄存器表 =====
    auto *regGroup = new QGroupBox(QStringLiteral("寄存器配置"), this);
    auto *regLayout = new QVBoxLayout(regGroup);

    auto *hint = new QLabel(
        QStringLiteral("读轮询只取「启用且访问为 Read/ReadWrite」的行；Write 仅用于写回。"),
        regGroup);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: gray; font-size: 11px; padding-bottom: 4px;"));
    regLayout->addWidget(hint);

    m_registerTable = new QTableWidget(regGroup);
    m_registerTable->setObjectName(QStringLiteral("finsRegisterTable"));
    m_registerTable->setColumnCount(RegisterColumnCount);
    m_registerTable->setHorizontalHeaderLabels({
        QStringLiteral("启用"),
        QStringLiteral("区域"),
        QStringLiteral("地址"),
        QStringLiteral("数据类型"),
        QStringLiteral("访问"),
        QStringLiteral("描述")
    });
    m_registerTable->horizontalHeader()->setStretchLastSection(false);
    m_registerTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_registerTable->horizontalHeader()->setSectionResizeMode(ColDescription, QHeaderView::Stretch);
    m_registerTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_registerTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_registerTable->setMinimumHeight(200);
    regLayout->addWidget(m_registerTable);

    auto *btnLayout = new QHBoxLayout();
    auto *addBtn = new QPushButton(QStringLiteral("＋添加寄存器"), regGroup);
    addBtn->setObjectName(QStringLiteral("finsAddRegisterBtn"));
    auto *removeBtn = new QPushButton(QStringLiteral("－删除寄存器"), regGroup);
    removeBtn->setObjectName(QStringLiteral("finsRemoveRegisterBtn"));
    btnLayout->addWidget(addBtn);
    btnLayout->addWidget(removeBtn);
    btnLayout->addStretch();
    regLayout->addLayout(btnLayout);

    connect(addBtn, &QPushButton::clicked, this, &FinsConfigDialog::onAddRegister);
    connect(removeBtn, &QPushButton::clicked, this, &FinsConfigDialog::onRemoveRegister);

    mainLayout->addWidget(regGroup, 1);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &FinsConfigDialog::onAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

void FinsConfigDialog::setConfig(const QJsonObject &initial)
{
    // 先回默认值：重复 setConfig 时缺键的字段不残留上一台设备的取值。
    m_targetIp->setText(QStringLiteral("127.0.0.1"));
    m_targetPort->setValue(7920);
    m_pollInterval->setValue(500);
    m_timeoutMs->setValue(1000);
    m_autoReconnect->setChecked(true);
    m_reconnectInterval->setValue(3000);
    m_localNode->setValue(0);
    m_remoteNode->setValue(0);

    m_targetIp->setText(initial.value(QStringLiteral("targetIp"))
                            .toString(QStringLiteral("127.0.0.1")));
    m_targetPort->setValue(initial.value(QStringLiteral("targetPort")).toInt(7920));
    m_pollInterval->setValue(initial.value(QStringLiteral("pollInterval")).toInt(500));
    m_timeoutMs->setValue(initial.value(QStringLiteral("timeoutMs")).toInt(1000));
    m_autoReconnect->setChecked(initial.value(QStringLiteral("autoReconnect")).toBool(true));
    m_reconnectInterval->setValue(initial.value(QStringLiteral("reconnectInterval")).toInt(3000));
    m_localNode->setValue(initial.value(QStringLiteral("localNode")).toInt(0));
    m_remoteNode->setValue(initial.value(QStringLiteral("remoteNode")).toInt(0));

    m_registerTable->setRowCount(0);
    const QJsonArray regsArr = initial.value(QStringLiteral("registers")).toArray();
    for (const QJsonValue &v : regsArr)
        appendRegisterRow(v.toObject());
}

QJsonObject FinsConfigDialog::config() const
{
    QJsonObject cfg;
    cfg[QStringLiteral("targetIp")] = m_targetIp->text().trimmed();
    cfg[QStringLiteral("targetPort")] = m_targetPort->value();
    cfg[QStringLiteral("pollInterval")] = m_pollInterval->value();
    cfg[QStringLiteral("timeoutMs")] = m_timeoutMs->value();
    cfg[QStringLiteral("autoReconnect")] = m_autoReconnect->isChecked();
    cfg[QStringLiteral("reconnectInterval")] = m_reconnectInterval->value();
    cfg[QStringLiteral("localNode")] = m_localNode->value();
    cfg[QStringLiteral("remoteNode")] = m_remoteNode->value();

    QJsonArray regsArr;
    for (int row = 0; row < m_registerTable->rowCount(); ++row) {
        // 地址控件是行的"骨架"：缺失说明该行没有完整建立，整行跳过（不产出默认地址的假项）。
        auto *addressSpin = qobject_cast<QSpinBox *>(m_registerTable->cellWidget(row, ColAddress));
        if (!addressSpin)
            continue;

        QJsonObject ro;
        QTableWidgetItem *enableItem = m_registerTable->item(row, ColEnabled);
        ro[QStringLiteral("enabled")] =
            enableItem ? (enableItem->checkState() == Qt::Checked) : true;

        auto *areaCombo = qobject_cast<QComboBox *>(m_registerTable->cellWidget(row, ColArea));
        ro[QStringLiteral("area")] =
            areaCombo ? areaCombo->currentText() : QStringLiteral("DM");

        ro[QStringLiteral("address")] = addressSpin->value();

        auto *typeCombo = qobject_cast<QComboBox *>(m_registerTable->cellWidget(row, ColDataType));
        ro[QStringLiteral("dataType")] =
            typeCombo ? typeCombo->currentText() : QStringLiteral("int16");

        auto *accessCombo = qobject_cast<QComboBox *>(m_registerTable->cellWidget(row, ColAccess));
        ro[QStringLiteral("accessMode")] =
            accessCombo ? accessCombo->currentText() : QStringLiteral("Read");

        QTableWidgetItem *descItem = m_registerTable->item(row, ColDescription);
        ro[QStringLiteral("description")] = descItem ? descItem->text() : QString();

        regsArr.append(ro);
    }
    cfg[QStringLiteral("registers")] = regsArr;
    return cfg;
}

void FinsConfigDialog::appendRegisterRow(const QJsonObject &reg)
{
    const int row = m_registerTable->rowCount();
    m_registerTable->insertRow(row);

    auto *enableItem = new QTableWidgetItem();
    enableItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    enableItem->setCheckState(reg.value(QStringLiteral("enabled")).toBool(true) ? Qt::Checked
                                                                               : Qt::Unchecked);
    m_registerTable->setItem(row, ColEnabled, enableItem);

    auto *areaCombo = new QComboBox(m_registerTable);
    areaCombo->addItems(areaNames());
    selectOrFirst(areaCombo, reg.value(QStringLiteral("area")).toString(QStringLiteral("DM")));
    m_registerTable->setCellWidget(row, ColArea, areaCombo);

    auto *addressSpin = new QSpinBox(m_registerTable);
    addressSpin->setRange(0, 65535);
    addressSpin->setValue(reg.value(QStringLiteral("address")).toInt(0));
    m_registerTable->setCellWidget(row, ColAddress, addressSpin);

    auto *typeCombo = new QComboBox(m_registerTable);
    typeCombo->addItems(dataTypeNames());
    selectOrFirst(typeCombo,
                  reg.value(QStringLiteral("dataType")).toString(QStringLiteral("int16")));
    m_registerTable->setCellWidget(row, ColDataType, typeCombo);

    auto *accessCombo = new QComboBox(m_registerTable);
    accessCombo->addItems(accessModeNames());
    selectOrFirst(accessCombo,
                  reg.value(QStringLiteral("accessMode")).toString(QStringLiteral("Read")));
    m_registerTable->setCellWidget(row, ColAccess, accessCombo);

    m_registerTable->setItem(row, ColDescription,
                             new QTableWidgetItem(reg.value(QStringLiteral("description")).toString()));
}

void FinsConfigDialog::onAccept()
{
    if (m_targetIp->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("目标 IP 不能为空。"));
        m_targetIp->setFocus();
        return;
    }
    // 端口/节点号/周期/超时由各 QSpinBox 的量程保证；寄存器可以为空（只连不轮询）。
    accept();
}

void FinsConfigDialog::onAddRegister()
{
    appendRegisterRow(QJsonObject());
}

void FinsConfigDialog::onRemoveRegister()
{
    const int row = m_registerTable->currentRow();
    if (row >= 0)
        m_registerTable->removeRow(row);
}
