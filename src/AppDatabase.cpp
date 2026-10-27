#include "AppDatabase.h"
#include "AppLog.h"
#include "SchemeCrypto.h"
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
// ---- 时间戳口径（2026-10-25 复核 P1）----
// 库里所有时间戳都由 SQLite CURRENT_TIMESTAMP 写入：**UTC**，格式 "yyyy-MM-dd HH:mm:ss"。
// 三个口径此前互相打架，现场后果是"凌晨查报表/报警/结果全是 0 行、界面时间整体慢 8 小时"：
//   ① 查询把 QDateTime 直接 addBindValue：Qt 6 写成**本地** ISO-T（2026-10-24T23:53:32.219），
//      与库里的 ' ' 分隔串做 BETWEEN 时 'T' > ' ' ⇒ 同一天的行全落在下界之外；
//   ② 读回 q.value(...).toDateTime()：把 UTC 墙钟当**本地**解析 ⇒ 界面整体慢一个时区偏移；
//   ③ purgeOldRecords 的 cutoff 用本地时间串 ⇒ 清理边界比预期早一个时区偏移。
// 统一收口在这两个函数：绑 UTC 同格式串、读回按 UTC 解析再转本地。无历史数据迁移
// （存量行本就是 UTC，只是此前查/读的口径错）。
constexpr char kTsFormat[] = "yyyy-MM-dd HH:mm:ss";

QString utcBindText(const QDateTime &dt)
{
    return dt.toUTC().toString(QString::fromLatin1(kTsFormat));
}

QDateTime readUtcTimestamp(const QVariant &v)
{
    const QString text = v.toString();
    QDateTime dt = QDateTime::fromString(text, QString::fromLatin1(kTsFormat));
    if (!dt.isValid())
        return v.toDateTime();   // 兼容带 T 的 ISO / 空值：不猜
    dt.setTimeSpec(Qt::UTC);     // 这段墙钟是 UTC
    return dt.toLocalTime();     // 回给界面的是本地时刻（比较/分组都按它）
}

} // namespace

