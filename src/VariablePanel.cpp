#include "VariablePanel.h"

#include "GlobalVariableManager.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QApplication>
#include <QClipboard>
#include <QLineEdit>
#include <QTextEdit>
#include <QBrush>
#include <QColor>
#include <QFont>

namespace {
constexpr int kColName = 0;
constexpr int kColValue = 1;
constexpr int kColRef = 2;

/// 值 -> 显示文本（与结果表一致：布尔/数值/其余按字符串）
QString valueText(const QVariant &v)
{
    if (v.typeId() == QMetaType::Bool)
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    const int id = v.metaType().id();
    if (id == QMetaType::Double || id == QMetaType::Float)
        return QString::number(v.toDouble(), 'g', 10);
    return v.toString();
}
}

VariablePanel::VariablePanel(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);

    auto *bar = new QHBoxLayout();
    auto *copyBtn = new QPushButton(QStringLiteral("复制引用"), this);
    copyBtn->setToolTip(QStringLiteral("复制选中变量的引用表达式（如 {3.foregroundPixels}），"
                                       "可直接粘贴到下游算子参数里做表达式联动"));
    auto *clearBtn = new QPushButton(QStringLiteral("清空"), this);
    clearBtn->setToolTip(QStringLiteral("清空本面板的变量行（不影响实际变量）"));
    bar->addWidget(copyBtn);
    bar->addWidget(clearBtn);
    bar->addStretch();
    m_summary = new QLabel(QStringLiteral("暂无变量"), this);
    bar->addWidget(m_summary);
    root->addLayout(bar);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({QStringLiteral("名称"), QStringLiteral("值"),
                             QStringLiteral("引用表达式")});
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setRootIsDecorated(true);

    m_moduleGroup = new QTreeWidgetItem(m_tree);
    m_moduleGroup->setText(kColName, QStringLiteral("模块输出"));
    QFont bold = m_moduleGroup->font(kColName);
    bold.setBold(true);
    m_moduleGroup->setFont(kColName, bold);
    m_moduleGroup->setExpanded(true);

    m_globalGroup = new QTreeWidgetItem(m_tree);
    m_globalGroup->setText(kColName, QStringLiteral("全局变量"));
    m_globalGroup->setFont(kColName, bold);
    m_globalGroup->setExpanded(true);

    root->addWidget(m_tree, 1);

    m_hint = new QLabel(QString(), this);
    root->addWidget(m_hint);

    connect(copyBtn, &QPushButton::clicked, this, &VariablePanel::copyCurrentReference);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *, int) { copyCurrentReference(); });
    connect(clearBtn, &QPushButton::clicked, this, &VariablePanel::clearAll);

    // 全局变量可被运行时改写（计数器等），变化时自动刷新
    connect(GlobalVariableManager::instance(), &GlobalVariableManager::variableChanged, this,
            [this](const QString &, const QVariant &) { refreshGlobalVariables(); });

    refreshGlobalVariables();
}

QString VariablePanel::moduleRefText(int moduleId, const QString &name)
{
    return QStringLiteral("{%1.%2}").arg(moduleId).arg(name);
}

QString VariablePanel::globalRefText(const QString &name)
{
    return QStringLiteral("{global.%1}").arg(name);
}

bool VariablePanel::insertReferenceInto(QWidget *target, const QString &ref)
{
    if (!target || ref.isEmpty())
        return false;
    if (auto *edit = qobject_cast<QLineEdit *>(target)) {
        edit->insert(ref);   // 光标处插入，用户可继续编辑
        return true;
    }
    if (auto *text = qobject_cast<QTextEdit *>(target)) {
        text->insertPlainText(ref);
        return true;
    }
    // 复合控件（如 QSpinBox/QComboBox）内嵌的编辑框
    if (auto *inner = target->findChild<QLineEdit *>()) {
        inner->insert(ref);
        return true;
    }
    return false;
}

