#include "FlowExecutor.h"
#include "FlowScene.h"
#include "NodeBase.h"
#include "HalconNode.h"
#include "ConditionalNode.h"
#include "LoopNode.h"
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

FlowExecutor::FlowExecutor(QObject *parent)
    : QThread(parent)
    , m_scene(nullptr)
    , m_state(ExecutionState::Idle)
    , m_flowMode(FlowMode::SoftwareTrigger)
{
    s_currentInstance = this;

    // 全局转发连线（CM dataReceived → GTM onDataReceived）已迁到 GlobalTriggerManager 构造里
    // 建立**一次**，不再寄生在"有人建过执行器"上——此前单跑触发用例 / 载入方案后无执行器的
    // 触发路径因链路未建立而整条失效（假阴性 + 负向断言假阳性）。见 GlobalTriggerManager 构造。
    // 本执行器只接自己这一份 triggerFired（按 flowName 过滤，互不干扰）。

    // 本执行器专属：外部触发 → 启动（按 flowName 过滤，互不干扰）
    connect(GlobalTriggerManager::instance(), &GlobalTriggerManager::triggerFired,
            this, [this](const QString &flowName, const QString &triggerSource) {
        Q_UNUSED(triggerSource)
        if (m_flowName == flowName && m_state == ExecutionState::Idle) {
            // 硬触发模式禁止外部触发启动
            if (!canTriggerFromExternal()) {
                VFP_DEBUG << "Flow" << m_flowName
                          << "in hardware-trigger mode, external trigger ignored";
                return;
            }
            startExecution();
        }
    });
}

FlowExecutor::~FlowExecutor()
{
    GlobalTriggerManager::instance()->unregisterExecutor(this);
    if (s_currentInstance.load() == this) {
        s_currentInstance.store(nullptr);
    }
    stopExecution();
    // 带超时等待，防止流程线程死循环/等待外部事件时析构永久阻塞
    if (!wait(5000)) {
        VFP_DEBUG << "FlowExecutor thread did not exit within 5000ms; state=" << int(m_state);
    }
    disconnectFromScene();
}

