#include "FlowExecutor.h"
#include "FlowScene.h"
#include "SessionManager.h"   // 173 行：闸开不发新跑的外部触发 idle 位判定
#include "NodeBase.h"
#include "HalconNode.h"
#include "ConditionalNode.h"
#include "LoopNode.h"
#include "SubFlowNode.h"   // FR15.10 运行期子流程调用点
#include "NodeResultFields.h"   // U-18：判红原因字段的唯一口径（发布侧，作废侧在 HalconNode.cpp 同一个谓词）
#include "DataObject.h"
#include "Connection.h"
#include "AppLog.h"
#include <QRegularExpression>
#include <QElapsedTimer>
#include <functional>
#include "AppDatabase.h"
#include "GlobalVariableManager.h"
#include "GlobalTriggerManager.h"
#include "CommunicationManager.h"
#include "FlowScene.h"

using namespace MyProject;

std::atomic<FlowExecutor *> FlowExecutor::s_currentInstance{nullptr};
QMutex FlowExecutor::s_currentContextMutex;
std::shared_ptr<FlowRunContext> FlowExecutor::s_currentContext;

FlowExecutor::FlowExecutor()
    : QThread(nullptr)
    , m_scene(nullptr)
    , m_runContext(std::make_shared<FlowRunContext>())
{
    s_currentInstance = this;
    {
        // 「最近一个执行器」的上下文块：与 s_currentInstance 同样的置位点（构造即置）
        QMutexLocker locker(&s_currentContextMutex);
        s_currentContext = m_runContext;
    }

    // 全局转发连线（CM dataReceived → GTM onDataReceived）已迁到 GlobalTriggerManager 构造里
    // 建立**一次**，不再寄生在"有人建过执行器"上——此前单跑触发用例 / 载入方案后无执行器的
    // 触发路径因链路未建立而整条失效（假阴性 + 负向断言假阳性）。见 GlobalTriggerManager 构造。
    // 本执行器只接自己这一份 triggerFired（按 flowName 过滤，互不干扰）。

    // 本执行器专属：外部触发 → 启动（按 flowName 过滤，互不干扰）
    connect(GlobalTriggerManager::instance(), &GlobalTriggerManager::triggerFired,
            this, [this](const QString &flowName, const QString &triggerSource) {
        Q_UNUSED(triggerSource)
        if (m_flowName == flowName && m_runContext->state() == ExecutionState::Idle) {
            // 硬触发模式禁止外部触发启动
            if (!canTriggerFromExternal()) {
                VFP_DEBUG << "Flow" << m_flowName
                          << "in hardware-trigger mode, external trigger ignored";
                return;
            }
            // 173 行拍板：闸开不发新跑——GTM 直触在空闲位起跑也是新一次发起，随闸收。
            // 引擎侧只判写闸本体不判会话（与 requestExternalRound 同口径，173 行登记）。
            if (SessionManager::instance()->writesBlocked()) {
                VFP_RUNTIME_INFO.noquote()
                    << QStringLiteral("写闸开启期间不受理外部触发的新运行（出厂口令未改，改密后恢复）");
                return;
            }
            startExecution();
        }
    });
}

/// 析构内两段有界等待（毫秒），常量本体在 FlowExecutor.h（腿座钉值可达）。
/// 一段 5s 兜底照旧；二段 15s 与令牌收尾的前置等待同量级——走到析构说明
/// 调用方已经判定线程可回收，这里只是两级兜底，不是常规出口。

std::function<void(const QString &)> FlowExecutor::s_orderlyStopHook = nullptr;

void FlowExecutor::setOrderlyStopHook(std::function<void(const QString &)> hook)
{
    s_orderlyStopHook = std::move(hook);
}

void FlowExecutor::invokeOrderlyStopHook(const QString &why)
{
    if (s_orderlyStopHook) {
        s_orderlyStopHook(why);
    }
}

bool FlowExecutor::joinForDestroy(int timeoutMs)
{
    stopExecution();
    if (!isRunning()) {
        return true;
    }
    return wait(timeoutMs);
}

FlowExecutor::~FlowExecutor()
{
    GlobalTriggerManager::instance()->unregisterExecutor(this);
    if (s_currentInstance.load() == this) {
        s_currentInstance.store(nullptr);
    }
    // U-203 期（终局撤除收尾）：本析构里原有的「两段有界等待（5s + 15s）＋ 硬中止」
    // 整段撤除。理由与新不变量：
    //   · 那段等待是给「绕过令牌直接 delete」兜底的——工作线程还活着就析构，必然
    //     use-after-free（旧实现先中止进程，是把不可逆的时序问题变成一次明确的死）。
    //   · 令牌协议（P3a/P3b）落地后，生产侧删除只经令牌，而令牌只在 join 成功后才
    //     delete 本体 ⇒ 这条兜底路径**不可达**；析构里的中止只剩「不可达兜底」这一层，
    //     而它恰好是全树唯一的硬中止残留。
    //   · 于是把删除权收进类型系统：析构私有（唯一删除者是 FlowExecutorToken）＋
    //     构造不接受父对象（父对象是第二条删除路径）⇒ 「绕过令牌直接 delete」与
    //     「栈上执行器」两种写法在**编译期**就写不出来。
    //   · 收尾语义随之归位：等待与超时处置全在令牌侧（joinForDestroy ＋ 放弃路径），
    //     超时不再中止进程，而是转入可观测终态（P3d）并让线程退出后自删。
    // 本析构因此只做「拆干净」：注销触发登记、清场景归属、清节点侧归属指针。
    disconnectFromScene();
    // S-1（第五轮审核）：本对象一出去，节点里存的归属指针必然是野指针——
    // setFlowScene(nullptr) 刻意不清（见 setFlowScene 里那段注释：节点可能转由别的执行器
    // 接管，清成空反而退回 current() 兜底），但那条理由在"执行器正在销毁"时不成立。
    // 读侧有四处（DelayNode / ScriptNode / SubFlowNode / MvsImageSourceNode），而场景通常
    // 比执行器活得久 ⇒ 这段窗口真实存在。与上面 s_currentInstance 的析构置空同一处理。
    if (FlowScene *s = flowScene()) {
        const QList<NodeBase *> liveNodes = s->nodes();
        for (NodeBase *n : liveNodes) {
            if (!n) {
                continue;
            }
            // P3c：节点侧改持**弱**令牌 ⇒ 令牌正在收尾时 lock 不到本体，不能再用
            //「ownerExecutor() == this」认身份。改用**归属块**：块是每执行器独有的共享件、
            // 节点强持，令牌收尾期间不会失效；已被别的执行器接管的节点持的是别的块，不会误清。
            if (m_runContext && n->ownerContext() == m_runContext) {
                n->setOwnerToken(nullptr);
                n->setOwnerContext(nullptr);
            }
        }
    }
}

FlowExecutor *FlowExecutor::current()
{
    return s_currentInstance;
}

std::shared_ptr<FlowRunContext> FlowExecutor::currentContext()
{
    // 读侧拿**强引用**（调用期间块必存活，读侧无悬垂）；写读用同一把互斥量串起来
    // （块是共享引用计数对象，其引用计数增减不可与写并发）。
    QMutexLocker locker(&s_currentContextMutex);
    return s_currentContext;
}

// ---- 切片 C 段二：执行线程借场景一律经存活控制块（在场借面唯一入口） ----
// 三面合同：① 售票即判走（场景亡/未绑 ⇒ 不借场景指针，票内已置空）；
// ② 票的块读锁跨快照全程 ⇒ 场景析构的 markGone 写锁被挡（析构等在飞快照完成，不并发）；
// ③ 未借到一律返回空句柄，调用方各自走显式退化路径（run 报错＋停轮；UI 路径静默返回）。
static FlowScene::GraphSnapshotGuard captureViaAliveBlock(
    const std::shared_ptr<FlowSceneAliveBlock> &block,
    QList<SubFlowDef> *outSubFlowDefs = nullptr)
{
    std::shared_ptr<FlowSceneAliveBlock> owned = block;   // 增可在任何时机（纯原子加）：
    // 借出的 shared_ptr 拷贝先到手、才碰块——GUI 线程换绑场景时成员可被整体替换，旧块随
    // 最后一个引用收核；票在手上时块必须活着（票持锁 ⇒ 块锁不得悬，否则收核即悬锁）。
    if (!owned) {
        return FlowScene::GraphSnapshotGuard();
    }
    FlowSceneAliveBlock::CaptureTicket ticket = owned->acquireForCapture();
    FlowScene *scene = ticket.scene();
    if (scene == nullptr) {
        return FlowScene::GraphSnapshotGuard();
    }
    // 二读同窗：子流程定义副本与拓扑快照的两笔场景读都放进票的块读锁窗（场所在窗内必活）。
    if (outSubFlowDefs)
        *outSubFlowDefs = scene->subFlows();
    return scene->captureGraphSnapshot();
}

void FlowExecutor::disconnectFromScene()
{
    FlowScene *s = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        s = m_scene;
    }
    if (!s) {
        return;
    }
    QObject::disconnect(s, &FlowScene::connectionAdded, this, &FlowExecutor::markGraphStructureDirty);
    QObject::disconnect(s, &FlowScene::connectionRemoved, this, &FlowExecutor::markGraphStructureDirty);
    QObject::disconnect(s, &FlowScene::nodeAdded, this, &FlowExecutor::markGraphStructureDirty);
    QObject::disconnect(s, &FlowScene::nodeRemoved, this, &FlowExecutor::markGraphStructureDirty);
    QObject::disconnect(s, &FlowScene::nodeRemoved, this, &FlowExecutor::onSceneNodeRemoved);
}

namespace {
/// 诊断阶段名（下标与 FlowExecutor::m_diagPhase 的写入点一致）
const char *const kWorkerPhaseNames[] = {
    "未设置",                 // 0
    "轮首：取快照前",          // 1
    "轮首：已取快照，进图锁前", // 2
    "图锁：等锁或锁内",        // 3
    "已排序：节点循环前",      // 4
    "节点循环中（执行节点）",  // 5
    "轮末：落库前",            // 6
    "轮末：落库后/统计前",      // 7
    "轮末：统计后/发信号前",    // 8
    "轮末：信号后（节拍/回转）", // 9
};
} // namespace

QString FlowExecutor::workerPhaseName() const
{
    const int p = m_diagPhase.load(std::memory_order_relaxed);
    const int nid = m_diagNodeId.load(std::memory_order_relaxed);
    const int count = int(sizeof(kWorkerPhaseNames) / sizeof(kWorkerPhaseNames[0]));
    if (p < 0 || p >= count) {
        return QStringLiteral("未设置");
    }
    QString s = QString::fromUtf8(kWorkerPhaseNames[p]);
    if (nid >= 0) {
        s += QStringLiteral("(节点模块 %1)").arg(nid);
    }
    return s;
}

/// 「输出可复用」标记的只读视图（E2 回归用）。
/// 该标记自增量复用分支回退后**没有任何读者**，故 E2 的不变量（清空输出即撤销标记）
/// 无法从外部行为观察到——没有本 accessor 就只能"改了但钉不住"。不参与任何执行决策。
bool FlowExecutor::isOutputMarkedValid(NodeBase *node) const
{
    QMutexLocker locker(&m_graphCacheMutex);
    return m_validOutputs.value(node, false);
}

/// 「图缓存清空次数」只读视图（段四·U-217 回归用）：慢路径每清一次 +1；
/// 同场景幂等快速路不计入。只供座腿钉「同场景重绑不走慢路径」——不参与任何执行决策。
quint64 FlowExecutor::graphCacheClearCount() const
{
    return m_graphCacheClearCount.load(std::memory_order_relaxed);
}

void FlowExecutor::onSceneNodeRemoved(NodeBase *node)
{
    // S1 Stage 1b 第 2 步定稿：本槽**只登记、不清理**。
    // 历史（留档）：本槽曾长期留空——在 GUI 线程直接改三类缓存会与执行线程的无锁写并发（堆损坏）；
    // 而"恢复立即清理"的首次尝试（Stage 1b 初版）又因 "GUI：场景锁 → 图锁" 与
    // "执行：图锁 → 节点/场景锁" 成**环**而在 ctest 下挂起，整段撤销。
    // 定稿方案 = 登记 + 执行线程安全点消费：
    //   · GUI 侧只向登记账记账（账簿内短取叶子锁，不取图锁、不触碰任何缓存）→ 环无结构位；
    //   · 执行线程在轮首 / 同步执行入口调 applyPendingCachePurgesLocked() 统一清理。
    // 覆盖率说明：登记在"删除/撤销/清空"三条路径都会走到（都经 FlowScene 发 nodeRemoved）。
    requestNodeCachePurge(node);
}

