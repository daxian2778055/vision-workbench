#include "AppDatabase.h"
#include "AppLog.h"
#include "SessionManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QCoreApplication>
#include <QDir>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QMutexLocker>
#include <QStringList>
#include <QThread>

static QMutex s_dbMutex;

namespace {
// 密码拉伸迭代次数：折中安全性与登录耗时（约 10~30ms）
constexpr int kHashIterations = 12000;
// 每个账户独立盐的字节数
constexpr int kSaltBytes = 16;

// 加盐迭代哈希（密码拉伸），抵抗暴力破解与彩虹表
QByteArray stretchHash(const QByteArray &salt, const QString &password)
{
    QByteArray digest = QCryptographicHash::hash(salt + password.toUtf8(), QCryptographicHash::Sha256);
    for (int i = 1; i < kHashIterations; ++i) {
        digest = QCryptographicHash::hash(digest + password.toUtf8(), QCryptographicHash::Sha256);
    }
    return digest;
}
}

AppDatabase *AppDatabase::instance()
{
    static AppDatabase *inst = new AppDatabase();
    return inst;
}

AppDatabase::AppDatabase(QObject *parent)
    : QObject(parent)
{
}

AppDatabase::~AppDatabase()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
}

QString AppDatabase::createPasswordHash(const QString &password)
{
    // 每个账户使用随机盐，保证相同密码哈希不同
    QByteArray salt(kSaltBytes, '\0');
    for (int i = 0; i < salt.size(); ++i) {
        salt[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    return QString::fromLatin1(salt.toHex() + ':' + stretchHash(salt, password).toHex());
}

bool AppDatabase::verifyPassword(const QString &password, const QString &stored)
{
    const int colon = stored.indexOf(QLatin1Char(':'));
    if (colon < 0) {
        // 兼容旧版无盐 SHA-256 格式（登录成功后由调用方迁移）
        const QByteArray legacy = QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256);
        return QString::fromLatin1(legacy.toHex()) == stored;
    }
    const QByteArray salt = QByteArray::fromHex(stored.left(colon).toLatin1());
    return QString::fromLatin1(stretchHash(salt, password).toHex()) == stored.mid(colon + 1);
}

bool AppDatabase::initialize(const QString &dbPath)
{
    QMutexLocker locker(&s_dbMutex);

    if (!dbPath.isEmpty()) {
        m_dbPath = dbPath;
    } else {
        // Default: <app>/data/visionflow.db
        QString appDir = QCoreApplication::applicationDirPath();
        QDir dir(appDir + QStringLiteral("/data"));
        if (!dir.exists()) {
            dir.mkpath(QStringLiteral("."));
        }
        m_dbPath = dir.absoluteFilePath(QStringLiteral("visionflow.db"));
    }

    if (QSqlDatabase::contains(QStringLiteral("visionflow_conn"))) {
        m_db = QSqlDatabase::database(QStringLiteral("visionflow_conn"));
    } else {
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("visionflow_conn"));
    }
    m_db.setDatabaseName(m_dbPath);
    m_ownerThread = QThread::currentThread();   // 记录主连接归属的线程

    if (!m_db.open()) {
        VFP_DEBUG << "Failed to open database:" << m_db.lastError().text();
        return false;
    }

    // Enable WAL mode for better concurrent performance
    QSqlQuery pragma(m_db);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    pragma.exec(QStringLiteral("PRAGMA busy_timeout=5000"));   // 多连接并发时避免立即返回 SQLITE_BUSY

    if (!createTables()) {
        VFP_DEBUG << "Failed to create database tables";
        return false;
    }

    VFP_DEBUG << "AppDatabase initialized at:" << m_dbPath;
    return true;
}

QSqlDatabase AppDatabase::threadDatabase() const
{
    // 初始化线程（主线程）直接复用已打开的 m_db，避免多开一条无用连接
    if (m_ownerThread && m_ownerThread == QThread::currentThread() && m_db.isValid())
        return m_db;

    // 其余线程（如 FlowExecutor 工作线程）各自持有一条同名规则的独立连接
    const QString name = QStringLiteral("visionflow_conn_%1")
                             .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    if (QSqlDatabase::contains(name))
        return QSqlDatabase::database(name);

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(m_dbPath);
    if (!db.open()) {
        VFP_DEBUG << "Failed to open per-thread database connection" << name
                  << ":" << db.lastError().text();
        return db;
    }
    QSqlQuery pragma(db);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    pragma.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
    return db;
}

