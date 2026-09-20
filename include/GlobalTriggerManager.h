#pragma once

#include <QObject>
#include <QMap>
#include <QString>
#include <QList>
#include <QVariant>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutex>

class FlowScene;
class FlowExecutor;

/// 全局触发管理器 — 将外部触发信号映射到流程执行
/// 注意：onDataReceived / onEventTriggered 由通信线程同步调用，而 set*/remove*/fromJson
/// 由界面线程调用，二者共享的多张表须加锁保护。
class GlobalTriggerManager : public QObject
{
    Q_OBJECT
public:
    static GlobalTriggerManager *instance();

    /// 触发类型
    enum TriggerType {
        STRING_TRIGGER,   /// 字符串触发：匹配接收到的字符串
        EVENT_TRIGGER     /// 事件触发：匹配接收事件ID
    };

    struct TriggerEntry {
        QString id;
        TriggerType type;
        QString triggerSource; /// 触发源：触发字符 / 事件ID
        QString flowName;      /// 目标流程名
        bool enabled = true;
        int triggerCount = 0;
    };

    // ---- 字符串触发 ----
    bool setStringTrigger(const QString &triggerChar, const QString &flowName);
    bool removeStringTrigger(const QString &triggerChar);
    QString flowForStringTrigger(const QString &triggerChar) const;

    // ---- 事件触发 ----
    bool setEventTrigger(const QString &eventId, const QString &flowName);
    bool removeEventTrigger(const QString &eventId);
    QString flowForEventTrigger(const QString &eventId) const;

    // ---- 触发执行 ----
    /// 通信数据到达时调用：依次尝试匹配字符串触发和接收事件触发
    void onDataReceived(const QString &deviceName, const QByteArray &data);

    /// 接收事件触发（由 ReceiveEvent 的系统调用）
    void onEventTriggered(const QString &eventId, const QList<QVariant> &fields);

    // ---- 查询 ----
    QList<TriggerEntry> allTriggers() const;
    TriggerEntry triggerEntry(const QString &id) const;

    // ---- 流程注册 ----
    void registerFlow(const QString &name, FlowScene *scene, FlowExecutor *executor);
    void unregisterFlow(const QString &name);
    /// 分配一个全局唯一的"流程 N"名称（扫描当前已注册绑定，返回首个空闲序号）。
    /// 用于新建流程命名，避免删除流程后 size() 复用导致同名覆盖仍存流程的触发路由（L2 存量）。
    QString allocFlowName() const;
    /// 按执行器身份注销：仅当当前绑定确实指向该执行器时才移除，避免迟到析构（deleteLater /
    /// 复用）误删新方案同名流程的注册（"载入后触发静默失效"复活路径）。
    void unregisterExecutor(FlowExecutor *executor);
    FlowExecutor *executorForFlow(const QString &name) const;

    // ---- 序列化 ----
    QJsonObject toJson() const;
    void fromJson(const QJsonObject &json);

signals:
    void triggerFired(const QString &flowName, const QString &triggerSource);
    void triggerAdded(const QString &id);
    void triggerRemoved(const QString &id);

private:
    GlobalTriggerManager(QObject *parent = nullptr);
    ~GlobalTriggerManager() override;
    GlobalTriggerManager(const GlobalTriggerManager &) = delete;
    GlobalTriggerManager &operator=(const GlobalTriggerManager &) = delete;

    /// 调用者必须已持有 m_mutex（供 onDataReceived/onEventTriggered 在持锁并发起查找时复用，避免嵌套加锁）
    FlowExecutor *executorForFlowLocked(const QString &name) const;

    int m_nextId = 1;
    QMap<QString, TriggerEntry> m_stringTriggers;
    QMap<QString, TriggerEntry> m_eventTriggers;
    QMap<QString, TriggerEntry> m_allTriggers;

    // 流程名 → {场景, 执行器}
    struct FlowBinding {
        FlowScene *scene = nullptr;
        FlowExecutor *executor = nullptr;
    };
    QMap<QString, FlowBinding> m_flowBindings;

    /// 保护 m_nextId 及以上所有表的并发读写
    mutable QMutex m_mutex;
};