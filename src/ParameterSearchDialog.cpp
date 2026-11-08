#include "ParameterSearchDialog.h"
#include "FlowScene.h"
#include "NodeBase.h"
#include "HalconNode.h"
#include "Port.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QTreeWidget>
#include <functional>

ParameterSearchDialog::ParameterSearchDialog(const QList<FlowScene *> &flows, QWidget *parent)
    : QDialog(parent)
    , m_flows(flows)
{
    setupUI();
    buildTree();
    applyFilter();
}

ParameterSearchDialog::~ParameterSearchDialog() = default;

void ParameterSearchDialog::setupUI()
{
    setWindowTitle(QStringLiteral("参数查找工具"));
    setMinimumSize(900, 560);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *mainLayout = new QVBoxLayout(this);

    auto *titleLabel = new QLabel(
        QStringLiteral("<b>参数查找工具</b>　"
                       "<span style='color:#8f9aa8;'>流程 → 模块 → 输入/输出/运行参数</span>"));
    titleLabel->setStyleSheet(QStringLiteral("font-size: 14px;"));
    mainLayout->addWidget(titleLabel);

    auto *searchLayout = new QHBoxLayout();
    searchLayout->addWidget(new QLabel(QStringLiteral("关键字:")));
    m_keywordEdit = new QLineEdit();
    m_keywordEdit->setPlaceholderText(QStringLiteral("过滤参数名 / 参数值（留空显示全部）"));
    m_keywordEdit->setMinimumWidth(240);
    searchLayout->addWidget(m_keywordEdit, 1);

    m_scopeCombo = new QComboBox();
    m_scopeCombo->addItem(QStringLiteral("全部"), 0);
    m_scopeCombo->addItem(QStringLiteral("参数名"), 1);
    m_scopeCombo->addItem(QStringLiteral("参数值"), 2);
    searchLayout->addWidget(m_scopeCombo);

    m_searchBtn = new QPushButton(QStringLiteral("搜索"));
    m_searchBtn->setMinimumHeight(28);
    searchLayout->addWidget(m_searchBtn);

    m_locateBtn = new QPushButton(QStringLiteral("定位到节点"));
    m_locateBtn->setMinimumHeight(28);
    searchLayout->addWidget(m_locateBtn);

    searchLayout->addStretch();
    mainLayout->addLayout(searchLayout);

    m_resultTree = new QTreeWidget();
    m_resultTree->setObjectName(QStringLiteral("parameterResultTree"));
    m_resultTree->setColumnCount(4);
    m_resultTree->setHeaderLabels({
        QStringLiteral("软件参数名"),
        QStringLiteral("算子参数名"),
        QStringLiteral("当前值"),
        QStringLiteral("调用代码")
    });
    m_resultTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultTree->setAlternatingRowColors(true);
    m_resultTree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_resultTree->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_resultTree->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_resultTree->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_resultTree->setColumnWidth(0, 220);
    m_resultTree->setColumnWidth(1, 180);
    m_resultTree->setColumnWidth(2, 160);
    mainLayout->addWidget(m_resultTree, 1);

    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_closeBtn = new QPushButton(QStringLiteral("关闭"));
    m_closeBtn->setMinimumHeight(30);
    btnLayout->addWidget(m_closeBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_searchBtn, &QPushButton::clicked, this, &ParameterSearchDialog::onSearch);
    connect(m_keywordEdit, &QLineEdit::returnPressed, this, &ParameterSearchDialog::onSearch);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_locateBtn, &QPushButton::clicked, this, &ParameterSearchDialog::onLocateNode);
    connect(m_resultTree, &QTreeWidget::itemDoubleClicked, this,
            &ParameterSearchDialog::onItemDoubleClicked);
}

