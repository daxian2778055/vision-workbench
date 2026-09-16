#include "HeartbeatManager.h"
#include "CommunicationManager.h"
#include <QDateTime>
#include "AppLog.h"
#include <QJsonArray>
#include <QJsonObject>

HeartbeatManager *HeartbeatManager::s_instance = nullptr;

HeartbeatManager *HeartbeatManager::instance()
{
    if (!s_instance) {
        s_instance = new HeartbeatManager();
    }
    return s_instance;
}

HeartbeatManager::HeartbeatManager(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    // 基定时器只是"扫描节拍"：每个条目是否该发包，由自身的 intervalMs 决定。
    // 用 100ms 节拍是为了让 sub-second 的 intervalMs 也能生效（此前固定 1s 且 intervalMs
    // 根本没被用到——配置写了多少都不影响发心跳频率）。
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, &HeartbeatManager::onTick);
}

bool HeartbeatManager::registerHeartbeat(const QString &deviceName, int intervalMs,
                                          const QString &pattern0, const QString &pattern1)
{
    if (m_entries.contains(deviceName)) {
        VFP_DEBUG << "Heartbeat already registered for:" << deviceName;
        return false;
    }

    HeartbeatEntry entry;
    entry.deviceName = deviceName;
    entry.intervalMs = qMax(500, intervalMs);
    entry.pattern0 = pattern0;
    entry.pattern1 = pattern1;
    entry.active = false;
    m_entries[deviceName] = entry;

    VFP_DEBUG << "Heartbeat registered for:" << deviceName;
    return true;
}

void HeartbeatManager::unregisterHeartbeat(const QString &deviceName)
{
    stopHeartbeat(deviceName);
    m_entries.remove(deviceName);
}

void HeartbeatManager::startAll()
{
    for (auto &entry : m_entries) {
        entry.active = true;
        entry.toggle = 0;
    }
    if (!m_timer->isActive()) {
        m_timer->start();
    }
    VFP_DEBUG << "All heartbeats started";
}

void HeartbeatManager::stopAll()
{
    for (auto &entry : m_entries) {
        entry.active = false;
    }
    m_timer->stop();
    VFP_DEBUG << "All heartbeats stopped";
}

bool HeartbeatManager::startHeartbeat(const QString &deviceName)
{
    if (!m_entries.contains(deviceName)) return false;
    m_entries[deviceName].active = true;
    m_entries[deviceName].toggle = 0;
    if (!m_timer->isActive()) {
        m_timer->start();
    }
    return true;
}

bool HeartbeatManager::stopHeartbeat(const QString &deviceName)
{
    if (!m_entries.contains(deviceName)) return false;
    m_entries[deviceName].active = false;
    // Stop timer if no active entries
    bool anyActive = false;
    for (const auto &e : m_entries) {
        if (e.active) { anyActive = true; break; }
    }
    if (!anyActive) m_timer->stop();
    return true;
}

void HeartbeatManager::onTick()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    for (auto &entry : m_entries) {
        if (!entry.active) continue;

        // 按条目自身的间隔发包（此前完全不看 intervalMs，配置写了也无效）
        if (entry.lastSentMs != 0 && now - entry.lastSentMs < entry.intervalMs)
            continue;
        entry.lastSentMs = now;

        entry.toggle = 1 - entry.toggle;
        const QString pattern = (entry.toggle == 0) ? entry.pattern0 : entry.pattern1;

        // 真正发包：此前只 emit 了一个无人接收的 heartBeatSent，
        // 设备侧永远收不到心跳——"心跳"配置了也是摆设。
        auto *cm = CommunicationManager::instance();
        if (!cm->sendData(entry.deviceName, pattern.toUtf8())) {
            qWarning() << QStringLiteral("HeartbeatManager: 心跳发送失败（设备不存在或未连接）：")
                       << entry.deviceName;
        }
        emit heartBeatSent(entry.deviceName, pattern);
    }
}

QJsonObject HeartbeatManager::toJson() const
{
    QJsonObject root;
    QJsonArray arr;
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        const HeartbeatEntry &e = it.value();
        QJsonObject obj;
        obj[QStringLiteral("deviceName")] = e.deviceName;
        obj[QStringLiteral("intervalMs")] = e.intervalMs;
        obj[QStringLiteral("pattern0")] = e.pattern0;
        obj[QStringLiteral("pattern1")] = e.pattern1;
        obj[QStringLiteral("active")] = e.active;
        arr.append(obj);
    }
    root[QStringLiteral("heartbeats")] = arr;
    return root;
}

void HeartbeatManager::fromJson(const QJsonObject &json)
{
    m_entries.clear();
    QJsonArray arr = json[QStringLiteral("heartbeats")].toArray();
    for (const auto &v : arr) {
        QJsonObject obj = v.toObject();
        HeartbeatEntry e;
        e.deviceName = obj[QStringLiteral("deviceName")].toString();
        e.intervalMs = obj[QStringLiteral("intervalMs")].toInt(1000);
        e.pattern0 = obj[QStringLiteral("pattern0")].toString(QStringLiteral("HeartBeat0"));
        e.pattern1 = obj[QStringLiteral("pattern1")].toString(QStringLiteral("HeartBeat1"));
        e.active = obj[QStringLiteral("active")].toBool();
        m_entries[e.deviceName] = e;
    }
}