QTreeWidgetItem *VariablePanel::findOrCreateModuleRow(int moduleId, const QString &moduleName)
{
    QTreeWidgetItem *row = m_moduleRows.value(moduleId, nullptr);
    if (!row) {
        row = new QTreeWidgetItem(m_moduleGroup);
        m_moduleRows.insert(moduleId, row);
        row->setExpanded(true);
    }
    row->setText(kColName, moduleName.isEmpty() ? QStringLiteral("模块 %1").arg(moduleId)
                                                : moduleName);
    return row;
}

QTreeWidgetItem *VariablePanel::appendValueRow(QTreeWidgetItem *parent, const QString &name,
                                               const QString &text, const QString &ref)
{
    auto *child = new QTreeWidgetItem(parent);
    child->setText(kColName, name);
    child->setText(kColValue, text);
    child->setText(kColRef, ref);
    // 引用文本随行携带，复制时无需再查表
    child->setData(kColRef, Qt::UserRole, ref);
    child->setForeground(kColRef, QBrush(QColor(0x1F, 0x4F, 0x8B)));
    return child;
}

void VariablePanel::setModuleVars(int moduleId, const QString &moduleName, const QVariantMap &vars)
{
    QTreeWidgetItem *row = findOrCreateModuleRow(moduleId, moduleName);

    // 重建子项：保证显示的是本轮可引用值，不残留上一轮的旧变量
    const QList<QTreeWidgetItem *> old = row->takeChildren();
    qDeleteAll(old);

    QStringList keys = vars.keys();
    keys.sort();
    for (const QString &k : keys) {
        appendValueRow(row, k, valueText(vars.value(k)), moduleRefText(moduleId, k));
    }
    row->setText(kColValue, QStringLiteral("(%1 项)").arg(keys.size()));

    m_moduleGroup->setExpanded(true);
    updateSummary();
}

void VariablePanel::refreshGlobalVariables()
{
    if (!m_globalGroup)
        return;
    const QList<QTreeWidgetItem *> old = m_globalGroup->takeChildren();
    qDeleteAll(old);

    const QMap<QString, GlobalVariableManager::Variable> vars =
        GlobalVariableManager::instance()->variables();
    for (auto it = vars.cbegin(); it != vars.cend(); ++it) {
        const GlobalVariableManager::Variable &v = it.value();
        appendValueRow(m_globalGroup, v.name, valueText(v.value), globalRefText(v.name));
    }
    m_globalGroup->setText(kColValue, QStringLiteral("(%1 项)").arg(vars.size()));
    updateSummary();
}

void VariablePanel::clearAll()
{
    for (auto it = m_moduleRows.begin(); it != m_moduleRows.end(); ++it)
        delete it.value();
    m_moduleRows.clear();

    const QList<QTreeWidgetItem *> old = m_globalGroup->takeChildren();
    qDeleteAll(old);
    m_globalGroup->setText(kColValue, QString());

    if (m_hint)
        m_hint->clear();
    updateSummary();
}

int VariablePanel::moduleCount() const
{
    return m_moduleGroup ? m_moduleGroup->childCount() : 0;
}

int VariablePanel::globalVariableCount() const
{
    return m_globalGroup ? m_globalGroup->childCount() : 0;
}

void VariablePanel::copyCurrentReference()
{
    QTreeWidgetItem *item = m_tree->currentItem();
    if (!item)
        return;
    const QString ref = item->text(kColRef);
    if (ref.isEmpty()) {
        if (m_hint)
            m_hint->setText(QStringLiteral("请选中一个变量行（分组行没有引用表达式）"));
        return;
    }
    QApplication::clipboard()->setText(ref);
    if (m_hint)
        m_hint->setText(QStringLiteral("已复制引用：%1（粘贴到下游参数即可联动）").arg(ref));
}

void VariablePanel::updateSummary()
{
    if (!m_summary)
        return;
    m_summary->setText(QStringLiteral("模块 %1 · 全局变量 %2")
                           .arg(moduleCount())
                           .arg(globalVariableCount()));
}
