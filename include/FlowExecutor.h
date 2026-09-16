#pragma once

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QMap>
#include <QQueue>
#include <QSharedPointer>
#include <QSet>
#include <QList>
#include <QElapsedTimer>
#include <atomic>
#include <algorithm>
#include <halconcpp/HalconCpp.h>
#include "FlowRuntimeStats.h"

class FlowScene;
class NodeBase;
class DataObject;

namespace MyProject {
class Connection;
}

using DataObjectPtr = QSharedPointer<DataObject>;

enum class ExecutionState {
    Idle,
    Running,
    Paused,
    Stopped,
    Error
};

/// 流程运行模式
enum class FlowMode {
    Continuous      = 0,  /// 连续模式：自动周期循环执行
    SoftwareTrigger = 1,  /// 软触发模式：需手动/通讯触发执行一次
    HardwareTrigger = 2   /// 硬触发模式：仅相机触发源生效时执行一次
};

class FlowExecutor : public QThread
{
    Q_OBJECT

public:
    explicit FlowExecutor(QObject *parent = nullptr);
    ~FlowExecutor();

    void setFlowScene(FlowScene *scene);
    void startExecution();
    void pauseExecution();
    void resumeExecution();
    /// 单步执行：进入步进模式并推进一个节点（未运行时从首个节点开始）
    void stepExecution();
    void stopExecution();
    ExecutionState getState() const;

    /// 流程运行模式
    void setFlowMode(FlowMode mode);
    FlowMode getFlowMode() const;

    /// 失败中断策略：节点执行失败时是否停止整个流程（默认 true，对标 VisionMaster）
    void setStopOnFailure(bool on) { m_stopOnFailure = on; }
    bool stopOnFailure() const { return m_stopOnFailure; }

    /// 流程名称（供全局触发识别）
    void setFlowName(const QString &name) { m_flowName = name; }
    QString flowName() const { return m_flowName; }

    /// 是否允许被外部触发（通讯/字符串/事件/手动）启动
    /// 硬触发模式下仅允许相机硬件触发帧门控执行，禁止外部触发
    bool canTriggerFromExternal() const
    {
        return m_flowMode != FlowMode::HardwareTrigger;
    }

    /// 返回当前绑定的流程场景
    FlowScene *flowScene() const { return m_scene; }

    /// 返回全局唯一 FlowExecutor 实例（供各算子检查运行状态）
    static FlowExecutor *current();

    /// 可取消等待：等待 ms 毫秒，但若流程被停止/暂停会立即唤醒返回（供延时节点等阻塞算子取消等待，E4）
    bool interruptibleSleep(int ms);
    /// 连续/硬触发循环节拍间隔（ms），0 表示无额外限速（按相机/触发事件驱动，E6）
    void setLoopIntervalMs(int ms) { m_loopIntervalMs = qMax(0, ms); }

    // ---- 运行期统计（现场长跑诊断：轮次/耗时/失败/句柄/内存）----
    /// 取统计快照（线程安全；返回前会采样一次进程句柄数/内存，故资源字段为查询时刻值）
    FlowRuntimeStats runtimeStats() const;
    /// 清零统计（例如开始长跑观测前调用）
    void resetRuntimeStats();
    /// 统计日志间隔（ms），0=不输出；默认 60000（首轮立即输出一条便于确认埋点生效）
    void setStatsLogIntervalMs(int ms);

    /// 执行从源节点到 endNode（含）的上游链路（空闲时同步执行，供右键调试）
    void executeUpTo(NodeBase *endNode);
    /// 执行从 startNode（含）到末端的下游链路
    void executeFrom(NodeBase *startNode);

