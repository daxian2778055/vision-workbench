#include "NodeBase.h"
#include "Port.h"
#include "DataObject.h"

// 模块 ID 分配器：单调且**不回收**。
// 回收复用会让"新节点"继承"旧节点"在执行器缓存里的输出变量表（S4：删 A 后新建 B 复用同号
// → m_nodeOutputVars[号] 残留 → 结果错）。模块号是方案内 {模块号.参数名} 引用与配方键的
// 稳定标识，不复用反而更稳。
#include <QMutex>
#include <QMutexLocker>
// 分配器是进程级共享状态，而节点可能在流程线程（方案加载/复制）与 UI 线程并发
// 创建/销毁，计数器非线程安全，必须加锁保护。
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
    // 单调不回收：模块号是方案内 {模块号.参数名} 引用与配方键的稳定标识，
    // 回收复用会让"新节点"继承"旧节点"在执行器缓存里的输出变量表（S4）。
    return s_nextId++;
}

void NodeBase::releaseModuleId(int id)
{
    // 不再回收（见 allocateModuleId）。保留为空实现，避免改动析构等调用点。
    Q_UNUSED(id)
}

void NodeBase::reserveModuleId(int id)
{
    // 从方案文件恢复节点时，模块号可能是会话内最大已分配号；把全局计数器抬高到它之上，
    // 否则随后新建节点会再次分配到同一号（新会话 s_nextId 从 1 重新计）。
    QMutexLocker locker(&s_idMutex);
    if (id + 1 > s_nextId)
        s_nextId = id + 1;
}

bool NodeBase::executionSuccess() const
{
    return m_executionSuccess;
}

bool NodeBase::hasExecuted() const
{
    return m_hasExecuted;
}


