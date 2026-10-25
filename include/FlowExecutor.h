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
#include <QPair>   // m_subFlowIOs 的元素类型（不依赖其它头间接引入）
#include <QElapsedTimer>
#include <atomic>
#include <functional>
#include <algorithm>
#include <memory>
#include <halconcpp/HalconCpp.h>
#include "FlowExecutionEnums.h"   // 执行状态与运行模式（切片 C 立项书 P2a 期：自此头拆出的轻头件）
#include "FlowRunContext.h"       // 运行上下文块（切片 C 立项书 P2a 期新增件，P2b 期起承载状态与模式）
#include "FlowSceneAliveBlock.h"   // 切片 C 段二：场景存活控制块（executor 借当值场景自代的块，shared_ptr 持有）
#include "SubFlowDefs.h"   // FR15.10 运行期子流程定义（值类型；段二：collectSubFlowBodies 改吃定义副本）
#include "FlowRuntimeStats.h"
#include "FlowCachePurgeLedger.h"   // 缓存清理登记账（切片 A 抽出：账叶锁与登记列随账走）

class FlowScene;
class NodeBase;
class DataObject;
class SubFlowNode;

namespace MyProject {
class Connection;
}

using DataObjectPtr = QSharedPointer<DataObject>;

class FlowExecutor : public QThread
{
    Q_OBJECT

public:
    /// 构造：**不接受父对象**（U-203 期：删除权归令牌，父对象是第二条删除路径）。
    /// 析构在 C++ 里是隐式 public 的，这里刻意把它收进 private 段——直接 delete 本体的写法
    /// 因此**编译期就写不出来**，不是靠约定。
    FlowExecutor();
    void setFlowScene(FlowScene *scene);
    /// 词条切片 C P3c 期（持有面迁移）：登记本体的**生命周期令牌**。
    /// 只存**弱引用**——令牌拥有本体，令牌若被强持在执行器里就是环（本体↔令牌互等，析构永不到）
    /// ⇒ 执行器借令牌时用 lock()，令牌没了就借不到（此时本执行器已无持有者，是正常的收尾态）。
    void setLifetimeToken(const std::shared_ptr<class FlowExecutorToken> &token);
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
    /// 停止并**在 timeoutMs 内等待工作线程真正退出**（析构与调用方销毁共用的唯一实现）。
    /// 返回 true = 线程已退出，可以安全销毁本对象；
    /// 返回 false = 线程还在跑，此时销毁本对象会让 run() 访问已释放内存（UB），
    /// 调用方必须改走"放弃销毁 / 挂 finished 自删"的泄漏保护路径（见 FlowExecutorToken 的收尾）。
    bool joinForDestroy(int timeoutMs);
    ExecutionState getState() const;

    /// P3d 期（终局撤除）：令牌收尾超时后把本体置入**可观测终态**——进程不中止。
    /// 置终态位（此后 getState 恒 Error、图编辑恒锁）＋ 写 Error ＋ 发 executionAbandoned。
    /// 本体的回收仍由令牌挂的 finished 自删路径接管（线程真退出后才删）。
    void markExecutionAbandoned(const QString &reason);
    bool isAbandoned() const { return m_abandoned.load(std::memory_order_acquire); }

