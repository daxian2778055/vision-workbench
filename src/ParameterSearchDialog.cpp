#include "ParameterSearchDialog.h"
#include "FlowScene.h"
#include "NodeBase.h"
#include "HalconNode.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>

ParameterSearchDialog::ParameterSearchDialog(const QList<FlowScene *> &flows, QWidget *parent)
    : QDialog(parent)
    , m_flows(flows)
{
    setupUI();
    collectNodes();
    doSearch();
}

ParameterSearchDialog::~ParameterSearchDialog()
{
}

void ParameterSearchDialog::setupUI()
{
    setWindowTitle(QStringLiteral("\u53C2\u6570\u67E5\u627E"));
    setMinimumSize(780, 480);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *mainLayout = new QVBoxLayout(this);

    auto *titleLabel = new QLabel(QStringLiteral("<b>\u53C2\u6570\u67E5\u627E\u5DE5\u5177</b>"));
    titleLabel->setStyleSheet("font-size: 14px;");
    mainLayout->addWidget(titleLabel);

    auto *searchLayout = new QHBoxLayout();
    searchLayout->addWidget(new QLabel(QStringLiteral("\u5173\u952E\u5B57:")));
    m_keywordEdit = new QLineEdit();
    m_keywordEdit->setPlaceholderText(QStringLiteral("\u8F93\u5165\u53C2\u6570\u540D\u79F0\u6216\u503C"));
    m_keywordEdit->setMinimumWidth(200);
    searchLayout->addWidget(m_keywordEdit);

    m_scopeCombo = new QComboBox();
    m_scopeCombo->addItem(QStringLiteral("\u5168\u90E8"), 0);
    m_scopeCombo->addItem(QStringLiteral("\u53C2\u6570\u540D"), 1);
    m_scopeCombo->addItem(QStringLiteral("\u53C2\u6570\u503C"), 2);
    searchLayout->addWidget(m_scopeCombo);

    m_searchBtn = new QPushButton(QStringLiteral("\u641C\u7D22"));
    m_searchBtn->setMinimumHeight(28);
    searchLayout->addWidget(m_searchBtn);

    m_locateBtn = new QPushButton(QStringLiteral("\u5B9A\u4F4D\u5230\u8282\u70B9"));
    m_locateBtn->setMinimumHeight(28);
    searchLayout->addWidget(m_locateBtn);

    searchLayout->addStretch();
    mainLayout->addLayout(searchLayout);

    m_resultTable = new QTableWidget();
    m_resultTable->setColumnCount(4);
    m_resultTable->setHorizontalHeaderLabels({
        QStringLiteral("\u6D41\u7A0B"),
        QStringLiteral("\u7B97\u5B50"),
        QStringLiteral("\u53C2\u6570\u540D"),
        QStringLiteral("\u53C2\u6570\u503C")
    });
    m_resultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_resultTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    mainLayout->addWidget(m_resultTable);

    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_closeBtn = new QPushButton(QStringLiteral("\u5173\u95ED"));
    m_closeBtn->setMinimumHeight(30);
    btnLayout->addWidget(m_closeBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_searchBtn, &QPushButton::clicked, this, &ParameterSearchDialog::onSearch);
    connect(m_keywordEdit, &QLineEdit::returnPressed, this, &ParameterSearchDialog::onSearch);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_locateBtn, &QPushButton::clicked, this, &ParameterSearchDialog::onLocateNode);
}