void FlowExecutor::requestNodeCachePurge(NodeBase *node)
{
    if (!node) {
        return;
    }

    // 登记时必须"此刻"取好的两样东西：
    //  · node 指针 —— 之后可能已被析构（FlowScene::removeNode 无存活快照时立即 delete），
    //    故此后按键口径入账（键比较不触内存，不解除引用）；
    //  · moduleId —— 之后无法再从对象上读（同因），而 m_nodeOutputVars 正是按模块号索引。
    const int moduleId = node->moduleId();
    m_purgeLedger.enqueuePurge(node, moduleId);
}

void FlowExecutor::applyPendingCachePurgesLocked()
{
    // 摘账走账簿（叶子锁短窗：账内 swap 摘净），remove 三族在图锁内做（调用方已持本锁）。
    const QList<QPair<NodeBase *, int>> pending = m_purgeLedger.takePending();
    if (pending.isEmpty()) {
        return;
    }

    for (const QPair<NodeBase *, int> &entry : pending) {
        // 两个维度各清一遍（S4）：指针维度（m_nodeData / m_validOutputs）+ 模块号维度（m_nodeOutputVars）。
        // 清不到的后果只是"下一次重新计算"（保守方向）；清多了同理——不会造成读到旧值。
        m_nodeData.remove(entry.first);
        m_validOutputs.remove(entry.first);
        m_nodeOutputVars.remove(entry.second);
    }
}

void FlowExecutor::connectToScene(FlowScene *scene)
{
    if (!scene) {
        return;
    }
    QObject::connect(scene, &FlowScene::connectionAdded, this, &FlowExecutor::markGraphStructureDirty,
                     Qt::UniqueConnection);
    QObject::connect(scene, &FlowScene::connectionRemoved, this, &FlowExecutor::markGraphStructureDirty,
                     Qt::UniqueConnection);
    QObject::connect(scene, &FlowScene::nodeAdded, this, &FlowExecutor::markGraphStructureDirty, Qt::UniqueConnection);
    QObject::connect(scene, &FlowScene::nodeRemoved, this, &FlowExecutor::markGraphStructureDirty,
                     Qt::UniqueConnection);
    // 节点被删除/撤销/清空时通知执行器：**登记**该节点的待清理缓存条目（S1 Stage 1b 第 2 步起生效）。
    // 槽体不直接改执行缓存（那正是首次尝试挂起的原因）：GUI 侧只取叶子锁登记，
    // 由执行线程在安全点（轮首 / 同步执行入口）统一清三类缓存 + m_validOutputs.remove。
    // 另有兜底：① 每轮快照 + 墓碑保证本轮不踩悬垂指针；② rebuildIncomingIndex 的懒剪枝
    // 会清掉"已不在场景"的条目（登记丢失也不会长期残留）。
    QObject::connect(scene, &FlowScene::nodeRemoved, this, &FlowExecutor::onSceneNodeRemoved,
                     Qt::UniqueConnection);
}

void FlowExecutor::markGraphStructureDirty()
{
    QMutexLocker locker(&m_graphCacheMutex);
    m_graphStructureDirty = true;
}

void FlowExecutor::setLifetimeToken(const std::shared_ptr<FlowExecutorToken> &token)
{
    // 词条切片 C P3c 期：只存弱引用（本体↔令牌互持会成环，析构永不到）。
    m_lifetimeToken = token;
}

void FlowExecutor::setFlowScene(FlowScene *scene)
{
    // 段四（P1-6 余量，r12 复核）：**同场景幂等快速路**——切回已绑定的同场景页签（executorForScene
    // 复用每场景执行器 ⇒ 绑定内容恒等，见 MainWindow 切页注释）时，慢路径的断/接信号与整块图缓
    // 存清空是**纯盖章**：绑定内容一致无须重建；且运行中的目标执行器（多流程自启后切到其页签）
    // worker 正在轮内无锁读 m_loopBodyNodes/m_subFlowBodyNodes/m_subFlowMembers/m_subFlowIOs/
    // m_subFlowCallStack——慢路径「锁内清」对 worker「无锁读写」＝U-213 收口后残余的混锁竞态源。
    // 快速路保留两项既有切页语义：① 最近激活执行器登记（current()/currentContext）；② 节点归属
    // 刷新（晚到节点的归属只能经 setFlowScene 补挂——撤了就是行为变更）。跳过的：disconnectFromScene、
    // 块重绑、图缓存清空（清空计数器不计入）、connectToScene。判同须在 m_mutex 内（与发布合同同锁）；
    // nullptr 永走慢路（解绑语义包含清缓存，不可并入幂等面）。
    FlowScene *bound = nullptr;
    {
        QMutexLocker locker(&m_mutex);
        bound = m_scene;
    }
    if (scene != nullptr && scene == bound) {
        const QList<NodeBase *> liveNodes = scene->nodes();
        for (NodeBase *n : liveNodes) {
            if (n) {
                n->setOwnerContext(m_runContext);
                n->setOwnerToken(m_lifetimeToken.lock());   // 残余面：仅子流程节点的执行调用需要（见 NodeBase 注释）
            }
        }
        s_currentInstance = this;
        {
            QMutexLocker locker(&s_currentContextMutex);
            s_currentContext = m_runContext;
        }
        return;
    }
    disconnectFromScene();
    // 段二：随场景换绑——取**当值场景自代**的存活块，并把当值场景写进块内（bind：弱侧指针、
    // 清墓碑、换绑计数＋1——块内 m_scene 由 bind 落笔；解绑（nullptr）＝清块持有，回到未借态）。
    // 段三（P1-6，r11 复核）：发布合同两半——bind 先落笔（块写锁短临界、在 m_mutex 外，让在飞
    // 票先完成）；随后与 m_scene 同锁发布：shared_ptr 对象自身的赋值/拷贝竞争，控制块引用计数
    // 的原子性替代不了对象互斥；发布先于可见（m_aliveBlock 换成新块的那一刻块必已绑当值场景，
    // 无「发布了未绑块」窗口）；旧块换出到锁外再释（§13.2 增减不对称：减不落任何锁临界区内；
    // 活票持照拷 ⇒ 活票期间引用计数≥2，此处减不会真析构带票的块）。
    std::shared_ptr<FlowSceneAliveBlock> newBlock = scene ? scene->aliveBlockShared()
                                                          : std::shared_ptr<FlowSceneAliveBlock>();
    if (newBlock)
        newBlock->bind(scene);
    std::shared_ptr<FlowSceneAliveBlock> oldBlock;
    {
        QMutexLocker locker(&m_mutex);
        m_scene = scene;
        oldBlock.swap(m_aliveBlock);   // 旧块换出到锁外（随后减在锁外做）
        m_aliveBlock = std::move(newBlock);   // 段三：同锁发布（P1-6）
    }
    oldBlock.reset();   // 锁外真减（可能真析构）
    {
        QMutexLocker locker(&m_graphCacheMutex);
        m_graphStructureDirty = true;
        ++m_graphCacheClearCount;   // 段四：慢路径清空计数（只读视图；同场景幂等快速路不计）
        // 切换场景：立即清空上一场景的图缓存。否则下一次 rebuildIncomingIndex
        // 识别循环体时会误用旧场景的节点（漏识别循环体），若旧场景已销毁还会
        // 解引用已释放的节点指针（P1）
        m_cachedSortedNodes.clear();
        m_incoming.clear();
        m_outgoing.clear();
        m_loopBodyNodes.clear();
        // FR15.10：子流程的轮首收集物同样随场景切换清空（成员指针属于旧场景，悬垂风险同循环体）
        m_subFlowBodyNodes.clear();
        m_subFlowMembers.clear();
        m_subFlowIOs.clear();
        m_subFlowCallStack.clear();
    }
    connectToScene(scene);
    // E3：挂上场景即建立归属——rebuildIncomingIndex 要等"有人驱动一轮/一次改图"才跑，
    // 而"流程从没启动过就去开相机参数面板"正是 E3 残留的原始场景。
    // 归属槽存的是运行上下文块（P2c 期：算子侧改持块，不持执行器裸指针），赋值不取锁
    // （见 NodeBase.h），与 executeNode 里的既有写法同口径；
    // 解绑（scene == nullptr）**不清**归属：节点可能已转由别的执行器接管，清成空反而退回兜底块。
    if (scene) {
        const QList<NodeBase *> liveNodes = scene->nodes();
        for (NodeBase *n : liveNodes) {
            if (n) {
                n->setOwnerContext(m_runContext);
                n->setOwnerToken(m_lifetimeToken.lock());   // 残余面：仅子流程节点的执行调用需要（见 NodeBase 注释）
            }
        }
        // 多流程并发：最近激活的流程执行器作为 current()（供节点查询运行状态）
        s_currentInstance = this;
        {
            QMutexLocker locker(&s_currentContextMutex);
            s_currentContext = m_runContext;
        }
    }
}

void FlowExecutor::startExecution()
{
    QMutexLocker locker(&m_mutex);
    if (m_runContext->state() == ExecutionState::Running) {
        return;
    }

    // 清掉上一轮遗留的"人为停止"标记（P2-1 残留）：两轮之间按停止时标记会一直悬着，
    // 不清的话下一次启动的第一轮会被误判成中止、良率漏掉一整轮。
    m_userStopRequested.store(false, std::memory_order_relaxed);
    setRunState(ExecutionState::Running);
    resetState();
    
    if (!isRunning()) {
        start();
    } else {
        m_waitCondition.wakeAll();
    }
}

void FlowExecutor::pauseExecution()
{
    QMutexLocker locker(&m_mutex);
    if (m_runContext->state() == ExecutionState::Running) {
        setRunState(ExecutionState::Paused);
        // 唤醒正在可取消等待中的阻塞节点（延时等），使其停止计时并等待恢复（P5）
        m_waitCondition.wakeAll();
        // 注意：此处只表示"暂停请求已受理"（当前节点可能仍在跑），executionPaused 保持原有语义；
        // "图已停稳"由 worker 在等待点发出的 executionParked 表达（改图判据用它，不能用本信号）。
        emit executionPaused();
    }
}

void FlowExecutor::parkWhilePausedLocked()
{
    // 须持 m_mutex 调用：先置位并上报"真停稳"，再阻塞等待恢复
    if (!m_workerParked) {
        m_workerParked = true;
        emit executionParked();   // 跨线程 → 队列投递到 UI 线程
    }
    while (m_runContext->state() == ExecutionState::Paused) {
        m_waitCondition.wait(&m_mutex);
    }
    m_workerParked = false;       // 已恢复执行，不再处于"停稳可改图"状态
}

bool FlowExecutor::allowsGraphEditing() const
{
    if (m_abandoned.load(std::memory_order_acquire))
        return false;                                   // P3d：已放弃强杀 ⇒ 工作线程可能仍在跑，图必须保持锁定
    QMutexLocker locker(&m_mutex);
    if (m_runContext->state() == ExecutionState::Running)
        return false;                                   // 真在跑：禁止
    if (m_runContext->state() == ExecutionState::Paused && !m_workerParked)
        return false;                                   // 暂停已受理但 worker 还在节点里：禁止（窗口期）
    return true;                                        // Idle/Stopped/已停稳的 Paused：允许
}

void FlowExecutor::resumeExecution()
{
    QMutexLocker locker(&m_mutex);
    if (m_runContext->state() == ExecutionState::Paused) {
        m_stepMode = false;   // 显式"继续"= 退出单步模式，回到正常连续运行
        setRunState(ExecutionState::Running);
        m_waitCondition.wakeAll();
        emit executionResumed();
    }
}

void FlowExecutor::stepExecution()
{
    QMutexLocker locker(&m_mutex);
    m_stepMode = true;
    if (m_runContext->state() == ExecutionState::Paused) {
        setRunState(ExecutionState::Running);
        m_waitCondition.wakeAll();
        emit executionResumed();
    } else if (m_runContext->state() == ExecutionState::Idle || m_runContext->state() == ExecutionState::Stopped) {
        // 未运行时：从首个节点开始步进
        m_stepMode = true;
        locker.unlock();
        startExecution();
    }
}

void FlowExecutor::stopExecution()
{
    QMutexLocker locker(&m_mutex);
    // 人为停止标记（2026-10-25 复核 P2-1 残留）：轮末据此判断"这一轮是被用户中止的"。
    // 不能只看状态 Stopped——失败停止（stopOnFailure 的 setRunState(Stopped)）同样是 Stopped，
    // 而且循环体里的算子在被停止时也可能把自己判失败（例如延时段），只看状态会把真失败
    // 当成中止而从良率里抹掉。
    m_userStopRequested.store(true, std::memory_order_relaxed);
    setRunState(ExecutionState::Stopped);
    m_stepMode = false;
    m_pendingExternalRounds = 0;   // S9：停止即放弃待补跑，避免"停止后又自己跑起来"
    m_waitCondition.wakeAll();
}