namespace {
// 账户口令 KDF：PBKDF2-HMAC-SHA256（SchemeCrypto 里那份实现，与方案信封共源）。
// 先前的形状是「加盐 12000 轮裸 SHA-256 自制迭代链」——升级的是构造不是成本线（152 行）。
// 167 行拍板把轮数抬到 60000（仍在本机一次派生 ≈0.6s 量级，与方案信封 100000 轮那条
// ~1s 的派生成本同档，登录是低频操作）；旧 token 自报轮数照常校验，不破旧账。
constexpr int kHashIterations = 60000;
// 读侧封顶：token 里的轮数是数据不是报价——0 轮与上限外的 token 一概不进派生（与方案信封
// 进口的先值检同一叙事，见 SchemePackage.h 的头注）。
constexpr quint32 kHashIterationsMax = 1000000;
// token 形：pbkdf2$<轮数>$<盐hex>$<摘要hex>——轮数写进 token 本身，未来抬默认不破旧账
// （旧 token 按它自报的轮数照常校验；盖在登录路径上的自动迁移把成功登录的旧格式换写成它）。
const char kKdfTokenPrefix[] = "pbkdf2$";
// 遗留格式一（2026-10 之前唯一的加盐形态）的轮数，只为旧账校验保留
constexpr int kHashIterationsLegacy = 12000;
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

// 遗留格式一的哈希：「加盐 12000 轮裸 SHA-256 自制迭代链」。现写户一律走 PBKDF2（见
// createPasswordHash），这函数只为已存旧串的登录比对保留，谁也别再拿它造新串。
QByteArray stretchHashLegacy(const QByteArray &salt, const QString &password)
{
    QByteArray digest = QCryptographicHash::hash(salt + password.toUtf8(), QCryptographicHash::Sha256);
    for (int i = 1; i < kHashIterationsLegacy; ++i) {
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

// 读侧 exec 判据只留一份。原先五处写的是 `if (!q.exec()) return records;`——返回值接了，
// 原因一个字都不留：调用方拿着空表去问 lastDatabaseError()，读到的是上一笔写失败留下的旧串。
bool AppDatabase::execRead(QSqlQuery &q, const QSqlDatabase &db, const QString &op) const
{
    if (q.exec()) {
        return true;
    }
    noteFailure(op, db, q.lastError().text());
    return false;
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
    // 每个账户使用随机盐，保证相同密码哈希不同；token 自报轮数，读侧按 token 走同一份 PBKDF2。
    // 盐是密钥材料：直接走 OS 的 CSPRNG 流（QRandomGenerator::system()，Qt 文档注明其
    // 输出可用于生成密码学凭据）；global() 的 MT 流只适合 Monte-Carlo，不再用于盐。
    QByteArray salt(kSaltBytes, '\0');
    quint32 word = 0;
    quint32 left = 0;
    for (int i = 0; i < salt.size(); ++i) {
        if (left == 0) {
            word = QRandomGenerator::system()->generate();
            left = 4;
        }
        salt[i] = static_cast<char>(word & 0xFFu);
        word >>= 8;
        --left;
    }
    const QByteArray dk = SchemeCrypto::pbkdf2HmacSha256(password.toUtf8(), salt,
                                                         kHashIterations, 32);
    return QString::fromLatin1(kKdfTokenPrefix)
         + QString::number(kHashIterations)
         + QStringLiteral("$") + QString::fromLatin1(salt.toHex())
         + QStringLiteral("$") + QString::fromLatin1(dk.toHex());
}

bool AppDatabase::verifyPassword(const QString &password, const QString &stored)
{
    if (stored.startsWith(QLatin1String(kKdfTokenPrefix))) {
        // pbkdf2$<轮数>$<盐hex>$<摘要hex>：轮数是 token 的一部分，读侧封顶后进派生
        const QStringList parts = stored.split(QLatin1Char('$'));
        if (parts.size() != 4) return false;
        bool itersOk = false;
        const quint64 iters = parts[1].toULongLong(&itersOk);
        if (!itersOk || iters < 1 || iters > quint64(kHashIterationsMax)) return false;
        const QByteArray salt = QByteArray::fromHex(parts[2].toLatin1());
        const QByteArray expected = QByteArray::fromHex(parts[3].toLatin1());
        const QByteArray dk = SchemeCrypto::pbkdf2HmacSha256(
            password.toUtf8(), salt, static_cast<int>(iters), 32);
        // 恒时比较（见 SchemeCrypto.h 侧注）：摘要本身不保密，差异位置不该进耗时
        return SchemeCrypto::constTimeEquals(dk, expected);
    }
    const int colon = stored.indexOf(QLatin1Char(':'));
    if (colon < 0) {
        // 遗留格式二（最早的形态）：无盐 SHA-256（登录成功后由调用方迁移）。
        // 摘要比对同走恒时——旧格式不能比新格式多出一条时序侧道。
        const QByteArray legacy = QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Sha256);
        return SchemeCrypto::constTimeEquals(legacy, QByteArray::fromHex(stored.toLatin1()));
    }
    // 遗留格式一：盐hex:摘要hex（自制迭代链，登录成功后由调用方迁移）
    const QByteArray salt = QByteArray::fromHex(stored.left(colon).toLatin1());
    return SchemeCrypto::constTimeEquals(stretchHashLegacy(salt, password),
                                         QByteArray::fromHex(stored.mid(colon + 1).toLatin1()));
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

    // 线程结束时摘除本线程的连接（2026-10-25 复核 P3-10）：连接名按线程 ID 编，
    // 而线程退出后其 ID 可能被新线程复用——旧连接仍挂在 QSqlDatabase 注册表里，
    // 新线程 contains(name) 为真、拿到的却是"别人家的线程"建的连接（Qt 只告警，
    // 随后查询失败），长期换方案时是"换了方案却连到旧库"一类难查现象。
    // 只有 QThread 管理的线程能挂 finished；裸 std::thread 建的连接留到进程退出
    //（数量有界，且这些线程目前不写库）。
    if (QThread *self = QThread::currentThread()) {
        QObject::connect(self, &QThread::finished, this, [name]() {
            if (QSqlDatabase::contains(name))
                QSqlDatabase::removeDatabase(name);
        }, Qt::DirectConnection);
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

    const QString createFeatureSamples = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS feature_samples ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  detector TEXT NOT NULL,"
        "  image BLOB,"
        "  keypoints BLOB NOT NULL,"
        "  descriptors BLOB NOT NULL,"
        "  desc_rows INTEGER NOT NULL,"
        "  desc_cols INTEGER NOT NULL,"
        "  desc_type INTEGER NOT NULL,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ")"
    );
    if (!q.exec(createFeatureSamples)) {
        noteFailure(QStringLiteral("createTables/feature_samples"), db, q.lastError().text());
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
    q.addBindValue(utcBindText(from));
    q.addBindValue(utcBindText(to));
    q.addBindValue(limit);
    if (!execRead(q, db, QStringLiteral("queryAlarms"))) return records;

    while (q.next()) {
        AlarmRecord r;
        r.id = q.value(0).toInt();
        r.module = q.value(1).toString();
        r.level = q.value(2).toString();
        r.message = q.value(3).toString();
        r.timestamp = readUtcTimestamp(q.value(4));
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
    q.addBindValue(utcBindText(from));
    q.addBindValue(utcBindText(to));
    q.addBindValue(limit);
    if (!execRead(q, db, QStringLiteral("queryResults"))) return records;

    while (q.next()) {
        InspectionRecord r;
        r.id = q.value(0).toInt();
        r.flowName = q.value(1).toString();
        r.nodeName = q.value(2).toString();
        r.passed = q.value(3).toInt() != 0;
        r.value = q.value(4).toString();
        r.timestamp = readUtcTimestamp(q.value(5));
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

bool AppDatabase::authenticateUser(const QString &name, const QString &password,
                                   bool *readFailed) const
{
    // 出参在入口就写成「判定做得出来」：原先只有两条失败出口写 true，三条成功路径一个字节
    // 都不写，调用方拿到的是它自己那个初值——本遍的反向对照腿就是这么把它抓红的。
    if (readFailed) {
        *readFailed = false;
    }
    QMutexLocker locker(&s_dbMutex);
    const QSqlDatabase db = threadDatabase();
    QSqlQuery q(db);
    if (!prepareRead(q, db, QStringLiteral("SELECT id, password_hash FROM users WHERE name = ?"),
                     QStringLiteral("authenticateUser"))) {
        if (readFailed) { *readFailed = true; }
        return false;
    }
    q.addBindValue(name);
    if (!execRead(q, db, QStringLiteral("authenticateUser"))) {
        if (readFailed) { *readFailed = true; }
        return false;
    }
    // 语句跑通了而没有这一行：那是「没有这个账户」，判定做得出来，不算读失败。
    if (!q.next()) return false;

    const QString stored = q.value(1).toString();
    if (!verifyPassword(password, stored)) return false;

    // 兼容旧版（无盐 SHA-256 或旧的自制迭代链）：登录已验证通过即换写成 PBKDF2 token
    if (!stored.startsWith(QLatin1String(kKdfTokenPrefix))) {
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

AdminPasswordState AppDatabase::adminPasswordState() const
{
    // 不能复用 authenticateUser()：它自己锁 s_dbMutex，而 QMutex 不可重入，这里再锁一次即自锁。
    // 只做读+校验，不做旧版哈希迁移（那是登录路径的事）。
    QMutexLocker locker(&s_dbMutex);
    const QSqlDatabase db = threadDatabase();
    QSqlQuery q(db);
    if (!prepareRead(q, db, QStringLiteral("SELECT password_hash FROM users WHERE name = ?"),
                     QStringLiteral("adminPasswordState"))) {
        return AdminPasswordState::Unreadable;
    }
    q.addBindValue(QStringLiteral("admin"));
    if (!execRead(q, db, QStringLiteral("adminPasswordState"))) {
        return AdminPasswordState::Unreadable;
    }
    if (!q.next()) {
        // 读通了而没有这一行 = 没有 admin 账户 ⇒ 出厂口令不可能在使用中；这是做出来的判定。
        return AdminPasswordState::Retired;
    }
    return verifyPassword(QStringLiteral("admin"), q.value(0).toString())
        ? AdminPasswordState::InUse : AdminPasswordState::Retired;
}

AccountSurface AppDatabase::accountSurface() const
{
    QMutexLocker locker(&s_dbMutex);
    const QSqlDatabase db = threadDatabase();
    QSqlQuery q(db);
    if (!prepareRead(q, db, QStringLiteral("SELECT COUNT(*) FROM users"),
                     QStringLiteral("accountSurface"))) {
        return AccountSurface::Unreadable;
    }
    if (!execRead(q, db, QStringLiteral("accountSurface"))) {
        return AccountSurface::Unreadable;
    }
    if (!q.next()) {
        // COUNT(*) 必然回一行。走到这里说明语句被动过或驱动异常——按「读不出来」交，
        // 不得按「没有账户」交：后者在登录页那边是一条创建 Admin 的路。
        return AccountSurface::Unreadable;
    }
    return q.value(0).toInt() > 0 ? AccountSurface::HasAccounts : AccountSurface::NoneYet;
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
        r.createdAt = readUtcTimestamp(q.value(3));
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
    q.addBindValue(utcBindText(from));
    q.addBindValue(utcBindText(to));
    q.addBindValue(limit);
    if (!execRead(q, db, QStringLiteral("queryOperationLogs"))) return records;

    while (q.next()) {
        OperationLogRecord r;
        r.id = q.value(0).toInt();
        r.user = q.value(1).toString();
        r.action = q.value(2).toString();
        r.detail = q.value(3).toString();
        r.timestamp = readUtcTimestamp(q.value(4));
        records.append(r);
    }
    return records;
}

int AppDatabase::purgeOldRecords(int retainDays)
{
    QMutexLocker locker(&s_dbMutex);

    if (retainDays < 1) retainDays = 30;
    // cutoff 与存储同口径（UTC）：用本地时间串会让清理边界比预期早一个时区偏移
    const QString cutoff = QDateTime::currentDateTimeUtc()
                               .addDays(-retainDays)
                               .toString(QString::fromLatin1(kTsFormat));

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

bool AppDatabase::saveFeatureSample(const FeatureSample &sample)
{
    QMutexLocker locker(&s_dbMutex);
    if (const QString refusal = SessionManager::instance()->writeGateRefusal(); !refusal.isEmpty()) {
        VFP_DEBUG << "特征样本保存被写操作闸拒绝:" << sample.name << refusal;
        return false;
    }
    const QSqlDatabase db = threadDatabase();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO feature_samples (name, detector, image, keypoints, descriptors,"
        " desc_rows, desc_cols, desc_type, created_at)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(sample.name);
    q.addBindValue(sample.detector);
    q.addBindValue(sample.imagePng);
    q.addBindValue(sample.keypoints);
    q.addBindValue(sample.descriptors);
    q.addBindValue(sample.descRows);
    q.addBindValue(sample.descCols);
    q.addBindValue(sample.descType);
    q.addBindValue(utcBindText(sample.createdAt.isValid() ? sample.createdAt
                                                          : QDateTime::currentDateTimeUtc()));
    if (!q.exec()) {
        noteFailure(QStringLiteral("saveFeatureSample"), db, q.lastError().text());
        return false;
    }
    return true;
}

QList<FeatureSample> AppDatabase::loadFeatureSamples(const QString &detector) const
{
    QMutexLocker locker(&s_dbMutex);
    QList<FeatureSample> out;
    const QSqlDatabase db = threadDatabase();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, name, detector, image, keypoints, descriptors,"
        " desc_rows, desc_cols, desc_type, created_at FROM feature_samples"
        " WHERE detector = ? ORDER BY id"));
    q.addBindValue(detector);
    if (!q.exec()) {
        noteFailure(QStringLiteral("loadFeatureSamples"), db, q.lastError().text());
        return out;
    }
    int skipped = 0;
    while (q.next()) {
        FeatureSample s;
        s.id = q.value(0).toInt();
        s.name = q.value(1).toString();
        s.detector = q.value(2).toString();
        s.imagePng = q.value(3).toByteArray();
        s.keypoints = q.value(4).toByteArray();
        s.descriptors = q.value(5).toByteArray();
        s.descRows = q.value(6).toInt();
        s.descCols = q.value(7).toInt();
        s.descType = q.value(8).toInt();
        s.createdAt = readUtcTimestamp(q.value(9));
        // 形状校验（2026-10-27 P2-4）：半写/损坏行跳过，不让坏 blob 进 BFMatcher
        if (!s.isValid()) {
            ++skipped;
            continue;
        }
        out.append(s);
    }
    if (skipped > 0)
        noteFailure(QStringLiteral("loadFeatureSamples/shape"),
                    db, QStringLiteral("跳过 %1 个损坏样本").arg(skipped));
    return out;
}

QList<FeatureSample> AppDatabase::loadFeatureSamplesMeta(const QString &detector) const
{
    // 元数据查询（2026-10-27 P3-4）：检索/按名查找不需要图像 BLOB——样本多了每次检索
    // 都搬几百 MB，现场明显卡顿。图像只在预览时取。
    QMutexLocker locker(&s_dbMutex);
    QList<FeatureSample> out;
    const QSqlDatabase db = threadDatabase();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, name, detector, keypoints, descriptors,"
        " desc_rows, desc_cols, desc_type, created_at FROM feature_samples"
        " WHERE detector = ? ORDER BY id"));
    q.addBindValue(detector);
    if (!q.exec()) {
        noteFailure(QStringLiteral("loadFeatureSamplesMeta"), db, q.lastError().text());
        return out;
    }
    while (q.next()) {
        FeatureSample s;
        s.id = q.value(0).toInt();
        s.name = q.value(1).toString();
        s.detector = q.value(2).toString();
        s.keypoints = q.value(3).toByteArray();
        s.descriptors = q.value(4).toByteArray();
        s.descRows = q.value(5).toInt();
        s.descCols = q.value(6).toInt();
        s.descType = q.value(7).toInt();
        s.createdAt = readUtcTimestamp(q.value(8));
        if (!s.isValid())
            continue;
        out.append(s);
    }
    return out;
}

bool AppDatabase::deleteFeatureSample(int id)
{
    QMutexLocker locker(&s_dbMutex);
    if (const QString refusal = SessionManager::instance()->writeGateRefusal(); !refusal.isEmpty()) {
        VFP_DEBUG << "特征样本删除被写操作闸拒绝:" << id << refusal;
        return false;
    }
    const QSqlDatabase db = threadDatabase();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM feature_samples WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) {
        noteFailure(QStringLiteral("deleteFeatureSample"), db, q.lastError().text());
        return false;
    }
    return q.numRowsAffected() > 0;
}

bool AppDatabase::clearFeatureSamples()
{
    QMutexLocker locker(&s_dbMutex);
    if (const QString refusal = SessionManager::instance()->writeGateRefusal(); !refusal.isEmpty()) {
        VFP_DEBUG << "特征库清空被写操作闸拒绝:" << refusal;
        return false;
    }
    const QSqlDatabase db = threadDatabase();
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("DELETE FROM feature_samples"))) {
        noteFailure(QStringLiteral("clearFeatureSamples"), db, q.lastError().text());
        return false;
    }
    return true;
}

FeatureSample AppDatabase::loadFeatureSampleByName(const QString &name) const
{
    // 按名取单行（含图像 BLOB）：FeatureMatch 按名做参照时用——比"整库读完再找名"
    // 省掉全部图像传输（2026-10-27 P3-4）。
    QMutexLocker locker(&s_dbMutex);
    FeatureSample s;
    const QSqlDatabase db = threadDatabase();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, name, detector, image, keypoints, descriptors,"
        " desc_rows, desc_cols, desc_type, created_at FROM feature_samples"
        " WHERE name = ? ORDER BY id DESC LIMIT 1"));
    q.addBindValue(name);
    if (!q.exec()) {
        noteFailure(QStringLiteral("loadFeatureSampleByName"), db, q.lastError().text());
        return s;
    }
    if (!q.next())
        return s;
    s.id = q.value(0).toInt();
    s.name = q.value(1).toString();
    s.detector = q.value(2).toString();
    s.imagePng = q.value(3).toByteArray();
    s.keypoints = q.value(4).toByteArray();
    s.descriptors = q.value(5).toByteArray();
    s.descRows = q.value(6).toInt();
    s.descCols = q.value(7).toInt();
    s.descType = q.value(8).toInt();
    s.createdAt = readUtcTimestamp(q.value(9));
    return s;
}