bool AppDatabase::createTables()
{
    const QSqlDatabase db = threadDatabase();
    QSqlQuery q(db);

    const QString createAlarms = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS alarms ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  module TEXT NOT NULL,"
        "  level TEXT NOT NULL,"
        "  message TEXT NOT NULL,"
        "  timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
        ")"
    );
    if (!q.exec(createAlarms)) {
        VFP_DEBUG << "Failed to create alarms table:" << q.lastError().text();
        return false;
    }

    const QString createResults = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS inspection_results ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  flow_name TEXT NOT NULL,"
        "  node_name TEXT NOT NULL,"
        "  passed INTEGER NOT NULL,"
        "  value TEXT,"
        "  timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
        ")"
    );
    if (!q.exec(createResults)) {
        VFP_DEBUG << "Failed to create inspection_results table:" << q.lastError().text();
        return false;
    }

    const QString createUsers = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS users ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT UNIQUE NOT NULL,"
        "  password_hash TEXT NOT NULL,"
        "  role TEXT NOT NULL DEFAULT 'Operator',"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ")"
    );
    if (!q.exec(createUsers)) {
        VFP_DEBUG << "Failed to create users table:" << q.lastError().text();
        return false;
    }

    const QString createLogs = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS operation_logs ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  user TEXT NOT NULL,"
        "  action TEXT NOT NULL,"
        "  detail TEXT,"
        "  timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
        ")"
    );
    if (!q.exec(createLogs)) {
        VFP_DEBUG << "Failed to create operation_logs table:" << q.lastError().text();
        return false;
    }

    // 高频查询/清理索引（时间倒序查询）
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_alarms_ts ON alarms(timestamp)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_results_ts ON inspection_results(timestamp)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_operlogs_ts ON operation_logs(timestamp)"));

    // Insert default admin user if none exists
    QSqlQuery countQ(db);
    countQ.exec(QStringLiteral("SELECT COUNT(*) FROM users"));
    if (countQ.next() && countQ.value(0).toInt() == 0) {
        QSqlQuery ins(db);
        ins.prepare(QStringLiteral("INSERT INTO users (name, password_hash, role) VALUES (?, ?, ?)"));
        ins.addBindValue(QStringLiteral("admin"));
        ins.addBindValue(createPasswordHash(QStringLiteral("admin")));
        ins.addBindValue(QStringLiteral("Admin"));
        if (!ins.exec()) {
            VFP_DEBUG << "Failed to insert default admin user:" << ins.lastError().text();
        }
    }

    return true;
}

// ---- Alarm operations ----

int AppDatabase::addAlarm(const QString &module, const QString &level, const QString &message)
{
    QMutexLocker locker(&s_dbMutex);
    const QSqlDatabase db = threadDatabase();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO alarms (module, level, message) VALUES (?, ?, ?)"));
    q.addBindValue(module);
    q.addBindValue(level);
    q.addBindValue(message);
    if (!q.exec()) {
        VFP_DEBUG << "Failed to add alarm:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toInt();
}

QList<AlarmRecord> AppDatabase::queryAlarms(const QDateTime &from, const QDateTime &to, int limit) const
{
    QMutexLocker locker(&s_dbMutex);
    QList<AlarmRecord> records;
    const QSqlDatabase db = threadDatabase();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, module, level, message, timestamp FROM alarms "
        "WHERE timestamp BETWEEN ? AND ? ORDER BY timestamp DESC LIMIT ?"
    ));
    q.addBindValue(from);
    q.addBindValue(to);
    q.addBindValue(limit);
    if (!q.exec()) return records;

    while (q.next()) {
        AlarmRecord r;
        r.id = q.value(0).toInt();
        r.module = q.value(1).toString();
        r.level = q.value(2).toString();
        r.message = q.value(3).toString();
        r.timestamp = q.value(4).toDateTime();
        records.append(r);
    }
    return records;
}

// ---- Inspection results ----

bool AppDatabase::saveInspectionResults(const QList<InspectionRecord> &records)
{
    if (records.isEmpty()) {
        return true;   // 空批不产生事务
    }
    QMutexLocker locker(&s_dbMutex);
    // 注意：不能用 const —— QSqlDatabase::transaction/commit/rollback 都是非 const 成员
    QSqlDatabase db = threadDatabase();
    if (!db.transaction()) {
        VFP_DEBUG << "Failed to begin transaction for inspection results:"
                  << db.lastError().text();
        return false;
    }
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO inspection_results (flow_name, node_name, passed, value) VALUES (?, ?, ?, ?)"
    ));
    bool ok = true;
    for (const InspectionRecord &r : records) {
        q.addBindValue(r.flowName);
        q.addBindValue(r.nodeName);
        q.addBindValue(r.passed ? 1 : 0);
        q.addBindValue(r.value);
        if (!q.exec()) {
            VFP_DEBUG << "Failed to save inspection result (batch):" << q.lastError().text();
            ok = false;
            break;
        }
    }
    if (!ok) {
        db.rollback();   // 整批原子：任一条失败则全部回滚，避免"半轮结果"
        return false;
    }
    if (!db.commit()) {
        VFP_DEBUG << "Failed to commit inspection results:" << db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}

