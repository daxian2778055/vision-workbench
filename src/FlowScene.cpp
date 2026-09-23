#include "FlowScene.h"
#include "NodeTemplateStore.h"
#include "NodeFactory.h"
#include "NodeRegistry.h"
#include "NodeBase.h"
#include "Port.h"
#include "Connection.h"
#include "PortConnectivity.h"
#include "NodeGraphicsItem.h"
#include "ConnectionGraphicsItem.h"
#include "ConnectionDragHelper.h"
#include "ProjectManager.h"
#include "CommentGraphicsItem.h"
#include "NodeGroupItem.h"
#include "AppLog.h"
#include <QMimeData>
#include <QDrag>
#include <QPainter>
#include <QMutexLocker>
#include <QThread>
#include <cmath>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QMenu>
#include <QGraphicsSceneContextMenuEvent>
#include <QJsonArray>
#include <QSet>

namespace {

/// FR2.5 闭环检测：判断从 from 出发沿已有连线的出边能否到达 to。
/// 新增 source→target 会形成环路，当且仅当 target 已能沿出边到达 source。
bool reachesNodeByOutgoingEdges(NodeBase *from, NodeBase *to)
{
    if (!from || !to)
        return false;
    if (from == to)
        return true;

    QSet<NodeBase *> visited;
    QList<NodeBase *> stack;
    visited.insert(from);
    stack.append(from);

    while (!stack.isEmpty()) {
        NodeBase *cur = stack.takeLast();
        for (Port *outPort : cur->outputPorts()) {
            for (MyProject::Connection *conn : outPort->connections()) {
                Port *dstPort = conn ? conn->targetPort() : nullptr;
                NodeBase *next = dstPort ? dstPort->node() : nullptr;
                if (!next)
                    continue;
                if (next == to)
                    return true;
                if (!visited.contains(next)) {
                    visited.insert(next);
                    stack.append(next);
                }
            }
        }
    }
    return false;
}

} // namespace

FlowScene::FlowScene(QObject *parent)
    : QGraphicsScene(parent)
{
    setSceneRect(0, 0, 10000, 10000);
    m_dragHelper = new ConnectionDragHelper(this);
    addItem(m_dragHelper);
    connect(m_dragHelper, &ConnectionDragHelper::connectionCreated, this,
            [this](Port *source, Port *target) {
                if (isEditLocked())
                    return;
                MyProject::Connection *connection = createConnection(source, target);
                if (connection) {
                    ConnectionGraphicsItem *connItem = getGraphicsItemForConnection(connection);
                    if (connItem)
                        connItem->updatePath();
                }
            });
}

void FlowScene::requestNodeHelp(NodeBase *node)
{
    if (node)
        emit nodeHelpRequested(node);
}

void FlowScene::requestNodeOutputData(NodeBase *node)
{
    if (node)
        emit nodeOutputDataRequested(node);
}

void FlowScene::requestExecuteToHere(NodeBase *node)
{
    if (node)
        emit executeToHereRequested(node);
}

void FlowScene::requestExecuteFromHere(NodeBase *node)
{
    if (node)
        emit executeFromHereRequested(node);
}

void FlowScene::requestRecomputeFrom(NodeBase *node)
{
    if (node)
        emit recomputeFromRequested(node);
}

void FlowScene::requestNodeEdit(NodeBase *node)
{
    if (node)
        emit nodeEditRequested(node);
}

bool FlowScene::s_danglingSnapshotAssertEnabled = true;   // M-2：默认开启（生产必须暴露顺序错误）

FlowScene::~FlowScene()
{
    // N-2：仍有存活快照时销毁场景 = 调用方顺序错误（必须先停执行器 / setFlowScene(nullptr)）。
    // 此时节点会随 QObject 父子机制一并析构，而执行线程仍持其指针 → 这里把"静默约定"变成可见错误：
    // 告警 + 断言（Debug 构建暴露，Release 只留告警）。句柄侧已用弱引用兜住"场景先亡"的 UAF。
    {
        QMutexLocker locker(&m_graphMutex);
        if (m_liveSnapshotCount > 0) {
            const int live = m_liveSnapshotCount;
            locker.unlock();
            qWarning() << "FlowScene destroyed while" << live
                       << "graph snapshot(s) alive — stop executors (setFlowScene(nullptr)) before "
                          "destroying the scene, otherwise the executor may touch destroyed nodes.";
            // M-2：默认断言（Debug 暴露顺序错误）；仅"刻意制造场景先亡"的用例临时关闭
            if (s_danglingSnapshotAssertEnabled)
                Q_ASSERT(live == 0);
        }
    }
    clearScene();
}