bool FlowExecutor::requestExternalRound()
{
    {
        QMutexLocker locker(&m_mutex);
        // M-1/F-1：只有软触发模式会在轮末消费补跑（run() 的轮末分支），其余模式排队等于"永远不执行"：
        //  · 硬触发：本就不该受理（canTriggerFromExternal 已挡，这里兜底）；
        //  · 连续：一直在跑，外部触发无实际意义，但排队会让 GTM 计数与 triggerFired 虚高——
        //    现场已按 B 方案"载入即自动连续跑"，若不收口就是全天候幻影计数（统计失真）。
        // 返回 false = "未受理" → GTM 不计数、不发 triggerFired（与"超界丢弃"同一口径，
        // 也与本函数"会不会真的多跑一轮"的语义严格一致）。
        if (m_runContext->flowMode() != FlowMode::SoftwareTrigger) {
            const FlowMode mode = m_runContext->flowMode();
            locker.unlock();
            VFP_DEBUG << "External round ignored: flow mode is not software-trigger:" << static_cast<int>(mode);
            return false;
        }
        if (m_runContext->state() == ExecutionState::Running || m_runContext->state() == ExecutionState::Paused) {
            if (m_pendingExternalRounds < kMaxPendingExternalRounds) {
                ++m_pendingExternalRounds;      // 排队补跑（FIFO 语义，按笔计数）
                m_waitCondition.wakeAll();
            } else {
                // 超界：丢最旧一笔（等价于丢弃最早那笔触发），并让"丢"可见
                ++m_droppedExternalRounds;
                const quint64 dropped = m_droppedExternalRounds;
                locker.unlock();
                VFP_RUNTIME_INFO.noquote()
                    << QStringLiteral("警告：外部触发超出排队上限(%1)，丢弃最旧一笔；累计丢弃=%2")
                           .arg(kMaxPendingExternalRounds).arg(dropped);
                return false;
            }
            return true;
        }
    }
    // 173 行拍板：闸开不发新跑——外部触发在空闲位起的这一轮也属「新的运行」（软触发的
    // idleness→running 转移）。跑着的照跑不受影响：运行中的排队补跑仍是轮末消费（上面分支）。
    // 引擎侧只判写闸本体（writesBlocked）不判会话：外触引擎发生在任何会话之前也必须有意义，
    // 出厂态的安全由闸本身与 UI 侧的 canRunFlow 分层承担（173 行登记）。
    if (SessionManager::instance()->writesBlocked()) {
        VFP_RUNTIME_INFO.noquote()
            << QStringLiteral("写闸开启期间不受理外部触发的新运行（出厂口令未改，改密后恢复）");
        return false;
    }
    startExecution();   // 空闲：语义同原 startExecution（立即起一轮）
    return true;
}

int FlowExecutor::pendingExternalRounds() const
{
    QMutexLocker locker(&m_mutex);
    return m_pendingExternalRounds;
}

quint64 FlowExecutor::droppedExternalRounds() const
{
    QMutexLocker locker(&m_mutex);
    return m_droppedExternalRounds;
}

void FlowExecutor::exitStepMode()
{
    // 历史缺陷：m_stepMode 只在 stopExecution 里清，单步用过一次后"开始执行"
    // 仍然每节点暂停——单步模式粘住，流程无法回到正常连续运行（单步"一次性失效"）。
    QMutexLocker locker(&m_mutex);
    m_stepMode = false;
}

void FlowExecutor::setRunState(ExecutionState state)
{
    // 写块（块内原子写并唤醒块内等待者）＋唤醒执行器侧等待点（park 等恢复信号）。
    // 与原来的「在主体锁内写状态再 wakeAll」同序同义：状态先落，再唤醒。
    m_runContext->setState(state);
    m_waitCondition.wakeAll();
}

ExecutionState FlowExecutor::getState() const
{
    // 运行态在块内（原子读；块是共享件，故读侧无需再取主体锁）
    if (m_abandoned.load(std::memory_order_acquire))
        return ExecutionState::Error;   // P3d：终态位优先——收尾超时后不得再回落到 Stopped
    return m_runContext->state();
}

void FlowExecutor::markExecutionAbandoned(const QString &reason)
{
    // 顺序：先置终态位（此后读侧恒见 Error），再写块（唤醒块内等待者），最后发信号。
    m_abandoned.store(true, std::memory_order_release);
    m_runContext->setState(ExecutionState::Error);
    qCritical().noquote() << reason;
    emit executionAbandoned(reason);
}

void FlowExecutor::setFlowMode(FlowMode mode)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_mutex);
        if (m_runContext->flowMode() != mode) {
            m_runContext->setFlowMode(mode);
            m_stepMode = false;   // 切换运行模式 = 明确要正常跑，退出单步模式
            changed = true;
        }
    }
    if (changed) {
        emit flowModeChanged(static_cast<int>(mode));
    }
}

FlowMode FlowExecutor::getFlowMode() const
{
    // 运行模式同样在块内（原子读）
    return m_runContext->flowMode();
}

void FlowExecutor::run()
{
    emit executionStarted();
    
    while (true) {
        QMutexLocker locker(&m_mutex);
        
        if (m_runContext->state() == ExecutionState::Stopped) {
            emit executionStopped();
            break;
        }
        
        if (m_runContext->state() == ExecutionState::Paused) {
            parkWhilePausedLocked();   // 真停稳才上报 executionParked
            continue;
        }
        
        if (m_runContext->state() != ExecutionState::Running) {
            break;
        }
        
        // 段三（P1-6，r11 复核）：轮首仍在 m_mutex 持握内抄块副本——与 setFlowScene 的同锁发布
        // 成对；解锁后 helper 只吃副本（shared_ptr 对象自身的读写在锁内完成，票口锁不管这层）。
        std::shared_ptr<FlowSceneAliveBlock> blockCopy = m_aliveBlock;
        locker.unlock();

        // 本轮计时（运行期统计用）
        QElapsedTimer roundTimer;
        roundTimer.start();

        // S1：本轮捕获一次拓扑快照（RAII 句柄，随迭代结束 / break / continue 自动释放）。
        // 持快照期间删除节点/连边只做墓碑延迟析构，整轮可安全使用这批裸指针；轮内不再访问场景容器。
        // 段二：借面改走存活控制块——alive 判走与块读锁跨快照均在票内；场景先亡不再
        // 解引用（与"未设场景"同一显式退化路径：报错＋停轮）。
        m_diagPhase.store(1, std::memory_order_relaxed);   // 诊断：轮首/取快照前
        QList<SubFlowDef> subFlowDefs;   // 段二：定义副本在票窗内一并带出（第二轮同窗读）
        FlowScene::GraphSnapshotGuard graphSnapshot = captureViaAliveBlock(blockCopy, &subFlowDefs);
        if (!graphSnapshot.isHeld()) {
            emit executionError(tr("Flow scene is not set"));
            emit executionStopped();   // 终态信号：错误路径不发终态会让界面按钮停在"运行中"
            break;
        }
        m_diagPhase.store(2, std::memory_order_relaxed);   // 诊断：已取快照
        const QList<NodeBase *> nodes = graphSnapshot.snapshot().nodes;
        if (nodes.isEmpty()) {
            emit executionStopped();   // 空场景同样需要终态信号（历史缺陷：静默退出，UI 卡"运行中"）
            break;
        }

        QList<NodeBase *> sortedNodes;
        {
            m_diagPhase.store(3, std::memory_order_relaxed);   // 诊断：等图锁 / 图锁内
            QMutexLocker cacheLock(&m_graphCacheMutex);
            // S1 Stage 1b：轮首安全点消费"删节点待清理"登记（GUI 侧只登记、不取本锁）
            applyPendingCachePurgesLocked();
            const bool needRebuild =
                m_graphStructureDirty || m_cachedSortedNodes.isEmpty() || !cacheMatchesScene(nodes);
            if (needRebuild) {
                QList<NodeBase *> topoOut;
                rebuildIncomingIndex(nodes, graphSnapshot.snapshot().connections);
                if (!topologicalSort(nodes, topoOut)) {
                    m_cachedSortedNodes.clear();
                    m_graphStructureDirty = true;
                    cacheLock.unlock();
                    emit executionError(tr("流程中存在循环连接，无法确定执行顺序。"));
                    emit executionStopped();   // 终态信号：恢复界面按钮状态
                    break;
                }
                m_cachedSortedNodes = topoOut;
                m_graphStructureDirty = false;
            }
            sortedNodes = m_cachedSortedNodes;
        }
        m_diagPhase.store(4, std::memory_order_relaxed);   // 诊断：已出图锁/节点循环前

        VFP_EXEC_DEBUG << "Sorted nodes count:" << sortedNodes.size();

        // 每轮执行前构建激活集合：无入边（源）节点激活；条件分支传播在节点执行后动态进行
        {
            QMutexLocker cacheLock(&m_graphCacheMutex);
            m_activeNodes.clear();
            for (NodeBase *n : sortedNodes) {
                const auto incIt = m_incoming.constFind(n);
                if (incIt == m_incoming.cend() || incIt->isEmpty()) {
                    m_activeNodes.insert(n);
                }
            }
        }

        // FR15.10 子流程：轮首收集被引用定义的成员/边界（同循环体 P3 模式，worker 线程私有）。
        // 必须在 m_loopBodyNodes 填充之后：与循环体交叉的成员让位给循环体（v1 限制）。
        collectSubFlowBodies(subFlowDefs, nodes);

        for (int i = 0; i < sortedNodes.size(); i++) {
            NodeBase *node = sortedNodes[i];
            // 循环体节点由所属 LoopNode 统一调度执行，主遍历不再重复执行（P3）
            if (m_loopBodyNodes.contains(node)) {
                VFP_EXEC_DEBUG << "Node skipped (loop body, scheduled by LoopNode):" << node->fullName();
                recordNodeSkipped(node, QStringLiteral("循环体由循环节点统一调度"));
                continue;
            }
            // 子流程体节点由调用点（SubFlowNode）内联调度执行，主遍历不再重复执行（FR15.10）
            if (m_subFlowBodyNodes.contains(node)) {
                VFP_EXEC_DEBUG << "Node skipped (subflow body, scheduled by SubFlowNode):" << node->fullName();
                recordNodeSkipped(node, QStringLiteral("子流程体由调用点统一调度"));
                continue;
            }
            VFP_EXEC_DEBUG << "Executing node:" << node->fullName();
            locker.relock();
            if (m_runContext->state() == ExecutionState::Stopped) {
                // 既有缺陷修复（由新压测段 + 阶段探针定位）：**必须先解锁再 break**。
                // 旧实现在持有 m_mutex 时 break，随后轮末/循环尾的 `QMutexLocker ml(&m_mutex)`
                // 会对同一把**非递归**锁再次加锁 → **自死锁**：worker 永不退出、停止/退出失效。
                // 触发条件：停止请求落在"轮内"（阶段探针实测卡在"轮末·信号后"）。
                locker.unlock();
                m_roundAborted = true;   // 人为中断：本轮只是半截流程，轮末不得写成整轮 OK
                emit executionStopped();
                break;
            }
            
            if (m_runContext->state() == ExecutionState::Paused) {
                parkWhilePausedLocked();   // 真停稳才上报 executionParked
            }
            
            if (m_runContext->state() == ExecutionState::Stopped) {
                locker.unlock();   // 同上：持 m_mutex 时 break 会在轮末/循环尾再锁同一把锁 → 自死锁
                m_roundAborted = true;   // 人为中断（含"暂停中被停止"）：轮末不得写成整轮 OK
                emit executionStopped();
                break;
            }
            locker.unlock();

            // 条件分支：未激活节点跳过执行并清空输出，防止下游误用旧数据
            if (!m_activeNodes.contains(node)) {
                VFP_EXEC_DEBUG << "Node skipped (inactive branch):" << node->fullName();
                for (int p = 0; p < node->outputPorts().size(); ++p) {
                    node->setOutputData(p, QSharedPointer<DataObject>());
                }
                {
                    // S1 Stage 1b：三类执行缓存统一受 m_graphCacheMutex 保护
                    //（锁内只有容器操作与 moduleId() 内联取值，无任何会再取锁的调用）
                    QMutexLocker cacheLock(&m_graphCacheMutex);
                    m_nodeData[node].clear();   // 清空缓存，避免下游误用上一轮数据（E2）
                    m_nodeOutputVars[node->moduleId()].clear();  // 清空变量缓存，避免引用上一轮数值（P2）
                    m_validOutputs.remove(node);   // E2：输出已清空，"可复用"标记必须同步撤销（口径见 executeNode 内注释）
                }
                recordNodeSkipped(node, QStringLiteral("分支未激活"));
                continue;
            }
            
            m_diagPhase.store(5, std::memory_order_relaxed);   // 诊断：节点循环
            m_diagNodeId.store(node->moduleId(), std::memory_order_relaxed);
            executeNode(node, i == sortedNodes.size() - 1);
            m_diagNodeId.store(-1, std::memory_order_relaxed);
            // 执行后激活下游（条件节点仅激活被选中分支）
            activateDownstream(node);

            // 循环执行：由 LoopNode 统一调度全部迭代（1..loopCount），主遍历已跳过循环体节点（P3）
            if (LoopNode *loopNode = qobject_cast<LoopNode *>(node)) {
                const int cnt = qMax(1, node->getParam(QStringLiteral("loopCount")).toInt());
                executeLoop(loopNode, cnt);
                if (!m_lastNodeSuccess && m_stopOnFailure) {
                    QMutexLocker fl(&m_mutex);
                    setRunState(ExecutionState::Stopped);
                    break;
                }
            }

            // 失败中断策略：节点失败且开启停止时，立即停止流程（对标 VisionMaster）
            if (!m_lastNodeSuccess && m_stopOnFailure) {
                {
                    QMutexLocker failLocker(&m_mutex);
                    setRunState(ExecutionState::Stopped);
                }
                emit executionError(QStringLiteral("节点 %1 执行失败，流程已停止（可在系统菜单关闭\"失败时停止\"）")
                                        .arg(node->fullName()));
                break;
            }

            // 单步模式：每执行完一个节点后暂停，等待下一步命令
            {
                QMutexLocker stepLocker(&m_mutex);
                if (m_stepMode && m_runContext->state() == ExecutionState::Running) {
                    setRunState(ExecutionState::Paused);
                    emit executionPaused();
                }
            }
        }
        
        m_diagPhase.store(6, std::memory_order_relaxed);   // 诊断：轮末·落库前
        // 轮末统一判定"这一轮是不是被人为中止的半截流程"（2026-10-25 复核 P2-1 残留）。
        // 只在主遍历的两个 break 点置位不够：循环体 executeLoop 在多个点直接 return、
        // 子流程 executeSubFlow 遇停止也直接返回——若那条路径恰好是最后一节点，主遍历
        // 自然结束，轮末仍会写 passed=true 的汇总。这里一处兜住所有退出路径。
        if (m_userStopRequested.load(std::memory_order_relaxed))
            m_roundAborted = true;   // 本轮被人为停止（不管有没有节点因此判失败）
        m_userStopRequested.store(false, std::memory_order_relaxed);
        // P1-11 报表：轮末追加一条「整轮汇总」记录（见 InspectionRecord.h 的 kRoundSummaryNodeName）。
        // 为什么在这里做：记录是"每节点一条"且无轮次标识，报表据此只能算节点执行通过率、算不出良率；
        // 本行让"良率/趋势"有可靠口径。此刻 m_roundHadFailure 还没被 recordRoundFinished 复位，取值有效。
        //
        // 中途被停止的一轮**不写汇总行**（2026-10-25 复核 P2-1）：半截流程写 passed=true 会让
        // 良率把一次人为中断记成一件 OK 产品（YieldMonitor 只读这类汇总行）——宁可这一轮不出现在
        // 良率口径里，也不谎报。已执行节点的那几条记录照常落库，追溯不丢。
        if (m_roundAborted) {
            VFP_EXEC_DEBUG << "Round aborted by stop; round-summary row skipped";
        } else {
        m_pendingResults.append(InspectionRecord{
            0,
            m_flowName.isEmpty() ? QStringLiteral("(未命名流程)") : m_flowName,
            kRoundSummaryNodeName,
            !m_roundHadFailure,
            m_roundHadFailure ? QStringLiteral("NG") : QStringLiteral("OK"),
            QDateTime() });
        }
        // 轮末批量落库：整轮结果一个事务提交。放在统计之前，保证本轮记录已落库。
        // 未启用数据库（databasePath 为空）时只清缓冲，不做任何 IO。
        if (!m_pendingResults.isEmpty()) {
            const QList<InspectionRecord> batch = m_pendingResults;
            m_pendingResults.clear();
            if (!AppDatabase::instance()->databasePath().isEmpty()) {
                // P1：这条返回值原来直接丢掉。连续模式下一旦写库失败（连接没开、表缺失、
                // 库被占用…），每轮的全部检测结果连同「整轮汇总」都会静默消失——
                // 报表与追溯凭空少一段，而界面一切正常。
                // 失败必须让操作员当场看见；但不据此停止流程：记录失败不该挡住检测本身。
                if (!AppDatabase::instance()->saveInspectionResults(batch)) {
                    const QString detail = AppDatabase::instance()->lastDatabaseError();
                    VFP_EXEC_DEBUG << "Round results not saved:" << batch.size() << detail;
                    emit executionError(QStringLiteral("本轮 %1 条检测结果未能写入数据库（报表/追溯会缺这一段）：%2")
                                            .arg(batch.size())
                                            .arg(detail));
                }
            }
        }
        m_diagPhase.store(7, std::memory_order_relaxed);   // 诊断：轮末·落库后

        // 轮次统计 + 周期性进程资源采样/日志
        recordRoundFinished(roundTimer.elapsed());
        m_diagPhase.store(8, std::memory_order_relaxed);   // 诊断：轮末·统计后

        emit executionFinished();
        m_diagPhase.store(9, std::memory_order_relaxed);   // 诊断：轮末·信号后（接下来是节拍/回转）

        // 连续模式 / 硬触发模式：自动循环执行（硬触发模式由相机触发帧门控每次循环）
        FlowMode currentMode;
        {
            QMutexLocker ml(&m_mutex);
            currentMode = m_runContext->flowMode();
        }
        if (currentMode == FlowMode::Continuous || currentMode == FlowMode::HardwareTrigger) {
            // 检查是否已被手动停止
            {
                QMutexLocker ml(&m_mutex);
                if (m_runContext->state() != ExecutionState::Running) {
                    break;
                }
            }

            // 硬触发模式：若流程中没有相机图像源，则无从等待触发帧，
            // 不允许无意义地空转，直接结束本轮（等待用户切换模式）
            if (currentMode == FlowMode::HardwareTrigger) {
                bool hasCameraSource = false;
                for (NodeBase *n : sortedNodes) {
                    if (n && n->isCameraSource()) {
                        hasCameraSource = true;
                        break;
                    }
                }
                if (!hasCameraSource) {
                    VFP_DEBUG << "Hardware-trigger flow has no camera source, exiting loop";
                    break;
                }
            }

            // 连续/硬触发：按可配置节拍调度（默认 0，由相机/触发事件驱动），不再固定限速（E6）
            if (m_loopIntervalMs > 0) {
                QThread::msleep(static_cast<unsigned long>(m_loopIntervalMs));
            }
            continue;
        }

        // S9：软触发模式下的外部触发排队补跑——本轮结束后逐笔消费待补跑（有界，见 requestExternalRound）。
        // N-1：消费检查与"退出置 Idle"必须同一临界区。否则触发恰落在两次独立加锁之间时
        // （状态仍为 Running）requestExternalRound 会成功排队（GTM 已计数并 triggerFired），
        // 而本线程已决定退出 → 该笔 pending 悬空，下次起轮（含界面手动"开始执行"）白多跑一轮。
        {
            QMutexLocker ml(&m_mutex);
            if (currentMode == FlowMode::SoftwareTrigger && m_pendingExternalRounds > 0
                && m_runContext->state() == ExecutionState::Running) {
                --m_pendingExternalRounds;
                continue;
            }
            setRunState(ExecutionState::Idle);   // 与消费判定原子：此后到达的触发会走 startExecution 立即起轮
        }
        break;
    }
    
    QMutexLocker locker(&m_mutex);
    setRunState(ExecutionState::Idle);
}

