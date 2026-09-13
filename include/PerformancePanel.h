#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMap>
#include <QVector>
#include <QPair>
#include <QTimer>

class NodeBase;
class FlowExecutor;

/// 算子执行耗时统计面板
class PerformancePanel : public QWidget
{
    Q_OBJECT

public:
    explicit PerformancePanel(QWidget *parent = nullptr);
    ~PerformancePanel() override = default;

    /// 绑定到流程执行器
    void bindExecutor(FlowExecutor *executor);

    /// 解绑执行器
    void unbindExecutor();

    /// 清空统计数据
    void clearStats();

    /// 导出统计数据到 CSV
    void exportToCsv(const QString &filePath);

public slots:
    /// 接收单算子执行耗时
    void onNodeExecutionTime(NodeBase *node, qint64 elapsedMs);

    /// 接收整体流程执行耗时
    void onFlowExecutionTime(qint64 totalMs);

signals:
    /// 点击节点时发出（供主窗口定位到画布）
    void nodeClicked(NodeBase *node);

private:
    void setupUi();
    void updateTable();
    void updateSummary();

    struct NodeStats {
        QString nodeName;
        QString nodeType;
        int executionCount = 0;
        qint64 totalTime = 0;
        qint64 minTime = INT64_MAX;
        qint64 maxTime = 0;
        qint64 lastTime = 0;

        double averageTime() const {
            return executionCount > 0 ? static_cast<double>(totalTime) / executionCount : 0.0;
        }
    };

    QTableWidget *m_table = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_flowTimeLabel = nullptr;
    QPushButton *m_clearBtn = nullptr;
    QPushButton *m_exportBtn = nullptr;

    QMap<NodeBase*, NodeStats> m_nodeStats;
    QVector<QPair<qint64, qint64>> m_flowTimes;  /// <开始时间, 耗时>
    FlowExecutor *m_boundExecutor = nullptr;
};
