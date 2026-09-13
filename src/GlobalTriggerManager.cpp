#include "GlobalTriggerManager.h"
#include "FlowScene.h"
#include "FlowExecutor.h"
#include "CommunicationManager.h"
#include "ReceiveEvent.h"
#include "AppLog.h"
#include <QJsonDocument>
#include <QFile>
#include <QMutexLocker>

GlobalTriggerManager *GlobalTriggerManager::instance()
{
    // 静态局部变量初始化在 C++11 后线程安全，且指针常驻避免析构顺序问题
    static GlobalTriggerManager *inst = new GlobalTriggerManager();
    return inst;
}

GlobalTriggerManager::GlobalTriggerManager(QObject *parent)
    : QObject(parent)
{
}

GlobalTriggerManager::~GlobalTriggerManager()
{
}

// ---- String Triggers ----

bool GlobalTriggerManager::setStringTrigger(const QString &triggerChar, const QString &flowName)
{
    if (triggerChar.isEmpty() || flowName.isEmpty()) return false;

    QMutexLocker locker(&m_mutex);

    TriggerEntry entry;
    entry.id = QStringLiteral("str_") + QString::number(m_nextId++);
    entry.type = STRING_TRIGGER;
    entry.triggerSource = triggerChar;
    entry.flowName = flowName;
    entry.enabled = true;

    // Remove previous trigger for same char
    if (m_stringTriggers.contains(triggerChar)) {
        QString oldId = m_stringTriggers[triggerChar].id;
        m_allTriggers.remove(oldId);
    }

    m_stringTriggers[triggerChar] = entry;
    m_allTriggers[entry.id] = entry;
    emit triggerAdded(entry.id);
    return true;
}

bool GlobalTriggerManager::removeStringTrigger(const QString &triggerChar)
{
    if (triggerChar.isEmpty()) return false;

    QMutexLocker locker(&m_mutex);

    if (!m_stringTriggers.contains(triggerChar)) return false;
    QString id = m_stringTriggers[triggerChar].id;
    m_allTriggers.remove(id);
    m_stringTriggers.remove(triggerChar);
    emit triggerRemoved(id);
    return true;
}

QString GlobalTriggerManager::flowForStringTrigger(const QString &triggerChar) const
{
    QMutexLocker locker(&m_mutex);
    return m_stringTriggers.value(triggerChar).flowName;
}

// ---- Event Triggers ----

bool GlobalTriggerManager::setEventTrigger(const QString &eventId, const QString &flowName)
{
    if (eventId.isEmpty() || flowName.isEmpty()) return false;

    QMutexLocker locker(&m_mutex);

    TriggerEntry entry;
    entry.id = QStringLiteral("evt_") + QString::number(m_nextId++);
    entry.type = EVENT_TRIGGER;
    entry.triggerSource = eventId;
    entry.flowName = flowName;
    entry.enabled = true;

    if (m_eventTriggers.contains(eventId)) {
        QString oldId = m_eventTriggers[eventId].id;
        m_allTriggers.remove(oldId);
    }

    m_eventTriggers[eventId] = entry;
    m_allTriggers[entry.id] = entry;
    emit triggerAdded(entry.id);
    return true;
}

bool GlobalTriggerManager::removeEventTrigger(const QString &eventId)
{
    if (eventId.isEmpty()) return false;

    QMutexLocker locker(&m_mutex);

    if (!m_eventTriggers.contains(eventId)) return false;
    QString id = m_eventTriggers[eventId].id;
    m_allTriggers.remove(id);
    m_eventTriggers.remove(eventId);
    emit triggerRemoved(id);
    return true;
}

QString GlobalTriggerManager::flowForEventTrigger(const QString &eventId) const
{
    QMutexLocker locker(&m_mutex);
    return m_eventTriggers.value(eventId).flowName;
}

// ---- Trigger Execution ----

void GlobalTriggerManager::onDataReceived(const QString &deviceName, const QByteArray &data)
{
    Q_UNUSED(deviceName)

    QString text = QString::fromUtf8(data).trimmed();

    // 1. 字符串触发：锁内定位首个匹配并拷贝必要信息、累加计数，锁外再执行流程，避免长任务持锁
    bool matched = false;
    QString flowName;
    QString matchedSource;
    FlowExecutor *executor = nullptr;

    {
        QMutexLocker locker(&m_mutex);
        for (auto it = m_stringTriggers.constBegin(); it != m_stringTriggers.constEnd(); ++it) {
            const TriggerEntry &entry = it.value();
            if (!entry.enabled) continue;

            if (text == entry.triggerSource || text.startsWith(entry.triggerSource)) {
                matched = true;
                flowName = entry.flowName;
                matchedSource = entry.triggerSource;
                executor = executorForFlowLocked(flowName);
                if (executor) {
                    m_allTriggers[entry.id].triggerCount++;
                    m_stringTriggers[it.key()].triggerCount++;
                }
                break; // First match wins
            }
        }
    }

    if (matched) {
        VFP_DEBUG << "String trigger matched:" << matchedSource << "→ flow:" << flowName;
        emit triggerFired(flowName, matchedSource);

        // 硬触发模式下禁止通讯/字符串触发
        if (executor && !executor->canTriggerFromExternal()) {
            VFP_DEBUG << "Flow is in hardware-trigger mode, external trigger ignored:" << flowName;
        } else if (executor) {
            executor->startExecution();
        }
        return;
    }

    // 2. Route to receive events for further parsing
    CommunicationManager *cm = CommunicationManager::instance();
    for (const QString &evId : cm->receiveEventIds()) {
        ReceiveEvent *ev = cm->receiveEvent(evId);
        if (!ev || !ev->enabled()) continue;

        QList<QVariant> fields;
        if (ev->parse(data, fields)) {
            // Will be handled via onEventTriggered
        }
    }
}

