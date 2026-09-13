#include "PerformancePanel.h"
#include "FlowExecutor.h"
#include "NodeBase.h"
#include <QHeaderView>
#include <QFileDialog>
#include <QTextStream>
#include <QDateTime>
#include <QMessageBox>

PerformancePanel::PerformancePanel(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void PerformancePanel::bindExecutor(FlowExecutor *executor)
{
    if (m_boundExecutor) {
        unbindExecutor();
    }

    m_boundExecutor = executor;
    if (executor) {
        connect(executor, &FlowExecutor::nodeExecutionTime,
                this, &PerformancePanel::onNodeExecutionTime);
        connect(executor, &FlowExecutor::flowExecutionTime,
                this, &PerformancePanel::onFlowExecutionTime);
        connect(executor, &FlowExecutor::executionStarted,
                this, &PerformancePanel::clearStats);
    }
}

void PerformancePanel::unbindExecutor()
{
    if (m_boundExecutor) {
        disconnect(m_boundExecutor, nullptr, this, nullptr);
        m_boundExecutor = nullptr;
    }
}

void PerformancePanel::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // 标题栏
    auto *headerLayout = new QHBoxLayout();
    auto *titleLabel = new QLabel(QStringLiteral("性能分析"));
    titleLabel->setStyleSheet(QStringLiteral("font-weight: bold; font-size: 14px;"));
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    m_clearBtn = new QPushButton(QStringLiteral("清空"));
    m_clearBtn->setToolTip(QStringLiteral("清空统计数据"));
    connect(m_clearBtn, &QPushButton::clicked, this, &PerformancePanel::clearStats);
    headerLayout->addWidget(m_clearBtn);

    m_exportBtn = new QPushButton(QStringLiteral("导出"));
    m_exportBtn->setToolTip(QStringLiteral("导出统计数据到 CSV 文件"));
    connect(m_exportBtn, &QPushButton::clicked, this, [this]() {
        QString filePath = QFileDialog::getSaveFileName(
            this, QStringLiteral("导出性能数据"),
            QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + "_performance.csv",
            QStringLiteral("CSV 文件 (*.csv)"));
        if (!filePath.isEmpty()) {
            exportToCsv(filePath);
        }
    });
    headerLayout->addWidget(m_exportBtn);

    layout->addLayout(headerLayout);

    // 整体耗时
    m_flowTimeLabel = new QLabel(QStringLiteral("流程总耗时: --"));
    m_flowTimeLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #666;"));
    layout->addWidget(m_flowTimeLabel);

    // 统计表格
    m_table = new QTableWidget();
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("算子名称"),
        QStringLiteral("类型"),
        QStringLiteral("执行次数"),
        QStringLiteral("总耗时(ms)"),
        QStringLiteral("平均耗时(ms)"),
        QStringLiteral("最小耗时(ms)"),
        QStringLiteral("最大耗时(ms)")
    });

    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->setSelectionBehavior(QTableWidget::SelectRows);
    m_table->setSelectionMode(QTableWidget::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->setSortingEnabled(true);

    // 点击行时发出信号
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int column) {
        Q_UNUSED(column);
        QTableWidgetItem *item = m_table->item(row, 0);
        if (item) {
            NodeBase *node = reinterpret_cast<NodeBase*>(item->data(Qt::UserRole).toULongLong());
            if (node) {
                emit nodeClicked(node);
            }
        }
    });

    layout->addWidget(m_table);

    // 汇总信息
    m_summaryLabel = new QLabel(QStringLiteral("共 0 个算子，总执行次数: 0"));
    m_summaryLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #888;"));
    layout->addWidget(m_summaryLabel);
}

void PerformancePanel::onNodeExecutionTime(NodeBase *node, qint64 elapsedMs)
{
    if (!node) return;

    NodeStats &stats = m_nodeStats[node];
    stats.nodeName = node->name();
    stats.nodeType = QString::number(static_cast<int>(node->type()));
    stats.executionCount++;
    stats.totalTime += elapsedMs;
    stats.minTime = qMin(stats.minTime, elapsedMs);
    stats.maxTime = qMax(stats.maxTime, elapsedMs);
    stats.lastTime = elapsedMs;

    updateTable();
    updateSummary();
}