bool FlowExecutor::topologicalSort(const QList<NodeBase *> &nodes, QList<NodeBase *> &outSorted)
{
    outSorted.clear();
    QSet<NodeBase *> visited;
    QSet<NodeBase *> tempMarked;

    for (NodeBase *node : nodes) {
        if (!visited.contains(node)) {
            if (!visit(node, visited, tempMarked, outSorted)) {
                outSorted.clear();
                return false;
            }
        }
    }
    return true;
}

bool FlowExecutor::cacheMatchesScene(const QList<NodeBase *> &nodes) const
{
    if (m_cachedSortedNodes.size() != nodes.size()) {
        return false;
    }
    QSet<NodeBase *> ns;
    for (NodeBase *n : nodes) {
        ns.insert(n);
    }
    for (NodeBase *n : m_cachedSortedNodes) {
        if (!n || !ns.contains(n)) {
            return false;
        }
    }
    return true;
}

void FlowExecutor::rebuildIncomingIndex(const QList<NodeBase *> &liveNodes,
                                       const QList<MyProject::Connection *> &connections)
{
    m_incoming.clear();
    m_outgoing.clear();
    m_incoming.reserve(connections.size());
    m_outgoing.reserve(connections.size());
    for (MyProject::Connection *conn : connections) {
        if (!conn) {
            continue;
        }
        NodeBase *dst = conn->getDestinationNode();
        if (dst) {
            m_incoming[dst].append(conn);
        }
        NodeBase *src = conn->getSourceNode();
        if (src) {
            m_outgoing[src].append(conn);
        }
    }

    // E3（2026-09-25 收口）：建图即建立归属，不再等"该节点被跑过一轮"。
    // 此前 setOwnerExecutor() 全仓只有 executeNode() 一处调用 ⇒ 从未执行过的节点 ownerExecutor() 恒空，
    // MvsImageSourceNode 的两条 UI 触发路径（applyParams 决定要不要写像素格式、refreshPixelFormatEnabled
    // 决定下拉框启用/置灰）只能回落 FlowExecutor::current()＝"最后启动的那条流程"，
    // 双流程下 A 的相机参数面板会按 B 的运行状态被锁或被放行。
    for (NodeBase *n : liveNodes) {
        if (n) {
            n->setOwnerContext(m_runContext);
            n->setOwnerToken(m_lifetimeToken.lock());   // 残余面：仅子流程节点的执行调用需要（见 NodeBase 注释）
        }
    }

    // 图结构已变化：清掉指向"已不在当前场景中"的节点缓存条目。
    // 撤销 / 删除节点后，旧指针地址与 moduleId 都可能被新节点复用，残留键会让
    // 新节点被误判为"有缓存 / 输出有效"，把上一代节点的数据喂进算子
    // （表面正常、结果错误——工业平台最坏故障）。
    {
        QSet<NodeBase *> liveSet;
        QSet<int> liveIds;
        for (NodeBase *n : liveNodes) {
            if (!n)
                continue;
            liveSet.insert(n);
            liveIds.insert(n->moduleId());
        }
        for (auto it = m_nodeData.begin(); it != m_nodeData.end();) {
            if (!liveSet.contains(it.key()))
                it = m_nodeData.erase(it);
            else
                ++it;
        }
        for (auto it = m_validOutputs.begin(); it != m_validOutputs.end();) {
            if (!liveSet.contains(it.key()))
                it = m_validOutputs.erase(it);
            else
                ++it;
        }
        for (auto it = m_nodeOutputVars.begin(); it != m_nodeOutputVars.end();) {
            if (!liveIds.contains(it.key()))
                it = m_nodeOutputVars.erase(it);
            else
                ++it;
        }
    }

    // 识别全部循环体节点（供主遍历跳过，统一由 LoopNode 调度执行，P3）
    // 注意：必须以当前场景的节点为准。本函数在拓扑排序前调用，m_cachedSortedNodes
    // 可能仍属于上一个场景（切换场景后），用它做种子会漏识别循环体并可能解引用悬垂指针
    m_loopBodyNodes.clear();
    // S1：循环体识别同样以本次快照的活节点表为准（不再回读场景容器）
    const QList<NodeBase *> &bodySeed = liveNodes;
    for (NodeBase *n : bodySeed) {
        if (LoopNode *ln = qobject_cast<LoopNode *>(n)) {
            for (NodeBase *bn : collectLoopBody(ln)) {
                m_loopBodyNodes.insert(bn);
            }
        }
    }
}

void FlowExecutor::activateDownstream(NodeBase *node)
{
    if (!node) {
        return;
    }
    const auto it = m_outgoing.constFind(node);
    if (it == m_outgoing.cend()) {
        return;
    }

    ConditionalNode *cond = qobject_cast<ConditionalNode *>(node);
    const bool isCond = (cond != nullptr);
    const bool condResult = cond ? cond->conditionResult() : false;
    const bool evaluated = cond ? cond->hasEvaluated() : true;

    for (MyProject::Connection *conn : *it) {
        if (!conn) {
            continue;
        }
        NodeBase *dst = conn->getDestinationNode();
        if (!dst) {
            continue;
        }
        if (!isCond) {
            m_activeNodes.insert(dst);
            continue;
        }
        // 条件节点：未执行（从未评估）则不激活任何分支
        if (!evaluated) {
            continue;
        }
        const int srcIdx = conn->getSourcePort();
        if (srcIdx == 1) {            // TRUE 分支
            if (condResult) m_activeNodes.insert(dst);
        } else if (srcIdx == 2) {     // FALSE 分支
            if (!condResult) m_activeNodes.insert(dst);
        } else {                      // 端口 0 直连（兼容旧项目连线）：无条件激活
            m_activeNodes.insert(dst);
        }
    }
}