bool AppDatabase::saveInspectionResult(const QString &flowName, const QString &nodeName,
                                        bool passed, const QString &value)
{
    QMutexLocker locker(&s_dbMutex);
    const QSqlDatabase db = threadDatabase();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO inspection_results (flow_name, node_name, passed, value) VALUES (?, ?, ?, ?)"
    ));
    q.addBindValue(flowName);
    q.addBindValue(nodeName);
    q.addBindValue(passed ? 1 : 0);
    q.addBindValue(value);
    if (!q.exec()) {
        VFP_DEBUG << "Failed to save inspection result:" << q.lastError().text();
        return false;
    }
    return true;
}

QList<InspectionRecord> AppDatabase::queryResults(const QDateTime &from, const QDateTime &to, int limit) const
{
    QMutexLocker locker(&s_dbMutex);
    QList<InspectionRecord> records;
    const QSqlDatabase db = threadDatabase();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, flow_name, node_name, passed, value, timestamp FROM inspection_results "
        "WHERE timestamp BETWEEN ? AND ? ORDER BY timestamp DESC LIMIT ?"
    ));
    q.addBindValue(from);
    q.addBindValue(to);
    q.addBindValue(limit);
    if (!q.exec()) return records;

    while (q.next()) {
        InspectionRecord r;
        r.id = q.value(0).toInt();
        r.flowName = q.value(1).toString();
        r.nodeName = q.value(2).toString();
        r.passed = q.value(3).toInt() != 0;
        r.value = q.value(4).toString();
        r.timestamp = q.value(5).toDateTime();
        records.append(r);
    }
    return records;
}

// ---- User management ----

bool AppDatabase::addUser(const QString &name, const QString &password, const QString &role)
{
    QMutexLocker locker(&s_dbMutex);
    // W-1①：闸必须落在这里。此前"用户管理需 Admin"只在菜单置灰上体现，而
    // enforceFactoryPasswordPolicy() 的文案宣称用户管理已被禁止——拿到出厂口令的人
    // 仍能建一个 Admin 账户当后门。落库层拒绝才是 fail-closed。
    if (const QString refusal = SessionManager::instance()->writeGateRefusal(); !refusal.isEmpty()) {
        VFP_DEBUG << "用户新增被写操作闸拒绝:" << name << refusal;
        return false;
    }
    const QSqlDatabase db = threadDatabase();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO users (name, password_hash, role) VALUES (?, ?, ?)"));
    q.addBindValue(name);
    q.addBindValue(createPasswordHash(password));
    q.addBindValue(role);
    if (!q.exec()) {
        VFP_DEBUG << "Failed to add user:" << q.lastError().text();
        return false;
    }
    return true;
}

bool AppDatabase::authenticateUser(const QString &name, const QString &password) const
{
    QMutexLocker locker(&s_dbMutex);
    const QSqlDatabase db = threadDatabase();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT id, password_hash FROM users WHERE name = ?"));
    q.addBindValue(name);
    if (!q.exec() || !q.next()) return false;

    const QString stored = q.value(1).toString();
    if (!verifyPassword(password, stored)) return false;

    // 兼容旧版：无盐 SHA-256 校验通过后自动迁移为加盐迭代哈希
    if (!stored.contains(QLatin1Char(':'))) {
        QSqlQuery up(db);
        up.prepare(QStringLiteral("UPDATE users SET password_hash = ? WHERE id = ?"));
        up.addBindValue(createPasswordHash(password));
        up.addBindValue(q.value(0).toInt());
        up.exec();
    }
    return true;
}

bool AppDatabase::changeUserPassword(const QString &name, const QString &newPassword)
{
    QMutexLocker locker(&s_dbMutex);
    const QSqlDatabase db = threadDatabase();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE users SET password_hash = ? WHERE name = ?"));
    q.addBindValue(createPasswordHash(newPassword));
    q.addBindValue(name);
    if (!q.exec()) {
        VFP_DEBUG << "Failed to change password for" << name << ":" << q.lastError().text();
        return false;
    }
    // 影响 0 行 = 该用户不存在：必须报失败，否则调用方会以为口令已被改掉（出厂口令仍在用却解锁）
    if (q.numRowsAffected() <= 0) {
        VFP_DEBUG << "changeUserPassword: user not found:" << name;
        return false;
    }
    return true;
}