    /// 应用层秩序收尾钩子（178 行软化；U-203 起挂在**令牌的放弃路径**上）：软失败时给 app
    /// 一格收好方案/冲日志，默认空 = 无操作。payload 是终局原因串。
    /// 为什么换挂点：硬中止已随析构的等待一并撤除，而「送别一格」真正该发生的地方
    /// 是放弃路径——进程继续，但这个执行器不会再被接管，方案与日志得先收好。
    static void setOrderlyStopHook(std::function<void(const QString &)> hook);
    static void invokeOrderlyStopHook(const QString &why);

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
        // 运行模式在块内（原子读；块是共享件，读侧不取主体锁）
        return m_runContext->flowMode() != FlowMode::HardwareTrigger;
    }

    /// 返回当前绑定的流程场景
    FlowScene *flowScene() const { return m_scene; }

    /// 返回全局唯一 FlowExecutor 实例（供各算子检查运行状态）
    static FlowExecutor *current();
    /// 返回全局「最近一个执行器」的运行上下文块（切片 C 立项书 P2c 期：算子侧兜底读面走块，
    /// 不再经执行器裸指针）——返回**一份强引用**（调用期间块必存活，读侧无悬垂）。
    static std::shared_ptr<FlowRunContext> currentContext();

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
    /// 只读查询：该节点当前是否带「输出可复用」标记（`m_validOutputs`）。
    /// 复用分支回退后该位无读者，E2 的不变量（清空输出即撤销标记）只能经此视图钉住。
    bool isOutputMarkedValid(NodeBase *node) const;
    /// 只读查询：慢路径图缓存清空计数（段四·U-217）——同场景幂等快速路不计入。
    /// 不参与任何执行决策；只供回归钉「同场景重绑不走慢路径」（切页对运行中执行器的竞态源）。
    quint64 graphCacheClearCount() const;
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
    /// 登记账（叶子锁与登记列表）自切片 A 迁入 FlowCachePurgeLedger，锁纪律钉在该头注释；
    /// 此处仍担 moduleId 的"此刻"取值责任（入账动作归账簿）。
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
    /// P3d 期（终局撤除）：令牌收尾超时——已放弃强杀，执行器进入可观测终态，进程继续。
    /// **不要**用本信号替代 executionStopped：后者会解锁图，而此刻工作线程可能仍在跑。
    void executionAbandoned(const QString &reason);
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
    /// 消费待清理条目（**须在已持 m_graphCacheMutex 时调用**）：摘账走 FlowCachePurgeLedger::
    /// takePending（自带叶子锁短窗），随后 remove 三族在图锁内做。
    void applyPendingCachePurgesLocked();

protected:
    /// 析构收进 protected（U-203 期）：本体的删除权**只经 FlowExecutorToken**。
    /// 理由：析构里的「两段有界等待 + 硬中止」本来是给「绕过令牌直接 delete」兜底的——
    /// 令牌协议下这条路径不可达（令牌只在 join 成功后才 delete 本体），兜底本身就成了
    /// 全树唯一的硬中止残留。把删除权收进类型系统，兜底与中止一并撤除：超时不再中止进程，
    /// 而是走令牌的放弃路径（可观测终态 ＋ 进程继续 ＋ 线程退出后自删）。
    /// 为什么是 protected 而不是 private：private 会让**派生类无法定义**（MSVC C2282：
    /// 派生析构覆盖了不可达的基类析构），而测试侧需要「自造卡死线程」这一机制来钉
    /// joinForDestroy 的超时判定。本体类型本身仍然**从外部不可析构**（直接 delete 编译期就
    /// 写不出来，见测试侧 is_destructible 契约腿）；派生类那条孔洞由「本仓生产侧不存在
    /// 执行器派生类」兜住（普查段点名）。
    ~FlowExecutor();
    friend class FlowExecutorToken;