bool FlowExecutor::visit(NodeBase *node, QSet<NodeBase*> &visited, QSet<NodeBase*> &tempMarked, QList<NodeBase*> &sortedNodes)
{
    if (tempMarked.contains(node)) {
        // 检测到环
        return false;
    }
    
    if (visited.contains(node)) {
        return true;
    }
    
    tempMarked.insert(node);
    
    const auto incIt = m_incoming.constFind(node);
    if (incIt != m_incoming.cend()) {
        for (MyProject::Connection *conn : *incIt) {
            NodeBase *sourceNode = conn ? conn->getSourceNode() : nullptr;
            if (!sourceNode) {
                continue;
            }
            if (!visit(sourceNode, visited, tempMarked, sortedNodes)) {
                return false;
            }
        }
    }
    
    tempMarked.remove(node);
    visited.insert(node);
    sortedNodes.append(node);
    
    return true;
}

namespace {

/// E1：参数引用表达式的还原器，**析构即还原**。
/// 原实现把还原循环写在 `try/catch` 之后，只有两条退出路径能走到它（正常返回、`const std::exception &`）；
/// 其余任何逃出 `try` 的异常都会把整段还原跳过，参数从此永久停在解析出的常量上——存盘即把引用存成常量。
/// `restore()` 可显式提前调用（信号必须看到已还原的表达式），之后析构时列表已空，不会重复写。
class ParamRefRestorer
{
public:
    ParamRefRestorer(NodeBase *node, QList<QPair<QString, QString>> *backups)
        : m_node(node), m_backups(backups)
    {
    }
    ParamRefRestorer(const ParamRefRestorer &) = delete;
    ParamRefRestorer &operator=(const ParamRefRestorer &) = delete;

    void restore()
    {
        if (!m_node || !m_backups) {
            return;
        }
        for (const auto &b : *m_backups) {
            m_node->setParam(b.first, b.second);
        }
        m_backups->clear();
    }

    ~ParamRefRestorer() { restore(); }

private:
    NodeBase *m_node;
    QList<QPair<QString, QString>> *m_backups;
};

} // namespace

void FlowExecutor::executeNode(NodeBase *node, bool isLastNode)
{
    if (!node) {
        return;
    }

    // 多流程隔离：把所属执行器的运行上下文块交给节点，供其查询运行状态（E3，P2c 期：节点持块不持执行器）
    node->setOwnerContext(m_runContext);
    node->setOwnerToken(m_lifetimeToken.lock());   // 残余面：仅子流程节点的执行调用需要（见 NodeBase 注释）

    bool success = false;
    QElapsedTimer nodeTimer;
    nodeTimer.start();

    // 参数引用解析备份：仅本轮临时替换为解析值，执行后还原表达式，避免永久写回（E1）
    QList<QPair<QString, QString>> paramRefBackups;
    ParamRefRestorer paramRefs(node, &paramRefBackups);

    try {
        // 执行当前选中的算子
        // 先传播数据，确保输入数据正确设置
        propagateData(node);

        // 参数引用解析：{模块号.参数名} 引用前级输出（对标 VisionMaster 参数引用）
        const QList<QString> paramKeys = node->getAllParamNames();
        for (const QString &k : paramKeys) {
            QVariant v = node->getParam(k);
            if (v.typeId() == QMetaType::QString) {
                const QString s = v.toString();
                if (s.contains(QLatin1Char('{')) && s.contains(QLatin1Char('}'))) {
                    paramRefBackups.append(qMakePair(k, s));
                    node->setParam(k, resolveParamRefs(s));
                }
            }
        }

        // 执行前统一清空输出端口：保证本轮下游可见的数据只可能由本轮产生。
        // 否则“无输入/空结果”分支未清输出的节点（如 DelayNode 无输入时直接返回）
        // 会把上一轮输出继续挂在端口上，被当作本轮结果写入缓存（P1）
        //
        // 【待办·增量执行复用】原计划在此处加入"命中 NodeBase::reusesCachedOutput() ∧ 输出仍有效
        // 则跳过 node->execute()"的分支（跳过时端口上保留上一次的输出，后续传播/缓存照常）。
        // 首次实现后回归用例显示复用未生效（readerA 仍被重跑），但同一对象在用例内直接调用
        // reusesCachedOutput() 却返回 true，两者矛盾且未能定位，故先回退该分支，
        // 避免留下"看起来在优化、实际没生效"的代码。NodeBase/ImageReadNode 上的契约声明保留备查。
        for (int p = 0; p < node->outputPorts().size(); ++p)
            node->setOutputData(p, QSharedPointer<DataObject>());

        success = node->execute();

        // 收集输出变量（供后级 {模块号.参数名} 引用）
        if (success) {
            collectNodeOutputVars(node);
        }

        // 为输出数据设置来源信息（仅成功节点写入缓存；失败或本轮无输出时清除缓存，避免下游误用上一轮结果，P2）
        // 顺带把端口 0 的输出指针留在 nodeOut0：后面的数据库记录、全局变量写入与预览推送都要用
        // 同一份，不必各自再 getOutputData(0) 一次（每次都加锁 + 拷贝 QSharedPointer）。
        QSharedPointer<DataObject> nodeOut0;
        if (success) {
            // S1 Stage 1（修整）：**临界区内不调用外部代码**——getOutputData()/setSourceInfo()/
            // fullName() 会触及节点 / 数据对象（可能还有场景）的其它锁。
            // （原担心的"GUI 侧 onSceneNodeRemoved 取同一把锁 → 成环"已在 Stage 1b 第 2 步消除：
            //   该槽改为 GUI 只登记、不取图锁；本条纪律仍保留——环没了，纪律仍然是好习惯。）
            for (int i = 0; i < node->outputPorts().size(); i++) {
                QSharedPointer<DataObject> outputData = node->getOutputData(i);
                if (outputData) {
                    outputData->setSourceInfo(QString("%1 的输出").arg(node->fullName()));
                    {
                        QMutexLocker cacheLock(&m_graphCacheMutex);
                        m_nodeData[node][i] = outputData;
                    }
                    if (i == 0) {
                        nodeOut0 = outputData;
                    }
                } else {
                    // 本轮该端口无输出：移除上一轮残留，否则下游会读到旧数据
                    QMutexLocker cacheLock(&m_graphCacheMutex);
                    m_nodeData[node].remove(i);
                }
            }
            {
                // 标记"输出有效"：供局部执行的可复用判定（见 reusesCachedOutput）
                QMutexLocker cacheLock(&m_graphCacheMutex);
                m_validOutputs[node] = true;
            }
        } else {
            for (int p = 0; p < node->outputPorts().size(); ++p)
                node->setOutputData(p, QSharedPointer<DataObject>());
            {
                // S1 Stage 1：失败/本轮无输出同样要清缓存（避免下游误用上一轮结果），同样入锁
                //
                // E2（2026-09-25 收口）：清空的同一处必须撤销 m_validOutputs。不变量（本轮起由
                // IntegrationTest::testSkippedAndFailedNodesLoseReusableMark 钉住）：
                // **凡整体清空某节点的输出端口 / 其 m_nodeData 条目，且本轮不再重跑该节点 ⇒ 同步撤销该位**。
                // 今天无人读它（增量复用分支已回退，见 executeNode 内【待办·增量执行复用】那段注释），故属**潜伏**；
                // 复用一恢复，陈旧"已有效"位就会让汇合节点原样读回上一件产品的数据。
                QMutexLocker cacheLock(&m_graphCacheMutex);
                m_nodeData[node].clear();
                m_nodeOutputVars[node->moduleId()].clear();
                m_validOutputs.remove(node);
            }
        }

        // 计算节点执行耗时
        qint64 nodeElapsed = nodeTimer.elapsed();

        // 运行期统计：累计节点执行次数 / 失败次数 / 耗时
        {
            QMutexLocker statsLocker(&m_statsMutex);
            m_stats.onNodeExecuted(node->fullName(), success, nodeElapsed);
        }
        if (!success) {
            m_roundHadFailure = true;
        }

        // E1 残留①（2026-09-25 收口）：还原必须**先于任何对外广播**。
        // 下面这五条 emit（nodeExecuted / nodeExecutionTime / nodeOutputsUpdated / imageAvailable /
        // imageReady）原先全部落在"参数仍是本轮解析值"的窗口里，而它们是直连槽（各界面面板、
        // 结果表、以及任何在槽里存盘/快照的代码）读节点参数的时刻——窗口内取到的就是常量。
        // 现在显式还原放在第一条 emit 之前；ParamRefRestorer 的析构继续兜"在这一点之前抛异常"的路径。
        paramRefs.restore();

        // 发出nodeExecuted信号，通知UI更新
        emit nodeExecuted(node, success);

        // 发出节点执行耗时信号
        emit nodeExecutionTime(node, nodeElapsed);

        // 输出变量快照 → 结果数据表（成功时带本轮各输出项；失败时带算子本轮写下的「原因」字段）
        {
            QVariantMap vars;
            if (success) {
                const QHash<QString, QVariant> collected = m_nodeOutputVars.value(node->moduleId());
                for (auto it = collected.cbegin(); it != collected.cend(); ++it)
                    vars.insert(it.key(), it.value());
            } else {
                // U-18：判红轮把算子本轮写下的原因一起推出；只认口径键（include/NodeResultFields.h），
                // 配置项与内部状态位不得冒充原因。现读节点而不读 m_nodeOutputVars：
                // 上面失败分支已清空该表，读它只会拿到空。
                const QList<QString> names = node->getAllParamNames();
                for (const QString &k : names) {
                    if (!isNodeReasonKey(k))
                        continue;
                    const QVariant v = node->getParam(k);
                    // 本轮没人写原因（基类守卫那几条不写）⇒ 不推，面板保留占位
                    if (v.toString().isEmpty())
                        continue;
                    vars.insert(k, v);
                }
            }
            emit nodeOutputsUpdated(node, success, nodeElapsed, vars);
        }

        // Phase 4: 保存检测结果到数据库
        if (!AppDatabase::instance()->databasePath().isEmpty()) {
            // \u6570\u636E\u5E93\u5DF2\u521D\u59CB\u5316
            // 报表「流程」列必须是可读且跨会话稳定的流程名：此前写的是场景指针的十进制数字
            // （reinterpret_cast<quintptr>(m_scene)），报表里显示内存地址且每次运行都不同。
            const QString flowName = m_flowName.isEmpty() ? QStringLiteral("(未命名流程)") : m_flowName;
            QString resultVal;
            if (nodeOut0) {
                resultVal = QStringLiteral("OK");
            }
            // 不在这里直接落库：按轮缓冲，轮末用**单个事务**批量提交（见 run() 中的轮末刷新）。
            // 连续模式下"每节点一次自动提交"是主要固定开销（40 节点 = 40 次提交/轮）。
            m_pendingResults.append(InspectionRecord{ 0, flowName, node->fullName(), success,
                                                      resultVal, QDateTime() });
            if (!success) {
                AppDatabase::instance()->addAlarm(node->fullName(), QStringLiteral("Error"),
                    QStringLiteral("\u7B97\u5B50\u6267\u884C\u5931\u8D25: %1").arg(node->name()));
            }
        }

        // 运行界面推送：任意节点有图像输出即通知（按节点名查表）
        if (success) {
            if (nodeOut0) {
                HalconCpp::HImage img = nodeOut0->getHImage();
                if (img.IsInitialized()) {
                    emit imageAvailable(node, img);
                }
            }

            // 全局变量输出链路：节点勾选"保存到全局变量 xxx"后，
            // 把输出端口 0 的值写入全局变量（运行界面数值/状态灯绑定全局变量即反映真实结果）
            {
                auto *gvm = GlobalVariableManager::instance();
                const QList<QString> paramKeys = node->getAllParamNames();
                for (const QString &key : paramKeys) {
                    if (key.startsWith(QStringLiteral("globalVarOutput_"))
                        && node->getParam(key).toBool()) {
                        const QString varName = key.mid(QStringLiteral("globalVarOutput_").size());
                        const QSharedPointer<DataObject> &out = nodeOut0;
                        if (out) {
                            switch (out->getType()) {
                            case DataObject::DataType::Number:
                                gvm->setVariable(varName, out->getData().toDouble());
                                break;
                            case DataObject::DataType::String:
                                gvm->setVariable(varName, out->getData().toString());
                                break;
                            case DataObject::DataType::Bool:
                                gvm->setVariable(varName, out->getData().toBool());
                                break;
                            case DataObject::DataType::Measure:
                                gvm->setVariable(varName, out->getMeasureResult().value);
                                break;
                            default:
                                gvm->setVariable(varName, out->getData().toString());
                                break;
                            }
                        }
                    }
                }
            }
        }

        // 仅拓扑末端节点发出 imageReady，避免整链重复刷新预览（与其它节点仍可经 nodeExecuted 更新状态）
        if (success && isLastNode) {
            if (nodeOut0) {
                HalconCpp::HImage image = nodeOut0->getHImage();
                if (image.IsInitialized()) {
                    emit imageReady(node, image);
                }
            }
        }
    } catch (const std::exception &e) {
        paramRefs.restore();   // 幂等：正常路径已在第一条 emit 前还原，这里兜"还原之前抛出"的路径
        success = false;        // 见函数尾注释：异常一律按失败收口，不再被 m_lastNodeSuccess 覆盖回 true
        clearNodeCachesOnException(node);   // P2-5：异常出口与正常失败路径同一套收口
        emit executionError(tr("Error executing node %1: %2").arg(node->name()).arg(e.what()));
        emit nodeExecuted(node, false);
    } catch (...) {
        // E1 残留②：这个 `try` 原来只捕 `const std::exception &`。逃出它的异常会一路离开 executeNode
        // （run() 里没有 catch ⇒ std::terminate），而参数就永久停在解析出的常量上。
        // 触发点不止算子本身：NodeBase::execute() 已经吞掉 process() 的所有异常，真正能逃到这里的是
        // propagateData / 缓存写入 / 落库 / 全局变量，以及**直连槽**——emit 就在这个 try 内。
        paramRefs.restore();
        success = false;
        clearNodeCachesOnException(node);   // P2-5：同上
        emit executionError(tr("算子 %1 执行期间抛出非标准异常（类型未知），本轮按失败处理").arg(node->name()));
        emit nodeExecuted(node, false);
    }

    // 异常路径已在上面把 success 置假 ⇒ 失败一定拦停（旧写法在 handler 里写 m_lastNodeSuccess=false，
    // 却被这一行的 `success`（异常前可能已是 true）覆盖，等于"报了 executionError 却不拦停"）
    m_lastNodeSuccess = success;
}

