#include "Port.h"
#include "Connection.h"
#include "NodeBase.h"

Port::Port(PortType type, const QString &name, NodeBase *parent, PortDataType dataType)
    : QObject(reinterpret_cast<QObject *>(parent))
    , m_type(type)
    , m_dataType(dataType)
    , m_name(name)
    , m_node(parent)
    , m_position(0, 0)
{
}

Port::~Port()
{
    qDeleteAll(m_connections);
}

Port::PortType Port::type() const
{
    return m_type;
}

QString Port::name() const
{
    return m_name;
}

PortDataType Port::dataType() const
{
    return m_dataType;
}

NodeBase *Port::node() const
{
    return m_node;
}

int Port::index() const
{
    if (!m_node) {
        return -1;
    }
    
    if (m_type == INPUT) {
        return m_node->inputPorts().indexOf(const_cast<Port*>(this));
    } else {
        return m_node->outputPorts().indexOf(const_cast<Port*>(this));
    }
}

QPointF Port::position() const
{
    return m_position;
}

void Port::setPosition(const QPointF &pos)
{
    m_position = pos;
}

QList<MyConnection *> Port::connections() const
{
    return m_connections;
}

void Port::addConnection(MyConnection *connection)
{
    if (!m_connections.contains(connection)) {
        m_connections.append(connection);
    }
}

void Port::removeConnection(MyConnection *connection)
{
    m_connections.removeOne(connection);
}

bool Port::isConnected() const
{
    return !m_connections.isEmpty();
}