void FlowScene::drawBackground(QPainter *painter, const QRectF &rect)
{
    const QColor bg(0x1e, 0x1e, 0x21);
    const QColor gridLine(0x2c, 0x2c, 0x32);
    painter->fillRect(rect, bg);

    painter->setPen(QPen(gridLine, 0));
    constexpr qreal grid = 20.0;
    qreal x = std::floor(rect.left() / grid) * grid;
    while (x <= rect.right()) {
        painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
        x += grid;
    }
    qreal y = std::floor(rect.top() / grid) * grid;
    while (y <= rect.bottom()) {
        painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
        y += grid;
    }
}

NodeBase *FlowScene::createNode(NodeBase::NodeType type, const QPointF &pos, const QString &nodeName)
{
    recordUndo();
    NodeBase *node = NodeFactory::createNode(this, type, nodeName);

    if (node) {
        node->setPosition(pos);
        node->setFlowSceneRef(this);
        node->init();
        
        // Create graphics item
        NodeGraphicsItem *item = new NodeGraphicsItem(node);
        item->setPos(pos);
        this->addItem(item);
        
        // Update ports to ensure they have correct positions
        item->updatePorts();
        
        {
            QMutexLocker locker(&m_graphMutex);
            m_nodeItems[node] = item;
        }
        emit nodeAdded(node);
        emit nodeGraphicsItemCreated(item);
    }
    
    return node;
}

NodeBase *FlowScene::createNodeByTypeIdOrName(const QString &typeId, int typeValue,
                                              const QString &name)
{
    NodeBase *node = nullptr;
    if (!typeId.isEmpty())
        node = NodeRegistry::instance().createById(typeId, this);
    if (!node && typeValue >= 0)
        node = NodeFactory::createNode(this, static_cast<NodeBase::NodeType>(typeValue), name);
    if (!node)
        return nullptr;
    // 端口在这里建（见各算子的 init()）。历史上 duplicateNode / createNodeFromTemplate 都漏了这一步，
    // 于是"复制/模板插入出来的算子没有端口、连不上线"——流程还照样能跑，极难发现。
    node->init();
    return node;
}

NodeBase *FlowScene::duplicateNode(NodeBase *node)
{
    if (!node || m_editLocked) return nullptr;
    recordUndo();

    // 序列化原算子参数（moduleId 不复制，副本保持自己的模块 ID）
    QJsonObject json = node->toJson();
    json.remove(QStringLiteral("moduleId"));
    json.remove(QStringLiteral("position"));

    // 优先按注册表 ID 克隆（保留具体算子类型），否则按名称回退。
    // 走统一入口：它负责 init()（建端口）——旧实现直接用 createById，副本没有端口、连不上线。
    NodeBase *clone = createNodeByTypeIdOrName(node->property("vfpNodeTypeId").toString(),
                                               int(node->type()), node->name());
    if (!clone) return nullptr;

    clone->fromJson(json);
    if (node->name().contains(QStringLiteral("_副本"))) {
        clone->setName(node->name() + QStringLiteral("2"));
    } else {
        clone->setName(node->name() + QStringLiteral("_副本"));
    }
    clone->setEnabled(node->isEnabled());

    adoptNode(clone, node->position() + QPointF(30, 30));
    return clone;
}

void FlowScene::adoptNode(NodeBase *node, const QPointF &pos)
{
    if (!node) return;
    node->setPosition(pos);

    // 创建图形项并加入场景
    NodeGraphicsItem *item = new NodeGraphicsItem(node);
    item->setPos(pos);
    this->addItem(item);
    item->updatePorts();

    {
        QMutexLocker locker(&m_graphMutex);
        m_nodeItems[node] = item;
    }
    emit nodeAdded(node);
    emit nodeGraphicsItemCreated(item);
}

NodeBase *FlowScene::createNodeFromTemplate(const QString &templateName, const QPointF &pos)
{
    if (m_editLocked) return nullptr;

    NodeTemplateStore &store = NodeTemplateStore::instance();
    const QJsonObject nodeJson = store.nodeJson(templateName);
    if (nodeJson.isEmpty()) return nullptr;

    recordUndo();

    // 优先按注册表 ID 克隆（保留具体算子类型），否则按类型枚举 + 名称回退
    // ——与 duplicateNode 完全同一条路径（含 init() 建端口），模板因此对新增算子自动可用。
    NodeBase *node = createNodeByTypeIdOrName(store.typeIdOf(templateName),
                                              store.typeValueOf(templateName),
                                              store.nodeNameOf(templateName));
    if (!node) return nullptr;

    node->fromJson(nodeJson);

    // 重名就加后缀：变量引用按算子名定位，撞名会让"引用指向哪个算子"变得不确定。
    // 与片段插入共用 makeUniqueNodeName（单一实现，避免两处规则漂移）。
    const QString base = node->name().isEmpty() ? templateName : node->name();
    const QString unique = makeUniqueNodeName(base, node);
    if (!unique.isEmpty() && unique != node->name())
        node->setName(unique);

    adoptNode(node, pos);
    return node;
}

