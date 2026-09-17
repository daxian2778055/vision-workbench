#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QDateTime>
#include <QSqlDatabase>

#include "InspectionRecord.h"   // 瘦头文件：不让 QtSql 依赖扩散到执行器侧

class QThread;

/// \u62A5\u8B66\u8BB0\u5F55
struct AlarmRecord {
    int id = 0;
    QString module;
    QString level;   // "Info", "Warning", "Error"
    QString message;
    QDateTime timestamp;
};

/// \u7528\u6237\u8BB0\u5F55
struct UserRecord {
    int id = 0;
    QString name;
    QString role;     // "Admin", "Operator", "Engineer"
    QDateTime createdAt;
};

/// \u64CD\u4F5C\u65E5\u5FD7\u8BB0\u5F55
struct OperationLogRecord {
    int id = 0;
    QString user;
    QString action;
    QString detail;
    QDateTime timestamp;
};

/// SQLite \u6570\u636E\u5E93\u5355\u4F8B\uFF0C\u7528\u4E8E\u62A5\u8B66\u3001\u68C0\u6D4B\u7ED3\u679C\u3001\u7528\u6237\u7BA1\u7406\u548C\u64CD\u4F5C\u65E5\u5FD7\u7684\u6301\u4E45\u5316
class AppDatabase : public QObject
{
    Q_OBJECT
public:
    static AppDatabase *instance();

    /// \u521D\u59CB\u5316\u6570\u636E\u5E93\uFF08\u521B\u5EFA/\u6253\u5F00\uFF09
    bool initialize(const QString &dbPath = QString());

    // ---- \u62A5\u8B66\u8BB0\u5F55 ----
    int addAlarm(const QString &module, const QString &level, const QString &message);
    QList<AlarmRecord> queryAlarms(const QDateTime &from, const QDateTime &to,
                                   int limit = 500) const;

    // ---- \u68C0\u6D4B\u7ED3\u679C ----
    bool saveInspectionResult(const QString &flowName, const QString &nodeName,
                              bool passed, const QString &value);
    /// 批量写入检测结果：整批在**同一个事务**内提交。
    /// 为什么需要它：SQLite 每次自动提交都是一次真实的写事务（WAL 下要写帧、可能触发检查点），
    /// 而连续模式下一轮里每个节点都会写一条记录（40 节点 = 40 次提交/轮），这是主要的固定开销；
    /// 同一轮的结果本就是一次逻辑写入，合并成一个事务既省开销，语义也更合理。
    /// 空列表直接返回 true（不产生任何事务）。
    bool saveInspectionResults(const QList<InspectionRecord> &records);
    QList<InspectionRecord> queryResults(const QDateTime &from, const QDateTime &to,
                                         int limit = 500) const;

    // ---- \u7528\u6237\u7BA1\u7406 ----
    bool addUser(const QString &name, const QString &password, const QString &role);
    bool authenticateUser(const QString &name, const QString &password) const;
    QList<UserRecord> queryUsers() const;
    bool removeUser(const QString &name);

    // ---- \u64CD\u4F5C\u65E5\u5FD7 ----
    void logOperation(const QString &user, const QString &action, const QString &detail);
    QList<OperationLogRecord> queryOperationLogs(const QDateTime &from, const QDateTime &to,
                                                  int limit = 500) const;

    /// \u83B7\u53D6\u6570\u636E\u5E93\u8DEF\u5F84
    QString databasePath() const { return m_dbPath; }

    /// \u6E05\u7406\u8FC7\u671F\u6570\u636E\uFF1A\u5220\u9664\u6309\u65E5\u671F\u8FC7\u4E8E\u4FDD\u7559\u5929\u6570\u7684\u62A5\u8B66/\u68C0\u6D4C\u7ED3\u679C/\u64CD\u4F5C\u65E5\u5FD7\u8BB0\u5F55
    /// \u9ED8\u8BA4\u4FDD\u7559 30 \u5929\uFF0C\u8FD4\u56DE\u88AB\u5220\u9664\u7684\u8BB0\u5F55\u603B\u6570\u3002\u53EF\u5728\u8FDE\u7EED\u8FD0\u884C\u4E0B\u5B9A\u671F\u8C03\u7528\u4EE5\u63A7\u5236\u6570\u636E\u5E93\u589E\u957F\u3002
    int purgeOldRecords(int retainDays = 30);

// ---- 密码哈希 ----
    /// 生成加盐迭代哈希（格式："<saltHex>:<hashHex>"），用于新增用户 / 默认账户 / 登录迁移
    static QString createPasswordHash(const QString &password);
    /// 校验密码：兼容"<salt>:<hash>"新格式与旧版无盐 SHA-256 格式
    static bool verifyPassword(const QString &password, const QString &stored);

private:
    AppDatabase(QObject *parent = nullptr);
    ~AppDatabase();
    AppDatabase(const AppDatabase &) = delete;
    AppDatabase &operator=(const AppDatabase &) = delete;

    bool createTables();

    /// 返回当前线程可用的数据库连接。
    /// QSqlDatabase 具有线程亲和性：一个连接只能在创建它的线程使用，
    /// 跨线程复用会报 "requested database does not belong to the calling thread"
    /// 并可能直接崩溃。检测流程在工作线程写库，因此必须按线程取连接。
    QSqlDatabase threadDatabase() const;

    QString m_dbPath;
    QSqlDatabase m_db;          ///< 初始化线程（主线程）持有的连接
    QThread *m_ownerThread = nullptr;  ///< 创建 m_db 的线程
};