FlowExecutor *FlowExecutor::current()
{
    return s_currentInstance;
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

void FlowExecutor::onSceneNodeRemoved(NodeBase *node)
{
    // 暂不在此直接改写执行缓存：m_nodeData / m_validOutputs / m_nodeOutputVars 由执行线程**无锁**
    // 写入（见 executeNode / collectNodeOutputVars / propagateData），本槽在 GUI 线程并发对同一 QMap
    // 增删会触发跨线程竞态（堆损坏，表现为全量测试随机段错误）。该竞态是既有的"锁纪律"缺陷，
    // 我的 S4 改动只是新增了一个 GUI 线程的并发访问把它暴露出来——已在父提交稳定、本改动触发即为证。
    // 正确修法是让所有缓存访问统一受 m_graphCacheMutex 保护（单独的锁纪律重构，不在 S4 半修）。
    // 注：S4 的"模块号不再回收"已让 moduleId 维度（m_nodeOutputVars）由 rebuildIncomingIndex 的
    // 懒剪枝稳定处理（回收号永不复现 → 永远被剪枝），本槽留空不影响该修复。
    Q_UNUSED(node)
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
    // S4：节点被删除/撤销/清空时立即清掉它在执行缓存里的条目，而不仅依赖
    // rebuildIncomingIndex 的"是否仍在场景"懒剪枝（后者无法区分指针地址复用与模块号复用）。
    QObject::connect(scene, &FlowScene::nodeRemoved, this, &FlowExecutor::onSceneNodeRemoved,
                     Qt::UniqueConnection);
}

void FlowExecutor::markGraphStructureDirty()
{
    QMutexLocker locker(&m_graphCacheMutex);
    m_graphStructureDirty = true;
}

void FlowExecutor::setFlowScene(FlowScene *scene)
{
    disconnectFromScene();
    {
        QMutexLocker locker(&m_mutex);
        m_scene = scene;
    }
    {
        QMutexLocker locker(&m_graphCacheMutex);
        m_graphStructureDirty = true;
        // 切换场景：立即清空上一场景的图缓存。否则下一次 rebuildIncomingIndex
        // 识别循环体时会误用旧场景的节点（漏识别循环体），若旧场景已销毁还会
        // 解引用已释放的节点指针（P1）
        m_cachedSortedNodes.clear();
        m_incoming.clear();
        m_outgoing.clear();
        m_loopBodyNodes.clear();
    }
    connectToScene(scene);
    // 多流程并发：最近激活的流程执行器作为 current()（供节点查询运行状态）
    if (scene)
        s_currentInstance = this;
}

void FlowExecutor::startExecution()
{
    QMutexLocker locker(&m_mutex);
    if (m_state == ExecutionState::Running) {
        return;
    }
    
    m_state = ExecutionState::Running;
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
    if (m_state == ExecutionState::Running) {
        m_state = ExecutionState::Paused;
        // 唤醒正在可取消等待中的阻塞节点（延时等），使其停止计时并等待恢复（P5）
        m_waitCondition.wakeAll();
        emit executionPaused();
    }
}

void FlowExecutor::resumeExecution()
{
    QMutexLocker locker(&m_mutex);
    if (m_state == ExecutionState::Paused) {
        m_stepMode = false;   // 显式"继续"= 退出单步模式，回到正常连续运行
        m_state = ExecutionState::Running;
        m_waitCondition.wakeAll();
        emit executionResumed();
    }
}

void FlowExecutor::stepExecution()
{
    QMutexLocker locker(&m_mutex);
    m_stepMode = true;
    if (m_state == ExecutionState::Paused) {
        m_state = ExecutionState::Running;
        m_waitCondition.wakeAll();
        emit executionResumed();
    } else if (m_state == ExecutionState::Idle || m_state == ExecutionState::Stopped) {
        // 未运行时：从首个节点开始步进
        m_stepMode = true;
        locker.unlock();
        startExecution();
    }
}

void FlowExecutor::stopExecution()
{
    QMutexLocker locker(&m_mutex);
    m_state = ExecutionState::Stopped;
    m_stepMode = false;
    m_waitCondition.wakeAll();
}

void FlowExecutor::exitStepMode()
{
    // 历史缺陷：m_stepMode 只在 stopExecution 里清，单步用过一次后"开始执行"
    // 仍然每节点暂停——单步模式粘住，流程无法回到正常连续运行（单步"一次性失效"）。
    QMutexLocker locker(&m_mutex);
    m_stepMode = false;
}

ExecutionState FlowExecutor::getState() const
{
    QMutexLocker locker(&m_mutex);
    return m_state;
}

void FlowExecutor::setFlowMode(FlowMode mode)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_mutex);
        if (m_flowMode != mode) {
            m_flowMode = mode;
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
    QMutexLocker locker(&m_mutex);
    return m_flowMode;
}

