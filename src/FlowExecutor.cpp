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
                continue;
            }
            
            executeNode(node, i == sortedNodes.size() - 1);
            // 执行后激活下游（条件节点仅激活被选中分支）
            activateDownstream(node);

            // 循环执行：LoopNode 设置 loopCount>1 时重复执行下游循环体
            if (qobject_cast<LoopNode *>(node)) {
                const int cnt = node->getParam(QStringLiteral("loopCount")).toInt();
                if (cnt > 1) {
                    runLoopBody(node, cnt - 1);
                    if (!m_lastNodeSuccess && m_stopOnFailure) {
                        QMutexLocker fl(&m_mutex);
                        m_state = ExecutionState::Stopped;
                        break;
                    }
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

            QThread::msleep(50); // 防止CPU满载
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

    bool success = false;
    QElapsedTimer nodeTimer;
    nodeTimer.start();

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
                    node->setParam(k, resolveParamRefs(s));
                }
            }
        }

        success = node->execute();

        // 收集输出变量（供后级 {模块号.参数名} 引用）
        if (success) {
            collectNodeOutputVars(node);
        }

        // 为输出数据设置来源信息
        for (int i = 0; i < node->outputPorts().size(); i++) {
            QSharedPointer<DataObject> outputData = node->getOutputData(i);
            if (outputData) {
                outputData->setSourceInfo(QString("%1 的输出").arg(node->fullName()));
                m_nodeData[node][i] = outputData;
            }
        }

        // 计算节点执行耗时
        qint64 nodeElapsed = nodeTimer.elapsed();

        // 发出nodeExecuted信号，通知UI更新
        emit nodeExecuted(node, success);

        // 发出节点执行耗时信号
        emit nodeExecutionTime(node, nodeElapsed);

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

void FlowExecutor::runLoopBody(NodeBase *loopNode, int extraRuns)
{
    const QList<NodeBase *> body = collectLoopBody(loopNode);
    if (body.isEmpty()) return;
    for (int run = 0; run < extraRuns; ++run) {
        // 循环迭代变量：主循环第 1 次执行 iteration=1，此处更新为 2..loopCount
        // （循环体内节点可经 {循环模块号.iteration} 引用当前次数）
        loopNode->setParam(QStringLiteral("iteration"), run + 2);
        m_nodeOutputVars[loopNode->moduleId()][QStringLiteral("iteration")] = run + 2;
        for (NodeBase *bn : body) {
            // 循环体节点强制激活（不依赖条件分支状态）
            m_activeNodes.insert(bn);
            executeNode(bn, false);
            activateDownstream(bn);
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
            m_nodeData[node][destPort] = data;
            node->setInputData(destPort, data);
        }
    }
}

void FlowExecutor::resetState()
{
    m_nodeData.clear();
    m_executionQueue.clear();
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
