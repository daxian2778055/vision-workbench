#include "FlowScene.h"
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
#include "AppLog.h"
#include <QMimeData>
#include <QDrag>
#include <QPainter>
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

void FlowScene::requestNodeEdit(NodeBase *node)
{
    if (node)
        emit nodeEditRequested(node);
}

FlowScene::~FlowScene()
{
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
        
        m_nodeItems[node] = item;
        emit nodeAdded(node);
        emit nodeGraphicsItemCreated(item);
    }
    
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

    // 优先按注册表 ID 克隆（保留具体算子类型），否则按名称回退
    NodeBase *clone = nullptr;
    const QString typeId = node->property("vfpNodeTypeId").toString();
    if (!typeId.isEmpty()) {
        clone = NodeRegistry::instance().createById(typeId, this);
    }
    if (!clone) {
        clone = NodeFactory::createNode(this, node->type(), node->name());
    }
    if (!clone) return nullptr;

    clone->fromJson(json);
    clone->setPosition(node->position() + QPointF(30, 30));
    if (node->name().contains(QStringLiteral("_副本"))) {
        clone->setName(node->name() + QStringLiteral("2"));
    } else {
        clone->setName(node->name() + QStringLiteral("_副本"));
    }
    clone->setEnabled(node->isEnabled());

    // 创建图形项并加入场景
    NodeGraphicsItem *item = new NodeGraphicsItem(clone);
    item->setPos(clone->position());
    this->addItem(item);
    item->updatePorts();

    m_nodeItems[clone] = item;
    emit nodeAdded(clone);
    emit nodeGraphicsItemCreated(item);
    return clone;
}

void FlowScene::removeNode(NodeBase *node)
{
    if (!node) {
        return;
    }

    recordUndo();          // 批量记录一次（内部连线删除由 m_undoBatch 抑制）
    ++m_undoBatch;

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
    
    // Remove graphics item
    NodeGraphicsItem *item = m_nodeItems.value(node, nullptr);
    if (item) {
        removeItem(item);
        delete item;
        m_nodeItems.remove(node);
    }

    emit nodeRemoved(node);
    delete node;
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
    
    m_connectionItems[connection] = item;
    
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
    
    // Remove graphics item
    ConnectionGraphicsItem *item = m_connectionItems.value(connection, nullptr);
    if (item) {
        removeItem(item);
        delete item;
        m_connectionItems.remove(connection);
    }

    // 先 emit 再 delete：槽函数收到的指针必须仍然有效
    // （原实现先 delete 后 emit，信号携带的是悬垂指针）
    emit connectionRemoved(connection);
    delete connection;
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
    return m_nodeItems.keys();
}

QList<MyProject::Connection *> FlowScene::connections() const
{
    return m_connectionItems.keys();
}

NodeGraphicsItem *FlowScene::getGraphicsItemForNode(NodeBase *node) const
{
    return m_nodeItems.value(node, nullptr);
}

ConnectionGraphicsItem *FlowScene::getGraphicsItemForConnection(MyProject::Connection *connection) const
{
    return m_connectionItems.value(connection, nullptr);
}

void FlowScene::clearScene()
{
    for (MyProject::Connection *conn : m_connectionItems.keys()) {
        removeConnection(conn);
    }
    for (NodeBase *node : m_nodeItems.keys()) {
        removeNode(node);
    }
    for (CommentGraphicsItem *c : m_comments) {
        if (c) {
            removeItem(c);
            delete c;
        }
    }
    m_comments.clear();
    m_nodeItems.clear();
    m_connectionItems.clear();
}

void FlowScene::setEditLocked(bool locked)
{
    m_editLocked = locked;
    // 锁定/解锁所有节点图形的可移动性
    for (NodeGraphicsItem *item : m_nodeItems) {
        item->setFlag(QGraphicsItem::ItemIsMovable, !locked);
    }
    for (CommentGraphicsItem *c : m_comments) {
        if (c)
            c->setFlag(QGraphicsItem::ItemIsMovable, !locked);
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
    for (NodeGraphicsItem *n : m_nodeItems) {
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
    QAction *chosen = menu.exec(event->screenPos());
    if (chosen == addCommentAct && !m_editLocked)
        addComment(event->scenePos(), QStringLiteral("注释"));
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
    for (QGraphicsItem *it : selectedItems()) {
        if (auto *c = dynamic_cast<CommentGraphicsItem *>(it))
            comments.append(c);
    }
    if (nodes.isEmpty() && comments.isEmpty())
        return;
    recordUndo();
    ++m_undoBatch;
    for (NodeBase *n : nodes)
        removeNode(n);
    for (CommentGraphicsItem *c : comments)
        removeComment(c);
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
}