void PerformancePanel::onFlowExecutionTime(qint64 totalMs)
{
    m_flowTimes.append({QDateTime::currentMSecsSinceEpoch(), totalMs});
    m_flowTimeLabel->setText(QStringLiteral("流程总耗时: %1 ms").arg(totalMs));
}

void PerformancePanel::clearStats()
{
    m_nodeStats.clear();
    m_flowTimes.clear();
    m_table->setRowCount(0);
    m_flowTimeLabel->setText(QStringLiteral("流程总耗时: --"));
    m_summaryLabel->setText(QStringLiteral("共 0 个算子，总执行次数: 0"));
}

void PerformancePanel::updateTable()
{
    m_table->setSortingEnabled(false);

    // 保存当前选中行
    int selectedRow = -1;
    QList<QTableWidgetItem*> selectedItems = m_table->selectedItems();
    if (!selectedItems.isEmpty()) {
        selectedRow = selectedItems.first()->row();
    }

    m_table->setRowCount(m_nodeStats.size());

    int row = 0;
    for (auto it = m_nodeStats.begin(); it != m_nodeStats.end(); ++it, ++row) {
        NodeBase *node = it.key();
        const NodeStats &stats = it.value();

        auto *nameItem = new QTableWidgetItem(stats.nodeName);
        nameItem->setData(Qt::UserRole, reinterpret_cast<qulonglong>(node));
        m_table->setItem(row, 0, nameItem);

        m_table->setItem(row, 1, new QTableWidgetItem(stats.nodeType));
        m_table->setItem(row, 2, new QTableWidgetItem(QString::number(stats.executionCount)));
        m_table->setItem(row, 3, new QTableWidgetItem(QString::number(stats.totalTime)));
        m_table->setItem(row, 4, new QTableWidgetItem(QString::number(stats.averageTime(), 'f', 2)));
        m_table->setItem(row, 5, new QTableWidgetItem(QString::number(stats.minTime)));
        m_table->setItem(row, 6, new QTableWidgetItem(QString::number(stats.maxTime)));

        // 高亮耗时较长的算子（平均耗时 > 100ms）
        if (stats.averageTime() > 100) {
            for (int col = 0; col < m_table->columnCount(); ++col) {
                QTableWidgetItem *item = m_table->item(row, col);
                if (item) {
                    item->setBackground(QColor(255, 200, 200));
                }
            }
        }
    }

    // 恢复选中行
    if (selectedRow >= 0 && selectedRow < m_table->rowCount()) {
        m_table->selectRow(selectedRow);
    }

    m_table->setSortingEnabled(true);
}

void PerformancePanel::updateSummary()
{
    int totalNodes = m_nodeStats.size();
    int totalExecutions = 0;
    qint64 totalTime = 0;

    for (const auto &stats : m_nodeStats) {
        totalExecutions += stats.executionCount;
        totalTime += stats.totalTime;
    }

    m_summaryLabel->setText(
        QStringLiteral("共 %1 个算子，总执行次数: %2，总耗时: %3 ms")
            .arg(totalNodes)
            .arg(totalExecutions)
            .arg(totalTime));
}

void PerformancePanel::exportToCsv(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"),
                           QStringLiteral("无法打开文件: %1").arg(filePath));
        return;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);

    // 写入 BOM
    stream << "\xEF\xBB\xBF";

    // 写入表头
    stream << QStringLiteral("算子名称,类型,执行次数,总耗时(ms),平均耗时(ms),最小耗时(ms),最大耗时(ms)\n");

    // 写入数据
    for (auto it = m_nodeStats.begin(); it != m_nodeStats.end(); ++it) {
        const NodeStats &stats = it.value();
        stream << stats.nodeName << ","
               << stats.nodeType << ","
               << stats.executionCount << ","
               << stats.totalTime << ","
               << QString::number(stats.averageTime(), 'f', 2) << ","
               << stats.minTime << ","
               << stats.maxTime << "\n";
    }

    // 写入汇总
    stream << "\n";
    stream << QStringLiteral("流程执行记录\n");
    stream << QStringLiteral("时间戳,耗时(ms)\n");
    for (const auto &record : m_flowTimes) {
        stream << QDateTime::fromMSecsSinceEpoch(record.first).toString("yyyy-MM-dd hh:mm:ss.zzz")
               << "," << record.second << "\n";
    }

    file.close();
    QMessageBox::information(this, QStringLiteral("导出成功"),
                           QStringLiteral("性能数据已导出到: %1").arg(filePath));
}