void ParameterSearchDialog::collectNodes()
{
    m_entries.clear();
    int flowIndex = 0;
    for (FlowScene *scene : m_flows) {
        if (!scene) { ++flowIndex; continue; }
        QString flowName = (!scene->flowName().isEmpty())
                              ? scene->flowName()
                              : QStringLiteral("\u6D41\u7A0B %1").arg(flowIndex + 1);
        for (NodeBase *node : scene->nodes()) {
            if (!node) continue;
            // 遍历该算子的参数描述，若无则尝试常见参数名
            auto *halcon = dynamic_cast<HalconNode *>(node);
            if (halcon) {
                const auto &specs = halcon->paramSpecs();
                if (!specs.isEmpty()) {
                    for (const auto &spec : specs) {
                        QVariant val = node->getParam(spec.name);
                        Entry e;
                        e.flowName = flowName;
                        e.nodeName = node->fullName();
                        e.paramName = spec.name;
                        e.paramValue = val.toString();
                        e.node = node;
                        m_entries.append(e);
                    }
                    continue;
                }
            }
            // 无 ParamSpec 时，用一组常见参数名兜底
            QStringList keys = { QStringLiteral("threshold"), QStringLiteral("min"),
                                 QStringLiteral("max"), QStringLiteral("angle"),
                                 QStringLiteral("distance"), QStringLiteral("sigma"),
                                 QStringLiteral("expression"), QStringLiteral("host"),
                                 QStringLiteral("port"), QStringLiteral("fileName") };
            for (const QString &key : keys) {
                QVariant val = node->getParam(key);
                if (!val.isNull() && val.isValid()) {
                    Entry e;
                    e.flowName = flowName;
                    e.nodeName = node->fullName();
                    e.paramName = key;
                    e.paramValue = val.toString();
                    e.node = node;
                    m_entries.append(e);
                }
            }
        }
        ++flowIndex;
    }
}

void ParameterSearchDialog::doSearch()
{
    QString keyword = m_keywordEdit->text().trimmed();
    int scope = m_scopeCombo->currentData().toInt();

    m_resultTable->setRowCount(0);
    for (int i = 0; i < m_entries.size(); ++i) {
        const auto &e = m_entries[i];
        bool match = keyword.isEmpty();
        if (!match) {
            if (scope == 1) {
                match = e.paramName.contains(keyword, Qt::CaseInsensitive);
            } else if (scope == 2) {
                match = e.paramValue.contains(keyword, Qt::CaseInsensitive);
            } else {
                match = e.paramName.contains(keyword, Qt::CaseInsensitive)
                        || e.paramValue.contains(keyword, Qt::CaseInsensitive)
                        || e.nodeName.contains(keyword, Qt::CaseInsensitive);
            }
        }
        if (!match) continue;

        int row = m_resultTable->rowCount();
        m_resultTable->insertRow(row);
        m_resultTable->setItem(row, 0, new QTableWidgetItem(e.flowName));
        m_resultTable->setItem(row, 1, new QTableWidgetItem(e.nodeName));
        m_resultTable->setItem(row, 2, new QTableWidgetItem(e.paramName));
        auto *valueItem = new QTableWidgetItem(e.paramValue);
        valueItem->setData(Qt::UserRole, i); // 记录原始条目索引，便于定位节点
        m_resultTable->setItem(row, 3, valueItem);
    }
    m_resultTable->resizeColumnsToContents();
}

void ParameterSearchDialog::onSearch()
{
    doSearch();
}

void ParameterSearchDialog::onLocateNode()
{
    int row = m_resultTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("\u8BF7\u5148\u5728\u7ED3\u679C\u4E2D\u9009\u62E9\u4E00\u884C"));
        return;
    }
    QTableWidgetItem *nameItem = m_resultTable->item(row, 1);
    QTableWidgetItem *idxItem = m_resultTable->item(row, 3);
    NodeBase *node = nullptr;
    if (idxItem) {
        int i = idxItem->data(Qt::UserRole).toInt();
        if (i >= 0 && i < m_entries.size()) node = m_entries[i].node;
    }
    QMessageBox::information(this, QStringLiteral("\u5B9A\u4F4D"),
        QStringLiteral("\u5DF2\u5B9A\u4F4D\u5230\u7B97\u5B50: %1")
            .arg(node ? node->fullName()
                      : (nameItem ? nameItem->text() : QStringLiteral("?"))));
}
