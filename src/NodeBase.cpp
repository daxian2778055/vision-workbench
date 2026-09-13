#include "NodeBase.h"
#include "Port.h"
#include "DataObject.h"

// 模块 ID 分配器：删除节点时回收，新建节点时从回收池取最小可用 ID
#include <QSet>
#include <algorithm>
#include <QMutex>
#include <QMutexLocker>
// 分配器是进程级共享状态，而节点可能在流程线程（方案加载/复制）与 UI 线程并发
// 创建/销毁。QSet 与计数器均非线程安全，必须加锁保护，否则会破坏容器结构。
static QSet<int> s_recycledIds;
static int s_nextId = 1;
static QMutex s_idMutex;

NodeBase::NodeBase(QObject *parent)
    : QObject(parent), m_position(0, 0), m_size(120, 80), m_moduleId(NodeBase::allocateModuleId()), m_type(IMAGE_ACQUISITION), m_executionSuccess(false), m_hasExecuted(false)
{
}

NodeBase::~NodeBase()
{
    NodeBase::releaseModuleId(m_moduleId);
    qDeleteAll(m_inputPorts);
    qDeleteAll(m_outputPorts);
}

void NodeBase::setPosition(const QPointF &pos)
{
    m_position = pos;
}

QPointF NodeBase::position() const
{
    return m_position;
}

void NodeBase::setSize(const QSizeF &size)
{
    m_size = size;
}

QSizeF NodeBase::size() const
{
    return m_size;
}

QString NodeBase::name() const
{
    return m_name;
}

void NodeBase::setName(const QString &name)
{
    m_name = name;
}

NodeBase::NodeType NodeBase::type() const
{
    return m_type;
}

QList<Port *> NodeBase::inputPorts() const
{
    return m_inputPorts;
}

QList<Port *> NodeBase::outputPorts() const
{
    return m_outputPorts;
}

Port *NodeBase::addInputPort(const QString &name, PortDataType dataType)
{
    Port *port = new Port(Port::INPUT, name, this, dataType);
    m_inputPorts.append(port);
    return port;
}

Port *NodeBase::addOutputPort(const QString &name, PortDataType dataType)
{
    Port *port = new Port(Port::OUTPUT, name, this, dataType);
    m_outputPorts.append(port);
    return port;
}

bool NodeBase::hasConnection() const
{
    for (Port *port : m_inputPorts) {
        if (port->isConnected()) {
            return true;
        }
    }
    for (Port *port : m_outputPorts) {
        if (port->isConnected()) {
            return true;
        }
    }
    return false;
}

bool NodeBase::execute()
{
    // 禁用算子：跳过算法执行，视为通过（数据透传交由流程运行时处理）
    if (!m_enabled) {
        m_hasExecuted = true;
        m_executionSuccess = true;
        return true;
    }

    try {
        m_hasExecuted = true;
        m_executionSuccess = process();

        return m_executionSuccess;
    } catch (const std::exception &e) {
        qWarning() << "Error executing node" << m_name << ":" << e.what();
        m_hasExecuted = true;
        m_executionSuccess = false;
        return false;
    } catch (...) {
        qWarning() << "Unknown error executing node" << m_name;
        m_hasExecuted = true;
        m_executionSuccess = false;
        return false;
    }
}

void NodeBase::setInputData(int portIndex, QSharedPointer<DataObject> data)
{
    QMutexLocker locker(&m_dataMutex);
    m_inputData[portIndex] = data;
}

QSharedPointer<DataObject> NodeBase::getInputData(int portIndex) const
{
    QMutexLocker locker(&m_dataMutex);
    return m_inputData.value(portIndex);
}

QSharedPointer<DataObject> NodeBase::getOutputData(int portIndex) const
{
    QMutexLocker locker(&m_dataMutex);
    return m_outputData.value(portIndex);
}

void NodeBase::setOutputData(int portIndex, QSharedPointer<DataObject> data)
{
    QMutexLocker locker(&m_dataMutex);
    m_outputData[portIndex] = data;
}

int NodeBase::moduleId() const
{
    return m_moduleId;
}

QString NodeBase::fullName() const
{
    return QString("%1 [%2]").arg(m_name).arg(m_moduleId);
}

int NodeBase::allocateModuleId()
{
    QMutexLocker locker(&s_idMutex);
    if (!s_recycledIds.isEmpty()) {
        int id = *std::min_element(s_recycledIds.begin(), s_recycledIds.end());
        s_recycledIds.remove(id);
        return id;
    }
    return s_nextId++;
}

void NodeBase::releaseModuleId(int id)
{
    if (id <= 0)
        return;
    QMutexLocker locker(&s_idMutex);
    s_recycledIds.insert(id);
}

bool NodeBase::executionSuccess() const
{
    return m_executionSuccess;
}

bool NodeBase::hasExecuted() const
{
    return m_hasExecuted;
}


