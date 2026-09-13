#include "Connection.h"
#include "Port.h"
#include "NodeBase.h"

namespace MyProject {

Connection::Connection(Port *source, Port *target, QObject *parent)
    : QObject(parent), m_sourcePort(source), m_targetPort(target)
{
    if (source && target) {
        source->addConnection(this);
        target->addConnection(this);
    }
}

Connection::~Connection()
{
    if (m_sourcePort) {
        m_sourcePort->removeConnection(this);
    }
    if (m_targetPort) {
        m_targetPort->removeConnection(this);
    }
}

Port *Connection::sourcePort() const
{
    return m_sourcePort;
}

Port *Connection::targetPort() const
{
    return m_targetPort;
}

NodeBase *Connection::getSourceNode() const
{
    return m_sourcePort ? m_sourcePort->node() : nullptr;
}

NodeBase *Connection::getDestinationNode() const
{
    return m_targetPort ? m_targetPort->node() : nullptr;
}

int Connection::getSourcePort() const
{
    return m_sourcePort ? m_sourcePort->index() : -1;
}

int Connection::getDestinationPort() const
{
    return m_targetPort ? m_targetPort->index() : -1;
}

bool Connection::isValid() const
{
    return m_sourcePort && m_targetPort && 
           m_sourcePort->type() == Port::OUTPUT && 
           m_targetPort->type() == Port::INPUT;
}

} // namespace MyProject
