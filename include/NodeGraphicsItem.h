#pragma once

#include <QGraphicsItem>
#include <QJsonObject>
#include <QPainter>
#include <QRectF>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>

class NodeBase;
class Port;
class PortGraphicsItem;

class NodeGraphicsItem : public QGraphicsItem
{
public:
    NodeGraphicsItem(NodeBase *node, QGraphicsItem *parent = nullptr);
    ~NodeGraphicsItem();

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    NodeBase *node() const;
    PortGraphicsItem *getPortGraphicsItem(Port *port) const;

    void updatePosition();
    void updatePorts();
    /// 把图形位置写回节点并刷新端口/连线
    void syncNodeGeometry();

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override;

public:
    bool m_selected; // 选中状态

private:
    NodeBase *m_node;
    QMap<Port *, PortGraphicsItem *> m_portItems;
    QRectF m_boundingRect;
    QPointF m_pressPos;        /// 鼠标按下时的位置（移动撤销判断）
    QJsonObject m_pressSnapshot;  /// 按下时的场景快照；仅松开且真移动时才压入撤销栈
    bool m_pushedMoveUndo = false;  /// 保留字段兼容（现松开时才压栈，恒为 false 路径不使用）
};