void FlowScene::removeNode(NodeBase *node)
{
    if (!node) {
        return;
    }

    recordUndo();          // 批量记录一次（内部连线删除由 m_undoBatch 抑制）
    ++m_undoBatch;

    // 分组维护：算子没了就不能再留在分组里；成员删光的分组自动解散（否则留下一个空框）。
    // 这里**不**记录撤销：外层已记录（或已批量抑制），见 deleteGroupItem 注释。
    const int removedModuleId = node->moduleId();
    for (NodeGroupItem *g : QList<NodeGroupItem *>(m_groups)) {
        if (g && g->removeMember(removedModuleId) && g->isEmpty())
            deleteGroupItem(g);
    }

    // Remove all connections
    for (Port *port : node->inputPorts()) {
        for (MyProject::Connection *conn : port->connections()) {
            removeConnection(conn);
        }
    }
    for (Port *port : node->outputPorts()) {
        for (MyProject::Connection *conn : port->connections()) {
            removeConnection(conn);
        }
    }
    
    // 摘除成员集并判定是否走墓碑：有存活快照（执行线程正持有裸指针）时只摘除、延迟析构（S1）
    NodeGraphicsItem *item = nullptr;
    bool deferred = false;
    {
        QMutexLocker locker(&m_graphMutex);
        item = m_nodeItems.value(node, nullptr);
        m_nodeItems.remove(node);
        deferred = (m_liveSnapshotCount > 0);
        if (deferred) {
            m_retiredNodes.append(node);
        }
    }
    if (item) {
        removeItem(item);
        delete item;
    }

    emit nodeRemoved(node);
    if (!deferred) {
        delete node;
    }
    --m_undoBatch;
}

MyProject::Connection *FlowScene::createConnection(Port *source, Port *target, bool relaxedSemantics)
{
    if (!source || !target) {
        return nullptr;
    }
    recordUndo();

    QString reject;
    const bool semanticOk =
        PortConnectivity::canConnectPorts(source, target, &reject, !relaxedSemantics);
    if (!semanticOk) {
        if (!relaxedSemantics)
            emit connectionRejected(reject.isEmpty() ? QStringLiteral("(connection rejected)") : reject);
        return nullptr;
    }

    // FR2.5：禁止创建闭环连接。创建期即拦截，避免用户画出环后直到运行才报错。
    if (reachesNodeByOutgoingEdges(target->node(), source->node())) {
        if (relaxedSemantics) {
            // 放宽语义仅用于方案加载：历史文件中的闭环连线直接跳过，保留其余流程可用
            VFP_DEBUG << "跳过形成环路的连线（方案文件中存在闭环）";
        } else {
            emit connectionRejected(QStringLiteral("禁止创建闭环连接：该连线会使流程形成环路"));
        }
        return nullptr;
    }

    // Create connection
    MyProject::Connection *connection = new MyProject::Connection(source, target, this);
    
    // Create graphics item
    ConnectionGraphicsItem *item = new ConnectionGraphicsItem(connection, nullptr);
    addItem(item);
    
    {
        QMutexLocker locker(&m_graphMutex);
        m_connectionItems[connection] = item;
    }
    
    // Update connection path to ensure it's displayed correctly
    item->updatePath();
    
    emit connectionAdded(connection);
    
    return connection;
}

void FlowScene::removeConnection(MyProject::Connection *connection)
{
    if (!connection) {
        return;
    }
    
    if (m_undoBatch <= 0) {
        recordUndo();   // 独立删除连线时记录（批量删除由 removeNode 统一记录）
    }
    
    // 摘除成员集并判定是否走墓碑（与 removeNode 同款，S1）
    ConnectionGraphicsItem *item = nullptr;
    bool deferred = false;
    {
        QMutexLocker locker(&m_graphMutex);
        item = m_connectionItems.value(connection, nullptr);
        m_connectionItems.remove(connection);
        deferred = (m_liveSnapshotCount > 0);
        if (deferred) {
            m_retiredConnections.append(connection);
        }
    }
    if (item) {
        removeItem(item);
        delete item;
    }

    // 先 emit 再 delete：槽函数收到的指针必须仍然有效
    // （原实现先 delete 后 emit，信号携带的是悬垂指针；墓碑路径下对象必然有效）
    emit connectionRemoved(connection);
    if (!deferred) {
        delete connection;
    }
}

// ---- 撤销/重做（快照式） ----

void FlowScene::recordUndo()
{
    if (m_restoring || m_editLocked || m_undoBatch > 0) {
        return;
    }
    ProjectManager pm;
    m_undoStack.append(pm.sceneToJson(this));
    if (m_undoStack.size() > kUndoLimit) {
        m_undoStack.removeFirst();
    }
    m_redoStack.clear();
    emit undoAvailable(true);
    emit redoAvailable(false);
}

