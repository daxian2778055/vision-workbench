#pragma once

#include "PortDataType.h"
#include <QObject>
#include <QPointF>
#include <QString>

class NodeBase;
namespace MyProject {
    class Connection;
}

using MyConnection = MyProject::Connection;

class Port : public QObject
{
    Q_OBJECT

public:
    enum PortType {
        INPUT,
        OUTPUT
    };

    Port(PortType type, const QString &name, NodeBase *parent,
         PortDataType dataType = PortDataType::Image);
    ~Port();

    PortType type() const;
    QString name() const;
    PortDataType dataType() const;
    NodeBase *node() const;
    int index() const;

    QPointF position() const;
    void setPosition(const QPointF &pos);

    QList<MyConnection *> connections() const;
    void addConnection(MyConnection *connection);
    void removeConnection(MyConnection *connection);
    bool isConnected() const;

private:
    PortType m_type;
    PortDataType m_dataType;
    QString m_name;
    NodeBase *m_node;
    QPointF m_position;
    QList<MyConnection *> m_connections;
};
