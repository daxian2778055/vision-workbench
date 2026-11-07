#include "NodeBase.h"
#include "Port.h"
#include "DataObject.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaType>

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
    : QObject(parent), m_position(0, 0), m_size(120, 80), m_type(IMAGE_ACQUISITION), m_executionSuccess(false), m_hasExecuted(false), m_stateBody(std::make_shared<NodeStateBody>())
{
    m_stateBody->moduleId = NodeBase::allocateModuleId();   // 单调不回收（见 allocateModuleId 注释）
}

NodeBase::~NodeBase()
{
    NodeBase::releaseModuleId(m_stateBody->moduleId);
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
    return m_stateBody->name;
}

void NodeBase::setName(const QString &name)
{
    m_stateBody->name = name;
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

int NodeBase::addInputSlot(const QString &name, PortDataType dataType)
{
    // 逻辑下标 = 已有真实端口数 + 已有逻辑槽数（inputBindings 以此为键）。
    const int index = m_inputPorts.size() + m_inputSlots.size();
    InputSlot slot;
    slot.name = name;
    slot.type = dataType;
    m_inputSlots.append(slot);
    return index;
}

void NodeBase::setInputBinding(int slot, int sourceModuleId, int sourcePort)
{
    if (slot < 0 || slot >= m_inputPorts.size() + m_inputSlots.size())
        return;
    if (slot < m_inputPorts.size())
        return;   // 真实端口由连线驱动，不接受订阅绑定
    if (sourceModuleId <= 0 || sourcePort < 0)
        return;
    QMutexLocker locker(&m_bindingMutex);
    m_inputBindings[slot] = InputBinding{sourceModuleId, sourcePort};
}

void NodeBase::clearInputBinding(int slot)
{
    QMutexLocker locker(&m_bindingMutex);
    m_inputBindings.remove(slot);
}

bool NodeBase::hasInputBinding(int slot) const
{
    QMutexLocker locker(&m_bindingMutex);
    return m_inputBindings.contains(slot);
}

QMap<int, NodeBase::InputBinding> NodeBase::inputBindings() const
{
    QMutexLocker locker(&m_bindingMutex);
    return m_inputBindings;
}

QString NodeBase::inputSlotName(int slot) const
{
    if (slot < m_inputPorts.size())
        return QString();
    const int pos = slot - m_inputPorts.size();
    if (pos < 0 || pos >= m_inputSlots.size())
        return QString();
    return m_inputSlots.at(pos).name;
}

PortDataType NodeBase::inputSlotType(int slot) const
{
    if (slot < m_inputPorts.size())
        return PortDataType::Any;
    const int pos = slot - m_inputPorts.size();
    if (pos < 0 || pos >= m_inputSlots.size())
        return PortDataType::Any;
    return m_inputSlots.at(pos).type;
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

NodeBase::ResultState NodeBase::resultState() const
{
    const quint32 s = m_resultState.load(std::memory_order_acquire);
    ResultState r;
    r.executed = (s & 1u) != 0;
    r.moduleOk = (s & 2u) != 0;
    r.resultNg = (s & 4u) != 0;
    return r;
}

void NodeBase::resetRoundState()
{
    m_resultState.store(0u, std::memory_order_release);
}

void NodeBase::commitResultState(bool moduleOk)
{
    const quint32 ng = isResultNg() ? 4u : 0u;
    m_resultState.store(1u | (moduleOk ? 2u : 0u) | ng, std::memory_order_release);
}

void NodeBase::setResultNg(bool on)
{
    // CAS 只改 NG 位：轮首/提交写入其它位时不会被覆盖
    quint32 cur = m_resultState.load(std::memory_order_relaxed);
    for (;;) {
        const quint32 next = on ? (cur | 4u) : (cur & ~4u);
        if (m_resultState.compare_exchange_weak(cur, next,
                                                std::memory_order_release,
                                                std::memory_order_relaxed))
            return;
    }
}

QVariantMap NodeBase::commonResultFields(qint64 elapsedMs) const
{
    QVariantMap fields;
    fields.insert(QStringLiteral("elapsedMs"), static_cast<double>(elapsedMs));
    return fields;   // HalconNode 覆写后补 roiCenter* / roiWidth / roiHeight / roiAngle
}

bool NodeBase::evaluateResultJudgment(const QVariantMap &commonFields, QString *reason) const
{
    // 配置格式（ModuleEditorDialog「结果显示 → 结果判断」写入）：
    // resJudgeConditions = [{"param":"maxGray","op":"<=","value":"200"}, ...]
    // resJudgeMode：0 = 全部条件满足（AND，默认）/ 1 = 任一条件满足（OR）。
    // 公共字段（elapsedMs / roi*）不在节点参数表里，由 commonFields 提供且优先。
    auto paramValue = [&](const QString &key) -> QVariant {
        if (commonFields.contains(key))
            return commonFields.value(key);
        return getParam(key);
    };
    const QString raw = paramValue(QStringLiteral("resJudgeConditions")).toString().trimmed();
    if (raw.isEmpty())
        return true;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &parseError);
    // 脏配置不能静默放行：条件根本没被解析就等于"没有判断"，产线会漏检。
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        if (reason)
            *reason = QStringLiteral("结果判断：条件配置格式错误（%1），无法判断")
                          .arg(parseError.error == QJsonParseError::NoError
                                   ? QStringLiteral("顶层不是数组")
                                   : parseError.errorString());
        return false;
    }
    const QJsonArray conditions = doc.array();
    if (conditions.isEmpty())
        return true;

    // 结果字段显示名：与「当前结果」面板同一口径，原因里用中文名而不是参数键
    QHash<QString, QString> labels;
    for (const NodeResultField &field : resultDisplayFields())
        labels.insert(field.key, field.label);

    const bool orMode = paramValue(QStringLiteral("resJudgeMode")).toInt() == 1;
    QString firstFailure;
    bool anyPass = false;
    int evaluated = 0;

    for (const QJsonValue &value : conditions) {
        // 坏条件/空字段名不能静默跳过：跳过等于该条件恒通过（尤其 OR 模式）。
        if (!value.isObject()) {
            ++evaluated;
            if (firstFailure.isEmpty())
                firstFailure = QStringLiteral("结果判断：第 %1 条条件格式错误（非对象）")
                                   .arg(evaluated);
            continue;
        }
        const QJsonObject condition = value.toObject();
        const QString key = condition.value(QStringLiteral("param")).toString().trimmed();
        if (key.isEmpty()) {
            ++evaluated;
            if (firstFailure.isEmpty())
                firstFailure = QStringLiteral("结果判断：第 %1 条条件缺少字段名")
                                   .arg(evaluated);
            continue;
        }
        ++evaluated;
        const QString op = condition.value(QStringLiteral("op")).toString().trimmed();
        const QString expectedText = condition.value(QStringLiteral("value")).toString();
        const QString label = labels.value(key, key);

        auto fail = [&](const QString &why) {
            if (firstFailure.isEmpty())
                firstFailure = QStringLiteral("结果判断：%1（%2）").arg(why, label);
        };

        const QVariant actualVar = paramValue(key);
        if (!actualVar.isValid() || actualVar.isNull()) {
            fail(QStringLiteral("缺少结果字段 %1，无法判断").arg(key));
            continue;
        }

        bool actualOk = false;
        bool expectedOk = false;
        double actual = actualVar.toDouble(&actualOk);
        double expected = expectedText.toDouble(&expectedOk);
        if (actualVar.typeId() == QMetaType::Bool) {
            actual = actualVar.toBool() ? 1.0 : 0.0;
            actualOk = true;
        }

        bool pass = false;
        if (actualOk && expectedOk) {
            if (op == QLatin1String(">="))
                pass = actual >= expected;
            else if (op == QLatin1String("<="))
                pass = actual <= expected;
            else if (op == QLatin1String(">"))
                pass = actual > expected;
            else if (op == QLatin1String("<"))
                pass = actual < expected;
            else if (op == QLatin1String("!="))
                pass = actual != expected;
            else
                pass = actual == expected;   // 默认 ==
        } else if (op == QLatin1String("==") || op == QLatin1String("!=")) {
            // 文本比较：期望值或实际值为空时不算「不等」，否则 != 会 100% 放行 → 漏检。
            const QString actualText = actualVar.toString();
            if (expectedText.trimmed().isEmpty() || actualText.isEmpty()) {
                fail(QStringLiteral("%1 缺少可比较的值，无法用 %2 比较（实际 '%3'，期望 '%4'）")
                         .arg(key, op, actualText, expectedText));
                continue;
            }
            const bool equal = actualText == expectedText;
            pass = (op == QLatin1String("==")) ? equal : !equal;
        } else {
            fail(QStringLiteral("%1 不是数值，无法用 %2 比较（实际 %3）")
                     .arg(key, op, actualVar.toString()));
            continue;
        }

        if (pass) {
            anyPass = true;
        } else {
            fail(QStringLiteral("%1 %2 %3，实际 %4")
                     .arg(label, op, expectedText, actualVar.toString()));
            if (!orMode)
                break;   // AND：第一条不满足即可返回
        }
    }

    if (evaluated == 0)
        return true;
    if (orMode) {
        if (anyPass)
            return true;
        if (reason)
            *reason = firstFailure.isEmpty()
                          ? QStringLiteral("结果判断：所有条件均不满足")
                          : firstFailure;
        return false;
    }
    if (firstFailure.isEmpty())
        return true;
    if (reason)
        *reason = firstFailure;
    return false;
}