void FlowScene::beginUndoBatch()
{
    ++m_undoBatch;
}

void FlowScene::endUndoBatch()
{
    if (m_undoBatch > 0)
        --m_undoBatch;
}

void FlowScene::restoreSnapshot(const QJsonObject &snapshot)
{
    m_restoring = true;
    clearScene();
    ProjectManager pm;
    pm.sceneFromJson(snapshot, this);
    m_restoring = false;
}

bool FlowScene::undo()
{
    if (m_undoStack.isEmpty() || m_editLocked) {
        return false;
    }
    ProjectManager pm;
    m_redoStack.append(pm.sceneToJson(this));
    if (m_redoStack.size() > kUndoLimit) {
        m_redoStack.removeFirst();
    }
    const QJsonObject snapshot = m_undoStack.takeLast();
    restoreSnapshot(snapshot);
    emit undoAvailable(!m_undoStack.isEmpty());
    emit redoAvailable(true);
    return true;
}

bool FlowScene::redo()
{
    if (m_redoStack.isEmpty() || m_editLocked) {
        return false;
    }
    ProjectManager pm;
    m_undoStack.append(pm.sceneToJson(this));
    if (m_undoStack.size() > kUndoLimit) {
        m_undoStack.removeFirst();
    }
    const QJsonObject snapshot = m_redoStack.takeLast();
    restoreSnapshot(snapshot);
    emit undoAvailable(true);
    emit redoAvailable(!m_redoStack.isEmpty());
    return true;
}

QList<NodeBase *> FlowScene::nodes() const
{
    QMutexLocker locker(&m_graphMutex);
    return m_nodeItems.keys();
}

QList<MyProject::Connection *> FlowScene::connections() const
{
    QMutexLocker locker(&m_graphMutex);
    return m_connectionItems.keys();
}

NodeGraphicsItem *FlowScene::getGraphicsItemForNode(NodeBase *node) const
{
    QMutexLocker locker(&m_graphMutex);
    return m_nodeItems.value(node, nullptr);
}

ConnectionGraphicsItem *FlowScene::getGraphicsItemForConnection(MyProject::Connection *connection) const
{
    QMutexLocker locker(&m_graphMutex);
    return m_connectionItems.value(connection, nullptr);
}

FlowScene::GraphSnapshotGuard FlowScene::captureGraphSnapshot()
{
    GraphSnapshot snap;
    {
        QMutexLocker locker(&m_graphMutex);
        snap.nodes = m_nodeItems.keys();
        snap.connections = m_connectionItems.keys();
        ++m_liveSnapshotCount;
    }
    return GraphSnapshotGuard(this, std::move(snap));
}

void FlowScene::GraphSnapshotGuard::releaseHeld()
{
    if (m_scene.isNull()) {
        return;   // 场景已析构：无事可做（N-2，避免对已亡场景回调）
    }
    if (auto *scene = qobject_cast<FlowScene *>(m_scene.data())) {
        scene->releaseGraphSnapshot();
    }
    m_scene = nullptr;
}

void FlowScene::releaseGraphSnapshot()
{
    bool flushNow = false;
    {
        QMutexLocker locker(&m_graphMutex);
        if (m_liveSnapshotCount > 0) {
            --m_liveSnapshotCount;
        }
        flushNow = (m_liveSnapshotCount == 0);
    }
    if (!flushNow) {
        return;
    }
    // 墓碑析构必须在场景线程执行：节点可能挂着 QTimer/QObject 子对象，跨线程析构不安全。
    // 执行器线程释放快照时改为投递到场景线程（UI）执行。
    if (QThread::currentThread() == thread()) {
        flushRetired();
    } else {
        QMetaObject::invokeMethod(this, [this]() { flushRetired(); }, Qt::QueuedConnection);
    }
}

void FlowScene::flushRetired()
{
    QList<NodeBase *> nodes;
    QList<MyProject::Connection *> conns;
    {
        QMutexLocker locker(&m_graphMutex);
        if (m_liveSnapshotCount > 0) {
            return;   // 仍有存活快照：本轮不析构，等最后一次释放再 flush
        }
        nodes.swap(m_retiredNodes);
        conns.swap(m_retiredConnections);
    }
    // 先删连边再删节点：连边持有两端端口指针，先释放更安全
    for (MyProject::Connection *c : conns) {
        delete c;
    }
    for (NodeBase *n : nodes) {
        delete n;
    }
}

