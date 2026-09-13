#pragma once

#include <QObject>

class Port;
class NodeBase;

namespace MyProject {

class Connection : public QObject
{
    Q_OBJECT

public:
    Connection(Port *source, Port *target, QObject *parent = nullptr);
    ~Connection();

    Port *sourcePort() const;
    Port *targetPort() const;

    NodeBase *getSourceNode() const;
    NodeBase *getDestinationNode() const;
    int getSourcePort() const;
    int getDestinationPort() const;

    bool isValid() const;

private:
    Port *m_sourcePort;
    Port *m_targetPort;
};

} // namespace MyProject
