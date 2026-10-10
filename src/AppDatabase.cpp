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

/// 每条连接建立后必须执行的 PRAGMA（WAL / 外键 / 忙等超时）。
/// 抽成一份的原因：initialize() 与 threadDatabase() 两条建连接路径都要设一遍，
/// 各写一份就会走偏（历史上每线程那条连返回值都没接）。
const QStringList &dbPragmas()
{
    static const QStringList kValues = {
        QStringLiteral("PRAGMA journal_mode=WAL"),
        QStringLiteral("PRAGMA foreign_keys=ON"),
        QStringLiteral("PRAGMA busy_timeout=5000"),   // 多连接并发时避免立即返回 SQLITE_BUSY
    };
    return kValues;
}

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

QString AppDatabase::failureText(const QString &op, const QString &stage,
                                 const QSqlDatabase &db, const QString &driverReason) const
{
    // label=value 片段拼接而非模板 + arg()：诊断串里要放库路径与驱动原文，
    // 它们一旦含 "%N" 就会被后续 arg 吃掉（先代入的值成了后一个占位符的替换源）。
    const QStringList fields = {
        QStringLiteral("op=") + op,
        QStringLiteral("stage=") + stage,
        QStringLiteral("conn=") + db.connectionName(),
        db.isOpen() ? QStringLiteral("open=yes") : QStringLiteral("open=no"),
        m_dbPath.isEmpty() ? QStringLiteral("db=(空：initialize 未成功)")
                           : QStringLiteral("db=") + m_dbPath,
        driverReason.isEmpty() ? QStringLiteral("driver=(驱动未给出原因)")
                               : QStringLiteral("driver=") + driverReason,
    };
    return QStringLiteral("AppDatabase 数据库失败 ") + fields.join(QLatin1Char(' '));
}

void AppDatabase::noteFailure(const QString &op, const QSqlDatabase &db,
                              const QString &driverReason, const QString &stage) const
{
    m_lastDatabaseError = failureText(op, stage, db, driverReason);
    VFP_DEBUG << m_lastDatabaseError;
}

bool AppDatabase::prepareWrite(QSqlQuery &q, const QSqlDatabase &db,
                               const QString &sql, const QString &op) const
{
    if (q.prepare(sql)) {
        return true;
    }
    noteFailure(op, db, q.lastError().text(), QStringLiteral("prepare"));
    return false;
}

// 读侧与写侧共用同一份判据（见头注释）。U-97 补读族六处时量到的形状是：
// 读侧原来"prepare 根本不查、exec 查了也只是 return records"，而 lastDatabaseError 的
// 成功路径不清空，于是"空表"与"上一笔写失败留下的旧原因"会一起被送到界面上。
// 读侧不改变失败时返回什么，只让失败开口。
bool AppDatabase::prepareRead(QSqlQuery &q, const QSqlDatabase &db,
                              const QString &sql, const QString &op) const
{
    return prepareWrite(q, db, sql, op);
}