void FlowScene::clearScene()
{
    // 用加锁读取的副本驱动删除（remove* 内部各自持锁；有存活快照时自动走墓碑延迟析构）
    for (MyProject::Connection *conn : connections()) {
        removeConnection(conn);
    }
    for (NodeBase *node : nodes()) {
        removeNode(node);
    }
    for (CommentGraphicsItem *c : m_comments) {
        if (c) {
            removeItem(c);
            delete c;
        }
    }
    m_comments.clear();
    // 分组框同样必须显式清理：QGraphicsScene::clear() 是禁用的（见头文件），
    // 而上面逐个 removeNode 时成员删光的分组已自动解散，这里收尾剩下的。
    for (NodeGroupItem *g : m_groups) {
        if (g) {
            removeItem(g);
            delete g;
        }
    }
    m_groups.clear();
    {
        QMutexLocker locker(&m_graphMutex);
        m_nodeItems.clear();
        m_connectionItems.clear();
    }
    // 兜底：无存活快照时把墓碑一并析构。否则 clearScene（含 ~FlowScene 路径）会把这些
    // 已摘除待删的对象漏掉，造成析构期泄漏。flushRetired() 内部自会检查存活句柄。
    flushRetired();
}

void FlowScene::setEditLocked(bool locked)
{
    m_editLocked = locked;
    // 锁定/解锁所有节点图形的可移动性（加锁取副本再改，避免与执行线程读成员集并发）
    QList<NodeGraphicsItem *> items;
    {
        QMutexLocker locker(&m_graphMutex);
        items = m_nodeItems.values();
    }
    for (NodeGraphicsItem *item : items) {
        item->setFlag(QGraphicsItem::ItemIsMovable, !locked);
    }
    for (CommentGraphicsItem *c : m_comments) {
        if (c)
            c->setFlag(QGraphicsItem::ItemIsMovable, !locked);
    }
    for (NodeGroupItem *g : m_groups) {
        if (g)
            g->setFlag(QGraphicsItem::ItemIsMovable, !locked);
    }
}

bool FlowScene::isEditLocked() const
{
    return m_editLocked;
}

void FlowScene::dragEnterEvent(QGraphicsSceneDragDropEvent *event)
{
    if (m_editLocked) { event->ignore(); return; }
    const QMimeData *mime = event->mimeData();
    if (!mime) return;
    if (mime->hasText() || mime->hasFormat(NodeFactory::toolPaletteMimeFormat()))
        event->acceptProposedAction();
}

void FlowScene::dragMoveEvent(QGraphicsSceneDragDropEvent *event)
{
    if (m_editLocked) { event->ignore(); return; }
    const QMimeData *mime = event->mimeData();
    if (!mime) return;
    if (mime->hasText() || mime->hasFormat(NodeFactory::toolPaletteMimeFormat()))
        event->acceptProposedAction();
}

void FlowScene::dropEvent(QGraphicsSceneDragDropEvent *event)
{
    if (m_editLocked) { event->ignore(); return; }
    const QMimeData *mime = event->mimeData();
    if (!mime) { QGraphicsScene::dropEvent(event); return; }

    NodeBase::NodeType type = NodeBase::IMAGE_ACQUISITION;
    QString nodeName;
    bool ok = false;

    const QString paletteMime = NodeFactory::toolPaletteMimeFormat();
    if (mime->hasFormat(paletteMime)) {
        const QString id = QString::fromUtf8(mime->data(paletteMime)).trimmed();
        ok = NodeFactory::resolvePaletteToolId(id, type, nodeName);
    }
    if (!ok && mime->hasText()) {
        ok = NodeFactory::parseDropMimeText(mime->text().trimmed(), type, nodeName);
    }
    if (!ok) { QGraphicsScene::dropEvent(event); return; }

    createNode(type, event->scenePos(), nodeName);
    event->acceptProposedAction();
}

void FlowScene::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsScene::mousePressEvent(event);
    NodeBase *sel = nullptr;
    for (QGraphicsItem *it : selectedItems()) {
        if (auto *n = dynamic_cast<NodeGraphicsItem *>(it)) {
            n->m_selected = true;
            if (!sel)
                sel = n->node();
        }
    }
    QList<NodeGraphicsItem *> nodeItems;
    {
        QMutexLocker locker(&m_graphMutex);
        nodeItems = m_nodeItems.values();
    }
    for (NodeGraphicsItem *n : nodeItems) {
        n->m_selected = n->isSelected();
        n->update();
    }
    emit nodeSelected(sel);
}

void FlowScene::keyPressEvent(QKeyEvent *event)
{
    if (!m_editLocked && (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)) {
        deleteSelectedItems();
        event->accept();
        return;
    }
    // 画布聚焦时的 Ctrl+C / Ctrl+V：转成请求交给窗口（粘贴位置要用视图中心），
    // 这样参数框/日志里的 Ctrl+C 仍是原生文本复制（那些控件不经过本场景）。
    if (event->matches(QKeySequence::Copy)) {
        emit copySelectionRequested();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Paste)) {
        emit pasteRequested();
        event->accept();
        return;
    }
    QGraphicsScene::keyPressEvent(event);
}

