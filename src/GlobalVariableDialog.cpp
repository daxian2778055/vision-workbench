#include "GlobalVariableDialog.h"
#include "GlobalVariableManager.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDebug>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QTextEdit>
#include <QTableWidget>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>

GlobalVariableDialog::GlobalVariableDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    setupConnections();
    updateVariableTable();
}

GlobalVariableDialog::~GlobalVariableDialog()
{}

void GlobalVariableDialog::setupUI()
{
    setWindowTitle("全局变量配置");
    setMinimumSize(600, 400);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // 变量表格
    m_variableTable = new QTableWidget(this);
    m_variableTable->setColumnCount(4);
    m_variableTable->setHorizontalHeaderLabels({"变量名", "类型", "值", "描述"});
    m_variableTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_variableTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mainLayout->addWidget(m_variableTable);

    // 按钮布局
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    m_addButton = new QPushButton("添加", this);
    m_removeButton = new QPushButton("删除", this);
    m_editButton = new QPushButton("编辑", this);
    m_saveButton = new QPushButton("保存", this);
    m_loadButton = new QPushButton("加载", this);

    buttonLayout->addWidget(m_addButton);
    buttonLayout->addWidget(m_removeButton);
    buttonLayout->addWidget(m_editButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_saveButton);
    buttonLayout->addWidget(m_loadButton);

    mainLayout->addLayout(buttonLayout);

    // 对话框按钮
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(m_buttonBox);

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void GlobalVariableDialog::setupConnections()
{
    connect(m_addButton, &QPushButton::clicked, this, &GlobalVariableDialog::addVariable);
    connect(m_removeButton, &QPushButton::clicked, this, &GlobalVariableDialog::removeVariable);
    connect(m_editButton, &QPushButton::clicked, this, &GlobalVariableDialog::editVariable);
    connect(m_saveButton, &QPushButton::clicked, this, &GlobalVariableDialog::saveVariables);
    connect(m_loadButton, &QPushButton::clicked, this, &GlobalVariableDialog::loadVariables);
}

void GlobalVariableDialog::addVariable()
{
    QDialog dialog(this);
    dialog.setWindowTitle("添加全局变量");

    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    // 变量名
    QHBoxLayout *nameLayout = new QHBoxLayout();
    QLabel *nameLabel = new QLabel("变量名:", &dialog);
    QLineEdit *nameEdit = new QLineEdit(&dialog);
    nameLayout->addWidget(nameLabel);
    nameLayout->addWidget(nameEdit);
    layout->addLayout(nameLayout);

    // 变量类型
    QHBoxLayout *typeLayout = new QHBoxLayout();
    QLabel *typeLabel = new QLabel("类型:", &dialog);
    QComboBox *typeCombo = new QComboBox(&dialog);
    typeCombo->addItems({"整数", "布尔", "浮点数", "字符串"});
    typeLayout->addWidget(typeLabel);
    typeLayout->addWidget(typeCombo);
    layout->addLayout(typeLayout);

    // 变量值
    QHBoxLayout *valueLayout = new QHBoxLayout();
    QLabel *valueLabel = new QLabel("值:", &dialog);
    QWidget *valueWidget = new QWidget(&dialog);
    QVBoxLayout *valueWidgetLayout = new QVBoxLayout(valueWidget);

    QSpinBox *intSpinBox = new QSpinBox(valueWidget);
    QCheckBox *boolCheckBox = new QCheckBox("true", valueWidget);
    QDoubleSpinBox *doubleSpinBox = new QDoubleSpinBox(valueWidget);
    QLineEdit *stringEdit = new QLineEdit(valueWidget);

    valueWidgetLayout->addWidget(intSpinBox);
    valueWidgetLayout->addWidget(boolCheckBox);
    valueWidgetLayout->addWidget(doubleSpinBox);
    valueWidgetLayout->addWidget(stringEdit);

    intSpinBox->hide();
    boolCheckBox->hide();
    doubleSpinBox->hide();
    stringEdit->hide();

    // 默认显示整数
    intSpinBox->show();

    connect(typeCombo, &QComboBox::currentIndexChanged, [=](int index) {
        intSpinBox->hide();
        boolCheckBox->hide();
        doubleSpinBox->hide();
        stringEdit->hide();

        switch (index) {
        case 0: // 整数
            intSpinBox->show();
            break;
        case 1: // 布尔
            boolCheckBox->show();
            break;
        case 2: // 浮点数
            doubleSpinBox->show();
            break;
        case 3: // 字符串
            stringEdit->show();
            break;
        }
    });

    valueLayout->addWidget(valueLabel);
    valueLayout->addWidget(valueWidget);
    layout->addLayout(valueLayout);

    // 变量描述
    QHBoxLayout *descLayout = new QHBoxLayout();
    QLabel *descLabel = new QLabel("描述:", &dialog);
    QTextEdit *descEdit = new QTextEdit(&dialog);
    descLayout->addWidget(descLabel);
    descLayout->addWidget(descEdit);
    layout->addLayout(descLayout);

    // 按钮
    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, [&]() {
        QString name = nameEdit->text().trimmed();
        if (name.isEmpty()) {
            QMessageBox::warning(&dialog, "警告", "变量名不能为空");
            return;
        }

        GlobalVariableManager::VariableType type;
        QVariant value;

        switch (typeCombo->currentIndex()) {
        case 0: // 整数
            type = GlobalVariableManager::IntType;
            value = intSpinBox->value();
            break;
        case 1: // 布尔
            type = GlobalVariableManager::BoolType;
            value = boolCheckBox->isChecked();
            break;
        case 2: // 浮点数
            type = GlobalVariableManager::FloatType;
            value = doubleSpinBox->value();
            break;
        case 3: // 字符串
            type = GlobalVariableManager::StringType;
            value = stringEdit->text();
            break;
        default:
            return;
        }

        if (GlobalVariableManager::instance()->addVariable(name, type, value, descEdit->toPlainText())) {
            updateVariableTable();
            dialog.accept();
        } else {
            QMessageBox::warning(&dialog, "警告", "变量名已存在");
        }
    });

    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    dialog.exec();
}

