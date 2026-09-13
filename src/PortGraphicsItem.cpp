#include "PortGraphicsItem.h"
#include "Port.h"
#include "NodeGraphicsItem.h"
#include "FlowScene.h"
#include "ConnectionDragHelper.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QCursor>
#include <QStyleOptionGraphicsItem>

PortGraphicsItem::PortGraphicsItem(Port *port, QGraphicsItem *parent)
    : QGraphicsItem(parent)
    , m_port(port)
{
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setCursor(Qt::CrossCursor);
    setZValue(10);
}

PortGraphicsItem::~PortGraphicsItem()
{
}

QRectF PortGraphicsItem::boundingRect() const
{
    return QRectF(-PORT_DETECTION_RADIUS, -PORT_DETECTION_RADIUS,
                  2 * PORT_DETECTION_RADIUS, 2 * PORT_DETECTION_RADIUS);
}

void PortGraphicsItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)

    QColor fillColor;
    if (m_port->type() == Port::INPUT) {
        fillColor = QColor(0x52, 0xc4, 0x1a);
    } else {
        fillColor = QColor(0x18, 0x90, 0xff);
    }

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(fillColor);
    painter->setPen(QPen(fillColor.darker(130), 1));
    painter->drawEllipse(QPointF(0, 0), PORT_RADIUS, PORT_RADIUS);

    if (m_port->isConnected()) {
        painter->setBrush(Qt::white);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(QPointF(0, 0), PORT_RADIUS * 0.4, PORT_RADIUS * 0.4);
    }
}

Port *PortGraphicsItem::port() const
{
    return m_port;
}

void PortGraphicsItem::updatePosition()
{
}

void PortGraphicsItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_port) {
        QGraphicsItem::mousePressEvent(event);
        return;
    }

    auto *flowScene = dynamic_cast<FlowScene *>(scene());
    if (!flowScene || flowScene->isEditLocked()) {
        event->ignore();
        return;
    }

    // 仅从输出端口拖出连线（对标 VisionMaster）
    if (m_port->type() != Port::OUTPUT) {
        event->ignore();
        return;
    }

    ConnectionDragHelper *helper = flowScene->dragHelper();
    if (helper) {
        helper->startDrag(this, event->scenePos());
        grabMouse();
        event->accept();
        return;
    }

    QGraphicsItem::mousePressEvent(event);
}

void PortGraphicsItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    auto *flowScene = dynamic_cast<FlowScene *>(scene());
    ConnectionDragHelper *helper = flowScene ? flowScene->dragHelper() : nullptr;
    if (helper && helper->isDragging()) {
        helper->updateDrag(event->scenePos());
        event->accept();
        return;
    }
    QGraphicsItem::mouseMoveEvent(event);
}

void PortGraphicsItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    auto *flowScene = dynamic_cast<FlowScene *>(scene());
    ConnectionDragHelper *helper = flowScene ? flowScene->dragHelper() : nullptr;
    if (helper && helper->isDragging()) {
        helper->updateDrag(event->scenePos());
        helper->endDrag();
        ungrabMouse();
        event->accept();
        return;
    }
    QGraphicsItem::mouseReleaseEvent(event);
}