void ParameterSearchDialog::buildTree()
{
    m_resultTree->clear();
    int flowIndex = 0;
    for (FlowScene *scene : m_flows) {
        if (!scene) {
            ++flowIndex;
            continue;
        }
        const QString flowName = scene->flowName().isEmpty()
                                     ? QStringLiteral("流程 %1").arg(flowIndex + 1)
                                     : scene->flowName();
        auto *flowItem = new QTreeWidgetItem(m_resultTree);
        flowItem->setText(0, flowName);
        flowItem->setFirstColumnSpanned(true);
        QFont bold = flowItem->font(0);
        bold.setBold(true);
        flowItem->setFont(0, bold);

        for (NodeBase *node : scene->nodes()) {
            if (!node)
                continue;
            auto *nodeItem = new QTreeWidgetItem(flowItem);
            nodeItem->setText(0, node->fullName());
            nodeItem->setFirstColumnSpanned(true);
            // 只存模块号（int），**不存裸 NodeBase\***：搜索结果可能在弹出提示前
            // 就已过期（节点被删），从 UserRole 还原指针再解引用就是 UAF。
            // 2026-11-08 复核 P3 修复。
            nodeItem->setData(0, Qt::UserRole, node->moduleId());

            // ---- 输入参数 ----
            auto *inputItem = new QTreeWidgetItem(nodeItem);
            inputItem->setText(0, QStringLiteral("输入参数"));
            inputItem->setFirstColumnSpanned(true);
            const QList<Port *> inputs = node->inputPorts();
            for (int i = 0; i < inputs.size(); ++i) {
                Port *port = inputs.at(i);
                if (!port)
                    continue;
                auto *leaf = new QTreeWidgetItem(inputItem);
                leaf->setText(0, QStringLiteral("输入 · %1").arg(port->name()));
                leaf->setText(1, port->name());
                leaf->setText(2, QStringLiteral("—"));
                leaf->setText(3, QStringLiteral("node->getInputData(%1)").arg(i));
                leaf->setData(0, Qt::UserRole, node->moduleId());
            }

            // ---- 输出参数 ----
            auto *outputItem = new QTreeWidgetItem(nodeItem);
            outputItem->setText(0, QStringLiteral("输出参数"));
            outputItem->setFirstColumnSpanned(true);
            const QList<Port *> outputs = node->outputPorts();
            for (int i = 0; i < outputs.size(); ++i) {
                Port *port = outputs.at(i);
                if (!port)
                    continue;
                auto *leaf = new QTreeWidgetItem(outputItem);
                leaf->setText(0, QStringLiteral("输出 · %1").arg(port->name()));
                leaf->setText(1, port->name());
                leaf->setText(2, QStringLiteral("—"));
                leaf->setText(3, QStringLiteral("node->getOutputData(%1)").arg(i));
                leaf->setData(0, Qt::UserRole, node->moduleId());
            }

            // ---- 运行参数 ----
            auto *paramItem = new QTreeWidgetItem(nodeItem);
            paramItem->setText(0, QStringLiteral("运行参数"));
            paramItem->setFirstColumnSpanned(true);
            struct ParamRow { QString label; QString name; };
            QList<ParamRow> rows;
            if (auto *halcon = dynamic_cast<HalconNode *>(node)) {
                for (const ParamSpec &spec : halcon->paramSpecs())
                    rows.append({spec.label.isEmpty() ? spec.name : spec.label, spec.name});
            }
            if (rows.isEmpty()) {
                for (const QString &name : node->getAllParamNames())
                    rows.append({name, name});
            }
            for (const ParamRow &row : rows) {
                auto *leaf = new QTreeWidgetItem(paramItem);
                leaf->setText(0, row.label);
                leaf->setText(1, row.name);
                leaf->setText(2, node->getParam(row.name).toString());
                leaf->setText(3, QStringLiteral("node->getParam(\"%1\")").arg(row.name));
                leaf->setData(0, Qt::UserRole, node->moduleId());
            }

            if (inputItem->childCount() == 0)
                delete inputItem;
            if (outputItem->childCount() == 0)
                delete outputItem;
            if (paramItem->childCount() == 0)
                delete paramItem;
        }
        if (flowItem->childCount() == 0)
            delete flowItem;
        ++flowIndex;
    }
    m_resultTree->expandAll();
}

void ParameterSearchDialog::applyFilter()
{
    const QString keyword = m_keywordEdit->text().trimmed();
    const int scope = m_scopeCombo->currentData().toInt();

    std::function<bool(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) -> bool {
        if (!item)
            return false;
        if (item->childCount() == 0) {
            bool match = keyword.isEmpty();
            if (!match) {
                if (scope == 1)
                    match = item->text(0).contains(keyword, Qt::CaseInsensitive)
                            || item->text(1).contains(keyword, Qt::CaseInsensitive);
                else if (scope == 2)
                    match = item->text(2).contains(keyword, Qt::CaseInsensitive);
                else
                    match = item->text(0).contains(keyword, Qt::CaseInsensitive)
                            || item->text(1).contains(keyword, Qt::CaseInsensitive)
                            || item->text(2).contains(keyword, Qt::CaseInsensitive)
                            || item->text(3).contains(keyword, Qt::CaseInsensitive);
            }
            item->setHidden(!match);
            return match;
        }
        bool anyVisible = false;
        for (int i = 0; i < item->childCount(); ++i)
            anyVisible = visit(item->child(i)) || anyVisible;
        item->setHidden(!anyVisible);
        if (anyVisible)
            item->setExpanded(true);
        return anyVisible;
    };

    for (int i = 0; i < m_resultTree->topLevelItemCount(); ++i)
        visit(m_resultTree->topLevelItem(i));
}

void ParameterSearchDialog::onSearch()
{
    applyFilter();
}

void ParameterSearchDialog::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column)
    if (item && item->childCount() == 0) {
        m_resultTree->setCurrentItem(item);
        onLocateNode();
    }
}

void ParameterSearchDialog::onLocateNode()
{
    QTreeWidgetItem *item = m_resultTree->currentItem();
    if (!item || item->childCount() != 0) {
        // 选中父级时取第一个叶子，减少用户操作
        if (item && item->childCount() > 0)
            item = item->child(0);
    }
    if (!item) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先在结果中选择一个参数"));
        return;
    }

    // 只读模块号，不解引用节点：搜索结果可能在弹提示前已过期（节点被删），
    // 旧实现从 UserRole 还原裸 NodeBase* 再取 fullName()，就是 UAF。
    // 模块名在构建结果时已写进树（nodeItem 的第 0 列），这里直接用。
    const int moduleId = item->data(0, Qt::UserRole).toInt();
    Q_UNUSED(moduleId)   // 当前「定位」只弹提示不跳转；保留模块号供后续跳转使用
    const QString nodeName = item->parent() && item->parent()->parent()
                                 ? item->parent()->parent()->text(0)
                                 : QString();
    QMessageBox::information(
        this, QStringLiteral("定位"),
        QStringLiteral("已定位到模块: %1\n参数: %2").arg(nodeName, item->text(1)));
}