void GlobalTriggerManager::onEventTriggered(const QString &eventId, const QList<QVariant> &fields)
{
    Q_UNUSED(fields)

    // 锁内定位匹配的事件触发并累加计数，锁外执行流程
    bool matched = false;
    QString flowName;
    QString matchedEventId;
    FlowExecutor *executor = nullptr;

    {
        QMutexLocker locker(&m_mutex);
        for (auto it = m_eventTriggers.constBegin(); it != m_eventTriggers.constEnd(); ++it) {
            const TriggerEntry &entry = it.value();
            if (!entry.enabled) continue;

            if (entry.triggerSource == eventId) {
                matched = true;
                flowName = entry.flowName;
                matchedEventId = eventId;
                executor = executorForFlowLocked(flowName);
                if (executor) {
                    m_allTriggers[entry.id].triggerCount++;
                    m_eventTriggers[it.key()].triggerCount++;
                }
                break; // First match wins
            }
        }
    }

    if (matched) {
        VFP_DEBUG << "Event trigger matched:" << matchedEventId << "→ flow:" << flowName;
        emit triggerFired(flowName, matchedEventId);

        // 硬触发模式下禁止接收事件触发
        if (executor && !executor->canTriggerFromExternal()) {
            VFP_DEBUG << "Flow is in hardware-trigger mode, event trigger ignored:" << flowName;
        } else if (executor) {
            executor->startExecution();
        }
    }
}

// ---- Query ----

QList<GlobalTriggerManager::TriggerEntry> GlobalTriggerManager::allTriggers() const
{
    QMutexLocker locker(&m_mutex);
    return m_allTriggers.values();
}

GlobalTriggerManager::TriggerEntry GlobalTriggerManager::triggerEntry(const QString &id) const
{
    QMutexLocker locker(&m_mutex);
    return m_allTriggers.value(id);
}

// ---- Flow Registration ----

void GlobalTriggerManager::registerFlow(const QString &name, FlowScene *scene, FlowExecutor *executor)
{
    QMutexLocker locker(&m_mutex);

    FlowBinding fb;
    fb.scene = scene;
    fb.executor = executor;
    m_flowBindings[name] = fb;
}

void GlobalTriggerManager::unregisterFlow(const QString &name)
{
    QMutexLocker locker(&m_mutex);
    m_flowBindings.remove(name);
}

FlowExecutor *GlobalTriggerManager::executorForFlow(const QString &name) const
{
    QMutexLocker locker(&m_mutex);
    return m_flowBindings.value(name).executor;
}

FlowExecutor *GlobalTriggerManager::executorForFlowLocked(const QString &name) const
{
    return m_flowBindings.value(name).executor;
}

// ---- Serialization ----

QJsonObject GlobalTriggerManager::toJson() const
{
    QMutexLocker locker(&m_mutex);

    QJsonObject root;
    QJsonArray arr;

    for (auto it = m_allTriggers.constBegin(); it != m_allTriggers.constEnd(); ++it) {
        const TriggerEntry &e = it.value();
        QJsonObject obj;
        obj[QStringLiteral("id")] = e.id;
        obj[QStringLiteral("type")] = static_cast<int>(e.type);
        obj[QStringLiteral("triggerSource")] = e.triggerSource;
        obj[QStringLiteral("flowName")] = e.flowName;
        obj[QStringLiteral("enabled")] = e.enabled;
        obj[QStringLiteral("triggerCount")] = e.triggerCount;
        arr.append(obj);
    }
    root[QStringLiteral("triggers")] = arr;
    return root;
}

void GlobalTriggerManager::fromJson(const QJsonObject &json)
{
    QMutexLocker locker(&m_mutex);

    m_allTriggers.clear();
    m_stringTriggers.clear();
    m_eventTriggers.clear();

    QJsonArray arr = json[QStringLiteral("triggers")].toArray();
    for (const auto &v : arr) {
        QJsonObject obj = v.toObject();
        TriggerEntry e;
        e.id = obj[QStringLiteral("id")].toString();
        e.type = static_cast<TriggerType>(obj[QStringLiteral("type")].toInt());
        e.triggerSource = obj[QStringLiteral("triggerSource")].toString();
        e.flowName = obj[QStringLiteral("flowName")].toString();
        e.enabled = obj[QStringLiteral("enabled")].toBool(true);
        e.triggerCount = obj[QStringLiteral("triggerCount")].toInt();

        if (e.id.isEmpty()) {
            e.id = QStringLiteral("trg_") + QString::number(m_nextId++);
        } else {
            // Extract numeric ID for nextId tracking
            int num = e.id.mid(4).toInt();
            if (num >= m_nextId) m_nextId = num + 1;
        }

        m_allTriggers[e.id] = e;
        if (e.type == STRING_TRIGGER) {
            m_stringTriggers[e.triggerSource] = e;
        } else {
            m_eventTriggers[e.triggerSource] = e;
        }
    }
}
