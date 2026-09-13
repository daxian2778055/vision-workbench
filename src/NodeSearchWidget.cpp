#include "NodeSearchWidget.h"
#include "FlowScene.h"
#include "NodeBase.h"
#include <QKeyEvent>
#include <QApplication>

NodeSearchWidget::NodeSearchWidget(QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setupUi();

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(SEARCH_DELAY_MS);
    connect(m_searchTimer, &QTimer::timeout, this, [this]() {
        performSearch(m_searchEdit->text());
    });

    // 初始隐藏
    hide();
}

void NodeSearchWidget::setFlowScene(FlowScene *scene)
{
    m_flowScene = scene;
    clear();
}

void NodeSearchWidget::clear()
{
    m_searchEdit->clear();
    m_resultList->clear();
    m_countLabel->clear();
}

void NodeSearchWidget::showAndFocus()
{
    show();
    raise();
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();

    // 调整位置到父窗口中心
    if (parentWidget()) {
        QRect parentRect = parentWidget()->geometry();
        int x = parentRect.x() + (parentRect.width() - width()) / 2;
        int y = parentRect.y() + 50;
        move(x, y);
    }
}

void NodeSearchWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        emit closed();
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        // 回车选中当前项
        if (m_resultList->currentRow() >= 0) {
            onSearchResultDoubleClicked(m_resultList->currentRow());
        }
    } else if (event->key() == Qt::Key_Down) {
        // 下箭头移动到下一个结果
        int nextRow = m_resultList->currentRow() + 1;
        if (nextRow < m_resultList->count()) {
            m_resultList->setCurrentRow(nextRow);
        }
    } else if (event->key() == Qt::Key_Up) {
        // 上箭头移动到上一个结果
        int prevRow = m_resultList->currentRow() - 1;
        if (prevRow >= 0) {
            m_resultList->setCurrentRow(prevRow);
        }
    } else {
        QWidget::keyPressEvent(event);
    }
}

void NodeSearchWidget::setupUi()
{
    setFixedWidth(350);
    setMaximumHeight(400);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);

    // 搜索框
    auto *searchLayout = new QHBoxLayout();
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索算子名称... (Ctrl+F)"));
    m_searchEdit->setClearButtonEnabled(true);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_searchTimer->start();
    });
    searchLayout->addWidget(m_searchEdit);

    m_closeBtn = new QPushButton(QStringLiteral("×"));
    m_closeBtn->setFixedSize(24, 24);
    m_closeBtn->setToolTip(QStringLiteral("关闭搜索"));
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        hide();
        emit closed();
    });
    searchLayout->addWidget(m_closeBtn);

    layout->addLayout(searchLayout);

    // 结果计数
    m_countLabel = new QLabel();
    m_countLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #666;"));
    layout->addWidget(m_countLabel);

    // 搜索结果列表
    m_resultList = new QListWidget();
    m_resultList->setAlternatingRowColors(true);
    m_resultList->setSelectionMode(QListWidget::SingleSelection);
    connect(m_resultList, &QListWidget::currentRowChanged, this, &NodeSearchWidget::onSearchResultClicked);
    connect(m_resultList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        onSearchResultDoubleClicked(m_resultList->row(item));
    });
    layout->addWidget(m_resultList);

    // 快捷键提示
    auto *hintLabel = new QLabel(QStringLiteral("↑↓ 切换 | Enter 选中 | Esc 关闭"));
    hintLabel->setStyleSheet(QStringLiteral("font-size: 10px; color: #999;"));
    layout->addWidget(hintLabel);
}

void NodeSearchWidget::onSearchTextChanged(const QString &text)
{
    Q_UNUSED(text);
    m_searchTimer->start();
}

void NodeSearchWidget::performSearch(const QString &keyword)
{
    m_resultList->clear();

    if (!m_flowScene || keyword.isEmpty()) {
        m_countLabel->clear();
        return;
    }

    const auto nodes = m_flowScene->nodes();
    QList<NodeBase*> matchedNodes;

    for (NodeBase *node : nodes) {
        if (!node) continue;

        // 搜索算子名称（不区分大小写）
        QString nodeName = node->name();
        QString fullName = node->fullName();

        if (nodeName.contains(keyword, Qt::CaseInsensitive) ||
            fullName.contains(keyword, Qt::CaseInsensitive) ||
            QString::number(node->moduleId()).contains(keyword)) {
            matchedNodes.append(node);
        }
    }

    // 填充结果列表
    for (NodeBase *node : matchedNodes) {
        auto *item = new QListWidgetItem();
        item->setText(QStringLiteral("%1 (模块%2)")
                         .arg(node->name())
                         .arg(node->moduleId()));
        item->setData(Qt::UserRole, reinterpret_cast<qulonglong>(node));

        // 添加图标提示节点类型
        switch (node->type()) {
        case NodeBase::IMAGE_ACQUISITION:
            item->setForeground(QColor(0, 128, 0));  // 绿色
            break;
        case NodeBase::IMAGE_PROCESSING:
            item->setForeground(QColor(0, 0, 200));  // 蓝色
            break;
        case NodeBase::SHAPE_ANALYSIS:
            item->setForeground(QColor(200, 0, 0));  // 红色
            break;
        case NodeBase::MEASUREMENT:
            item->setForeground(QColor(128, 0, 128));  // 紫色
            break;
        default:
            break;
        }

        m_resultList->addItem(item);
    }

    // 更新计数
    m_countLabel->setText(QStringLiteral("找到 %1 个结果").arg(matchedNodes.size()));

    // 自动选中第一个
    if (!matchedNodes.isEmpty()) {
        m_resultList->setCurrentRow(0);
    }
}

void NodeSearchWidget::onSearchResultClicked(int row)
{
    if (row < 0 || row >= m_resultList->count()) return;

    QListWidgetItem *item = m_resultList->item(row);
    if (!item) return;

    NodeBase *node = reinterpret_cast<NodeBase*>(item->data(Qt::UserRole).toULongLong());
    if (node) {
        // 高亮节点但不关闭搜索框
        if (m_flowScene) {
            // TODO: 高亮节点
        }
    }
}

void NodeSearchWidget::onSearchResultDoubleClicked(int row)
{
    if (row < 0 || row >= m_resultList->count()) return;

    QListWidgetItem *item = m_resultList->item(row);
    if (!item) return;

    NodeBase *node = reinterpret_cast<NodeBase*>(item->data(Qt::UserRole).toULongLong());
    selectNode(node);
}

void NodeSearchWidget::selectNode(NodeBase *node)
{
    if (!node) return;

    emit nodeSelected(node);
    hide();
}