// P2-5（2026-10-25 复核）：executeNode 的异常出口必须与"正常失败路径"同一套收口。
// try 里的失败分支（清三类执行缓存 / 置 m_roundHadFailure）在异常抛出时会被跳过：
//   · 若异常发生在输出已发布之后，三类缓存里留着失败节点的半成品输出 ⇒ 下游读到脏数据；
//   · m_roundHadFailure 不置位 ⇒ 轮末汇总仍写成 passed=true ⇒ 良率虚高。
// 触发面窄（NodeBase::execute 已吞掉算子自身异常；能逃到这里的多为直连槽、缓存写入、
// 落库、全局变量），但一旦触发就是"报了错却照常算 OK"。
void FlowExecutor::clearNodeCachesOnException(NodeBase *node)
{
    if (!node)
        return;
    // ① 清输出端口（与正常失败分支同口径，2026-10-25 复核 P3-1 残留）：只清 m_nodeData
    //    不够——propagateData 在没有缓存条目时会回落到 sourceNode->getOutputData(port)，
    //    于是下游仍能读到失败节点的旧/半成品输出（stopOnFailure=false 时下游会继续跑）。
    const QList<Port *> outPorts = node->outputPorts();
    for (int i = 0; i < outPorts.size(); ++i)
        node->setOutputData(i, QSharedPointer<DataObject>());
    {
        QMutexLocker cacheLock(&m_graphCacheMutex);
        m_nodeData[node].clear();
        m_nodeOutputVars[node->moduleId()].clear();
        m_validOutputs.remove(node);
    }
    m_roundHadFailure = true;   // 本轮按失败收口（与 try 内失败分支同口径）

    // ② 失败节点同样落库 + 告警（P3-1 残留）：异常路径此前跳过这一块，追溯只剩一条 NG 汇总，
    //    看不出是哪个算子、什么原因。与正常失败分支同表同字段。
    if (!AppDatabase::instance()->databasePath().isEmpty()) {
        const QString flowName = m_flowName.isEmpty() ? QStringLiteral("(未命名流程)") : m_flowName;
        m_pendingResults.append(InspectionRecord{ 0, flowName, node->fullName(), false,
                                                  QString(), QDateTime() });
        AppDatabase::instance()->addAlarm(node->fullName(), QStringLiteral("Error"),
                                          QStringLiteral("算子执行异常: %1").arg(node->name()));
    }
}

void FlowExecutor::collectNodeOutputVars(NodeBase *node)
{
    if (!node) return;
    QHash<QString, QVariant> vars;
    const QList<QString> keys = node->getAllParamNames();
    for (const QString &k : keys) {
        const QVariant v = node->getParam(k);
        if (v.isValid() && !v.isNull())
            vars[k] = v;
    }
    // 主值：优先取端口1（Measure），其次端口0（Number/String）
    QSharedPointer<DataObject> out = node->getOutputData(1);
    if (out && out->getType() == DataObject::DataType::Measure) {
        vars[QStringLiteral("value")] = out->getMeasureResult().value;
    } else {
        QSharedPointer<DataObject> out0 = node->getOutputData(0);
        if (out0) {
            if (out0->getType() == DataObject::DataType::Number)
                vars[QStringLiteral("value")] = out0->getData().toDouble();
            else if (out0->getType() == DataObject::DataType::String)
                vars[QStringLiteral("value")] = out0->getData().toString();
        }
    }
    {
        // S1 Stage 1：输出变量表写入入锁（三类执行缓存统一受 m_graphCacheMutex 保护）
        QMutexLocker cacheLock(&m_graphCacheMutex);
        m_nodeOutputVars[node->moduleId()] = vars;
    }
}

QString FlowExecutor::resolveParamRefs(const QString &raw) const
{
    QString out = raw;
    // 1) 全局变量引用 {global.变量名}（变量名可含中文）
    static const QRegularExpression gRe(
        QStringLiteral(R"(\{global\.([^}]+)\})"));
    {
        QList<QPair<QString, QString>> grepl;
        QRegularExpressionMatchIterator git = gRe.globalMatch(raw);
        while (git.hasNext()) {
            const QRegularExpressionMatch m = git.next();
            const QString varName = m.captured(1).trimmed();
            QString val;
            auto *gvm = GlobalVariableManager::instance();
            if (gvm && gvm->variableExists(varName)) {
                const QVariant v = gvm->getVariable(varName);
                if (v.typeId() == QMetaType::Double || v.typeId() == QMetaType::Float)
                    val = QString::number(v.toDouble());
                else
                    val = v.toString();
            }
            grepl.append({m.captured(0), val});
        }
        for (const auto &p : grepl)
            out.replace(p.first, p.second);
    }
    // 2) 模块引用 {模块号} / {模块号.参数名}
    static const QRegularExpression re(
        QStringLiteral(R"(\{(\d+)(?:\.([A-Za-z0-9_]+))?\})"));
    QList<QPair<QString, QString>> repl;
    QRegularExpressionMatchIterator it = re.globalMatch(out);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const int modId = m.captured(1).toInt();
        const QString key = m.captured(2).isEmpty() ? QStringLiteral("value")
                                                    : m.captured(2);
        QString val;
        // S1 Stage 1：输出变量表读取同样入锁（本函数在执行线程被调用；加锁后 GUI 侧将来读也安全）
        QMutexLocker cacheLock(&m_graphCacheMutex);
        const auto mit = m_nodeOutputVars.constFind(modId);
        if (mit != m_nodeOutputVars.cend()) {
            const auto &vars = mit.value();
            const auto vit = vars.constFind(key);
            if (vit != vars.cend()) {
                const QVariant &v = vit.value();
                if (v.typeId() == QMetaType::Double || v.typeId() == QMetaType::Float)
                    val = QString::number(v.toDouble());
                else
                    val = v.toString();
            }
        }
        repl.append({m.captured(0), val});
    }
    for (const auto &p : repl)
        out.replace(p.first, p.second);
    return out;
}

void FlowExecutor::executeLoop(NodeBase *loopNode, int loopCount)
{
    const QList<NodeBase *> body = collectLoopBody(loopNode);
    if (body.isEmpty()) return;

    QSet<NodeBase *> bodySet(body.begin(), body.end());

    // 嵌套循环：循环体内若含 LoopNode，其循环体由内层自行调度，
    // 外层不得再重复执行（否则内层循环次数不生效、或被外层多跑一遍）
    QSet<NodeBase *> nestedBodyNodes;
    for (NodeBase *bn : body) {
        if (LoopNode *inner = qobject_cast<LoopNode *>(bn)) {
            for (NodeBase *ib : collectLoopBody(inner)) {
                nestedBodyNodes.insert(ib);
            }
        }
    }

    // 统一调度全部迭代（1..loopCount），循环体内节点看到的迭代号为 1,2,...,loopCount（P3）
    for (int iter = 1; iter <= loopCount; ++iter) {
        // 迭代变量
        // （循环体内节点可经 {循环模块号.iteration} 引用当前次数）
        loopNode->setParam(QStringLiteral("iteration"), iter);
        {
            // S1 Stage 1b：迭代变量写入入锁（后级 {循环模块号.iteration} 由 resolveParamRefs 读取）
            QMutexLocker cacheLock(&m_graphCacheMutex);
            m_nodeOutputVars[loopNode->moduleId()][QStringLiteral("iteration")] = iter;
        }

        // 重新评估循环体激活集合：尊重条件分支，每轮重算（E5/P3）
        for (NodeBase *bn : body) m_activeNodes.remove(bn);
        // 激活入口：循环节点的直接下游，以及由循环体外部输入驱动的节点
        const auto outIt = m_outgoing.constFind(loopNode);
        if (outIt != m_outgoing.cend()) {
            for (MyProject::Connection *c : *outIt) {
                NodeBase *dst = c ? c->getDestinationNode() : nullptr;
                if (dst) m_activeNodes.insert(dst);
            }
        }
        for (NodeBase *bn : body) {
            const auto incIt = m_incoming.constFind(bn);
            if (incIt == m_incoming.cend()) continue;
            bool externalIn = false;
            for (MyProject::Connection *c : *incIt) {
                NodeBase *src = c ? c->getSourceNode() : nullptr;
                if (src && !bodySet.contains(src)) { externalIn = true; break; }
            }
            if (externalIn) m_activeNodes.insert(bn);
        }

        // 迭代开始：暂停 / 停止检查（P5 取消语义）
        {
            QMutexLocker l(&m_mutex);
            if (m_runContext->state() == ExecutionState::Stopped) return;
            if (m_runContext->state() == ExecutionState::Paused) parkWhilePausedLocked();   // 真停稳才上报
            if (m_runContext->state() == ExecutionState::Stopped) return;
        }

        for (NodeBase *bn : body) {
            // 每个循环体节点边界处理暂停（P5）
            {
                QMutexLocker l(&m_mutex);
                if (m_runContext->state() == ExecutionState::Stopped) return;
                if (m_runContext->state() == ExecutionState::Paused) parkWhilePausedLocked();   // 真停稳才上报
                if (m_runContext->state() == ExecutionState::Stopped) return;
            }
            // 内层循环的循环体由内层 LoopNode 调度，外层跳过（嵌套循环）
            if (nestedBodyNodes.contains(bn)) {
                recordNodeSkipped(bn, QStringLiteral("嵌套循环体由内层循环节点调度"));
                continue;
            }

            if (!m_activeNodes.contains(bn)) {
                // 跳过未激活分支：清空输出与缓存，避免下游误用旧数据（E2/E5/P2）
                for (int p = 0; p < bn->outputPorts().size(); ++p)
                    bn->setOutputData(p, QSharedPointer<DataObject>());
                {
                    // S1 Stage 1b：循环体内跳过分支的清理同样入锁
                    QMutexLocker cacheLock(&m_graphCacheMutex);
                    m_nodeData[bn].clear();
                    m_nodeOutputVars[bn->moduleId()].clear();
                    m_validOutputs.remove(bn);   // E2：同上，清空输出即撤销"可复用"标记
                }
                recordNodeSkipped(bn, QStringLiteral("分支未激活"));
                continue;
            }
            executeNode(bn, false);
            activateDownstream(bn);
            // 循环体内嵌套的循环节点：递归调度其循环体（迭代号由内层自行维护）
            if (LoopNode *inner = qobject_cast<LoopNode *>(bn)) {
                executeLoop(inner, qMax(1, bn->getParam(QStringLiteral("loopCount")).toInt()));
            }
            {
                QMutexLocker l(&m_mutex);
                if (m_runContext->state() == ExecutionState::Stopped) return;
            }
            if (!m_lastNodeSuccess && m_stopOnFailure) {
                {
                    QMutexLocker failLocker(&m_mutex);
                    setRunState(ExecutionState::Stopped);
                }
                emit executionError(QStringLiteral("循环体节点 %1 执行失败，流程已停止")
                                        .arg(bn->fullName()));
                return;
            }
        }
    }
}