void FlowScene::contextMenuEvent(QGraphicsSceneContextMenuEvent *event)
{
    QGraphicsItem *hit = itemAt(event->scenePos(), QTransform());
    if (hit) {
        QGraphicsScene::contextMenuEvent(event);
        return;
    }
    QMenu menu;
    QAction *addCommentAct = menu.addAction(QStringLiteral("添加注释"));
    addCommentAct->setEnabled(!m_editLocked);
    // FR1.9：把选中的算子框成一组（纯视觉容器，不影响执行）
    const int selCount = selectedNodes().size();
    QAction *groupAct = menu.addAction(selCount >= 2
                                           ? QStringLiteral("创建分组（%1 个算子）").arg(selCount)
                                           : QStringLiteral("创建分组（请先选中 ≥2 个算子）"));
    groupAct->setEnabled(!m_editLocked && selCount >= 2);
    QAction *chosen = menu.exec(event->screenPos());
    if (chosen == addCommentAct && !m_editLocked)
        addComment(event->scenePos(), QStringLiteral("注释"));
    else if (chosen == groupAct && !m_editLocked)
        createGroupFromSelection();
}

QList<NodeBase *> FlowScene::selectedNodes() const
{
    QList<NodeBase *> out;
    for (QGraphicsItem *it : selectedItems()) {
        if (auto *n = dynamic_cast<NodeGraphicsItem *>(it))
            out.append(n->node());
    }
    return out;
}

void FlowScene::deleteSelectedItems()
{
    if (m_editLocked)
        return;
    const auto nodes = selectedNodes();
    QList<CommentGraphicsItem *> comments;
    QList<NodeGroupItem *> groups;
    for (QGraphicsItem *it : selectedItems()) {
        if (auto *c = dynamic_cast<CommentGraphicsItem *>(it))
            comments.append(c);
        else if (auto *g = dynamic_cast<NodeGroupItem *>(it))
            groups.append(g);
    }
    if (nodes.isEmpty() && comments.isEmpty() && groups.isEmpty())
        return;
    recordUndo();
    ++m_undoBatch;
    for (NodeBase *n : nodes)
        removeNode(n);
    for (CommentGraphicsItem *c : comments)
        removeComment(c);
    // 选中分组按 Delete = 解散（只删框，组内算子保留）；上面删节点可能已把某个分组连带解散，
    // 故先确认它还在容器里再删（避免对已析构对象二次操作）。
    for (NodeGroupItem *g : groups) {
        if (m_groups.contains(g))
            deleteGroupItem(g);
    }
    --m_undoBatch;
}

void FlowScene::setSelectedNodesEnabled(bool on)
{
    if (m_editLocked)
        return;
    recordUndo();
    for (NodeBase *n : selectedNodes()) {
        if (n)
            n->setEnabled(on);
    }
    update();
}

CommentGraphicsItem *FlowScene::addComment(const QPointF &pos, const QString &text)
{
    if (!m_restoring)
        recordUndo();
    auto *item = new CommentGraphicsItem(text);
    item->setPos(pos);
    item->setFlag(QGraphicsItem::ItemIsMovable, !m_editLocked);
    addItem(item);
    m_comments.append(item);
    return item;
}

void FlowScene::removeComment(CommentGraphicsItem *item)
{
    if (!item)
        return;
    if (!m_restoring && m_undoBatch == 0)
        recordUndo();
    m_comments.removeAll(item);
    removeItem(item);
    delete item;
}

// ---- 算子分组（Group，FR1.9）----
// 分组是纯视觉容器：不参与执行（不发 nodeAdded / connection* 信号），只随方案与撤销快照持久化。

void FlowScene::deleteGroupItem(NodeGroupItem *group)
{
    if (!group)
        return;
    // 注意：本函数**不记撤销**。记录时机由调用方掌握（结构变化前记 / 批量内只记一次），
    // 否则会把"改动之后"的状态塞进撤销栈，导致第一次撤销看起来没反应。
    m_groups.removeAll(group);
    removeItem(group);
    delete group;
}

NodeGroupItem *FlowScene::createGroupFromSelection(const QString &title)
{
    if (m_editLocked)
        return nullptr;
    const QList<NodeBase *> sel = selectedNodes();
    if (sel.size() < 2)
        return nullptr;   // 单个算子的"分组"没有意义（框只会贴住它自己）

    recordUndo();   // 结构变化前记录（分组会进方案文件、也会被撤销快照带上）

    QList<int> ids;
    for (NodeBase *n : sel) {
        if (n)
            ids.append(n->moduleId());
    }

    auto *group = new NodeGroupItem(
        title.isEmpty() ? QStringLiteral("分组 %1").arg(m_groups.size() + 1) : title);
    group->setMembers(ids);
    addItem(group);
    group->fitToMembers();
    m_groups.append(group);
    update();
    return group;
}

