#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include "FeatureIndex.h"
#include <QDateTime>
#include <QSqlDatabase>

#include "InspectionRecord.h"   // 瘦头文件：不让 QtSql 依赖扩散到执行器侧

class QThread;
class QSqlQuery;

// 读侧判定的返回类型：把「今天做不出这条判定」保留成第三种可能。
// 为什么不能是 bool：false 同时说得出「出厂口令已改掉」与「用户表读不到」，
// 而这两个方向一个该放开写操作闸、一个该把它钉死。空表同理，既能说「还没有账户」
// 又能说「读不到账户」，后一次判定若走首装分支就是凭空建一个 Admin 并当场登录进去。
enum class AdminPasswordState { InUse, Retired, Unreadable };
enum class AccountSurface { HasAccounts, NoneYet, Unreadable };

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
    /// 校验口令。readFailed 非空时告知道调用方「这次判定没能做出来」：
    /// 库读不到与「用户名或口令错」是两件事，前者不得显示成后者——原实现只有 false 一种结果，
    /// 于是一次 database is locked 报成「密码错误」，还被当作失败登录写进操作日志。
    bool authenticateUser(const QString &name, const QString &password,
                          bool *readFailed = nullptr) const;
    QList<UserRecord> queryUsers() const;

    /// 特征库（2026-10-27）：图库索引的持久化出入口，与其它表同一把锁/同一套线程连接。
    bool saveFeatureSample(const FeatureSample &sample);
    QList<FeatureSample> loadFeatureSamples(const QString &detector) const;
    /// 不带图像 BLOB 的元数据查询（检索/按名查找用，2026-10-27 P3-4）
    QList<FeatureSample> loadFeatureSamplesMeta(const QString &detector) const;
    /// 按名取单行（含图像）：参照匹配用（2026-10-27 P3-4）
    FeatureSample loadFeatureSampleByName(const QString &name) const;
    bool deleteFeatureSample(int id);
    bool clearFeatureSamples();

    /// 账户面三态：有账户 / 确实一个都没有 / 今天读不出来。登录页据此决定走
    /// 「首次使用初始化管理员」还是走认证。为什么不能就地用 queryUsers().isEmpty()：
    /// 读失败交回来的同样是空表，而那条分支的动作是创建一个 Admin 并直接给它 Admin 会话。
    AccountSurface accountSurface() const;
    bool removeUser(const QString &name);

    /// 覆盖指定用户的口令（加盐迭代哈希后写回）。
    /// 返回 false：数据库写入失败**或该用户不存在**（影响 0 行）——调用方必须把失败显示出来。
    bool changeUserPassword(const QString &name, const QString &newPassword);

    /// 出厂默认口令判定：InUse = admin 账户的哈希仍能匹配口令 "admin"；
    /// Retired = 已改掉、或读通了而根本没有 admin 账户；Unreadable = 这条判定今天做不出来。
    /// 口令字面量只出现在这一处，避免各处调用 authenticateUser("admin","admin")。
    /// 为什么原来是 bool：false 既说「已改掉」也说「读不到」，而调用方拿到 false 就把写操作闸放开
    /// ——这道闸存在的唯一目的就是拦住出厂口令，被一次读失败解开等于没有这道闸。
    AdminPasswordState adminPasswordState() const;

    // ---- \u64CD\u4F5C\u65E5\u5FD7 ----
    void logOperation(const QString &user, const QString &action, const QString &detail);
    QList<OperationLogRecord> queryOperationLogs(const QDateTime &from, const QDateTime &to,
                                                  int limit = 500) const;

    /// \u83B7\u53D6\u6570\u636E\u5E93\u8DEF\u5F84
    QString databasePath() const { return m_dbPath; }

    /// 最近一次数据库失败的诊断文本（初始化 / 建表 / 写入与读取失败时写入；成功路径不清空，
    /// 因为「上一轮写失败」正是下一轮排查要看的现场信息）。
    /// 为什么需要它：写侧原先只看 exec() 的返回值，prepare() 的返回值一处都没查。
    /// 本轮撤闸自证实测到的形态：把 prepare 的检查撤掉后，同一条批量写在 exec() 报出的是
    /// driver=Parameter count mismatch（一条没准备好的语句有 0 个占位符，却配了 4 个绑定值），
    /// 真因（no such table / 连接没打开）整个被这句冒名顶替——现场只能对着它猜。
    /// 本串把「操作名 + 阶段 + 连接名 + 是否已打开 + 库路径 + 驱动原文」拼在一起，
    /// 调用方拿到 false 之后必须把它显示出来（判红原因、状态栏、日志），不能只当布尔用。
    /// 口径：串里记的是**主失败**（调用方据以返回 false 的那一步）；回滚/收尾这类二次动作
    /// 的失败只进调试日志，不覆盖它，否则现场看到的原因是副作用而不是丢数据的那一步。
    /// 可依赖的契约：写库函数凡返回 false、读侧凡 prepare 或 exec 失败，本串都已先写好
    /// ——调用方直接显示即可，不必判空。
    /// 读族的 exec 那一半此前是断的：六处里只有 queryUsers 接了 exec 留痕，另五处仍是
    /// `if (!q.exec()) return ...;`，于是「空表」与「上一笔写失败留下的旧原因」能一起到界面。
    QString lastDatabaseError() const;

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

    /// 建表。**调用方必须已持有 s_dbMutex**：本函数自己不取锁，今天唯一的调用方是
    /// initialize()（它持锁）。函数体内有 7 处 noteFailure 与 1 处 prepareWrite，
    /// 全都按这个前提写。这条前提原来只写在 noteFailure / prepareWrite 的注释上，
    /// 这两个中间节点自己是空档——下一个从无锁路径调它们的人不会收到任何信号。
    bool createTables();

    /// 返回当前线程可用的数据库连接。
    /// QSqlDatabase 具有线程亲和性：一个连接只能在创建它的线程使用，
    /// 跨线程复用会报 "requested database does not belong to the calling thread"
    /// 并可能直接崩溃。检测流程在工作线程写库，因此必须按线程取连接。
    /// **调用方必须已持有 s_dbMutex**：本函数自己不取锁，体内 2 处 noteFailure 按这个前提写。
    /// 今天的不变式靠调用方纪律维持：initialize() 持锁、11 个读/写方法各自持锁。
    /// 与 createTables() 同一条：这两个中间节点的契约原来没人写。
    QSqlDatabase threadDatabase() const;

    /// 拼一条失败诊断：操作名 / 阶段 / 连接名 / 是否已打开 / 库路径 / 驱动原文。
    /// const 且写 mutable 成员：写侧既有 const 方法（authenticateUser 的旧版哈希迁移）。
    QString failureText(const QString &op, const QString &stage,
                        const QSqlDatabase &db, const QString &driverReason) const;

    /// 记录一次数据库失败到 m_lastDatabaseError，并同步写调试日志。
    /// **调用方必须已持有 s_dbMutex**（本函数不加锁：QMutex 不可重入，加锁即自锁）。
    /// stage 默认 "exec"（多数站点就是执行失败）；open / transaction / commit / rollback 另传。
    void noteFailure(const QString &op, const QSqlDatabase &db,
                     const QString &driverReason,
                     const QString &stage = QStringLiteral("exec")) const;

    /// 带失败记录的 prepare()。返回值必须查：prepare 失败一旦放过，后面的 exec()
    /// 会拿 0 个占位符去配 N 个绑定值，报出的是与真因无关的 "Parameter count mismatch"。
    /// **调用方必须已持有 s_dbMutex。**
    bool prepareWrite(QSqlQuery &q, const QSqlDatabase &db,
                      const QString &sql, const QString &op) const;

    /// 读侧同一个 prepare 判据，另起一个名字只为让调用点读得懂。读失败不改返回值
    /// （空表 / 登录失败），但必须留痕：lastDatabaseError() 的头注释明写"成功路径不清空"，
    /// 所以读侧一旦无声，queryResults 返回空表时调用方去问 lastDatabaseError()，
    /// 读到的是上一笔写失败留下的旧串——把上一轮的错当成这次的错显示给现场。
    /// 实现就是 prepareWrite：判据只有一份，不会出现"修了一处忘了另一处"。
    /// **调用方必须已持有 s_dbMutex。**
    bool prepareRead(QSqlQuery &q, const QSqlDatabase &db,
                     const QString &sql, const QString &op) const;

    /// 读侧同一个 exec 判据：失败先留痕再交回 false，返回值语义不变（空表 / 登录失败）。
    /// 抽成一处的理由与 prepareRead 同一条：判据只有一份，不会出现「修了一处忘了另一处」。
    /// **调用方必须已持有 s_dbMutex。**
    bool execRead(QSqlQuery &q, const QSqlDatabase &db, const QString &op) const;

    mutable QString m_lastDatabaseError;   ///< 最近一次失败诊断（由 s_dbMutex 保护的调用方写）
    QString m_dbPath;
    QSqlDatabase m_db;          ///< 初始化线程（主线程）持有的连接
    QThread *m_ownerThread = nullptr;  ///< 创建 m_db 的线程
};