void GlobalVariableDialog::removeVariable()
{
    int row = m_variableTable->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "警告", "请选择要删除的变量");
        return;
    }

    QString name = m_variableTable->item(row, 0)->text();
    if (QMessageBox::question(this, "确认", QString("确定要删除变量 %1 吗？").arg(name)) == QMessageBox::Yes) {
        GlobalVariableManager::instance()->removeVariable(name);
        updateVariableTable();
    }
}

void GlobalVariableDialog::editVariable()
{
    int row = m_variableTable->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "警告", "请选择要编辑的变量");
        return;
    }

    QString name = m_variableTable->item(row, 0)->text();
    GlobalVariableManager::VariableType type = GlobalVariableManager::instance()->getVariableType(name);
    QVariant value = GlobalVariableManager::instance()->getVariable(name);
    QString description = m_variableTable->item(row, 3)->text();

    QDialog dialog(this);
    dialog.setWindowTitle("编辑全局变量");

    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    // 变量名
    QHBoxLayout *nameLayout = new QHBoxLayout();
    QLabel *nameLabel = new QLabel("变量名:", &dialog);
    QLineEdit *nameEdit = new QLineEdit(name, &dialog);
    nameEdit->setEnabled(false); // 变量名不可编辑
    nameLayout->addWidget(nameLabel);
    nameLayout->addWidget(nameEdit);
    layout->addLayout(nameLayout);

    // 变量类型
    QHBoxLayout *typeLayout = new QHBoxLayout();
    QLabel *typeLabel = new QLabel("类型:", &dialog);
    QComboBox *typeCombo = new QComboBox(&dialog);
    typeCombo->addItems({"整数", "布尔", "浮点数", "字符串"});
    typeCombo->setCurrentIndex(static_cast<int>(type));
    typeCombo->setEnabled(false); // 类型不可编辑
    typeLayout->addWidget(typeLabel);
    typeLayout->addWidget(typeCombo);
    layout->addLayout(typeLayout);

    // 变量值
    QHBoxLayout *valueLayout = new QHBoxLayout();
    QLabel *valueLabel = new QLabel("值:", &dialog);
    QWidget *valueWidget = new QWidget(&dialog);
    QVBoxLayout *valueWidgetLayout = new QVBoxLayout(valueWidget);

    QSpinBox *intSpinBox = new QSpinBox(valueWidget);
    QCheckBox *boolCheckBox = new QCheckBox("true", valueWidget);
    QDoubleSpinBox *doubleSpinBox = new QDoubleSpinBox(valueWidget);
    QLineEdit *stringEdit = new QLineEdit(valueWidget);

    valueWidgetLayout->addWidget(intSpinBox);
    valueWidgetLayout->addWidget(boolCheckBox);
    valueWidgetLayout->addWidget(doubleSpinBox);
    valueWidgetLayout->addWidget(stringEdit);

    intSpinBox->hide();
    boolCheckBox->hide();
    doubleSpinBox->hide();
    stringEdit->hide();

    // 根据类型显示相应的控件
    switch (type) {
    case GlobalVariableManager::IntType:
        intSpinBox->setValue(value.toInt());
        intSpinBox->show();
        break;
    case GlobalVariableManager::BoolType:
        boolCheckBox->setChecked(value.toBool());
        boolCheckBox->show();
        break;
    case GlobalVariableManager::FloatType:
        doubleSpinBox->setValue(value.toDouble());
        doubleSpinBox->show();
        break;
    case GlobalVariableManager::StringType:
        stringEdit->setText(value.toString());
        stringEdit->show();
        break;
    }

    valueLayout->addWidget(valueLabel);
    valueLayout->addWidget(valueWidget);
    layout->addLayout(valueLayout);

    // 变量描述
    QHBoxLayout *descLayout = new QHBoxLayout();
    QLabel *descLabel = new QLabel("描述:", &dialog);
    QTextEdit *descEdit = new QTextEdit(description, &dialog);
    descLayout->addWidget(descLabel);
    descLayout->addWidget(descEdit);
    layout->addLayout(descLayout);

    // 按钮
    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, [&]() {
        QVariant newValue;

        switch (type) {
        case GlobalVariableManager::IntType:
            newValue = intSpinBox->value();
            break;
        case GlobalVariableManager::BoolType:
            newValue = boolCheckBox->isChecked();
            break;
        case GlobalVariableManager::FloatType:
            newValue = doubleSpinBox->value();
            break;
        case GlobalVariableManager::StringType:
            newValue = stringEdit->text();
            break;
        }

        GlobalVariableManager::instance()->setVariable(name, newValue);
        updateVariableTable();
        dialog.accept();
    });

    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    dialog.exec();
}

