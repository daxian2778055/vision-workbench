#include "NodeGroupItem.h"
#include "FlowScene.h"
#include "NodeBase.h"
#include "NodeGraphicsItem.h"

#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QJsonArray>
#include <QInputDialog>
#include <QMenu>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

namespace {
constexpr qreal kTitleBarHeight = 22.0;
constexpr qreal kPadding = 14.0;

/// 分组标题栏配色（与注释框同一套暗色画布风格；要改风格只动这里）
const QColor kBarColor(0x33, 0x4c, 0x63, 170);
const QColor kBodyColor(0x2b, 0x3a, 0x4a, 55);
const QColor kBorderColor(0x4a, 0x6b, 0x8a);
const QColor kTextColor(0xd8, 0xe4, 0xf0);
const QColor kSelectedColor(0x40, 0xa9, 0xff);
}   // namespace

NodeGroupItem::NodeGroupItem(const QString &title, QGraphicsItem *parent)
    : QGraphicsItem(parent)
    , m_title(title.isEmpty() ? QStringLiteral("分组") : title)
{
    setFlag(QGraphicsItem::ItemIsMovable);
    setFlag(QGraphicsItem::ItemIsSelectable);
    // 画在所有节点/连线之下：既不会被节点压住看不见，也不会抢节点的点击
    setZValue(-10);
}

qreal NodeGroupItem::titleBarHeight()
{
    return kTitleBarHeight;
}

QRectF NodeGroupItem::boundingRect() const
{
    return m_rect.adjusted(-2, -2, 2, 2);
}

void NodeGroupItem::setTitle(const QString &title)
{
    m_title = title.trimmed().isEmpty() ? QStringLiteral("分组") : title.trimmed();
    update();
}

bool NodeGroupItem::hasMember(int moduleId) const
{
    return m_members.contains(moduleId);
}

void NodeGroupItem::setMembers(const QList<int> &moduleIds)
{
    QList<int> kept;
    FlowScene *s = flowScene();
    for (int id : moduleIds) {
        if (kept.contains(id))
            continue;
        // 场景可用时按"算子确实还在"过滤；从方案加载（尚未挂进场景）时先原样收下，
        // 由 FlowScene::extrasFromJson 挂进场景后再调一次本函数完成过滤。
        if (s && !s->nodeByModuleId(id))
            continue;
        kept.append(id);
    }
    m_members = kept;
    update();
}

bool NodeGroupItem::removeMember(int moduleId)
{
    if (m_members.removeAll(moduleId) == 0)
        return false;
    update();
    return true;
}

bool NodeGroupItem::fitToMembers()
{
    FlowScene *s = flowScene();
    if (!s)
        return false;

    QRectF box;
    bool any = false;
    for (int id : m_members) {
        NodeBase *n = s->nodeByModuleId(id);
        if (!n)
            continue;
        if (NodeGraphicsItem *item = s->getGraphicsItemForNode(n)) {
            const QRectF r = item->sceneBoundingRect();
            box = any ? box.united(r) : r;
            any = true;
        }
    }
    if (!any)
        return false;

    prepareGeometryChange();
    // 框体左上角 = 成员外接矩形左上角再让出内边距与标题栏高度
    setPos(box.left() - kPadding, box.top() - kPadding - kTitleBarHeight);
    m_rect = QRectF(0, 0, box.width() + 2 * kPadding,
                    box.height() + 2 * kPadding + kTitleBarHeight);
    update();
    return true;
}

void NodeGroupItem::setFrameSize(qreal width, qreal height)
{
    if (width < 20.0 || height < 20.0)
        return;
    prepareGeometryChange();
    m_rect = QRectF(0, 0, width, height);
    update();
}

void NodeGroupItem::moveMembersBy(const QPointF &delta)
{
    if (delta.isNull())
        return;
    FlowScene *s = flowScene();
    if (!s)
        return;
    for (int id : m_members) {
        NodeBase *n = s->nodeByModuleId(id);
        if (!n)
            continue;
        if (NodeGraphicsItem *item = s->getGraphicsItemForNode(n)) {
            item->setPos(item->pos() + delta);
            item->syncNodeGeometry();   // 端口/连线跟随（与节点自身拖动同一条路径）
        }
    }
    if (QGraphicsScene *sc = scene())
        sc->update();
}

void NodeGroupItem::moveGroupBy(const QPointF &delta)
{
    if (delta.isNull())
        return;
    setPos(pos() + delta);
    moveMembersBy(delta);
}

bool NodeGroupItem::hitTitleBar(const QPointF &itemPos) const
{
    return itemPos.y() >= 0.0 && itemPos.y() <= kTitleBarHeight
           && itemPos.x() >= 0.0 && itemPos.x() <= m_rect.width();
}

QJsonObject NodeGroupItem::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("title")] = m_title;
    o[QStringLiteral("x")] = pos().x();
    o[QStringLiteral("y")] = pos().y();
    o[QStringLiteral("w")] = m_rect.width();
    o[QStringLiteral("h")] = m_rect.height();
    QJsonArray members;
    for (int id : m_members)
        members.append(id);
    o[QStringLiteral("members")] = members;   // 模块号（跨会话稳定；见头文件"设计契约"）
    return o;
}