void FlowScene::removeGroup(NodeGroupItem *group)
{
    if (!group)
        return;
    if (!m_restoring && m_undoBatch == 0)
        recordUndo();
    deleteGroupItem(group);
}

int FlowScene::dissolveSelectedGroups()
{
    if (m_editLocked)
        return 0;
    QList<NodeGroupItem *> selected;
    for (QGraphicsItem *it : selectedItems()) {
        if (auto *g = dynamic_cast<NodeGroupItem *>(it))
            selected.append(g);
    }
    if (selected.isEmpty())
        return 0;

    recordUndo();
    ++m_undoBatch;
    for (NodeGroupItem *g : selected)
        deleteGroupItem(g);
    --m_undoBatch;
    update();
    return selected.size();
}

NodeGroupItem *FlowScene::groupOfNode(NodeBase *node) const
{
    if (!node)
        return nullptr;
    const int id = node->moduleId();
    for (NodeGroupItem *g : m_groups) {
        if (g && g->hasMember(id))
            return g;
    }
    return nullptr;
}

QString FlowScene::makeUniqueNodeName(const QString &base, NodeBase *exclude) const
{
    const QString trimmed = base.trimmed();
    if (trimmed.isEmpty())
        return trimmed;

    // nodes() 内部持图锁取副本，避免与执行线程并发读写成员集
    const QList<NodeBase *> existing = nodes();
    QString candidate = trimmed;
    int suffix = 2;
    for (;;) {
        bool taken = false;
        for (NodeBase *other : existing) {
            if (other && other != exclude && other->name() == candidate) {
                taken = true;
                break;
            }
        }
        if (!taken)
            return candidate;
        candidate = trimmed + QStringLiteral("_%1").arg(suffix++);
    }
}

void FlowScene::registerLoadedGroup(NodeGroupItem *group, qreal width, qreal height)
{
    if (!group)
        return;
    group->setFlag(QGraphicsItem::ItemIsMovable, !m_editLocked);
    group->setFrameSize(width, height);
    addItem(group);
    m_groups.append(group);
}

NodeBase *FlowScene::nodeByModuleId(int moduleId) const
{
    // nodes() 内部持图锁取副本，避免与执行线程并发读写成员集
    for (NodeBase *n : nodes()) {
        if (n && n->moduleId() == moduleId)
            return n;
    }
    return nullptr;
}

void FlowScene::setFlowVariable(const QString &name, int type, const QVariant &value,
                               const QString &description)
{
    if (name.trimmed().isEmpty())
        return;
    FlowVariable v;
    v.name = name.trimmed();
    v.type = type;
    v.value = value;
    v.description = description;
    m_flowVariables[v.name] = v;
}

bool FlowScene::removeFlowVariable(const QString &name)
{
    return m_flowVariables.remove(name) > 0;
}

QVariant FlowScene::flowVariable(const QString &name) const
{
    return m_flowVariables.value(name).value;
}

void FlowScene::setFixture(const FlowFixture &fixture)
{
    if (fixture.name.trimmed().isEmpty())
        return;
    m_fixtures[fixture.name.trimmed()] = fixture;
}

void FlowScene::setFixtureHomography(const QString &name, const QVector<double> &hom)
{
    if (name.trimmed().isEmpty() || hom.size() < 6)
        return;
    FlowFixture f = m_fixtures.value(name.trimmed());
    f.name = name.trimmed();
    f.hom = hom.mid(0, 6);
    f.hasHom = true;
    m_fixtures[f.name] = f;
}

void FlowScene::setFixturePose(const QString &name, double row, double col,
                              double angleDeg, double scale)
{
    if (name.trimmed().isEmpty())
        return;
    FlowFixture f = m_fixtures.value(name.trimmed());
    f.name = name.trimmed();
    f.poseRow = row;
    f.poseCol = col;
    f.poseAngle = angleDeg;
    f.poseScale = scale;
    f.hasPose = true;
    m_fixtures[f.name] = f;
}

FlowFixture FlowScene::fixture(const QString &name) const
{
    return m_fixtures.value(name.trimmed());
}

QStringList FlowScene::fixtureNames() const
{
    return m_fixtures.keys();
}