QList<NodeBase *> FlowExecutor::collectLoopBody(NodeBase *loopNode) const
{
    QList<NodeBase *> body;
    QSet<NodeBase *> visited;
    QQueue<NodeBase *> queue;
    const auto outIt = m_outgoing.constFind(loopNode);
    if (outIt == m_outgoing.cend()) return body;
    for (MyProject::Connection *conn : *outIt) {
        NodeBase *dst = conn->getDestinationNode();
        if (dst && !visited.contains(dst)) { visited.insert(dst); queue.enqueue(dst); }
    }
    while (!queue.isEmpty()) {
        NodeBase *n = queue.dequeue();
        const int inCnt = m_incoming.value(n).size();
        if (inCnt > 1) continue;  // 汇合点（多入边）：不入循环体
        body.append(n);
        const auto it = m_outgoing.constFind(n);
        if (it == m_outgoing.cend()) continue;
        for (MyProject::Connection *conn : *it) {
            NodeBase *dst = conn->getDestinationNode();
            if (dst && !visited.contains(dst)) { visited.insert(dst); queue.enqueue(dst); }
        }
    }
    // 按缓存拓扑序排序，保证循环体内执行顺序
    std::sort(body.begin(), body.end(), [this](NodeBase *a, NodeBase *b) {
        const int ia = m_cachedSortedNodes.indexOf(a);
        const int ib = m_cachedSortedNodes.indexOf(b);
        return ia < ib;
    });
    return body;
}

// ---- FR15.10 运行期子流程 ----

void FlowExecutor::collectSubFlowBodies(const QList<SubFlowDef> &defs, const QList<NodeBase *> &liveNodes)
{
    // worker 线程私有（与 m_loopBodyNodes 同语义）：每轮重收，不做跨轮缓存——
    // 子流程定义可在两次运行之间被增删/改名（不触发图结构脏标记），按轮重收天然一致。
    m_subFlowBodyNodes.clear();
    m_subFlowMembers.clear();
    m_subFlowIOs.clear();
    if (liveNodes.isEmpty())
        return;

    // 只登记"本轮确实被调用点引用"的定义：调用点写了不存在的名字 → 执行时显式报"未定义"
    QSet<QString> referenced;
    for (NodeBase *n : liveNodes) {
        if (qobject_cast<SubFlowNode *>(n)) {
            const QString name =
                n->getParam(QStringLiteral("subFlowName")).toString().trimmed();
            if (!name.isEmpty())
                referenced.insert(name);
        }
    }
    if (referenced.isEmpty())
        return;

    // 段二：定义副本由票口 helper 在同窗第二读带出（此处只吃副本）；成员/出入口解析改从快照
    // 节点本地查（快照列受墓碑机保护 ⇒ 本函数不再解引用场景，nodeByModuleId 的场景依赖归零；
    // 查不到一律 nullptr，与旧 registry 语义等价——调用侧本就按空处理）。
    QMap<int, NodeBase *> nodesById;
    for (NodeBase *n : liveNodes) {
        if (n)
            nodesById.insert(n->moduleId(), n);
    }
    for (const SubFlowDef &def : defs) {
        if (!referenced.contains(def.name))
            continue;

        QList<NodeBase *> members;
        NodeBase *inputNode = nullptr;
        NodeBase *outputNode = nullptr;
        bool broken = false;
        for (int id : def.members) {
            if (NodeBase *m = nodesById.value(id, nullptr))
                members.append(m);
            else {
                broken = true;   // 成员已被删除：登记为"损坏"，执行时可见报错（不静默跑旧逻辑）
                break;
            }
        }
        if (!broken)
            inputNode = def.input >= 0 ? nodesById.value(def.input, nullptr) : nullptr;
        if (!broken)
            outputNode = def.output >= 0 ? nodesById.value(def.output, nullptr) : nullptr;

        if (broken || !inputNode || !outputNode) {
            m_subFlowMembers.insert(def.name, {});
            m_subFlowIOs.insert(def.name, qMakePair(nullptr, nullptr));
            continue;
        }

        // 与循环体交叉的成员让位给循环体（v1 限制：子流程成员不得同时是循环体）。
        QList<NodeBase *> usable;
        for (NodeBase *m : members) {
            if (!m_loopBodyNodes.contains(m))
                usable.append(m);
        }
        // 任何成员被循环体过滤掉都要判坏（2026-10-25 复核 P3-8 + 残留）：
        // 入口/出口被过滤时会拿错数据；**内部成员**被过滤同样是半截子流程——主遍历跳过它、
        // 调用点又没调度它，它根本不会执行，下游读到的还是上一轮的旧值。v1 不支持成员与
        // 循环体交叉，所以一律显式报"定义损坏"，不静默跑半截。
        if (usable.size() != members.size()
            || !usable.contains(inputNode) || !usable.contains(outputNode)) {
            m_subFlowMembers.insert(def.name, {});
            m_subFlowIOs.insert(def.name, qMakePair(nullptr, nullptr));
            continue;
        }
        m_subFlowMembers.insert(def.name, usable);
        m_subFlowIOs.insert(def.name, qMakePair(inputNode, outputNode));
        for (NodeBase *m : usable)
            m_subFlowBodyNodes.insert(m);
    }
}

bool FlowExecutor::executeSubFlow(SubFlowNode *caller)
{
    auto failWith = [this](const QString &msg) {
        emit executionError(msg);
        return false;
    };

    if (!caller)
        return false;
    const QString name = caller->getParam(QStringLiteral("subFlowName")).toString().trimmed();
    if (name.isEmpty())
        return failWith(QStringLiteral("子流程算子「%1」未设置要调用的子流程名称")
                            .arg(caller->fullName()));

    const auto memIt = m_subFlowMembers.constFind(name);
    const auto ioIt = m_subFlowIOs.constFind(name);
    if (memIt == m_subFlowMembers.cend() || ioIt == m_subFlowIOs.cend()) {
        return failWith(QStringLiteral("子流程「%1」未定义（可能已被删除或改名，或名字写错）。调用点：%2")
                            .arg(name, caller->fullName()));
    }
    if (memIt->isEmpty() || !ioIt->first || !ioIt->second) {
        return failWith(QStringLiteral("子流程「%1」定义损坏（成员已被删除，或与循环节点体交叉）。调用点：%2")
                            .arg(name, caller->fullName()));
    }

    // 递归保护：调用链上再次出现同名定义 = 环；深度上限兜底（A→B→A / 自调 / 超深嵌套）
    if (m_subFlowCallStack.contains(name)) {
        return failWith(QStringLiteral("检测到子流程递归调用：%1→「%2」，已拒绝。调用点：%3")
                            .arg(m_subFlowCallStack.join(QStringLiteral("→")), name,
                                 caller->fullName()));
    }
    if (m_subFlowCallStack.size() >= kMaxSubFlowDepth) {
        return failWith(QStringLiteral("子流程嵌套深度超过上限（%1）：%2→「%3」。调用点：%4")
                            .arg(kMaxSubFlowDepth)
                            .arg(m_subFlowCallStack.join(QStringLiteral("→")), name,
                                 caller->fullName()));
    }

    NodeBase *inputNode = ioIt->first;
    NodeBase *outputNode = ioIt->second;

    // 入口喂数据：调用点的输入在 executeNode(caller) 的 propagateData 阶段已备好；
    // 入口成员无来自成员外部的入边（定义时校验），内联执行它的 propagateData 不会覆盖这里的写入。
    const QSharedPointer<DataObject> in = caller->getInputData(0);
    {
        QMutexLocker cacheLock(&m_graphCacheMutex);
        if (in)
            m_nodeData[inputNode][0] = in;
        else {
            m_nodeData[inputNode].remove(0);
            m_validOutputs.remove(inputNode);   // E2：本轮无喂入 ⇒ 撤销"可复用"标记
        }
    }
    inputNode->setInputData(0, in);

    m_subFlowCallStack.append(name);
    bool aborted = false;
    for (NodeBase *bn : memIt.value()) {
        // 成员边界处理暂停/停止（P5 同循环体；真停稳才上报 executionParked）
        {
            QMutexLocker l(&m_mutex);
            if (m_runContext->state() == ExecutionState::Stopped) {
                aborted = true;
                break;
            }
            if (m_runContext->state() == ExecutionState::Paused)
                parkWhilePausedLocked();
            if (m_runContext->state() == ExecutionState::Stopped) {
                aborted = true;
                break;
            }
        }
        // isLastNode=false：子图末节点不得误发 imageReady（图像预览只属于主图末端，同循环体先例）
        executeNode(bn, false);
        activateDownstream(bn);
        // 成员内的循环节点：其循环体由内层自行调度（嵌套语义同 executeLoop）
        if (LoopNode *inner = qobject_cast<LoopNode *>(bn)) {
            executeLoop(inner, qMax(1, bn->getParam(QStringLiteral("loopCount")).toInt()));
        }
        {
            QMutexLocker l(&m_mutex);
            if (m_runContext->state() == ExecutionState::Stopped) {
                aborted = true;
                break;
            }
        }
        if (!m_lastNodeSuccess && m_stopOnFailure) {
            m_subFlowCallStack.removeLast();
            return failWith(QStringLiteral("子流程「%1」的算子「%2」执行失败。调用点：%3")
                                .arg(name, bn->fullName(), caller->fullName()));
        }
    }
    m_subFlowCallStack.removeLast();
    if (aborted) {
        // 人为停止 ≠ 执行失败（2026-10-25 复核 P2-1 残留）：原实现返回 false，调用点被判失败，
        // 默认 stopOnFailure 立刻把它报成"节点 … 执行失败"并写 NG 汇总——一次人为中断被记成
        // 一次产品失败。这里按"中止"收口：置轮中止位、输出留空、返回成功，让主循环的状态检查
        // 或轮末统一判定收尾。
        m_roundAborted = true;
        caller->setOutputData(0, QSharedPointer<DataObject>());
        return true;
    }

    // 出口结果 → 调用点输出端口 0。executeNode(caller) 在 execute() 返回后统一
    // 读取输出端口写缓存/发信号，下游由此拿到数据（与普通算子同一出口，无特殊路径）。
    QSharedPointer<DataObject> out;
    {
        QMutexLocker cacheLock(&m_graphCacheMutex);
        out = m_nodeData.value(outputNode).value(0);
    }
    if (!out)
        out = outputNode->getOutputData(0);
    caller->setOutputData(0, out);
    return true;
}

void FlowExecutor::propagateData(NodeBase *node)
{
    if (!node) {
        return;
    }

    const auto incIt = m_incoming.constFind(node);
    if (incIt == m_incoming.cend()) {
        return;
    }

    for (MyProject::Connection *conn : *incIt) {
        if (!conn) {
            continue;
        }
        NodeBase *sourceNode = conn->getSourceNode();
        if (!sourceNode) {
            continue;
        }
        const int sourcePort = conn->getSourcePort();
        const int destPort = conn->getDestinationPort();

        DataObjectPtr data;
        {
            // S1 Stage 1b：读上游缓存入锁（端口回退取值走端口自身的锁，放在图锁之外）
            QMutexLocker cacheLock(&m_graphCacheMutex);
            if (m_nodeData.contains(sourceNode) && m_nodeData[sourceNode].contains(sourcePort)) {
                data = m_nodeData[sourceNode][sourcePort];
            }
        }
        if (!data) {
            data = sourceNode->getOutputData(sourcePort);
        }

        if (data) {
            data->setSourceInfo(QString("%1 的输出").arg(sourceNode->fullName()));
        }
        // 显式传播（含空值）：每轮覆盖下游旧输入，避免上一轮数据进入本轮检测（E2）
        {
            // S1 Stage 1b：写下游缓存入锁（setInputData 走端口自身的锁，放在图锁之外）
            QMutexLocker cacheLock(&m_graphCacheMutex);
            m_nodeData[node][destPort] = data;
        }
        node->setInputData(destPort, data);
    }
}

void FlowExecutor::resetState()
{
    {
        // S1 Stage 1b：重启清空缓存入锁
        //
        // E2（2026-09-25 更正）：原注释写"m_validOutputs 不在清除之列——由轮首剪枝处理"，该说法不成立：
        // rebuildIncomingIndex 的剪枝只摘除"**已不在场景**"的节点条目，仍活着的节点会带着上一轮的
        // "输出有效"位跨过这次重启，而它的 m_nodeData 刚刚被整表清空。整表清空 ⇒ 整表撤销。
        QMutexLocker cacheLock(&m_graphCacheMutex);
        m_nodeData.clear();
        m_validOutputs.clear();
        m_nodeOutputVars.clear();   // 重新启动清空变量缓存，避免引用上一轮数值（P2）
    }
    m_executionQueue.clear();
}