QString AppDatabase::lastDatabaseError() const
{
    QMutexLocker locker(&s_dbMutex);
    return m_lastDatabaseError;
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
        noteFailure(QStringLiteral("initialize"), m_db, m_db.lastError().text(), QStringLiteral("open"));
        return false;
    }

    // Enable WAL mode for better concurrent performance
    // 三条 PRAGMA 逐条查返回值：历史上连返回值都没接，busy_timeout 没生效时现场只会
    // 以"随机 database is locked"的形式在别处冒出来，日志里查不到根。
    // 但**不因此判初始化失败**：WAL 在网络共享盘等场景可能开不起来，那是降级（并发性能变差），
    // 中止初始化会把整个检测记录功能停掉，后果更重。失败留痕 + 继续。
    const QStringList &pragmas = dbPragmas();
    for (const QString &sql : pragmas) {
        QSqlQuery pragma(m_db);
        if (!pragma.exec(sql)) {
            noteFailure(QStringLiteral("initialize"), m_db, pragma.lastError().text(), sql);
        }
    }

    if (!createTables()) {
        return false;   // 具体哪条语句失败已由 createTables 逐条记录
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
        // 仍返回这条未打开的连接：调用方每次都按线程取连接，此处直接返回空句柄等于
        // "本线程永远写不了库"；把失败记进诊断串后，写侧文本会带上 open=no 与驱动原文。
        noteFailure(QStringLiteral("threadDatabase"), db, db.lastError().text(),
                    QStringLiteral("open %1").arg(name));
        return db;
    }
    const QStringList &pragmas = dbPragmas();
    for (const QString &sql : pragmas) {
        QSqlQuery pragma(db);
        if (!pragma.exec(sql)) {
            noteFailure(QStringLiteral("threadDatabase"), db, pragma.lastError().text(), sql);
        }
    }
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
        noteFailure(QStringLiteral("createTables/alarms"), db, q.lastError().text());
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
        noteFailure(QStringLiteral("createTables/inspection_results"), db, q.lastError().text());
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
        noteFailure(QStringLiteral("createTables/users"), db, q.lastError().text());
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
        noteFailure(QStringLiteral("createTables/operation_logs"), db, q.lastError().text());
        return false;
    }

    // 高频查询/清理索引（时间倒序查询）
    // 失败只留痕、不判建表失败：索引缺失只让查询变慢，不影响数据完整性，
    // 但一句都不打的话，"清理/查询越来越慢"在现场就查不到根。
    const QStringList indexes = {
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_alarms_ts ON alarms(timestamp)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_results_ts ON inspection_results(timestamp)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_operlogs_ts ON operation_logs(timestamp)"),
    };
    for (const QString &sql : indexes) {
        QSqlQuery iq(db);
        if (!iq.exec(sql)) {
            noteFailure(QStringLiteral("createTables/index"), db, iq.lastError().text(), sql);
        }
    }

    // Insert default admin user if none exists
    QSqlQuery countQ(db);
    if (!countQ.exec(QStringLiteral("SELECT COUNT(*) FROM users"))) {
        noteFailure(QStringLiteral("createTables/countUsers"), db, countQ.lastError().text());
        return false;
    }
    if (countQ.next() && countQ.value(0).toInt() == 0) {
        QSqlQuery ins(db);
        // 播种默认账户是出厂口令守卫的地基：这里静默失败 = users 表为空 =
        // 「默认口令仍在使用」的判定与写操作闸全都落空，所以必须留痕。
        if (!prepareWrite(ins, db,
                          QStringLiteral("INSERT INTO users (name, password_hash, role) VALUES (?, ?, ?)"),
                          QStringLiteral("createTables/seedAdmin"))) {
            return false;
        }
        ins.addBindValue(QStringLiteral("admin"));
        ins.addBindValue(createPasswordHash(QStringLiteral("admin")));
        ins.addBindValue(QStringLiteral("Admin"));
        if (!ins.exec()) {
            noteFailure(QStringLiteral("createTables/seedAdmin"), db, ins.lastError().text());
            return false;
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
    if (!prepareWrite(q, db,
                      QStringLiteral("INSERT INTO alarms (module, level, message) VALUES (?, ?, ?)"),
                      QStringLiteral("addAlarm"))) {
        return -1;
    }
    q.addBindValue(module);
    q.addBindValue(level);
    q.addBindValue(message);
    if (!q.exec()) {
        noteFailure(QStringLiteral("addAlarm"), db, q.lastError().text());
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
    if (!prepareRead(q, db, QStringLiteral(
                           "SELECT id, module, level, message, timestamp FROM alarms "
                           "WHERE timestamp BETWEEN ? AND ? ORDER BY timestamp DESC LIMIT ?"),
                     QStringLiteral("queryAlarms"))) {
        return records;
    }
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
    const QString op = QStringLiteral("saveInspectionResults(%1)").arg(records.size());
    if (!db.transaction()) {
        noteFailure(op, db, db.lastError().text(), QStringLiteral("transaction"));
        return false;
    }
    QSqlQuery q(db);
    // prepare 的返回值必须查：整批用同一条语句，prepare 一旦失败而放过，后面每条 exec()
    // 都是「0 个占位符配 4 个绑定值」，报出来的是与真因无关的 "Parameter count mismatch"，
    // 连续模式下每轮的全部检测结果就这样丢掉而查不到根。
    if (!prepareWrite(q, db,
                      QStringLiteral("INSERT INTO inspection_results (flow_name, node_name, passed, value) VALUES (?, ?, ?, ?)"),
                      op)) {
        // 回滚是二次动作：只进调试日志，不覆盖上面那条主原因（见 lastDatabaseError 的注释）
        if (!db.rollback()) {
            VFP_DEBUG << "Rollback after prepare failure failed:" << db.lastError().text();
        }
        return false;
    }
    bool ok = true;
    for (int i = 0; i < records.size(); ++i) {
        const InspectionRecord &r = records.at(i);
        q.addBindValue(r.flowName);
        q.addBindValue(r.nodeName);
        q.addBindValue(r.passed ? 1 : 0);
        q.addBindValue(r.value);
        if (!q.exec()) {
            noteFailure(op, db, q.lastError().text(),
                        QStringLiteral("exec record %1/%2 node=%3").arg(i + 1).arg(records.size()).arg(r.nodeName));
            ok = false;
            break;
        }
    }
    if (!ok) {
        // 整批原子：任一条失败则全部回滚，避免"半轮结果"。回滚失败只进调试日志——
        // 覆盖掉上面那条"第几条写不进去"的主原因，现场就只能看到副作用。
        if (!db.rollback()) {
            VFP_DEBUG << "Rollback after batch failure failed:" << db.lastError().text();
        }
        return false;
    }
    if (!db.commit()) {
        noteFailure(op, db, db.lastError().text(), QStringLiteral("commit"));
        if (!db.rollback()) {
            VFP_DEBUG << "Rollback after commit failure failed:" << db.lastError().text();
        }
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
    if (!prepareWrite(q, db,
                      QStringLiteral("INSERT INTO inspection_results (flow_name, node_name, passed, value) VALUES (?, ?, ?, ?)"),
                      QStringLiteral("saveInspectionResult"))) {
        return false;
    }
    q.addBindValue(flowName);
    q.addBindValue(nodeName);
    q.addBindValue(passed ? 1 : 0);
    q.addBindValue(value);
    if (!q.exec()) {
        noteFailure(QStringLiteral("saveInspectionResult %1/%2").arg(flowName, nodeName),
                    db, q.lastError().text());
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
    if (!prepareRead(q, db, QStringLiteral(
                           "SELECT id, flow_name, node_name, passed, value, timestamp FROM inspection_results "
                           "WHERE timestamp BETWEEN ? AND ? ORDER BY timestamp DESC LIMIT ?"),
                     QStringLiteral("queryResults"))) {
        return records;
    }
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
    if (!prepareWrite(q, db,
                      QStringLiteral("INSERT INTO users (name, password_hash, role) VALUES (?, ?, ?)"),
                      QStringLiteral("addUser"))) {
        return false;
    }
    q.addBindValue(name);
    q.addBindValue(createPasswordHash(password));
    q.addBindValue(role);
    if (!q.exec()) {
        noteFailure(QStringLiteral("addUser %1").arg(name), db, q.lastError().text());
        return false;
    }
    return true;
}

bool AppDatabase::authenticateUser(const QString &name, const QString &password) const
{
    QMutexLocker locker(&s_dbMutex);
    const QSqlDatabase db = threadDatabase();
    QSqlQuery q(db);
    if (!prepareRead(q, db, QStringLiteral("SELECT id, password_hash FROM users WHERE name = ?"),
                     QStringLiteral("authenticateUser"))) {
        return false;
    }
    q.addBindValue(name);
    if (!q.exec() || !q.next()) return false;

    const QString stored = q.value(1).toString();
    if (!verifyPassword(password, stored)) return false;

    // 兼容旧版：无盐 SHA-256 校验通过后自动迁移为加盐迭代哈希
    if (!stored.contains(QLatin1Char(':'))) {
        QSqlQuery up(db);
        // 迁移失败**不改变登录结果**（口令已验证通过），但必须留痕：静默失败意味着
        // 这条账户一直是无盐哈希，而界面上没人看得出来（原实现 prepare/exec 返回值都没查）。
        if (prepareWrite(up, db,
                         QStringLiteral("UPDATE users SET password_hash = ? WHERE id = ?"),
                         QStringLiteral("authenticateUser/migrateHash"))) {
            up.addBindValue(createPasswordHash(password));
            up.addBindValue(q.value(0).toInt());
            if (!up.exec()) {
                noteFailure(QStringLiteral("authenticateUser/migrateHash"), db, up.lastError().text());
            } else if (up.numRowsAffected() <= 0) {
                noteFailure(QStringLiteral("authenticateUser/migrateHash"), db,
                            QStringLiteral("UPDATE 影响 0 行（刚查到的 id 不见了）"),
                            QStringLiteral("affected"));
            }
        }
    }
    return true;
}

bool AppDatabase::changeUserPassword(const QString &name, const QString &newPassword)
{
    QMutexLocker locker(&s_dbMutex);
    const QSqlDatabase db = threadDatabase();
    QSqlQuery q(db);
    if (!prepareWrite(q, db,
                      QStringLiteral("UPDATE users SET password_hash = ? WHERE name = ?"),
                      QStringLiteral("changeUserPassword"))) {
        return false;
    }
    q.addBindValue(createPasswordHash(newPassword));
    q.addBindValue(name);
    if (!q.exec()) {
        noteFailure(QStringLiteral("changeUserPassword %1").arg(name), db, q.lastError().text());
        return false;
    }
    // 影响 0 行 = 该用户不存在：必须报失败，否则调用方会以为口令已被改掉（出厂口令仍在用却解锁）
    if (q.numRowsAffected() <= 0) {
        noteFailure(QStringLiteral("changeUserPassword %1").arg(name), db,
                    QStringLiteral("UPDATE 影响 0 行（账户不存在）"), QStringLiteral("affected"));
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
    if (!prepareRead(q, db, QStringLiteral("SELECT password_hash FROM users WHERE name = ?"),
                     QStringLiteral("isFactoryAdminPasswordInUse"))) {
        return false;
    }
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
    // 这条没有占位符，失败只可能出在 exec；但它原来连 exec 的返回值都不接。
    if (!q.exec(QStringLiteral("SELECT id, name, role, created_at FROM users ORDER BY id"))) {
        noteFailure(QStringLiteral("queryUsers"), db, q.lastError().text());
        return records;
    }
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
    if (!prepareWrite(q, db,
                      QStringLiteral("DELETE FROM users WHERE name = ?"),
                      QStringLiteral("removeUser"))) {
        return false;
    }
    q.addBindValue(name);
    if (!q.exec()) {
        noteFailure(QStringLiteral("removeUser %1").arg(name), db, q.lastError().text());
        return false;
    }
    // 与 changeUserPassword 同口径：影响 0 行 = 该账户本来就不存在，调用方不该收到"已删除"
    if (q.numRowsAffected() <= 0) {
        noteFailure(QStringLiteral("removeUser %1").arg(name), db,
                    QStringLiteral("DELETE 影响 0 行（账户不存在）"), QStringLiteral("affected"));
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
    // 返回 void：操作日志写不进去不该挡住业务动作，但失败必须留痕（原实现 prepare 没查，
    // 现场表现为"审计记录凭空少了一段"而日志里一个字都没有）
    if (!prepareWrite(q, db,
                      QStringLiteral("INSERT INTO operation_logs (user, action, detail) VALUES (?, ?, ?)"),
                      QStringLiteral("logOperation"))) {
        return;
    }
    q.addBindValue(user);
    q.addBindValue(action);
    q.addBindValue(detail);
    if (!q.exec()) {
        noteFailure(QStringLiteral("logOperation %1").arg(action), db, q.lastError().text());
    }
}

QList<OperationLogRecord> AppDatabase::queryOperationLogs(const QDateTime &from, const QDateTime &to,
                                                           int limit) const
{
    QMutexLocker locker(&s_dbMutex);
    QList<OperationLogRecord> records;
    const QSqlDatabase db = threadDatabase();
    QSqlQuery q(db);
    if (!prepareRead(q, db, QStringLiteral(
                           "SELECT id, user, action, detail, timestamp FROM operation_logs "
                           "WHERE timestamp BETWEEN ? AND ? ORDER BY timestamp DESC LIMIT ?"),
                     QStringLiteral("queryOperationLogs"))) {
        return records;
    }
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
        const QString op = QStringLiteral("purgeOldRecords %1").arg(table);
        if (!prepareWrite(q, db,
                          QStringLiteral("DELETE FROM %1 WHERE timestamp < ?").arg(table),
                          op)) {
            continue;   // 这条表本轮清不动，计数不含它
        }
        q.addBindValue(cutoff);
        if (q.exec()) {
            total += q.numRowsAffected();
        } else {
            noteFailure(op, db, q.lastError().text());
        }
    }

    if (total > 0) {
        QSqlQuery vq(db);
        if (!vq.exec(QStringLiteral("VACUUM"))) {
            // 记录已删但库文件没收缩：现场问"为什么还在涨"时这条是唯一线索
            noteFailure(QStringLiteral("purgeOldRecords/VACUUM"), db, vq.lastError().text());
        }
        VFP_DEBUG << "AppDatabase::purgeOldRecords removed" << total
                  << "records older than" << retainDays << "days";
    }
    return total;
}