void FlowExecutor::run()
{
    emit executionStarted();
    
    while (true) {
        QMutexLocker locker(&m_mutex);
        
        if (m_state == ExecutionState::Stopped) {
            emit executionStopped();
            break;
        }
        
        if (m_state == ExecutionState::Paused) {
            m_waitCondition.wait(&m_mutex);
            continue;
        }
        
        if (m_state != ExecutionState::Running) {
            break;
        }
        
        locker.unlock();

        // 本轮计时（运行期统计用）
        QElapsedTimer roundTimer;
        roundTimer.start();

        FlowScene *scene = nullptr;
        {
            QMutexLocker sceneLocker(&m_mutex);
            scene = m_scene;
        }
        if (!scene) {
            emit executionError(tr("Flow scene is not set"));
            emit executionStopped();   // 终态信号：错误路径不发终态会让界面按钮停在"运行中"
            break;
        }

        // S1：本轮捕获一次拓扑快照（RAII 句柄，随迭代结束 / break / continue 自动释放）。
        // 持快照期间删除节点/连边只做墓碑延迟析构，整轮可安全使用这批裸指针；轮内不再访问场景容器。
        FlowScene::GraphSnapshotGuard graphSnapshot = scene->captureGraphSnapshot();
        const QList<NodeBase *> nodes = graphSnapshot.snapshot().nodes;
        if (nodes.isEmpty()) {
            emit executionStopped();   // 空场景同样需要终态信号（历史缺陷：静默退出，UI 卡"运行中"）
            break;
        }

        QList<NodeBase *> sortedNodes;
        {
            QMutexLocker cacheLock(&m_graphCacheMutex);
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

        for (int i = 0; i < sortedNodes.size(); i++) {
            NodeBase *node = sortedNodes[i];
            // 循环体节点由所属 LoopNode 统一调度执行，主遍历不再重复执行（P3）
            if (m_loopBodyNodes.contains(node)) {
                VFP_EXEC_DEBUG << "Node skipped (loop body, scheduled by LoopNode):" << node->fullName();
                recordNodeSkipped(node, QStringLiteral("循环体由循环节点统一调度"));
                continue;
            }
            VFP_EXEC_DEBUG << "Executing node:" << node->fullName();
            locker.relock();
            if (m_state == ExecutionState::Stopped) {
                emit executionStopped();
                break;
            }
            
            while (m_state == ExecutionState::Paused) {
                m_waitCondition.wait(&m_mutex);
            }
            
            if (m_state == ExecutionState::Stopped) {
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
                m_nodeData[node].clear();   // 清空缓存，避免下游误用上一轮数据（E2）
                m_nodeOutputVars[node->moduleId()].clear();  // 清空变量缓存，避免引用上一轮数值（P2）
                recordNodeSkipped(node, QStringLiteral("分支未激活"));
                continue;
            }
            
            executeNode(node, i == sortedNodes.size() - 1);
            // 执行后激活下游（条件节点仅激活被选中分支）
            activateDownstream(node);

            // 循环执行：由 LoopNode 统一调度全部迭代（1..loopCount），主遍历已跳过循环体节点（P3）
            if (LoopNode *loopNode = qobject_cast<LoopNode *>(node)) {
                const int cnt = qMax(1, node->getParam(QStringLiteral("loopCount")).toInt());
                executeLoop(loopNode, cnt);
                if (!m_lastNodeSuccess && m_stopOnFailure) {
                    QMutexLocker fl(&m_mutex);
                    m_state = ExecutionState::Stopped;
                    break;
                }
            }

            // 失败中断策略：节点失败且开启停止时，立即停止流程（对标 VisionMaster）
            if (!m_lastNodeSuccess && m_stopOnFailure) {
                {
                    QMutexLocker failLocker(&m_mutex);
                    m_state = ExecutionState::Stopped;
                }
                emit executionError(QStringLiteral("节点 %1 执行失败，流程已停止（可在系统菜单关闭\"失败时停止\"）")
                                        .arg(node->fullName()));
                break;
            }

            // 单步模式：每执行完一个节点后暂停，等待下一步命令
            {
                QMutexLocker stepLocker(&m_mutex);
                if (m_stepMode && m_state == ExecutionState::Running) {
                    m_state = ExecutionState::Paused;
                    emit executionPaused();
                }
            }
        }
        
        // 轮末批量落库：整轮结果一个事务提交。放在统计之前，保证本轮记录已落库。
        // 未启用数据库（databasePath 为空）时只清缓冲，不做任何 IO。
        if (!m_pendingResults.isEmpty()) {
            const QList<InspectionRecord> batch = m_pendingResults;
            m_pendingResults.clear();
            if (!AppDatabase::instance()->databasePath().isEmpty()) {
                AppDatabase::instance()->saveInspectionResults(batch);
            }
        }

        // 轮次统计 + 周期性进程资源采样/日志
        recordRoundFinished(roundTimer.elapsed());

        emit executionFinished();

        // 连续模式 / 硬触发模式：自动循环执行（硬触发模式由相机触发帧门控每次循环）
        FlowMode currentMode;
        {
            QMutexLocker ml(&m_mutex);
            currentMode = m_flowMode;
        }
        if (currentMode == FlowMode::Continuous || currentMode == FlowMode::HardwareTrigger) {
            // 检查是否已被手动停止
            {
                QMutexLocker ml(&m_mutex);
                if (m_state != ExecutionState::Running) {
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
        break;
    }
    
    QMutexLocker locker(&m_mutex);
    m_state = ExecutionState::Idle;
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

void FlowExecutor::executeNode(NodeBase *node, bool isLastNode)
{
    if (!node) {
        return;
    }

    // 多流程隔离：把所属执行器上下文交给节点，供其查询运行状态（E3）
    node->setOwnerExecutor(this);

    bool success = false;
    QElapsedTimer nodeTimer;
    nodeTimer.start();

    // 参数引用解析备份：仅本轮临时替换为解析值，执行后还原表达式，避免永久写回（E1）
    QList<QPair<QString, QString>> paramRefBackups;

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
            for (int i = 0; i < node->outputPorts().size(); i++) {
                QSharedPointer<DataObject> outputData = node->getOutputData(i);
                if (outputData) {
                    outputData->setSourceInfo(QString("%1 的输出").arg(node->fullName()));
                    m_nodeData[node][i] = outputData;
                    if (i == 0) {
                        nodeOut0 = outputData;
                    }
                } else {
                    // 本轮该端口无输出：移除上一轮残留，否则下游会读到旧数据
                    m_nodeData[node].remove(i);
                }
            }
            // 标记"输出有效"：供局部执行的可复用判定（见 reusesCachedOutput）
            m_validOutputs[node] = true;
        } else {
            for (int p = 0; p < node->outputPorts().size(); ++p)
                node->setOutputData(p, QSharedPointer<DataObject>());
            m_nodeData[node].clear();
            m_nodeOutputVars[node->moduleId()].clear();
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

        // 发出nodeExecuted信号，通知UI更新
        emit nodeExecuted(node, success);

        // 发出节点执行耗时信号
        emit nodeExecutionTime(node, nodeElapsed);

        // 输出变量快照 → 结果数据表（成功时带本轮各输出项；失败时为空，由面板标注失败）
        {
            QVariantMap vars;
            if (success) {
                const QHash<QString, QVariant> collected = m_nodeOutputVars.value(node->moduleId());
                for (auto it = collected.cbegin(); it != collected.cend(); ++it)
                    vars.insert(it.key(), it.value());
            }
            emit nodeOutputsUpdated(node, success, nodeElapsed, vars);
        }

        // Phase 4: \u4FDD\u5B58\u68C0\u6D4B\u7ED3\u679C\u5230\u6570\u636E\u5E93
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
        QString error = tr("Error executing node %1: %2").arg(node->name()).arg(e.what());
        emit executionError(error);
        emit nodeExecuted(node, false);
        m_lastNodeSuccess = false;
    }

    // 还原参数表达式，确保下一轮重新解析上游最新值（E1）
    for (const auto &b : paramRefBackups) {
        node->setParam(b.first, b.second);
    }

    m_lastNodeSuccess = success;
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
    m_nodeOutputVars[node->moduleId()] = vars;
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
        m_nodeOutputVars[loopNode->moduleId()][QStringLiteral("iteration")] = iter;

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
            if (m_state == ExecutionState::Stopped) return;
            while (m_state == ExecutionState::Paused) m_waitCondition.wait(&m_mutex);
            if (m_state == ExecutionState::Stopped) return;
        }

        for (NodeBase *bn : body) {
            // 每个循环体节点边界处理暂停（P5）
            {
                QMutexLocker l(&m_mutex);
                if (m_state == ExecutionState::Stopped) return;
                while (m_state == ExecutionState::Paused) m_waitCondition.wait(&m_mutex);
                if (m_state == ExecutionState::Stopped) return;
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
                m_nodeData[bn].clear();
                m_nodeOutputVars[bn->moduleId()].clear();
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
                if (m_state == ExecutionState::Stopped) return;
            }
            if (!m_lastNodeSuccess && m_stopOnFailure) {
                {
                    QMutexLocker failLocker(&m_mutex);
                    m_state = ExecutionState::Stopped;
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
        if (m_nodeData.contains(sourceNode) && m_nodeData[sourceNode].contains(sourcePort)) {
            data = m_nodeData[sourceNode][sourcePort];
        } else {
            data = sourceNode->getOutputData(sourcePort);
        }

        if (data) {
            data->setSourceInfo(QString("%1 的输出").arg(sourceNode->fullName()));
        }
        // 显式传播（含空值）：每轮覆盖下游旧输入，避免上一轮数据进入本轮检测（E2）
        m_nodeData[node][destPort] = data;
        node->setInputData(destPort, data);
    }
}

void FlowExecutor::resetState()
{
    m_nodeData.clear();
    m_nodeOutputVars.clear();   // 重新启动清空变量缓存，避免引用上一轮数值（P2）
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
    if (ms <= 0) return true;
    QMutexLocker locker(&m_mutex);
    if (m_state == ExecutionState::Stopped) return false;
    // 可取消等待：维护剩余时间，暂停期间不消耗延时预算，避免恢复时提前结束（E4/P5）
    qint64 remaining = ms;
    while (remaining > 0) {
        if (m_state == ExecutionState::Stopped) return false;
        if (m_state == ExecutionState::Paused) {
            m_waitCondition.wait(&m_mutex);  // 暂停时一直等待，直到恢复唤醒
            continue;                        // 不扣减 remaining（暂停时间不计入延时）
        }
        QElapsedTimer t;
        t.start();
        m_waitCondition.wait(&m_mutex, remaining);
        remaining -= t.elapsed();            // 仅扣减实际等待时间
    }
    return true;
}

void FlowExecutor::executeUpTo(NodeBase *endNode)
{
    if (!endNode || !m_scene)
        return;
    // 暂停中不可再起同步执行：否则与仍在挂起的工作线程并发跑同一批节点（双跑写设备算子）。
    const ExecutionState st = getState();
    if (st == ExecutionState::Running || st == ExecutionState::Paused)
        return;

    // S1：同步执行同样走快照（UI 线程单发；持快照期间删除走墓碑，函数返回即释放）
    FlowScene::GraphSnapshotGuard graphSnapshot = m_scene->captureGraphSnapshot();
    const QList<NodeBase *> nodes = graphSnapshot.snapshot().nodes;
    QList<NodeBase *> sorted;
    {
        QMutexLocker cacheLock(&m_graphCacheMutex);
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
    if (!startNode || !m_scene)
        return;
    // 暂停中不可再起同步执行：否则与仍在挂起的工作线程并发跑同一批节点（双跑写设备算子）。
    const ExecutionState st = getState();
    if (st == ExecutionState::Running || st == ExecutionState::Paused)
        return;

    // S1：同步执行同样走快照（UI 线程单发；持快照期间删除走墓碑，函数返回即释放）
    FlowScene::GraphSnapshotGuard graphSnapshot = m_scene->captureGraphSnapshot();
    const QList<NodeBase *> nodes = graphSnapshot.snapshot().nodes;
    QList<NodeBase *> sorted;
    {
        QMutexLocker cacheLock(&m_graphCacheMutex);
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
        if (m_state == ExecutionState::Running || m_state == ExecutionState::Paused) {
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
            FlowScene *scene = nullptr;
            {
                QMutexLocker sceneLock(&m_mutex);
                scene = m_scene;
            }
            if (scene) {
                graphSnapshot = scene->captureGraphSnapshot();
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

    // 逐节点清空三类缓存：输出端口数据、按端口的 m_nodeData、{模块号.参数名} 输出变量表。
    for (NodeBase *n : affected) {
        if (!n) {
            continue;
        }
        // setOutputData 以「输出端口索引」为参数（与主遍历清理缓存时的用法一致）
        const QList<Port *> outPorts = n->outputPorts();
        for (int i = 0; i < outPorts.size(); ++i) {
            n->setOutputData(i, QSharedPointer<DataObject>());
        }
        m_nodeData[n].clear();
        m_nodeOutputVars[n->moduleId()].clear();
        // 同时判为失效：否则局部执行会把"已被作废的输出"当成可复用结果（数据其实是空的/旧的）
        m_validOutputs.remove(n);
    }
}
