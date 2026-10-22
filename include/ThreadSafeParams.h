#pragma once

#include <QMap>
#include <QReadWriteLock>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <memory>

/// 线程安全参数表：接口兼容 QMap<QString, QVariant> 的常用子集。
///
/// 背景：参数表同时被界面线程（参数面板/配置对话框/看板）与执行线程（节点 run/process）
/// 访问；QMap 并发读写会破坏内部结构（随机崩溃、或把旧值喂进算子——工业平台最坏故障）。
/// 本类型把所有读写统一到 QReadWriteLock 下：
///   - `params[key] = value`          → 加锁写入（WriteProxy 代理赋值）
///   - `params.value/contains/keys/isEmpty/size/count` → 加锁读取
///   - `params.insert/remove/clear`   → 加锁写入
///   - `params.snapshot()`            → 取整体快照用于遍历/序列化（遍历期间持锁不可行）
///
/// 注意：不要持有 value() 返回的 QVariant 引用；跨线程一致性由本类型保证，
/// 但不提供"多键原子读"——需要原子地读多键时先 snapshot()。
///
/// 共享化（切片 C 立项书 P2 期段一）：参数本体（映射＋读写锁）收进内核 Core（纯数据、
/// 非 QObject），节点壳持 shared_ptr，快照捕获时再入快照列一份——壳先亡（场景销毁）
/// 时参数表仍活着，执行线程经快照列可读（立项书 §13.1 净收益）。拷贝与赋值仍禁用，
/// 因此不存在内核被意外共享成两份口径的风险。
class ThreadSafeParams
{
public:
    /// 参数内核（映射＋读写锁）。公开仅为让快照列持完整类型；内核指针只该被持有，
    /// 读写一律走下方加锁方法，绕锁直摸映射不在契约之内。
    struct Core
    {
        mutable QReadWriteLock lock;
        QMap<QString, QVariant> map;
    };

    ThreadSafeParams() : m_core(std::make_shared<Core>()) {}
    ThreadSafeParams(const ThreadSafeParams &) = delete;
    ThreadSafeParams &operator=(const ThreadSafeParams &) = delete;

    /// 参数内核共享句柄（立项书 §13.2 补充行：增（拷贝）可在图锁内＝纯原子加；
    /// 减（可能真析构内核）不得在任何其他锁的临界区内）。仅供快照列持有。
    const std::shared_ptr<Core> &core() const { return m_core; }

    /// `params[key] = value` 的写入代理（不能安全返回内部引用，故用代理对象）
    class WriteProxy
    {
    public:
        WriteProxy(ThreadSafeParams *owner, QString key)
            : m_owner(owner), m_key(std::move(key)) {}

        WriteProxy &operator=(const QVariant &value)
        {
            m_owner->insert(m_key, value);
            return *this;
        }
        /// 读取路径：`params[key].toInt()` 之类（返回加锁拷贝，不插入缺失键）
        operator QVariant() const { return m_owner->value(m_key); }
        // QVariant 常用方法的直通（C++ 不会对成员访问做用户定义转换，
        // 所以 `params[k].toString()` 必须显式转发）
        QString toString() const { return m_owner->value(m_key).toString(); }
        int toInt() const { return m_owner->value(m_key).toInt(); }
        double toDouble() const { return m_owner->value(m_key).toDouble(); }
        bool toBool() const { return m_owner->value(m_key).toBool(); }
        QByteArray toByteArray() const { return m_owner->value(m_key).toByteArray(); }
        bool isValid() const { return m_owner->value(m_key).isValid(); }
        bool isNull() const { return m_owner->value(m_key).isNull(); }

    private:
        ThreadSafeParams *m_owner;
        QString m_key;
    };

    // ---- 读取（读锁）----
    QVariant value(const QString &key) const
    {
        QReadLocker locker(&m_core->lock);
        return m_core->map.value(key);
    }
    QVariant value(const QString &key, const QVariant &def) const
    {
        QReadLocker locker(&m_core->lock);
        return m_core->map.value(key, def);
    }
    bool contains(const QString &key) const
    {
        QReadLocker locker(&m_core->lock);
        return m_core->map.contains(key);
    }
    bool isEmpty() const
    {
        QReadLocker locker(&m_core->lock);
        return m_core->map.isEmpty();
    }
    int size() const
    {
        QReadLocker locker(&m_core->lock);
        return m_core->map.size();
    }
    int count() const { return size(); }
    QStringList keys() const
    {
        QReadLocker locker(&m_core->lock);
        return m_core->map.keys();
    }
    QList<QVariant> values() const
    {
        QReadLocker locker(&m_core->lock);
        return m_core->map.values();
    }
    /// 整体快照（遍历/序列化用；请遍历快照而不是遍历本对象）
    QMap<QString, QVariant> snapshot() const
    {
        QReadLocker locker(&m_core->lock);
        return m_core->map;
    }

    // ---- 写入（写锁）----
    void insert(const QString &key, const QVariant &value)
    {
        QWriteLocker locker(&m_core->lock);
        m_core->map.insert(key, value);
    }
    void remove(const QString &key)
    {
        QWriteLocker locker(&m_core->lock);
        m_core->map.remove(key);
    }
    void clear()
    {
        QWriteLocker locker(&m_core->lock);
        m_core->map.clear();
    }

    // ---- 下标：写用代理，读返回加锁拷贝 ----
    WriteProxy operator[](const QString &key) { return WriteProxy(this, key); }
    QVariant operator[](const QString &key) const { return value(key); }

private:
    std::shared_ptr<Core> m_core;
};