FlowRuntimeStats FlowExecutor::runtimeStats() const
{
    QMutexLocker locker(&m_statsMutex);
    FlowRuntimeStats snapshot = m_stats;
    // 采样放在查询侧：避免每轮都做系统调用，同时保证任何时刻取到的
    // 句柄数/内存都是查询时刻的值（长跑诊断关心的是"现在多少"）
    snapshot.sampleProcessResources();
    return snapshot;
}

void FlowExecutor::resetRuntimeStats()
{
    QMutexLocker locker(&m_statsMutex);
    m_stats.reset();
}

void FlowExecutor::setStatsLogIntervalMs(int ms)
{
    QMutexLocker locker(&m_statsMutex);
    m_statsLogIntervalMs = qMax(0, ms);
}

void FlowExecutor::recordNodeSkipped(NodeBase *node, const QString &reason)
{
    if (!node) {
        return;
    }
    {
        QMutexLocker locker(&m_statsMutex);
        m_stats.onNodeSkipped(node->fullName());
    }
    // 三态可视化：把"跳过"从统计数字变成 UI 可消费的事件（跨线程排队到界面线程）
    emit nodeSkipped(node, reason);
}

void FlowExecutor::recordRoundFinished(qint64 roundMs)
{
    const bool hadFailure = m_roundHadFailure;
    m_roundHadFailure = false;
    m_roundAborted = false;   // 与 hadFailure 同周期复位（worker 线程私有，仅此处写）

    bool shouldLog = false;
    QString line;
    {
        QMutexLocker locker(&m_statsMutex);
        m_stats.onRoundFinished(roundMs, hadFailure);
        // 首个轮次立即输出一条（便于确认埋点生效），之后按间隔节流
        if (m_statsLogIntervalMs > 0
            && (!m_statsLogTimer.isValid() || m_statsLogTimer.elapsed() >= m_statsLogIntervalMs)) {
            m_stats.sampleProcessResources();
            m_statsLogTimer.start();
            line = m_stats.summary();
            shouldLog = true;
        }
    }
    if (shouldLog) {
        VFP_RUNTIME_INFO.noquote() << QStringLiteral("runtime stats:") << line;
    }
}

bool FlowExecutor::interruptibleSleep(int ms)
{
    // 可取消等待已收进运行上下文块（P2b 期）：状态与取消信号在块内（块的互斥量是叶子），
    // 语义与原来那段逐式同形——停止即醒并返回未完成；暂停期间不扣延时预算；恢复后继续扣。
    return m_runContext->interruptibleSleep(ms);
}

void FlowExecutor::executeUpTo(NodeBase *endNode)
{
    if (!endNode)
        return;
    // 暂停中不可再起同步执行：否则与仍在挂起的工作线程并发跑同一批节点（双跑写设备算子）。
    const ExecutionState st = getState();
    if (st == ExecutionState::Running || st == ExecutionState::Paused)
        return;

    // S1：同步执行同样走快照（UI 线程单发；持快照期间删除走墓碑，函数返回即释放）。
    // 段二：借面改走存活控制块（同族同面）；未借到（亡/未绑）＝与「无起点」同形：静默返回。
    // 段三（P1-6）：副本在 m_mutex 内抄后喂票口（发布/拷贝同锁；直传成员＝锁外读 shared_ptr 对象，
    // 与 setFlowScene 的同锁赋值成竞争面）。
    std::shared_ptr<FlowSceneAliveBlock> blockCopy;
    {
        QMutexLocker sceneLock(&m_mutex);
        blockCopy = m_aliveBlock;
    }
    FlowScene::GraphSnapshotGuard graphSnapshot = captureViaAliveBlock(blockCopy);
    if (!graphSnapshot.isHeld())
        return;
    const QList<NodeBase *> nodes = graphSnapshot.snapshot().nodes;
    QList<NodeBase *> sorted;
    {
        QMutexLocker cacheLock(&m_graphCacheMutex);
        applyPendingCachePurgesLocked();   // S1 Stage 1b：同步执行入口同样消费待清理登记
        rebuildIncomingIndex(nodes, graphSnapshot.snapshot().connections);
        if (!topologicalSort(nodes, sorted))
            return;
        m_cachedSortedNodes = sorted;
        m_graphStructureDirty = false;
    }

    QSet<NodeBase *> keep;
    std::function<void(NodeBase *)> walkUp = [&](NodeBase *n) {
        if (!n || keep.contains(n))
            return;
        keep.insert(n);
        const auto it = m_incoming.constFind(n);
        if (it == m_incoming.cend())
            return;
        for (MyProject::Connection *c : *it) {
            if (c)
                walkUp(c->getSourceNode());
        }
    };
    walkUp(endNode);

    for (NodeBase *node : sorted) {
        if (!keep.contains(node))
            continue;
        executeNode(node, node == endNode);
        if (!m_lastNodeSuccess && m_stopOnFailure)
            break;
    }
}

void FlowExecutor::executeFrom(NodeBase *startNode)
{
    if (!startNode)
        return;
    // 暂停中不可再起同步执行：否则与仍在挂起的工作线程并发跑同一批节点（双跑写设备算子）。
    const ExecutionState st = getState();
    if (st == ExecutionState::Running || st == ExecutionState::Paused)
        return;

    // S1：同步执行同样走快照（UI 线程单发；持快照期间删除走墓碑，函数返回即释放）。
    // 段二：借面改走存活控制块（同族同面）；未借到（亡/未绑）＝与「无起点」同形：静默返回。
    // 段三（P1-6）：副本在 m_mutex 内抄后喂票口（发布/拷贝同锁；直传成员＝锁外读 shared_ptr 对象，
    // 与 setFlowScene 的同锁赋值成竞争面）。
    std::shared_ptr<FlowSceneAliveBlock> blockCopy;
    {
        QMutexLocker sceneLock(&m_mutex);
        blockCopy = m_aliveBlock;
    }
    FlowScene::GraphSnapshotGuard graphSnapshot = captureViaAliveBlock(blockCopy);
    if (!graphSnapshot.isHeld())
        return;
    const QList<NodeBase *> nodes = graphSnapshot.snapshot().nodes;
    QList<NodeBase *> sorted;
    {
        QMutexLocker cacheLock(&m_graphCacheMutex);
        applyPendingCachePurgesLocked();   // S1 Stage 1b：同步执行入口同样消费待清理登记
        rebuildIncomingIndex(nodes, graphSnapshot.snapshot().connections);
        if (!topologicalSort(nodes, sorted))
            return;
        m_cachedSortedNodes = sorted;
        m_graphStructureDirty = false;
    }

    QSet<NodeBase *> keep;
    std::function<void(NodeBase *)> walkDown = [&](NodeBase *n) {
        if (!n || keep.contains(n))
            return;
        keep.insert(n);
        const auto it = m_outgoing.constFind(n);
        if (it == m_outgoing.cend())
            return;
        for (MyProject::Connection *c : *it) {
            if (c)
                walkDown(c->getDestinationNode());
        }
    };
    walkDown(startNode);

    // 末节点判定的修正：isLastNode 只用来决定"是否把执行结果推到预览"，
    // 它应当是**本次实际执行的最后一个节点**（keep 集合的末端），而不是整张图的拓扑末端。
    // 此前传 sorted.last()，导致「执行到此/重算下游」这类局部执行永远不满足条件，
    // 结果算完了但预览不刷新（表现为"点了没反应"）。
    NodeBase *lastKept = nullptr;
    for (NodeBase *node : sorted) {
        if (keep.contains(node))
            lastKept = node;
    }

    for (NodeBase *node : sorted) {
        if (!keep.contains(node))
            continue;
        executeNode(node, node == lastKept);
        if (!m_lastNodeSuccess && m_stopOnFailure)
            break;
    }
}

void FlowExecutor::invalidateDownstreamOf(NodeBase *startNode)
{
    if (!startNode) {
        return;
    }

    // 运行/暂停中不得触碰执行器缓存：缓存由执行线程独占使用，界面线程清空会与之
    // 竞争（QMap/QHash 结构破坏 → 随机崩溃或把旧数据喂给算子）。此场景跳过作废
    // 是安全的：下次 startExecution 的 resetState 会整体清缓存。
    {
        QMutexLocker stateLock(&m_mutex);
        if (m_runContext->state() == ExecutionState::Running || m_runContext->state() == ExecutionState::Paused) {
            VFP_DEBUG << "invalidateDownstreamOf ignored: flow is running/paused";
            return;
        }
    }

    // 沿出边做传递闭包，收集 startNode 自身及其全部下游。
    // m_outgoing 由 rebuildIncomingIndex() 构建，受 m_graphCacheMutex 保护。
    QSet<NodeBase *> affected;
    // S1：本次索引重建 + 下游遍历期间持一份拓扑快照（函数返回即释放），
    // 避免遍历 m_outgoing 时节点/连边被并发析构（删除会走墓碑延迟到快照释放）。
    FlowScene::GraphSnapshotGuard graphSnapshot;
    {
        QMutexLocker graphLocker(&m_graphCacheMutex);
        // 图结构有变更（撤销/删除节点等）时 m_outgoing 可能仍指向已释放的连接/节点，
        // 直接遍历会踩悬垂指针——先按当前场景重建索引（含缓存剪枝），再遍历。
        if (m_graphStructureDirty) {
            // 段二：借面改走存活控制块（同族同面）；未借到走 else 分支＝旧"无场景"行为原样保留
            //（清缓存并标清洁；场景亡后不再重建、留给下次在世重建）。
            // 段三（P1-6）：副本在 m_mutex 内抄后喂票口（持图缓存锁再取 m_mutex＝既有同向次序；
            // 发布/拷贝同锁，与 setFlowScene 成对）。
            std::shared_ptr<FlowSceneAliveBlock> blockCopy;
            {
                QMutexLocker sceneLock(&m_mutex);
                blockCopy = m_aliveBlock;
            }
            graphSnapshot = captureViaAliveBlock(blockCopy);
            if (graphSnapshot.isHeld()) {
                const QList<NodeBase *> &snapNodes = graphSnapshot.snapshot().nodes;
                rebuildIncomingIndex(snapNodes, graphSnapshot.snapshot().connections);
                QList<NodeBase *> sorted;
                if (topologicalSort(snapNodes, sorted))
                    m_cachedSortedNodes = sorted;
                m_graphStructureDirty = false;
            } else {
                m_incoming.clear();
                m_outgoing.clear();
                m_cachedSortedNodes.clear();
                m_loopBodyNodes.clear();
                m_subFlowBodyNodes.clear();
                m_subFlowMembers.clear();
                m_subFlowIOs.clear();
                m_graphStructureDirty = false;
            }
        }
        QQueue<NodeBase *> queue;
        queue.enqueue(startNode);
        affected.insert(startNode);
        while (!queue.isEmpty()) {
            NodeBase *cur = queue.dequeue();
            const auto it = m_outgoing.constFind(cur);
            if (it == m_outgoing.cend()) {
                continue;
            }
            for (MyProject::Connection *conn : *it) {
                NodeBase *dst = conn ? conn->getDestinationNode() : nullptr;
                if (dst && !affected.contains(dst)) {
                    affected.insert(dst);
                    queue.enqueue(dst);
                }
            }
        }
    }

    // 逐节点清空输出端口数据（节点自身的端口锁，与主遍历清理时同一用法）。
    for (NodeBase *n : affected) {
        if (!n) {
            continue;
        }
        // setOutputData 以「输出端口索引」为参数（与主遍历清理缓存时的用法一致）
        const QList<Port *> outPorts = n->outputPorts();
        for (int i = 0; i < outPorts.size(); ++i) {
            n->setOutputData(i, QSharedPointer<DataObject>());
        }
    }

    // 三类执行缓存统一入锁（2026-10-25 复核 P3-2）：头文件写明 m_nodeData /
    // m_nodeOutputVars / m_validOutputs 受 m_graphCacheMutex 保护，而这里此前在锁外清——
    // 与执行线程的缓存写入不是同一套纪律。状态检查只挡住了 Running/Paused 这个窗口，
    // 纪律仍应一致。
    {
        QMutexLocker cacheLock(&m_graphCacheMutex);
        for (NodeBase *n : affected) {
            if (!n) {
                continue;
            }
            m_nodeData[n].clear();
            m_nodeOutputVars[n->moduleId()].clear();
            // 同时判为失效：否则局部执行会把"已被作废的输出"当成可复用结果（数据其实是空的/旧的）
            m_validOutputs.remove(n);
        }
    }
}
