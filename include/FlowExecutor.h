#pragma once

#include "InspectionRecord.h"   // 待落库的结果缓冲按值存放（瘦头：不再把 QtSql 牵进执行器）

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QMap>
#include <QQueue>
#include <QSharedPointer>
#include <QSet>
#include <QList>
#include <QPair>   // m_pendingCachePurges 的元素类型（不依赖其它头间接引入）
#include <QElapsedTimer>
#include <atomic>
#include <algorithm>
#include <halconcpp/HalconCpp.h>
#include "FlowRuntimeStats.h"

class FlowScene;
class NodeBase;
class DataObject;
class SubFlowNode;

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

    /// S9：外部触发受理。空闲→立即起一轮；忙（运行/暂停）→有界排队补跑（上限
    /// kMaxPendingExternalRounds）；超界→丢最旧一笔并累计 droppedExternalRounds（"丢"可见，不再静默）。
    /// 返回 true = 这一笔会被执行（已起或已排队）；false = 超界丢弃。
    /// 说明：只用于"外部触发"（通讯事件/字符串/硬触发帧），界面按钮仍走 startExecution() 原语义。
    bool requestExternalRound();
    /// 待补跑轮数（界面/统计观测用）
    int pendingExternalRounds() const;
    /// 累计因超出排队上限而丢弃的触发数
    quint64 droppedExternalRounds() const;
    void pauseExecution();
    void resumeExecution();
    /// 单步执行：进入步进模式并推进一个节点（未运行时从首个节点开始）
    void stepExecution();
    /// 退出单步模式（点"开始执行/继续"或切换运行模式时调用；单步入口会重新置位）
    void exitStepMode();
    void stopExecution();
    ExecutionState getState() const;

    /// 图当前是否允许编辑（S1 / Phase B 小步）：只有"执行线程真在跑"或"暂停请求已受理但 worker 尚未
    /// 停进等待点"才禁止；Idle/Stopped 与"已停稳的 Paused"都允许（暂停中改图，恢复后下一轮生效）。
    bool allowsGraphEditing() const;

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
    /// **诊断**：执行线程当前所处阶段（含正在执行的节点模块号），供定位"worker 不退出"类挂起。
    /// 由执行线程在关键点写入两个原子变量、任意线程可读；不参与任何业务逻辑，故可在挂起时安全轮询。
    QString workerPhaseName() const;
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

    /// S1 Stage 1b 第 2 步：删节点时**只登记**待清理的执行缓存条目，**不取图锁**。
    /// 为什么不在 GUI 侧直接清：该槽由 FlowScene::removeNode 同步发出，若在槽内取 m_graphCacheMutex，
    /// 会与执行线程"图锁内再触碰节点/场景"的既有路径构成**锁环**（Stage 1b 首次尝试即因此挂起）。
    /// 改为"登记 + 执行线程安全点消费"后，GUI 侧只取叶子锁 m_purgeMutex，环从结构上不存在。
    /// 注意：登记的裸指针**只作 map 键使用**（节点可能已被立即析构，键比较不触内存，不得解除引用）。
    void requestNodeCachePurge(NodeBase *node);

signals:
    void executionStarted();
    void executionPaused();
    void executionResumed();
    /// 执行线程真正停进等待点（暂停请求已受理，且当前节点已跑完）——即"图已停稳、可安全改图"。
    /// 与 executionPaused 的区别：后者在点暂停的瞬间即发（当前节点可能仍在跑），不能作为改图依据。
    void executionParked();
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
    /// 节点执行后的输出变量快照（供结果数据表；跨线程排队传递，UI 侧无需加锁读缓存）
    void nodeOutputsUpdated(NodeBase *node, bool success, qint64 elapsedMs, const QVariantMap &vars);
    /// 节点被跳过（未激活分支 / 循环体由 LoopNode 调度）——三态可视化用，
    /// 结果面板据此把该模块标为"跳过"（灰色），与"失败"区分开
    void nodeSkipped(NodeBase *node, const QString &reason);
    /// 整体流程执行耗时信号
    void flowExecutionTime(qint64 totalMs);

protected:
    void run() override;

public:
    void propagateData(NodeBase *node);

    /// FR15.10 运行期子流程：同线程内联执行 caller 引用的子流程定义
    /// （由 SubFlowNode::process 在执行线程调用）。入口吃 caller 的输入数据、
    /// 出口结果回写 caller 的输出端口 0；成员失败/定义缺失/递归返回 false，
    /// 并 emit executionError 带具体原因（哪个子节点失败/递归链/未定义）。
    bool executeSubFlow(SubFlowNode *caller);

private slots:
    void markGraphStructureDirty();
    /// 节点从场景移除（删除/撤销/清空）时**登记**它在执行缓存里的待清理条目，
    /// 避免"删 A 后新建 B 复用同指针地址/同模块号"让 B 继承 A 的输出变量表（S4）。
    /// S1 Stage 1b 第 2 步：本槽**不取图锁**（只取叶子锁登记），实际清理由执行线程安全点消费
    /// （见 requestNodeCachePurge / applyPendingCachePurgesLocked）。
    void onSceneNodeRemoved(NodeBase *node);
    /// 消费待清理条目（**须在已持 m_graphCacheMutex 时调用**）：先短取叶子锁 m_purgeMutex 摘出列表，
    /// 再在图锁内 remove；两把锁不嵌套，故不改变既有"graph → m_mutex"锁序。
    void applyPendingCachePurgesLocked();