    /// 作废 node 及其**传递下游**的缓存（输出端口 + m_nodeData + 输出变量表）。
    /// 参数改动后只重算下游时必须先调用：否则下游会读到上一轮的旧值（与 E2/P2 同类问题）。
    /// 仅应在流程空闲（Idle/Stopped）时调用。
    void invalidateDownstreamOf(NodeBase *node);

signals:
    void executionStarted();
    void executionPaused();
    void executionResumed();
    void executionStopped();
    void executionError(const QString &error);
    void nodeExecuted(NodeBase *node, bool success);
    void executionFinished();
    void imageReady(NodeBase *node, const HalconCpp::HImage &image);
    /// 任意节点有图像输出时发出（供运行界面按节点名推送；imageReady 仍保持末端语义）
    void imageAvailable(NodeBase *node, const HalconCpp::HImage &image);
    /// 流程运行模式变更（int 避免 Qt MOC 对 enum class 推导问题）
    void flowModeChanged(int mode);
    /// 单算子执行耗时信号（供性能面板接收）
    void nodeExecutionTime(NodeBase *node, qint64 elapsedMs);
    /// 整体流程执行耗时信号
    void flowExecutionTime(qint64 totalMs);

protected:
    void run() override;

public:
    void propagateData(NodeBase *node);

private slots:
    void markGraphStructureDirty();

private:
    void executeNode(NodeBase *node, bool isLastNode = false);
    /// 最近一次节点执行是否成功（供主循环失败中断判断）
    bool m_lastNodeSuccess = true;
    /// 收集节点输出变量（供后级参数引用 {模块号.参数名}）
    void collectNodeOutputVars(NodeBase *node);
    /// 替换字符串中的 {模块号} / {模块号.参数名} 引用
    QString resolveParamRefs(const QString &raw) const;
    /// 循环执行：由 LoopNode 统一调度全部迭代（1..loopCount），主遍历已跳过循环体节点（P3）
    void executeLoop(NodeBase *loopNode, int loopCount);
    /// 识别循环体节点（按缓存拓扑序）
    QList<NodeBase *> collectLoopBody(NodeBase *loopNode) const;
    void resetState();
    /// 记录一个节点被跳过（未激活分支 / 循环体由外层调度）
    void recordNodeSkipped(NodeBase *node);
    /// 轮次结束：累计轮次统计，并按间隔采样进程资源 + 输出统计日志
    void recordRoundFinished(qint64 roundMs);
    void disconnectFromScene();
    void connectToScene(FlowScene *scene);
    /// 从场景构建「目标节点 -> 入边」索引（配合跨 run 缓存，图未变则跳过）
    void rebuildIncomingIndex(FlowScene *scene);
    /// 执行成功后按分支激活下游节点（条件节点仅激活被选中分支）
    void activateDownstream(NodeBase *node);
    /// 在已持有 m_graphCacheMutex 时调用
    bool cacheMatchesScene(const QList<NodeBase *> &nodes) const;
    /// 拓扑排序；若存在环返回 false，`outSorted` 为空（依赖已构建的 m_incoming）
    bool topologicalSort(const QList<NodeBase *> &nodes, QList<NodeBase *> &outSorted);
    bool visit(NodeBase *node, QSet<NodeBase *> &visited, QSet<NodeBase *> &tempMarked, QList<NodeBase *> &sortedNodes);

private:
    FlowScene *m_scene;
    ExecutionState m_state;
    FlowMode m_flowMode;           /// 当前流程运行模式
    bool m_stopOnFailure = true;   /// 节点失败时停止流程（对标 VisionMaster 默认行为）
    bool m_stepMode = false;       /// 单步执行模式（每执行一个节点后暂停）
    int m_loopIntervalMs = 0;      /// 连续/硬触发循环节拍间隔（ms），0=无额外限速（E6）
    /// 运行期统计（跨轮累计）
    FlowRuntimeStats m_stats;
    mutable QMutex m_statsMutex;      /// 统计读写锁（执行线程写 / 界面线程读）
    QElapsedTimer m_statsLogTimer;    /// 统计日志节流
    int m_statsLogIntervalMs = 60000; /// 统计日志间隔（ms），0=不输出
    bool m_roundHadFailure = false;   /// 本轮是否出现失败节点（仅执行线程访问）
    QString m_flowName;            /// 流程名称
    mutable QMutex m_mutex;
    QWaitCondition m_waitCondition;
    QMap<NodeBase*, QMap<int, DataObjectPtr>> m_nodeData;
    QQueue<NodeBase*> m_executionQueue;
    mutable QMutex m_graphCacheMutex;
    bool m_graphStructureDirty {true};
    QList<NodeBase *> m_cachedSortedNodes;
    QHash<NodeBase *, QList<MyProject::Connection *>> m_incoming;
    QHash<NodeBase *, QList<MyProject::Connection *>> m_outgoing;  /// 源节点 -> 出边（分支激活用）
    QSet<NodeBase *> m_activeNodes;                                /// 本轮执行激活集合（条件分支）
    /// 模块号 -> 输出变量表（供后级参数引用 {模块号.参数名}，跨轮保留最近值）
    QHash<int, QHash<QString, QVariant>> m_nodeOutputVars;
    /// 循环体节点集合（由 LoopNode 统一调度，主遍历跳过，P3）
    QSet<NodeBase *> m_loopBodyNodes;

    /// current() 可能被运行线程/界面线程并发读取，用原子变量避免数据竞争
    static std::atomic<FlowExecutor *> s_currentInstance;
};
