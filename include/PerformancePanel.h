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
#include <QElapsedTimer>
#include <QPointer>

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
    /// AH3b：当前绑定的执行器（供关闭流程页时只清理即将销毁的那一枚）
    FlowExecutor *boundExecutor() const;

    /// 清空统计数据
    void clearStats();

    /// 导出统计数据到 CSV
    void exportToCsv(const QString &filePath);

public slots:
    /// AH3：按值接收单算子执行耗时，不再解引用跨线程排队的裸节点指针
    void onNodeExecutionTime(const QString &nodeName, int nodeType, int moduleId, qint64 elapsedMs);

    /// 接收整体流程执行耗时
    void onFlowExecutionTime(qint64 totalMs);

signals:
    /// 点击节点时发出（供主窗口定位到画布）
    void nodeClicked(int moduleId);   ///< AH3：点击定位用模块号，节点已亡也不会解引用

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
    QLabel *m_backendLabel = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLabel *m_flowTimeLabel = nullptr;
    QPushButton *m_clearBtn = nullptr;
    QPushButton *m_exportBtn = nullptr;

    QMap<int, NodeStats> m_nodeStats;   ///< AH3：按模块号统计，不持有裸节点指针
    QVector<QPair<qint64, qint64>> m_flowTimes;  /// <开始时间, 耗时>
    QPointer<FlowExecutor> m_boundExecutor;
    /// AG3：每节点信号都会到，但 UI 全表重建只需有限频率；统计照常累计，刷新节流。
    QElapsedTimer m_uiRefreshTimer;
    bool m_flushPending = false;   ///< AH4：把同一事件循环内的多次轮末刷新合并成一次
    /// AH3c：每次绑定递增；旧执行器已排队的信号按代次过滤，重绑后不污染新流程统计。
    quint64 m_bindGeneration = 0;
    QMetaObject::Connection m_nodeTimeConnection;
    QMetaObject::Connection m_flowTimeConnection;
    QMetaObject::Connection m_startConnection;
};
