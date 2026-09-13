#include "ConnectionGraphicsItem.h"
#include "Connection.h"
#include "Port.h"
#include "FlowScene.h"
#include <QPainter>
#include <QMenu>
#include <QAction>
#include <cmath>
#include <QStyleOptionGraphicsItem>
#include <QStyle>

ConnectionGraphicsItem::ConnectionGraphicsItem(MyConnection *connection, QGraphicsItem *parent)
    : QGraphicsItem(parent), m_connection(connection)
{
    setFlag(QGraphicsItem::ItemIsSelectable);
    setZValue(-1); // 确保线在算子下面
    updatePath();
}

ConnectionGraphicsItem::~ConnectionGraphicsItem()
{
}

QRectF ConnectionGraphicsItem::boundingRect() const
{
    return m_path.boundingRect();
}

void ConnectionGraphicsItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(widget);

    painter->setRenderHint(QPainter::Antialiasing, true);
    const QStyle::State st = option ? option->state : QStyle::State_None;
    QColor lineColor(0x4a, 0xc4, 0xff);
    qreal width = (st & QStyle::State_Selected) ? 2.8 : 2.2;
    if (st & QStyle::State_Selected) {
        lineColor = QColor(0xff, 0xd8, 0x66);
    }

    // 绘制线条（工业 IDE 常用的浅蓝线 + 高亮变黄）
    painter->setPen(QPen(lineColor, width));
    painter->drawPath(m_path);
    
    // 绘制箭头
    if (!m_path.isEmpty()) {
        QPointF endPoint = m_path.currentPosition();
        QPointF startPoint = m_path.elementAt(m_path.elementCount() - 2);
        
        QLineF line(startPoint, endPoint);
        qreal arrowSize = 20;
        
        // 计算箭头角度
        double angle = std::atan2(-line.dy(), line.dx());
        
        // 创建箭头
        QPointF arrowP1 = endPoint + QPointF(sin(angle - M_PI / 3) * arrowSize, cos(angle - M_PI / 3) * arrowSize);
        QPointF arrowP2 = endPoint + QPointF(sin(angle + M_PI / 3) * arrowSize, cos(angle + M_PI / 3) * arrowSize);
        
        // 绘制箭头
        painter->setBrush(lineColor);
        painter->drawPolygon(QPolygonF() << endPoint << arrowP1 << arrowP2);
    }
}

MyConnection *ConnectionGraphicsItem::connection() const
{
    return m_connection;
}

void ConnectionGraphicsItem::updatePath()
{
    if (!m_connection) {
        return;
    }
    
    Port *sourcePort = m_connection->sourcePort();
    Port *targetPort = m_connection->targetPort();
    
    if (!sourcePort || !targetPort) {
        return;
    }
    
    QPointF sourcePos = sourcePort->position();
    QPointF targetPos = targetPort->position();
    
    // Create a curved path
    m_path = QPainterPath();
    m_path.moveTo(sourcePos);
    
    // Add a control point for curve
    QPointF controlPoint((sourcePos.x() + targetPos.x()) / 2, (sourcePos.y() + targetPos.y()) / 2);
    m_path.quadTo(controlPoint, targetPos);
    
    update();
}

void ConnectionGraphicsItem::contextMenuEvent(QGraphicsSceneContextMenuEvent *event)
{
    FlowScene *scene = dynamic_cast<FlowScene *>(this->scene());
    if (!scene) return;

    QMenu menu;
    QAction *deleteAction = menu.addAction(scene->isEditLocked() ? "锁定中，无法删除" : "Delete Connection");
    deleteAction->setEnabled(!scene->isEditLocked());
    QAction *selectedAction = menu.exec(event->screenPos());
    
    if (!scene->isEditLocked() && selectedAction == deleteAction) {
        scene->removeConnection(m_connection);
    }
}
