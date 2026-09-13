#include "ConnectionDragHelper.h"
#include "PortGraphicsItem.h"
#include "Port.h"
#include "PortConnectivity.h"
#include "FlowScene.h"
#include <QPainter>
#include <QPen>
#include <QGraphicsScene>

ConnectionDragHelper::ConnectionDragHelper(FlowScene *scene, QGraphicsItem *parent)
    : QGraphicsObject(parent), m_flowScene(scene)
{
    setZValue(1000);  // 确保在最上层
    hide();
}

void ConnectionDragHelper::startDrag(PortGraphicsItem *sourcePort, const QPointF &startPos)
{
    if (!sourcePort || !sourcePort->port()) return;

    prepareGeometryChange();
    m_sourcePortItem = sourcePort;
    m_startPos = sourcePort->scenePos();
    m_currentPos = startPos;
    m_dragging = true;

    // 设置源端口高亮
    sourcePort->setOpacity(0.7);

    show();
    update();
}

void ConnectionDragHelper::updateDrag(const QPointF &currentPos)
{
    if (!m_dragging) return;

    prepareGeometryChange();
    m_currentPos = currentPos;

    // 检测鼠标下方的端口
    if (m_flowScene) {
        QList<QGraphicsItem *> items = m_flowScene->items(currentPos);
        PortGraphicsItem *targetPort = nullptr;

        for (QGraphicsItem *item : items) {
            PortGraphicsItem *portItem = dynamic_cast<PortGraphicsItem *>(item);
            if (portItem && portItem != m_sourcePortItem) {
                targetPort = portItem;
                break;
            }
        }

        // 更新高亮
        if (targetPort != m_highlightedPort) {
            clearHighlight();
            if (targetPort && canConnect(m_sourcePortItem->port(), targetPort->port())) {
                highlightTargetPort(targetPort);
            }
        }
    }

    update();
}

void ConnectionDragHelper::endDrag()
{
    if (!m_dragging) return;

    // 检查是否释放在可连接的端口上
    if (m_highlightedPort && m_sourcePortItem) {
        Port *sourcePort = m_sourcePortItem->port();
        Port *targetPort = m_highlightedPort->port();

        if (canConnect(sourcePort, targetPort)) {
            emit connectionCreated(sourcePort, targetPort);
        }
    }

    // 清理状态
    if (m_sourcePortItem) {
        m_sourcePortItem->setOpacity(1.0);
    }
    clearHighlight();

    m_sourcePortItem = nullptr;
    m_highlightedPort = nullptr;
    m_dragging = false;

    hide();
}

void ConnectionDragHelper::cancelDrag()
{
    if (!m_dragging) return;

    // 清理状态
    if (m_sourcePortItem) {
        m_sourcePortItem->setOpacity(1.0);
    }
    clearHighlight();

    m_sourcePortItem = nullptr;
    m_highlightedPort = nullptr;
    m_dragging = false;

    hide();
}

void ConnectionDragHelper::highlightTargetPort(PortGraphicsItem *targetPort)
{
    if (!targetPort) return;

    m_highlightedPort = targetPort;

    // 高亮目标端口
    QColor matchColor = getPortMatchColor(m_sourcePortItem->port(), targetPort->port());
    targetPort->setOpacity(0.7);

    // 添加高亮效果（通过设置刷子颜色）
    // 这里我们通过 update() 来触发重绘
    update();
}

void ConnectionDragHelper::clearHighlight()
{
    if (m_highlightedPort) {
        m_highlightedPort->setOpacity(1.0);
        m_highlightedPort = nullptr;
    }
}

QRectF ConnectionDragHelper::boundingRect() const
{
    if (!m_dragging) return QRectF();

    // 计算包围矩形
    qreal minX = qMin(m_startPos.x(), m_currentPos.x());
    qreal minY = qMin(m_startPos.y(), m_currentPos.y());
    qreal maxX = qMax(m_startPos.x(), m_currentPos.x());
    qreal maxY = qMax(m_startPos.y(), m_currentPos.y());

    return QRectF(minX - 10, minY - 10, maxX - minX + 20, maxY - minY + 20);
}

void ConnectionDragHelper::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    if (!m_dragging) return;

    painter->setRenderHint(QPainter::Antialiasing, true);

    // 绘制拖拽连线
    QPen pen(DRAG_LINE_COLOR, 2, Qt::DashLine);
    painter->setPen(pen);
    painter->drawLine(m_startPos, m_currentPos);

    // 绘制端点圆圈
    painter->setBrush(DRAG_LINE_COLOR);
    painter->drawEllipse(m_startPos, 4, 4);
    painter->drawEllipse(m_currentPos, 4, 4);

    // 如果有高亮的目标端口，绘制连接提示
    if (m_highlightedPort) {
        QColor matchColor = getPortMatchColor(m_sourcePortItem->port(), m_highlightedPort->port());

        // 绘制目标端口的高亮圈
        QPointF targetPos = m_highlightedPort->scenePos();
        painter->setPen(QPen(matchColor, 3));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(targetPos, 12, 12);
    }
}

bool ConnectionDragHelper::canConnect(Port *source, Port *target) const
{
    return PortConnectivity::canConnectPorts(source, target, nullptr, true);
}

QColor ConnectionDragHelper::getPortMatchColor(Port *source, Port *target) const
{
    if (!source || !target) return INVALID_COLOR;

    if (canConnect(source, target)) {
        return VALID_COLOR;
    } else {
        return INVALID_COLOR;
    }
}