bool NodeBase::execute()
{
    // 每轮统一复位整轮结果状态（executed / moduleOk / resultNg 一起清）：这是所有节点
    // 执行的唯一入口，覆盖禁用、自定义 process()、processDataOutputs() 等全部路径。
    // 未被本轮执行的节点另由 FlowExecutor 的轮首遍历 / 跳过记录复位。
    // 用单原子清整轮，UI 便不会在"轮首已复位 NG、参数表还是上一轮值"的窗口里读到矛盾态。
    resetRoundState();
    // 禁用算子：跳过算法执行，视为通过（数据透传交由流程运行时处理）
    if (!m_enabled) {
        m_hasExecuted = true;
        m_executionSuccess = true;
        // 禁用节点没跑 process()：必须同步本轮状态，否则上一轮的 NG 原因、耗时和
        // moduleStatus=false 会挂在这一轮「成功」行上（结果表/画布出现矛盾态）。
        m_lastElapsedMs.store(0, std::memory_order_relaxed);
        setParam(QStringLiteral("moduleStatus"), true);
        setParam(QStringLiteral("lastError"), QString());
        commitResultState(true);
        return true;
    }

    try {
        m_hasExecuted = true;
        QElapsedTimer nodeTimer;
        nodeTimer.start();
        m_executionSuccess = process();
        const qint64 elapsedMs = nodeTimer.elapsed();
        m_lastElapsedMs.store(elapsedMs, std::memory_order_relaxed);

        // VM「结果显示 → 结果判断」统一入口：
        //  · 所有节点类型（HalconNode / 数据型 processDataOutputs / 自定义 process）都经过这里；
        //  · 所有执行路径（流程执行、参数坞/模块编辑器「执行算子」）也都经过这里；
        //  · 耗时与 ROI 等公共字段在此自测，和结果表快照用同一份 commonResultFields()，
        //    判断依据不再与面板显示脱节。
        // 不满足时判红但**结果有效**：保留已发布的输出，写原因，置 resultNg。
        if (m_executionSuccess) {
            QString judgeReason;
            if (!evaluateResultJudgment(commonResultFields(elapsedMs), &judgeReason)) {
                setParam(QStringLiteral("moduleStatus"), false);
                if (!judgeReason.isEmpty())
                    setParam(QStringLiteral("lastError"), judgeReason);
                setResultNg(true);
                m_executionSuccess = false;
            }
        }

        // 提交一致快照：UI 一次读到 executed / moduleOk / resultNg 三项
        const QVariant ms = getParam(QStringLiteral("moduleStatus"));
        const bool moduleOk = ms.isValid() ? ms.toBool() : m_executionSuccess;
        commitResultState(m_executionSuccess && moduleOk);
        return m_executionSuccess;
    } catch (const std::exception &e) {
        // 异常 = 执行失败、结果无效：先清 NG 位再提交（commitResultState 会保留 NG 位，
        // 若节点在抛异常前置过「判 NG 但结果有效」，不清就会让已清空的输出继续按 NG 语义推送）
        setResultNg(false);
        commitResultState(false);   // 异常：本轮跑过但失败
        qWarning() << "Error executing node" << name() << ":" << e.what();
        // r19 P2：qWarning 只进日志，界面上的红节点没有原因——同时写 lastError
        if (getParam(QStringLiteral("lastError")).toString().trimmed().isEmpty())
            setParam(QStringLiteral("lastError"),
                     QStringLiteral("%1 执行异常：%2").arg(name(), QString::fromUtf8(e.what())));
        m_hasExecuted = true;
        m_executionSuccess = false;
        return false;
    } catch (...) {
        setResultNg(false);         // 同 std::exception 分支：异常时结果无效，NG 位必须清
        commitResultState(false);   // 异常：本轮跑过但失败
        qWarning() << "Unknown error executing node" << name();
        if (getParam(QStringLiteral("lastError")).toString().trimmed().isEmpty())
            setParam(QStringLiteral("lastError"),
                     QStringLiteral("%1 执行异常（未知类型）").arg(name()));
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

QVector<QPair<QString, QSharedPointer<DataObject>>> NodeBase::namedOutputs() const
{
    QVector<QPair<QString, QSharedPointer<DataObject>>> out;
    const QList<Port *> ports = outputPorts();
    for (int i = 0; i < ports.size(); ++i) {
        Port *p = ports.at(i);
        out.append(qMakePair(p ? p->name() : QStringLiteral("输出%1").arg(i), getOutputData(i)));
    }
    return out;
}

int NodeBase::moduleId() const
{
    return m_stateBody->moduleId;
}

QString NodeBase::fullName() const
{
    return QString("%1 [%2]").arg(m_stateBody->name).arg(m_stateBody->moduleId);
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

void NodeBase::setModuleId(int id)
{
    if (id <= 0 || id == m_stateBody->moduleId)
        return;
    reserveModuleId(id);   // 先抬高计数器，避免后续新建的算子被分到同一个号
    m_stateBody->moduleId = id;
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



bool NodeBase::executionAborted() const
{
    return m_runAborted;
}

void NodeBase::setRunAborted(bool on)
{
    m_runAborted = on;
}