NodeGroupItem *NodeGroupItem::fromJson(const QJsonObject &json)
{
    auto *item = new NodeGroupItem(json.value(QStringLiteral("title")).toString());
    item->setPos(json.value(QStringLiteral("x")).toDouble(),
                 json.value(QStringLiteral("y")).toDouble());
    const double w = json.value(QStringLiteral("w")).toDouble(0);
    const double h = json.value(QStringLiteral("h")).toDouble(0);
    if (w > 20 && h > 20)
        item->m_rect = QRectF(0, 0, w, h);

    QList<int> members;
    for (const QJsonValue &v : json.value(QStringLiteral("members")).toArray()) {
        const int id = v.toInt(0);
        if (id > 0 && !members.contains(id))
            members.append(id);
    }
    item->m_members = members;
    return item;
}

void NodeGroupItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                          QWidget *widget)
{
    Q_UNUSED(widget);
    painter->setRenderHint(QPainter::Antialiasing, true);

    // 框体：虚线边框 + 半透明填充，一眼能看出是"容器"而不是算子
    painter->setPen(QPen(kBorderColor, 1.2, Qt::DashLine));
    painter->setBrush(kBodyColor);
    painter->drawRoundedRect(m_rect, 6, 6);

    // 标题栏
    const QRectF bar(0, 0, m_rect.width(), kTitleBarHeight);
    painter->setPen(Qt::NoPen);
    painter->setBrush(kBarColor);
    painter->drawRoundedRect(bar, 6, 6);

    QFont font = painter->font();
    font.setPointSizeF(9.0);
    font.setBold(true);
    painter->setFont(font);
    painter->setPen(kTextColor);
    const QString label = m_members.isEmpty()
                              ? m_title
                              : QStringLiteral("%1  ·  %2 个算子")
                                    .arg(m_title)
                                    .arg(m_members.size());
    painter->drawText(bar.adjusted(8, 0, -8, 0), Qt::AlignLeft | Qt::AlignVCenter, label);

    if (option && (option->state & QStyle::State_Selected)) {
        painter->setPen(QPen(kSelectedColor, 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(m_rect.adjusted(-1, -1, 1, 1), 6, 6);
    }
}

FlowScene *NodeGroupItem::flowScene() const
{
    return dynamic_cast<FlowScene *>(scene());
}

void NodeGroupItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    // 只有标题栏接受拖动：其余区域放行，交给场景做框选/点中组内算子
    if (!hitTitleBar(event->pos())) {
        event->ignore();
        return;
    }
    m_undoRecorded = false;
    QGraphicsItem::mousePressEvent(event);
}

void NodeGroupItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    // 首次真正移动时记录撤销（在基类移动之前 ⇒ 快照是"移动前"的状态，一次撤销即可复原）
    if (!m_undoRecorded) {
        if (FlowScene *s = flowScene()) {
            if (!s->isEditLocked())
                s->recordUndo();
        }
        m_undoRecorded = true;
    }

    const QPointF before = pos();
    QGraphicsItem::mouseMoveEvent(event);   // 基类按鼠标位移移动框体
    const QPointF applied = pos() - before;
    if (!applied.isNull())
        moveMembersBy(applied);             // 成员跟随同样的位移
}

void NodeGroupItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsItem::mouseReleaseEvent(event);
    m_undoRecorded = false;
    if (QGraphicsScene *sc = scene())
        sc->update();
}

void NodeGroupItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    if (!hitTitleBar(event->pos())) {
        event->ignore();
        return;
    }
    QGraphicsItem::mouseDoubleClickEvent(event);
    renameInteractively();
}

void NodeGroupItem::renameInteractively()
{
    FlowScene *s = flowScene();
    if (!s || s->isEditLocked())
        return;
    bool ok = false;
    const QString text = QInputDialog::getText(nullptr, QStringLiteral("重命名分组"),
                                               QStringLiteral("名称:"), QLineEdit::Normal,
                                               m_title, &ok);
    if (!ok || text.trimmed().isEmpty() || text.trimmed() == m_title)
        return;
    s->recordUndo();   // 改名可撤销（放在真正改动之前）
    setTitle(text);
}

void NodeGroupItem::contextMenuEvent(QGraphicsSceneContextMenuEvent *event)
{
    FlowScene *s = flowScene();
    if (!s)
        return;
    const bool locked = s->isEditLocked();

    QMenu menu;
    QAction *rename = menu.addAction(QStringLiteral("重命名分组…"));
    QAction *dissolve = menu.addAction(QStringLiteral("解散分组（保留算子）"));
    QAction *selectMembers = menu.addAction(QStringLiteral("选中组内算子"));
    rename->setEnabled(!locked);
    dissolve->setEnabled(!locked);

    QAction *chosen = menu.exec(event->screenPos());
    if (chosen == rename) {
        renameInteractively();
    } else if (chosen == dissolve) {
        s->removeGroup(this);   // 内部记录撤销；只删框，不动算子
    } else if (chosen == selectMembers) {
        for (int id : m_members) {
            if (NodeBase *n = s->nodeByModuleId(id)) {
                if (NodeGraphicsItem *item = s->getGraphicsItemForNode(n))
                    item->setSelected(true);
            }
        }
    }
}