QJsonObject FlowScene::extrasToJson() const
{
    QJsonObject o;
    QJsonArray comments;
    for (CommentGraphicsItem *c : m_comments) {
        if (c)
            comments.append(c->toJson());
    }
    o[QStringLiteral("comments")] = comments;

    // FR1.9 分组：成员用模块号（跨会话稳定），随方案保存/加载并被撤销快照带上
    QJsonArray groups;
    for (NodeGroupItem *g : m_groups) {
        if (g)
            groups.append(g->toJson());
    }
    o[QStringLiteral("nodeGroups")] = groups;
    o[QStringLiteral("flowName")] = m_flowName;   // S2：流程名随方案持久化，触发按名路由保持身份
    o[QStringLiteral("flowMode")] = m_flowMode;   // 每流程运行模式随方案持久化（不是所有流程都要连续）

    QJsonObject vars;
    for (auto it = m_flowVariables.constBegin(); it != m_flowVariables.constEnd(); ++it) {
        QJsonObject v;
        v[QStringLiteral("type")] = it->type;
        v[QStringLiteral("value")] = QJsonValue::fromVariant(it->value);
        v[QStringLiteral("description")] = it->description;
        vars[it.key()] = v;
    }
    o[QStringLiteral("flowVariables")] = vars;

    QJsonObject fixtures;
    for (auto it = m_fixtures.constBegin(); it != m_fixtures.constEnd(); ++it) {
        QJsonObject f;
        QJsonArray hom;
        for (double d : it->hom)
            hom.append(d);
        f[QStringLiteral("hom")] = hom;
        f[QStringLiteral("poseRow")] = it->poseRow;
        f[QStringLiteral("poseCol")] = it->poseCol;
        f[QStringLiteral("poseAngle")] = it->poseAngle;
        f[QStringLiteral("poseScale")] = it->poseScale;
        f[QStringLiteral("hasHom")] = it->hasHom;
        f[QStringLiteral("hasPose")] = it->hasPose;
        fixtures[it.key()] = f;
    }
    o[QStringLiteral("fixtures")] = fixtures;
    return o;
}

void FlowScene::extrasFromJson(const QJsonObject &json)
{
    m_flowVariables.clear();
    m_fixtures.clear();
    m_flowName = json.value(QStringLiteral("flowName")).toString();   // S2：恢复流程名
    m_flowMode = json.value(QStringLiteral("flowMode")).toInt(1);     // 恢复每流程运行模式（默认软触发）

    const QJsonObject vars = json.value(QStringLiteral("flowVariables")).toObject();
    for (auto it = vars.constBegin(); it != vars.constEnd(); ++it) {
        const QJsonObject v = it.value().toObject();
        FlowVariable fv;
        fv.name = it.key();
        fv.type = v.value(QStringLiteral("type")).toInt(2);
        fv.value = v.value(QStringLiteral("value")).toVariant();
        fv.description = v.value(QStringLiteral("description")).toString();
        m_flowVariables[fv.name] = fv;
    }
    const QJsonObject fixtures = json.value(QStringLiteral("fixtures")).toObject();
    for (auto it = fixtures.constBegin(); it != fixtures.constEnd(); ++it) {
        const QJsonObject f = it.value().toObject();
        FlowFixture ff;
        ff.name = it.key();
        for (const QJsonValue &x : f.value(QStringLiteral("hom")).toArray())
            ff.hom.append(x.toDouble());
        ff.poseRow = f.value(QStringLiteral("poseRow")).toDouble();
        ff.poseCol = f.value(QStringLiteral("poseCol")).toDouble();
        ff.poseAngle = f.value(QStringLiteral("poseAngle")).toDouble();
        ff.poseScale = f.value(QStringLiteral("poseScale")).toDouble(1.0);
        ff.hasHom = f.value(QStringLiteral("hasHom")).toBool();
        ff.hasPose = f.value(QStringLiteral("hasPose")).toBool();
        m_fixtures[ff.name] = ff;
    }
    const QJsonArray comments = json.value(QStringLiteral("comments")).toArray();
    for (const QJsonValue &cv : comments) {
        CommentGraphicsItem *item = CommentGraphicsItem::fromJson(cv.toObject());
        if (!item)
            continue;
        item->setFlag(QGraphicsItem::ItemIsMovable, !m_editLocked);
        addItem(item);
        m_comments.append(item);
    }

    // FR1.9 分组：本函数由 ProjectManager::sceneFromJson 在**节点与连线都建好之后**调用，
    // 所以这里能按模块号把成员解析回算子（setMembers 会丢掉已不存在的成员）。
    const QJsonArray groups = json.value(QStringLiteral("nodeGroups")).toArray();
    for (const QJsonValue &gv : groups) {
        NodeGroupItem *group = NodeGroupItem::fromJson(gv.toObject());
        if (!group)
            continue;
        const QJsonObject groupJson = gv.toObject();
        registerLoadedGroup(group, groupJson.value(QStringLiteral("w")).toDouble(),
                            groupJson.value(QStringLiteral("h")).toDouble());
        group->setMembers(group->memberIds());   // 挂进场景后再过滤一次
    }
}