private:
    void executeNode(NodeBase *node, bool isLastNode = false);
    /// （178 行）应用层收尾钩子的存放位（终局中止前的送别窗，见公共区注）
    static std::function<void(const QString &)> s_orderlyStopHook;
    /// 在等待点调用（须持 m_mutex）：置位并上报 executionParked 后阻塞等待恢复；恢复后清位。
    /// 所有"可作为安全改图点"的等待点都必须走这里，否则 executionParked 会名不副实。
    void parkWhilePausedLocked();
    /// 写运行态（写块）并唤醒等待者（执行器侧的等待点与块内取消等待都走这一条路径）
    void setRunState(ExecutionState state);
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
    void collectSubFlowBodies(const QList<SubFlowDef> &defs, const QList<NodeBase *> &liveNodes);
    /// P2-5：executeNode 异常出口的收口（清三类执行缓存 + 置轮失败位），
    /// 与 try 内失败分支同口径。仅执行线程调用。
    void clearNodeCachesOnException(NodeBase *node);
    /// 把按轮缓冲的节点记录批量落库（单个事务）。轮末与同步局部执行都用它（P2-B）。
    void flushPendingResults();
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
    /// 切片 C 段二（立项书 §13.1）：场景存活控制块——随 setFlowScene 取**当值场景自代**的块
    /// （每场景一块；块随执行器的 shared_ptr 拷贝活着 ⇒ 场景先亡后墓碑与运行态仍可读，
    /// 执行线程在飞快照的借面在块读锁内，析构的 markGone 写锁被其互斥）。
    /// GUI 线程快路径（flowScene() 访问器与断接面）仍走 m_scene——其生死由既有解除绑定合同管。
    std::shared_ptr<FlowSceneAliveBlock> m_aliveBlock;
    /// 运行上下文块（切片 C 立项书 P2b 期：运行态与运行模式已改走此块）。
    /// 块是**非对象**的共享件（不持任何裸指针、其互斥量为叶子）；算子读面在 P2c 期改持它。
    std::shared_ptr<FlowRunContext> m_runContext;
    /// 词条切片 C P3c 期：本体的生命周期令牌（**弱引用**，避免本体↔令牌互持成环）。
    /// 持有面（触发面绑定、节点残余面）同样只持弱引用；取用方先 lock 出**强**副本再调方法
    /// ⇒ 调用期间本体必存活，而收尾点仍落在界面侧释放最后一个强令牌处。
    std::weak_ptr<class FlowExecutorToken> m_lifetimeToken;
    /// P3d 期（终局撤除）：收尾超时后的终态位。一旦置位不再清除——本体正在放弃路径上自删。
    std::atomic<bool> m_abandoned{false};
    bool m_stopOnFailure = true;   /// 节点失败时停止流程（对标 VisionMaster 默认行为）
    bool m_stepMode = false;       /// 单步执行模式（每执行一个节点后暂停）
    int m_loopIntervalMs = 0;      /// 连续/硬触发循环节拍间隔（ms），0=无额外限速（E6）
    /// 运行期统计（跨轮累计）
    FlowRuntimeStats m_stats;
    mutable QMutex m_statsMutex;      /// 统计读写锁（执行线程写 / 界面线程读）
    QElapsedTimer m_statsLogTimer;    /// 统计日志节流
    int m_statsLogIntervalMs = 60000; /// 统计日志间隔（ms），0=不输出
    bool m_roundHadFailure = false;   /// 本轮是否出现失败节点（仅执行线程访问）
    bool m_roundAborted = false;      /// 本轮是否被中途停止（仅执行线程访问）：半截流程不得计入整轮 OK
    /// 人为停止请求（界面线程置、执行线程读）：轮末据此判"中止"，而不是看 Stopped 状态
    /// ——失败停止同样是 Stopped，且被停止的算子可能自判失败（2026-10-25 复核 P2-1 残留）
    std::atomic<bool> m_userStopRequested{false};
    /// 本次 executeNode 调用是否"因人为中止而收口"（worker 线程私有）：中止不是成功，
    /// 调用点不得落 passed=1 的节点行（2026-10-25 复核 P3-1）
    bool m_lastNodeAborted = false;
    /// 本次 executeNode 是否已写过节点记录（worker 线程私有）：异常若发生在正常落库块之后，
    /// 不得再补一条失败行 —— 否则同轮同节点出现 OK+NG 两条（2026-10-25 复核 P3-2）
    bool m_nodeRecordWritten = false;

    // ---- worker 阶段观测（正式能力）：定位"worker 不退出"类挂起时可在挂起态安全轮询。
    // 执行线程在关键点写、任意线程经 workerPhaseName() 读；relaxed 原子，不参与任何业务逻辑。
    std::atomic<int> m_diagPhase {-1};    /// 当前阶段编号（见 kWorkerPhaseNames）
    std::atomic<int> m_diagNodeId {-1};   /// 正在执行的节点模块号（无则 -1）
    std::atomic<quint64> m_graphCacheClearCount {0};  /// 段四·U-217：慢路径缓存清空次数（只读视图的累加点）

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
    /// 缓存清理登记账与账长相随成员迁入 FlowCachePurgeLedger（切片 A）；
    /// 纪律条款（叶子无后沿、界侧不进图锁、键用即止）钉在账簿头注释。
    FlowCachePurgeLedger m_purgeLedger;

    /// current() 可能被运行线程/界面线程并发读取，用原子变量避免数据竞争
    static std::atomic<FlowExecutor *> s_currentInstance;
    /// 「最近一个执行器」的运行上下文块（P2c 期）：与 s_currentInstance 同样的两个置位点
    /// （构造与挂场景），读侧拿**强引用**（调用期间块必存活）；写读用一把互斥量串起来
    /// （块是共享引用计数对象，其引用计数增减不可与写并发）。
    static QMutex s_currentContextMutex;
    static std::shared_ptr<FlowRunContext> s_currentContext;
};