bool AppDatabase::isFactoryAdminPasswordInUse() const
{
    // 不能复用 authenticateUser()：它自己锁 s_dbMutex，而 QMutex 不可重入，这里再锁一次即自锁。
    // 只做读+校验，不做旧版哈希迁移（那是登录路径的事）。
    QMutexLocker locker(&s_dbMutex);
    const QSqlDatabase db = threadDatabase();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT password_hash FROM users WHERE name = ?"));
    q.addBindValue(QStringLiteral("admin"));
    if (!q.exec() || !q.next()) {
        return false;   // 没有 admin 账户 ⇒ 出厂口令不可能在使用中
    }
    return verifyPassword(QStringLiteral("admin"), q.value(0).toString());
}

QList<UserRecord> AppDatabase::queryUsers() const
{
    QMutexLocker locker(&s_dbMutex);
    QList<UserRecord> records;
    const QSqlDatabase db = threadDatabase();
    QSqlQuery q(db);
    q.exec(QStringLiteral("SELECT id, name, role, created_at FROM users ORDER BY id"));
    while (q.next()) {
        UserRecord r;
        r.id = q.value(0).toInt();
        r.name = q.value(1).toString();
        r.role = q.value(2).toString();
        r.createdAt = q.value(3).toDateTime();
        records.append(r);
    }
    return records;
}

bool AppDatabase::removeUser(const QString &name)
{
    QMutexLocker locker(&s_dbMutex);
    if (const QString refusal = SessionManager::instance()->writeGateRefusal(); !refusal.isEmpty()) {
        VFP_DEBUG << "用户删除被写操作闸拒绝:" << name << refusal;
        return false;
    }
    const QSqlDatabase db = threadDatabase();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM users WHERE name = ?"));
    q.addBindValue(name);
    if (!q.exec()) {
        VFP_DEBUG << "Failed to remove user:" << q.lastError().text();
        return false;
    }
    return true;
}

// ---- Operation logs ----

void AppDatabase::logOperation(const QString &user, const QString &action, const QString &detail)
{
    QMutexLocker locker(&s_dbMutex);
    const QSqlDatabase db = threadDatabase();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO operation_logs (user, action, detail) VALUES (?, ?, ?)"));
    q.addBindValue(user);
    q.addBindValue(action);
    q.addBindValue(detail);
    if (!q.exec()) {
        VFP_DEBUG << "Failed to log operation:" << q.lastError().text();
    }
}

QList<OperationLogRecord> AppDatabase::queryOperationLogs(const QDateTime &from, const QDateTime &to,
                                                           int limit) const
{
    QMutexLocker locker(&s_dbMutex);
    QList<OperationLogRecord> records;
    const QSqlDatabase db = threadDatabase();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, user, action, detail, timestamp FROM operation_logs "
        "WHERE timestamp BETWEEN ? AND ? ORDER BY timestamp DESC LIMIT ?"
    ));
    q.addBindValue(from);
    q.addBindValue(to);
    q.addBindValue(limit);
    if (!q.exec()) return records;

    while (q.next()) {
        OperationLogRecord r;
        r.id = q.value(0).toInt();
        r.user = q.value(1).toString();
        r.action = q.value(2).toString();
        r.detail = q.value(3).toString();
        r.timestamp = q.value(4).toDateTime();
        records.append(r);
    }
    return records;
}

int AppDatabase::purgeOldRecords(int retainDays)
{
    QMutexLocker locker(&s_dbMutex);

    if (retainDays < 1) retainDays = 30;
    const QString cutoff = QDateTime::currentDateTime()
                               .addDays(-retainDays)
                               .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));

    int total = 0;
    const QSqlDatabase db = threadDatabase();
    const QStringList tables = {
        QStringLiteral("alarms"),
        QStringLiteral("inspection_results"),
        QStringLiteral("operation_logs")
    };
    for (const QString &table : tables) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("DELETE FROM %1 WHERE timestamp < ?").arg(table));
        q.addBindValue(cutoff);
        if (q.exec()) {
            total += q.numRowsAffected();
        } else {
            VFP_DEBUG << "Failed to purge table:" << table << q.lastError().text();
        }
    }

    if (total > 0) {
        QSqlQuery vq(db);
        vq.exec(QStringLiteral("VACUUM"));
        VFP_DEBUG << "AppDatabase::purgeOldRecords removed" << total
                  << "records older than" << retainDays << "days";
    }
    return total;
}