private:
    void executeNode(NodeBase *node, bool isLastNode = false);
    /// 在等待点调用（须持 m_mutex）：置位并上报 executionParked 后阻塞等待恢复；恢复后清位。
    /// 所有"可作为安全改图点"的等待点都必须走这里，否则 executionParked 会名不副实。
    void parkWhilePausedLocked();
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
    /// FR15.10 子流程：轮首收集"被引用子流程定义"的成员/边界（同循环体 P3 模式；
    /// worker 线程私有，仅执行线程访问。须在 m_loopBodyNodes 填充之后调用）
    void collectSubFlowBodies(FlowScene *scene, const QList<NodeBase *> &liveNodes);
    void resetState();
    /// 记录一个节点被跳过（未激活分支 / 循环体由外层调度），并对外发 nodeSkipped
    void recordNodeSkipped(NodeBase *node, const QString &reason);
    /// 轮次结束：累计轮次统计，并按间隔采样进程资源 + 输出统计日志
    void recordRoundFinished(qint64 roundMs);
    void disconnectFromScene();
    void connectToScene(FlowScene *scene);
    /// 从**图快照**构建「目标节点 -> 入边」索引（配合跨 run 缓存，图未变则跳过）。
    /// S1：改为吃快照的活节点表与连边表，执行期不再访问 FlowScene 的容器。
    void rebuildIncomingIndex(const QList<NodeBase *> &liveNodes,
                              const QList<MyProject::Connection *> &connections);
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

    // ---- worker 阶段观测（正式能力）：定位"worker 不退出"类挂起时可在挂起态安全轮询。
    // 执行线程在关键点写、任意线程经 workerPhaseName() 读；relaxed 原子，不参与任何业务逻辑。
    std::atomic<int> m_diagPhase {-1};    /// 当前阶段编号（见 kWorkerPhaseNames）
    std::atomic<int> m_diagNodeId {-1};   /// 正在执行的节点模块号（无则 -1）

    // ---- S9 外部触发排队（有界 FIFO；忙时不再静默丢弃）----
    static constexpr int kMaxPendingExternalRounds = 3;  /// 待补跑上限：有界，避免越跑越落后腿
    int m_pendingExternalRounds = 0;      /// 待补跑轮数（m_mutex 保护）
    quint64 m_droppedExternalRounds = 0;  /// 累计超界丢弃数（m_mutex 保护）
    QString m_flowName;            /// 流程名称
    mutable QMutex m_mutex;
    QWaitCondition m_waitCondition;
    bool m_workerParked = false;   /// worker 是否真停进等待点（m_mutex 保护；executionParked 的信号源）

    QMap<NodeBase*, QMap<int, DataObjectPtr>> m_nodeData;
    /// 本轮待落库的检测结果（仅执行线程访问）：按轮缓冲，轮末用**单个事务**批量提交。
    /// 连续模式下"每节点一次自动提交"是主要固定开销（40 节点 = 40 次提交/轮）。
    QList<InspectionRecord> m_pendingResults;
    /// 输出仍然有效的节点：局部执行（executeUpTo/executeFrom）时，"输出可复用"的节点
    /// （见 NodeBase::reusesCachedOutput）若仍在此集合中，就不必再跑一遍（例如重新读图）。
    /// 参数改动会经 invalidateDownstreamOf 把相关节点移出该集合。
    QHash<NodeBase*, bool> m_validOutputs;
    QQueue<NodeBase*> m_executionQueue;
    /// 执行器侧"图/执行缓存"的统一锁（S1 Stage 1：三类缓存的**全部**访问入锁）。
    /// 纪律（定稿）：
    ///  · 保护对象：m_nodeData / m_validOutputs / m_nodeOutputVars，以及图索引缓存
    ///    （m_graphStructureDirty / m_cachedSortedNodes / m_incoming / m_outgoing）；
    ///  · **锁序**：graphCacheMutex → m_mutex（setFlowScene / invalidateDownstreamOf 即此序），
    ///    严禁反向获取；跨锁调用只允许"graph 锁内取 m_mutex"，不得反过来；
    ///  · **粒度**：只在函数内开短临界区，**绝不跨越**"会再触碰上述缓存的调用"
    ///    （executeNode → propagateData / collectNodeOutputVars 即此类，故各自在自己函数内加锁，
    ///     不使用递归锁）；
    ///  · 不在本锁范围：m_pendingResults（仅执行线程）、m_activeNodes / m_loopBodyNodes
    ///    （轮次内执行线程私有；轮首构建时持本锁）、端口数据（节点自身的锁）。
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
    // ---- FR15.10 运行期子流程（worker 线程私有，语义同 m_loopBodyNodes）----
    QSet<NodeBase *> m_subFlowBodyNodes;                          /// 全部被引用定义的成员（主遍历跳过）
    QHash<QString, QList<NodeBase *>> m_subFlowMembers;           /// 定义名 → 拓扑序成员
    QHash<QString, QPair<NodeBase *, NodeBase *>> m_subFlowIOs;   /// 定义名 → 入口/出口
    QList<QString> m_subFlowCallStack;                            /// 执行中的定义名（递归保护）
    static constexpr int kMaxSubFlowDepth = 8;                    /// 嵌套深度上限
    /// 待清理的执行缓存条目（S1 Stage 1b 第 2 步）：QPair<节点指针, 登记时的模块号>。
    /// 指针**只作 map 键**（登记后节点可能已被立即析构，故绝不解除引用；模块号在登记时取好）。
    /// m_purgeMutex 是**叶子锁**：只允许"graph 锁 → purge 锁"方向，持有它时不得再取任何锁。
    QMutex m_purgeMutex;
    QList<QPair<NodeBase *, int>> m_pendingCachePurges;

    /// current() 可能被运行线程/界面线程并发读取，用原子变量避免数据竞争
    static std::atomic<FlowExecutor *> s_currentInstance;
};
