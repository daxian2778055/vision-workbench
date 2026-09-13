#pragma once

#include <QGraphicsItem>
#include <QGraphicsSceneMouseEvent>

class Port;
class ConnectionDragHelper;

class PortGraphicsItem : public QGraphicsItem
{
public:
    PortGraphicsItem(Port *port, QGraphicsItem *parent = nullptr);
    ~PortGraphicsItem();

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    Port *port() const;

    void updatePosition();

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

private:
    Port *m_port;
    const qreal PORT_RADIUS = 8;
    const qreal PORT_DETECTION_RADIUS = 16;
};
