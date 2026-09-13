#pragma once

#include <QObject>
#include <QTimer>
#include <QMap>
#include <QString>

/// 心跳条目
struct HeartbeatEntry {
    QString deviceName;
    int intervalMs = 1000;
    QString pattern0 = QStringLiteral("HeartBeat0");
    QString pattern1 = QStringLiteral("HeartBeat1");
    bool active = false;
    int toggle = 0;
};

/// 心跳管理器 — 监测通信连接健康状态
class HeartbeatManager : public QObject
{
    Q_OBJECT
public:
    static HeartbeatManager *instance();

    /// 为指定设备注册心跳
    bool registerHeartbeat(const QString &deviceName, int intervalMs = 1000,
                           const QString &pattern0 = QStringLiteral("HeartBeat0"),
                           const QString &pattern1 = QStringLiteral("HeartBeat1"));

    /// 注销心跳
    void unregisterHeartbeat(const QString &deviceName);

    /// 启动/停止所有心跳
    void startAll();
    void stopAll();

    /// 启动/停止指定设备的心跳
    bool startHeartbeat(const QString &deviceName);
    bool stopHeartbeat(const QString &deviceName);

    /// 获取心跳配置列表
    QList<HeartbeatEntry> entries() const { return m_entries.values(); }

    QJsonObject toJson() const;
    void fromJson(const QJsonObject &json);

signals:
    void heartBeatSent(const QString &deviceName, const QString &data);
    void connectionLost(const QString &deviceName);
    void connectionRestored(const QString &deviceName);

private slots:
    void onTick();

private:
    HeartbeatManager(QObject *parent = nullptr);
    ~HeartbeatManager() = default;
    HeartbeatManager(const HeartbeatManager &) = delete;
    HeartbeatManager &operator=(const HeartbeatManager &) = delete;

    QTimer *m_timer = nullptr;
    QMap<QString, HeartbeatEntry> m_entries;
    static HeartbeatManager *s_instance;
};