void GlobalVariableDialog::saveVariables()
{
    QString fileName = QFileDialog::getSaveFileName(this, "保存全局变量", "", "JSON Files (*.json)");
    if (!fileName.isEmpty()) {
        if (GlobalVariableManager::instance()->saveToFile(fileName)) {
            QMessageBox::information(this, "成功", "全局变量保存成功");
        } else {
            QMessageBox::warning(this, "失败", "全局变量保存失败");
        }
    }
}

void GlobalVariableDialog::loadVariables()
{
    QString fileName = QFileDialog::getOpenFileName(this, "加载全局变量", "", "JSON Files (*.json)");
    if (!fileName.isEmpty()) {
        if (GlobalVariableManager::instance()->loadFromFile(fileName)) {
            updateVariableTable();
            QMessageBox::information(this, "成功", "全局变量加载成功");
        } else {
            QMessageBox::warning(this, "失败", "全局变量加载失败");
        }
    }
}

void GlobalVariableDialog::updateVariableTable()
{
    m_variableTable->setRowCount(0);

    QMap<QString, GlobalVariableManager::Variable> variables = GlobalVariableManager::instance()->variables();
    for (auto it = variables.constBegin(); it != variables.constEnd(); ++it) {
        const GlobalVariableManager::Variable &var = it.value();

        int row = m_variableTable->rowCount();
        m_variableTable->insertRow(row);

        m_variableTable->setItem(row, 0, new QTableWidgetItem(var.name));

        QString typeString;
        switch (var.type) {
        case GlobalVariableManager::IntType:
            typeString = "整数";
            break;
        case GlobalVariableManager::BoolType:
            typeString = "布尔";
            break;
        case GlobalVariableManager::FloatType:
            typeString = "浮点数";
            break;
        case GlobalVariableManager::StringType:
            typeString = "字符串";
            break;
        }
        m_variableTable->setItem(row, 1, new QTableWidgetItem(typeString));

        m_variableTable->setItem(row, 2, new QTableWidgetItem(var.value.toString()));
        m_variableTable->setItem(row, 3, new QTableWidgetItem(var.description));
    }

    m_variableTable->resizeColumnsToContents();
}