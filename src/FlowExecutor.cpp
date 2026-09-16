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

    // Connect to CommunicationManager data signals for trigger matching
    connect(CommunicationManager::instance(), &CommunicationManager::dataReceived,
            GlobalTriggerManager::instance(), &GlobalTriggerManager::onDataReceived);
    // Connect CommunicationManager receive events to GlobalTriggerManager event triggers
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
    GlobalTriggerManager::instance()->unregisterFlow(m_flowName);
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
            break;
        }

        QList<NodeBase *> nodes = scene->nodes();
        if (nodes.isEmpty()) {
            break;
        }

        QList<NodeBase *> sortedNodes;
        {
            QMutexLocker cacheLock(&m_graphCacheMutex);
            const bool needRebuild =
                m_graphStructureDirty || m_cachedSortedNodes.isEmpty() || !cacheMatchesScene(nodes);
            if (needRebuild) {
                QList<NodeBase *> topoOut;
                rebuildIncomingIndex(scene);
                if (!topologicalSort(nodes, topoOut)) {
                    m_cachedSortedNodes.clear();
                    m_graphStructureDirty = true;
                    cacheLock.unlock();
                    emit executionError(tr("流程中存在循环连接，无法确定执行顺序。"));
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
                recordNodeSkipped(node);
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
                recordNodeSkipped(node);
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

void FlowExecutor::rebuildIncomingIndex(FlowScene *scene)
{
    m_incoming.clear();
    m_outgoing.clear();
    if (!scene) {
        return;
    }
    const QList<MyProject::Connection *> all = scene->connections();
    m_incoming.reserve(all.size());
    m_outgoing.reserve(all.size());
    for (MyProject::Connection *conn : all) {
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

    // 识别全部循环体节点（供主遍历跳过，统一由 LoopNode 调度执行，P3）
    // 注意：必须以当前场景的节点为准。本函数在拓扑排序前调用，m_cachedSortedNodes
    // 可能仍属于上一个场景（切换场景后），用它做种子会漏识别循环体并可能解引用悬垂指针
    m_loopBodyNodes.clear();
    const QList<NodeBase *> bodySeed = scene->nodes();
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
        for (int p = 0; p < node->outputPorts().size(); ++p)
            node->setOutputData(p, QSharedPointer<DataObject>());

        success = node->execute();

        // 收集输出变量（供后级 {模块号.参数名} 引用）
        if (success) {
            collectNodeOutputVars(node);
        }

        // 为输出数据设置来源信息（仅成功节点写入缓存；失败或本轮无输出时清除缓存，避免下游误用上一轮结果，P2）
        if (success) {
            for (int i = 0; i < node->outputPorts().size(); i++) {
                QSharedPointer<DataObject> outputData = node->getOutputData(i);
                if (outputData) {
                    outputData->setSourceInfo(QString("%1 的输出").arg(node->fullName()));
                    m_nodeData[node][i] = outputData;
                } else {
                    // 本轮该端口无输出：移除上一轮残留，否则下游会读到旧数据
                    m_nodeData[node].remove(i);
                }
            }
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
            QString flowName = m_scene ? QString::number(reinterpret_cast<quintptr>(m_scene)) : QString();
            QString resultVal;
            QSharedPointer<DataObject> resultData = node->getOutputData(0);
            if (resultData) {
                resultVal = QStringLiteral("OK");
            }
            AppDatabase::instance()->saveInspectionResult(flowName, node->fullName(), success, resultVal);
            if (!success) {
                AppDatabase::instance()->addAlarm(node->fullName(), QStringLiteral("Error"),
                    QStringLiteral("\u7B97\u5B50\u6267\u884C\u5931\u8D25: %1").arg(node->name()));
            }
        }

        // 运行界面推送：任意节点有图像输出即通知（按节点名查表）
        if (success) {
            QSharedPointer<DataObject> imgData = node->getOutputData(0);
            if (imgData) {
                HalconCpp::HImage img = imgData->getHImage();
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
                        QSharedPointer<DataObject> out = node->getOutputData(0);
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
            QSharedPointer<DataObject> outputData = node->getOutputData(0);
            if (outputData) {
                HalconCpp::HImage image = outputData->getHImage();
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
                recordNodeSkipped(bn);
                continue;
            }

            if (!m_activeNodes.contains(bn)) {
                // 跳过未激活分支：清空输出与缓存，避免下游误用旧数据（E2/E5/P2）
                for (int p = 0; p < bn->outputPorts().size(); ++p)
                    bn->setOutputData(p, QSharedPointer<DataObject>());
                m_nodeData[bn].clear();
                m_nodeOutputVars[bn->moduleId()].clear();
                recordNodeSkipped(bn);
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

void FlowExecutor::recordNodeSkipped(NodeBase *node)
{
    if (!node) {
        return;
    }
    QMutexLocker locker(&m_statsMutex);
    m_stats.onNodeSkipped(node->fullName());
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
    if (getState() == ExecutionState::Running)
        return;

    QList<NodeBase *> nodes = m_scene->nodes();
    QList<NodeBase *> sorted;
    {
        QMutexLocker cacheLock(&m_graphCacheMutex);
        rebuildIncomingIndex(m_scene);
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
    if (getState() == ExecutionState::Running)
        return;

    QList<NodeBase *> nodes = m_scene->nodes();
    QList<NodeBase *> sorted;
    {
        QMutexLocker cacheLock(&m_graphCacheMutex);
        rebuildIncomingIndex(m_scene);
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

    for (NodeBase *node : sorted) {
        if (!keep.contains(node))
            continue;
        executeNode(node, node == sorted.last());
        if (!m_lastNodeSuccess && m_stopOnFailure)
            break;
    }
}

void FlowExecutor::invalidateDownstreamOf(NodeBase *startNode)
{
    if (!startNode) {
        return;
    }

    // 沿出边做传递闭包，收集 startNode 自身及其全部下游。
    // m_outgoing 由 rebuildIncomingIndex() 构建，受 m_graphCacheMutex 保护。
    QSet<NodeBase *> affected;
    {
        QMutexLocker graphLocker(&m_graphCacheMutex);
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
    }
}
