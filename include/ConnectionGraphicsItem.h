#pragma once

#include <QGraphicsItem>
#include <QPainterPath>
#include <QGraphicsSceneContextMenuEvent>

namespace MyProject {
    class Connection;
}

using MyConnection = MyProject::Connection;

class ConnectionGraphicsItem : public QGraphicsItem
{
public:
    ConnectionGraphicsItem(MyConnection *connection, QGraphicsItem *parent = nullptr);
    ~ConnectionGraphicsItem();

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    MyConnection *connection() const;

    void updatePath();

protected:
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override;

private:
    MyConnection *m_connection;
    QPainterPath m_path;
};
