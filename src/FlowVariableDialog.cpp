#include "FlowVariableDialog.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QGroupBox>

FlowVariableDialog::FlowVariableDialog(FlowScene *scene, QWidget *parent)
    : QDialog(parent)
    , m_scene(scene)
{
    setWindowTitle(QStringLiteral("流程变量 / Fixture"));
    resize(720, 480);

    auto *root = new QVBoxLayout(this);

    auto *varBox = new QGroupBox(QStringLiteral("流程变量（随本流程 .vfp 保存）"));
    auto *varLay = new QVBoxLayout(varBox);
    m_varTable = new QTableWidget();
    m_varTable->setColumnCount(4);
    m_varTable->setHorizontalHeaderLabels({QStringLiteral("名称"), QStringLiteral("类型"),
                                           QStringLiteral("值"), QStringLiteral("描述")});
    m_varTable->horizontalHeader()->setStretchLastSection(true);
    m_varTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    varLay->addWidget(m_varTable);

    auto *addRow = new QHBoxLayout();
    m_nameEdit = new QLineEdit();
    m_nameEdit->setPlaceholderText(QStringLiteral("变量名"));
    m_typeCombo = new QComboBox();
    m_typeCombo->addItems({QStringLiteral("Int"), QStringLiteral("Bool"),
                           QStringLiteral("Float"), QStringLiteral("String")});
    m_typeCombo->setCurrentIndex(2);
    m_valueEdit = new QLineEdit();
    m_valueEdit->setPlaceholderText(QStringLiteral("值"));
    auto *addBtn = new QPushButton(QStringLiteral("添加/更新"));
    auto *delBtn = new QPushButton(QStringLiteral("删除所选"));
    addRow->addWidget(m_nameEdit);
    addRow->addWidget(m_typeCombo);
    addRow->addWidget(m_valueEdit);
    addRow->addWidget(addBtn);
    addRow->addWidget(delBtn);
    varLay->addLayout(addRow);
    root->addWidget(varBox);

    auto *fixBox = new QGroupBox(QStringLiteral("Fixture（N 点标定矩阵 + 匹配位姿）"));
    auto *fixLay = new QVBoxLayout(fixBox);
    m_fixTable = new QTableWidget();
    m_fixTable->setColumnCount(5);
    m_fixTable->setHorizontalHeaderLabels({
        QStringLiteral("名称"), QStringLiteral("有矩阵"), QStringLiteral("有位姿"),
        QStringLiteral("列,行"), QStringLiteral("角/尺度")});
    m_fixTable->horizontalHeader()->setStretchLastSection(true);
    m_fixTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    fixLay->addWidget(m_fixTable);
    fixLay->addWidget(new QLabel(QStringLiteral(
        "由「N点标定」写入矩阵、「模板匹配」写入位姿；坐标系变换填写同名即可，不必手填矩阵。")));
    root->addWidget(fixBox);

    auto *ok = new QPushButton(QStringLiteral("完成"));
    auto *bottom = new QHBoxLayout();
    bottom->addStretch();
    bottom->addWidget(ok);
    root->addLayout(bottom);

    connect(addBtn, &QPushButton::clicked, this, &FlowVariableDialog::addVariable);
    connect(delBtn, &QPushButton::clicked, this, &FlowVariableDialog::removeVariable);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    refreshTables();
}

void FlowVariableDialog::refreshTables()
{
    m_varTable->setRowCount(0);
    m_fixTable->setRowCount(0);
    if (!m_scene)
        return;
    const auto vars = m_scene->flowVariables();
    int row = 0;
    m_varTable->setRowCount(vars.size());
    const QStringList typeNames{QStringLiteral("Int"), QStringLiteral("Bool"),
                                QStringLiteral("Float"), QStringLiteral("String")};
    for (auto it = vars.constBegin(); it != vars.constEnd(); ++it, ++row) {
        m_varTable->setItem(row, 0, new QTableWidgetItem(it->name));
        m_varTable->setItem(row, 1, new QTableWidgetItem(
            (it->type >= 0 && it->type < typeNames.size()) ? typeNames[it->type] : QStringLiteral("?")));
        m_varTable->setItem(row, 2, new QTableWidgetItem(it->value.toString()));
        m_varTable->setItem(row, 3, new QTableWidgetItem(it->description));
    }
    const auto fixtures = m_scene->fixtures();
    row = 0;
    m_fixTable->setRowCount(fixtures.size());
    for (auto it = fixtures.constBegin(); it != fixtures.constEnd(); ++it, ++row) {
        m_fixTable->setItem(row, 0, new QTableWidgetItem(it->name));
        m_fixTable->setItem(row, 1, new QTableWidgetItem(it->hasHom ? QStringLiteral("是") : QStringLiteral("否")));
        m_fixTable->setItem(row, 2, new QTableWidgetItem(it->hasPose ? QStringLiteral("是") : QStringLiteral("否")));
        m_fixTable->setItem(row, 3, new QTableWidgetItem(
            QStringLiteral("%1, %2").arg(it->poseCol, 0, 'f', 1).arg(it->poseRow, 0, 'f', 1)));
        m_fixTable->setItem(row, 4, new QTableWidgetItem(
            QStringLiteral("%1° / %2").arg(it->poseAngle, 0, 'f', 1).arg(it->poseScale, 0, 'f', 3)));
    }
}

void FlowVariableDialog::addVariable()
{
    if (!m_scene)
        return;
    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty())
        return;
    const int type = m_typeCombo->currentIndex();
    QVariant value = m_valueEdit->text();
    if (type == 0) value = value.toInt();
    else if (type == 1) value = (value.toString() == QStringLiteral("1")
                                 || value.toString().compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
    else if (type == 2) value = value.toDouble();
    m_scene->setFlowVariable(name, type, value);
    refreshTables();
}

void FlowVariableDialog::removeVariable()
{
    if (!m_scene)
        return;
    const int row = m_varTable->currentRow();
    if (row < 0)
        return;
    const QTableWidgetItem *nameItem = m_varTable->item(row, 0);
    if (!nameItem)
        return;
    m_scene->removeFlowVariable(nameItem->text());
    refreshTables();
}
