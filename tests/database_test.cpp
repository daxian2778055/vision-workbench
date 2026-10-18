// 数据库跨线程访问测试
//
// 背景：QSqlDatabase 具有线程亲和性 —— 一个连接只能在创建它的线程使用。
// 本工程在 main() 中于主线程 initialize()，但检测流程（FlowExecutor 工作线程）
// 会调用 saveInspectionResult / addAlarm / logOperation 写库。
// 若沿用单条共享连接，Qt 会打印
//   "QSqlDatabasePrivate::database: requested database does not belong to the calling thread."
// 并且写入静默失败，极端情况下崩溃。
//
// 本测试用「工作线程写库 + 多线程并发写库」覆盖该路径：
//   · 旧实现：writeOk == false（QSqlQuery 在非归属线程上 exec 失败）
//   · 新实现：每线程独立连接（同一 SQLite 文件 + WAL），writeOk == true
//
// 另加一条 P1 腿（testZeroRowWritesAreFailures）：UPDATE/DELETE 影响 0 行不是成功，
// 且写库函数凡返回 false 都必须留下一条能显示给现场的原因文本（op/stage/open/db/driver）。
#include <QtTest/QtTest>
#include <QObject>
#include <QThread>
#include <QTemporaryDir>
#include <QDateTime>
#include <QElapsedTimer>
#include <QList>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>   // lastError().text()：只含前两个头时 QSqlError 只有前向声明
#include <QCryptographicHash>

#include "AppDatabase.h"
#include "RecordNode.h"
#include "DataObject.h"

/// 模拟 FlowExecutor 工作线程执行一次检测结果落库
class DbWorkerThread : public QThread
{
public:
    bool writeOk = false;
    bool readOk = false;
    int visibleRows = -1;

    void run() override
    {
        AppDatabase *db = AppDatabase::instance();

        writeOk = db->saveInspectionResult(QStringLiteral("workerFlow"),
                                           QStringLiteral("workerNode"),
                                           true,
                                           QStringLiteral("42.5"));

        // 用宽窗口查询，避免时区/格式造成的边界抖动
        const QList<InspectionRecord> rows =
            db->queryResults(QDateTime::currentDateTime().addYears(-1),
                             QDateTime::currentDateTime().addDays(1),
                             200);
        visibleRows = rows.size();
        readOk = true;

        db->logOperation(QStringLiteral("worker"),
                         QStringLiteral("dbThreadTest"),
                         QStringLiteral("write from worker thread"));
    }
};

/// 模拟多个流程同时落库
class ConcurrentWriterThread : public QThread
{
public:
    QString tag;
    int iterations = 0;
    int succeeded = 0;

    void run() override
    {
        AppDatabase *db = AppDatabase::instance();
        for (int i = 0; i < iterations; ++i) {
            if (db->saveInspectionResult(tag,
                                         QStringLiteral("node%1").arg(i),
                                         (i % 2) == 0,
                                         QString::number(i)))
                ++succeeded;
        }
    }
};

class DatabaseTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void testMainThreadWriteRead();
    void testWorkerThreadWriteRead();
    void testConcurrentWriters();
    void testPasswordHashRoundTrip();
    void testBatchInspectionResults();
    void testZeroRowWritesAreFailures();
    // U-97：读族六处 prepare/exec 返回值原先整份丢掉，读失败返回空表而无声。
    // 必须排在 testWriteFailureIsVisibleAndNotMisleading 之前——那一腿要 DROP
    // inspection_results，本腿 DROP users 之后要把它建回来，两张表互不相干。
    void testReadFailureIsAlsoVisible();
    // 第九轮复核 P1-1/P1-2/P1-3：读侧的三态判定与 exec 留痕。
    // 排在 testReadFailureIsAlsoVisible 之后、testWriteFailureIsVisibleAndNotMisleading 之前：
    // 这三条只动 users 表，自己造夹具也自己建回来，与 inspection_results 那条互不相干。
    void testAdminPasswordStateIsThreeValued();
    void testAccountSurfaceSeparatesEmptyFromUnreadable();
    void testReadFailureIsNotMistakenForWrongPassword();
    // 账户口令 KDF 的旧格式走通腿：无盐 SHA-256（最早的形态）与「盐hex:摘要hex」（2026-10
    // 之前的加盐自制迭代链）都要能登进来，且登录成功那一刻被换写成 PBKDF2 token。
    // 只动 users 表、自己造夹具也自己收，排在最后一腿（DROP inspection_results 那条）之前。
    void testLegacyHashesAuthenticateAndUpgrade();
    // ⚠️ 必须排在最后：本腿会 DROP 掉 inspection_results 来复现"表不在"的写失败路径
    void testWriteFailureIsVisibleAndNotMisleading();

private:
    QTemporaryDir m_tempDir;
};

void DatabaseTest::initTestCase()
{
    QVERIFY2(m_tempDir.isValid(), "无法创建临时目录");
    // 指向临时库文件，避免污染真实 data/visionflow.db
    QVERIFY2(AppDatabase::instance()->initialize(m_tempDir.filePath(QStringLiteral("test.db"))),
             "数据库初始化失败");
}

namespace {
/// users 表的 DDL：与 AppDatabase::createTables 里那一份逐字节同一句。
/// 三条新腿都要「把表建回来」，抄三份就会走偏，所以留一处。
const QString kUsersDdl = QStringLiteral(
    "CREATE TABLE users ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  name TEXT UNIQUE NOT NULL,"
    "  password_hash TEXT NOT NULL,"
    "  role TEXT NOT NULL DEFAULT 'Operator',"
    "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP)");

/// 在探针连接上跑一条语句：成功回空串，失败回驱动原文（当断言消息用）。
QString probeExec(QSqlDatabase &probe, const QString &sql)
{
    QSqlQuery q(probe);
    if (q.exec(sql)) {
        return QString();
    }
    return q.lastError().text();
}
}

void DatabaseTest::testMainThreadWriteRead()
{
    AppDatabase *db = AppDatabase::instance();
    QVERIFY(db->saveInspectionResult(QStringLiteral("mainFlow"),
                                     QStringLiteral("mainNode"),
                                     false,
                                     QStringLiteral("7.5")));

    const QList<InspectionRecord> rows =
        db->queryResults(QDateTime::currentDateTime().addYears(-1),
                         QDateTime::currentDateTime().addDays(1),
                         500);
    QVERIFY2(rows.size() >= 1, "主线程写入后应能查到记录");
}

// 旧实现会在工作线程上失败；这是本测试的核心断言
void DatabaseTest::testWorkerThreadWriteRead()
{
    DbWorkerThread worker;
    worker.start();
    QVERIFY2(worker.wait(10000), "工作线程未在超时内结束");

    QVERIFY2(worker.writeOk, "工作线程写库失败（连接线程亲和性未处理）");
    QVERIFY2(worker.readOk, "工作线程查询失败");
    QVERIFY2(worker.visibleRows >= 1, "工作线程写入的记录应可见");
}

void DatabaseTest::testConcurrentWriters()
{
    // 4 条流程并发落库，各写 25 条
    QList<ConcurrentWriterThread *> writers;
    for (int t = 0; t < 4; ++t) {
        auto *w = new ConcurrentWriterThread();
        w->tag = QStringLiteral("flow%1").arg(t);
        w->iterations = 25;
        writers.append(w);
    }
    for (ConcurrentWriterThread *w : writers)
        w->start();
    for (ConcurrentWriterThread *w : writers) {
        QVERIFY2(w->wait(20000), "并发写线程未在超时内结束");
        QCOMPARE(w->succeeded, w->iterations);
    }

    const QList<InspectionRecord> rows =
        AppDatabase::instance()->queryResults(QDateTime::currentDateTime().addYears(-1),
                                              QDateTime::currentDateTime().addDays(1),
                                              1000);
    QVERIFY2(rows.size() >= 100, "并发写入的记录数量不足");
}

void DatabaseTest::testPasswordHashRoundTrip()
{
    const QString hash = AppDatabase::createPasswordHash(QStringLiteral("p@ssw0rd"));
    // token 形：pbkdf2$<轮数>$<盐hex>$<摘要hex>——旧形里那个冒号分隔不再出现在新 token 中
    QVERIFY2(hash.startsWith(QStringLiteral("pbkdf2$")), qPrintable(hash));
    QVERIFY2(hash.count(QLatin1Char('$')) == 3, qPrintable(hash));
    QVERIFY(AppDatabase::verifyPassword(QStringLiteral("p@ssw0rd"), hash));
    QVERIFY(!AppDatabase::verifyPassword(QStringLiteral("wrong"), hash));

    // token 里的轮数是数据不是报价：封顶外的一概不进派生（与方案信封进口的先值检同一叙事）。
    // 钉「任何先派生的退回形状」：把上限拧掉重编，这枚腿读红（封顶外 token 全额付派生）。
    const QStringList parts = hash.split(QLatin1Char('$'));
    const QString oversized = QStringLiteral("pbkdf2$999999999$%1$%2").arg(parts[2], parts[3]);
    QElapsedTimer clock;
    clock.start();
    const bool accepted = AppDatabase::verifyPassword(QStringLiteral("p@ssw0rd"), oversized);
    const qint64 elapsed = clock.elapsed();
    QVERIFY2(!accepted, "封顶外的轮数 token 不该通过校验");
    QVERIFY2(elapsed < 700,
             qPrintable(QStringLiteral("封顶外 token 耗时 %1 ms，先值检后派生的界是 700 ms")
                            .arg(elapsed)));
}

// 一轮执行的检测结果应当整批落到**一个事务**里（连续模式下每节点一次自动提交是主要固定开销）
void DatabaseTest::testBatchInspectionResults()
{
    AppDatabase *db = AppDatabase::instance();

    // 空批：直接成功，不产生事务
    QVERIFY2(db->saveInspectionResults(QList<InspectionRecord>()), "空批应当直接成功");

    QList<InspectionRecord> batch;
    for (int i = 0; i < 30; ++i) {
        InspectionRecord r;
        r.flowName = QStringLiteral("batchFlow");
        r.nodeName = QStringLiteral("batchNode%1").arg(i);
        r.passed = (i % 3) != 0;
        r.value = QString::number(i * 1.5);
        batch.append(r);
    }
    QVERIFY2(db->saveInspectionResults(batch), "批量写入失败");

    // 用宽窗口查询避开时区/格式边界（与其它用例一致），再按流程名过滤
    const QList<InspectionRecord> rows =
        db->queryResults(QDateTime::currentDateTime().addYears(-1),
                         QDateTime::currentDateTime().addDays(1),
                         1000);
    int found = 0;
    for (const InspectionRecord &r : rows) {
        if (r.flowName == QStringLiteral("batchFlow")) {
            ++found;
        }
    }
    QCOMPARE(found, 30);
}

// P1：`UPDATE/DELETE 影响 0 行` 过去也算成功。改口令那条尤其危险——出厂口令守卫按
// "调用方报成功"就认为口令已改，实际库里还是 admin/admin（解锁了提示却什么都没改）。
// 现在 0 行 = 失败，并且失败必须留下一条能显示给现场的原因文本。
void DatabaseTest::testZeroRowWritesAreFailures()
{
    AppDatabase *db = AppDatabase::instance();
    const QString ghost = QStringLiteral("p1_ghost_user");

    // 反向对照（只差"账户是否存在"这一处）：真存在的账户必须照样成功，
    // 否则本条判据就退化成"永远失败"，那等于什么都没测。
    QVERIFY2(db->addUser(QStringLiteral("p1_probe_user"), QStringLiteral("pw"), QStringLiteral("Operator")),
             "探针账户创建失败");
    QVERIFY2(db->changeUserPassword(QStringLiteral("p1_probe_user"), QStringLiteral("pw2")),
             "已存在账户改口令应当成功");
    QVERIFY2(db->removeUser(QStringLiteral("p1_probe_user")),
             "已存在账户删除应当成功");

    QVERIFY2(!db->changeUserPassword(ghost, QStringLiteral("pw2")),
             "不存在的账户改口令却报成功（出厂口令仍在使用却被判已解锁）");
    const QString whyChange = db->lastDatabaseError();
    qDebug().noquote() << QStringLiteral("P1-ZEROROW changeUserPassword:") << whyChange;
    QVERIFY2(!whyChange.isEmpty(), "返回 false 却没有诊断文本，调用方无从显示原因");
    QVERIFY2(whyChange.contains(QStringLiteral("op=changeUserPassword")), qPrintable(whyChange));
    QVERIFY2(whyChange.contains(QStringLiteral("stage=affected")), qPrintable(whyChange));
    QVERIFY2(whyChange.contains(QStringLiteral("open=yes")), qPrintable(whyChange));

    QVERIFY2(!db->removeUser(ghost), "删除不存在的账户却报成功");
    const QString whyRemove = db->lastDatabaseError();
    qDebug().noquote() << QStringLiteral("P1-ZEROROW removeUser:") << whyRemove;
    QVERIFY2(whyRemove.contains(QStringLiteral("op=removeUser")), qPrintable(whyRemove));
    QVERIFY2(whyRemove.contains(QStringLiteral("stage=affected")), qPrintable(whyRemove));
}

// P1 的负向半边：表不在（等价于"库从未初始化"这条现场路径）时
//   ① 写必须判失败；② 原因必须是 prepare 阶段的真错，而不是被 exec 冒名顶替的
//   "Parameter count mismatch"；③「数据记录」算子不得因此绿灯，且要把原因写进 lastError。
// 历史实现三处都缺：prepare 返回值没人查、RecordNode 把 bool 丢掉、main() 把 initialize() 丢掉。
// U-97：读侧原来"prepare 根本不查、exec 查了也只是 return records"，而
// lastDatabaseError() 的成功路径不清空，所以空表 + 旧原因会一起送到界面上。
// 本腿钉住：① 健康库上读照旧成功（否则就退化成"读永远失败"）；② users 表删掉后
// 读失败要留痕，op= 是读自己的名字；③ 那一条不是上一笔写失败留下的旧串。
void DatabaseTest::testReadFailureIsAlsoVisible()
{
    AppDatabase *db = AppDatabase::instance();

    // 反向守栏：健康库上读必须照旧有结果，否则本腿什么都没测
    const QList<InspectionRecord> okRows = db->queryResults(
        QDateTime::currentDateTime().addYears(-1),
        QDateTime::currentDateTime().addDays(1), 500);
    const QList<UserRecord> okUsers = db->queryUsers();
    QVERIFY2(!okRows.isEmpty(), "健康库上 queryResults 读不到行，反向守栏先红");
    QVERIFY2(!okUsers.isEmpty(), "健康库上 queryUsers 读不到账户（seedAdmin 没生效）");
    const QList<AlarmRecord> okAlarms = db->queryAlarms(
        QDateTime::currentDateTime().addYears(-1),
        QDateTime::currentDateTime().addDays(1), 500);
    const QList<OperationLogRecord> okLogs = db->queryOperationLogs(
        QDateTime::currentDateTime().addYears(-1),
        QDateTime::currentDateTime().addDays(1), 500);
    qDebug().noquote() << QStringLiteral("P1-READ-CONTROL alarms=%1 logs=%2")
                          .arg(okAlarms.size()).arg(okLogs.size());

    // 先制造一笔写失败的旧串，后面要证明读失败不会拿它冒充自己
    QVERIFY2(!db->changeUserPassword(QStringLiteral("p1_read_ghost"), QStringLiteral("x")),
             "删表前改一个不存在的账户口令却报成功");
    const QString writeWhy = db->lastDatabaseError();
    QVERIFY2(writeWhy.contains(QStringLiteral("op=changeUserPassword")), qPrintable(writeWhy));

    {
        QSqlDatabase probe = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                      QStringLiteral("p1_probe_read"));
        probe.setDatabaseName(db->databasePath());
        QVERIFY2(probe.open(), qPrintable(probe.lastError().text()));
        QSqlQuery drop(probe);
        QVERIFY2(drop.exec(QStringLiteral("DROP TABLE users")), qPrintable(drop.lastError().text()));
        probe.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("p1_probe_read"));

    // ① 读失败：返回值照旧是空/假（本轮不改返回值，只让它开口）
    // ② 留痕：串记的是"最近一次"，所以每次调用之后各取一次、各断一条，
    //    不能攒着——第二枚读的 op= 盖掉第一枚的那枚正是头注释写明的契约。
    QCOMPARE(db->queryUsers().size(), 0);
    const QString usersWhy = db->lastDatabaseError();
    qDebug().noquote() << QStringLiteral("P1-READ-FAILURE queryUsers:") << usersWhy;
    QVERIFY2(usersWhy.contains(QStringLiteral("op=queryUsers")), qPrintable(usersWhy));
    QVERIFY2(usersWhy.contains(QStringLiteral("no such table: users")), qPrintable(usersWhy));

    // U-97 那时这条只能断言「不是 true」——因为 false 同时说得出「已改掉」和「读不到」。
    // 判据换成三态后必须点名 Unreadable，否则那两种意思又一次共用一个答案。
    QVERIFY2(db->adminPasswordState() == AdminPasswordState::Unreadable,
             "users 表已删除，adminPasswordState 却交出了 InUse/Retired 这种实答案");
    const QString factoryWhy = db->lastDatabaseError();
    qDebug().noquote() << QStringLiteral("P1-READ-FAILURE adminPasswordState:") << factoryWhy;
    QVERIFY2(factoryWhy.contains(QStringLiteral("op=adminPasswordState")),
             qPrintable(factoryWhy));
    QVERIFY2(factoryWhy.contains(QStringLiteral("no such table: users")), qPrintable(factoryWhy));

    // ③ 两枚都不是上一笔写失败那条旧串（U-97 修的就是这个）
    QVERIFY2(usersWhy != writeWhy && factoryWhy != writeWhy,
             "读失败把上一笔写失败的旧串原样交了出来——调用方会把上一轮的错当这次的错");
    QVERIFY2(!usersWhy.contains(QStringLiteral("op=changeUserPassword"))
             && !factoryWhy.contains(QStringLiteral("op=changeUserPassword")),
             qPrintable(usersWhy + QStringLiteral(" / ") + factoryWhy));

    // 把 users 表建回来（DDL 与 AppDatabase::createTables 同一份），
    // 并用一次能读回自己的写证明读路径在活表上照旧工作
    {
        QSqlDatabase probe = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                      QStringLiteral("p1_probe_restore"));
        probe.setDatabaseName(db->databasePath());
        QVERIFY2(probe.open(), qPrintable(probe.lastError().text()));
        QSqlQuery make(probe);
        QVERIFY2(make.exec(QStringLiteral(
            "CREATE TABLE users ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  name TEXT UNIQUE NOT NULL,"
            "  password_hash TEXT NOT NULL,"
            "  role TEXT NOT NULL DEFAULT 'Operator',"
            "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP)")),
                 qPrintable(make.lastError().text()));
        probe.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("p1_probe_restore"));
    QVERIFY2(db->addUser(QStringLiteral("p1_after_restore"), QStringLiteral("pw"),
                          QStringLiteral("Operator")),
             "users 表建回来之后 addUser 却失败");
    const QList<UserRecord> back = db->queryUsers();
    QCOMPARE(back.size(), 1);
    QVERIFY2(db->removeUser(QStringLiteral("p1_after_restore")),
             "读路径恢复之后 removeUser 却失败");
    QCOMPARE(db->queryUsers().size(), 0);
}

// 出厂口令判定的三形各自要有一条能推动它的断言。这条判据是写操作闸的唯一入口，
// 旧口径里 false 一次说出「已改掉」和「读不到」两种意思，后者会把闸放开。
void DatabaseTest::testAdminPasswordStateIsThreeValued()
{
    AppDatabase *db = AppDatabase::instance();
    QSqlDatabase probe = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                  QStringLiteral("p13_probe_state"));
    probe.setDatabaseName(db->databasePath());
    QVERIFY2(probe.open(), qPrintable(probe.lastError().text()));

    QVERIFY(probeExec(probe, QStringLiteral("DROP TABLE IF EXISTS users")).isEmpty());
    QVERIFY(probeExec(probe, kUsersDdl).isEmpty());
    // ① 读到了，确实在用：出厂哈希由 createPasswordHash 现做，不靠上一腿留下的行
    QVERIFY(probeExec(probe, QStringLiteral(
        "INSERT INTO users (name, password_hash, role) VALUES ('admin', '%1', 'Admin')")
        .arg(AppDatabase::createPasswordHash(QStringLiteral("admin")))).isEmpty());
    const bool inUse = db->adminPasswordState() == AdminPasswordState::InUse;
    QVERIFY2(inUse, "刚播种成 admin/admin 却读不出「出厂口令在用」");

    // ② 读到了，确实没在用：把口令改掉，判定必须跟着翻面
    QVERIFY2(db->changeUserPassword(QStringLiteral("admin"), QStringLiteral("Rk7!retiredOnce")),
             "改口令失败，本腿的第二个形状就没有夹具");
    const bool retired = db->adminPasswordState() == AdminPasswordState::Retired;
    QVERIFY2(retired, "口令已改掉却读不出 Retired（那 InUse 这一格也无从信）");

    // ③ 判定做不出来：表整个不在。
    // DROP 之前先把此刻那条串抄下来：lastDatabaseError() 的成功路径不清空，而本套件前面已有
    // 一条腿写过 op=adminPasswordState —— 只断言 contains(op=) 会被上一格留下的旧串满足（A1 那臂
    // 摘掉 exec 留痕时这条腿就是不红的）。要求新串与旧串不同，这一格才说的是「这次」。
    const QString staleBeforeMissing = db->lastDatabaseError();
    QVERIFY(probeExec(probe, QStringLiteral("DROP TABLE users")).isEmpty());
    const AdminPasswordState unreadable = db->adminPasswordState();
    const QString whyUnreadable = db->lastDatabaseError();
    qDebug().noquote() << QStringLiteral("P13-ADMIN-STATE")
                       << QStringLiteral("in_use=%1 retired=%2 unreadable=%3")
                              .arg(inUse ? "yes" : "no")
                              .arg(retired ? "yes" : "no")
                              .arg(unreadable == AdminPasswordState::Unreadable ? "yes" : "no")
                              << whyUnreadable;
    QVERIFY2(unreadable == AdminPasswordState::Unreadable,
             "users 表已不存在，判定却交出了一个实答案（闸会照那个答案放开）");
    QVERIFY2(whyUnreadable.contains(QStringLiteral("op=adminPasswordState")),
             qPrintable(whyUnreadable));
    QVERIFY2(whyUnreadable != staleBeforeMissing,
             "读不到时交回的还是上一格那条旧串——本轮接上的正是这一格的留痕");

    // ④ 恢复对照：表建回来判定重新做得出来——没有这一格，③ 只是「永远判不出」
    QVERIFY(probeExec(probe, kUsersDdl).isEmpty());
    QVERIFY2(db->adminPasswordState() == AdminPasswordState::Retired,
             "表建回来（空表）应读得出 Retired，本腿前面对 ③ 的判定才不是环境假象");

    probe.close();
    QSqlDatabase::removeDatabase(QStringLiteral("p13_probe_state"));
}

// 登录页那条「首次使用」分支的判据。空表与读不到都必须能被区分开：
// 走错方向的后果是凭空建一个 Admin 账户并当场给它 Admin 会话。
void DatabaseTest::testAccountSurfaceSeparatesEmptyFromUnreadable()
{
    AppDatabase *db = AppDatabase::instance();
    QSqlDatabase probe = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                  QStringLiteral("p13_probe_surface"));
    probe.setDatabaseName(db->databasePath());
    QVERIFY2(probe.open(), qPrintable(probe.lastError().text()));

    QVERIFY(probeExec(probe, QStringLiteral("DROP TABLE IF EXISTS users")).isEmpty());
    QVERIFY(probeExec(probe, kUsersDdl).isEmpty());
    const bool emptyIsNone = db->accountSurface() == AccountSurface::NoneYet;
    QVERIFY2(emptyIsNone, "空 users 表读不出 NoneYet：首装分支从此永远进不去（那也是另一种错）");

    // 有账户：同一张表里插一行，判据必须翻面（与上一格只差「表里有没有行」）
    QVERIFY(probeExec(probe, QStringLiteral(
        "INSERT INTO users (name, password_hash, role) VALUES ('surface', 'x', 'Operator')")).isEmpty());
    const bool hasOne = db->accountSurface() == AccountSurface::HasAccounts;
    QVERIFY2(hasOne, "表里有一行却读不出 HasAccounts");

    // 读不出来：整张表不在。同上一条腿：先抄下此刻的旧串，再要求新串与它不同。
    const QString staleBeforeSurface = db->lastDatabaseError();
    QVERIFY(probeExec(probe, QStringLiteral("DROP TABLE users")).isEmpty());
    const AccountSurface unreadable = db->accountSurface();
    const QString why = db->lastDatabaseError();
    qDebug().noquote() << QStringLiteral("P13-ACCOUNT-SURFACE")
                       << QStringLiteral("empty_none=%1 one_has=%2 missing_unreadable=%3")
                              .arg(emptyIsNone ? "yes" : "no")
                              .arg(hasOne ? "yes" : "no")
                              .arg(unreadable == AccountSurface::Unreadable ? "yes" : "no")
                              << why;
    QVERIFY2(unreadable == AccountSurface::Unreadable,
             "users 表已不存在，accountSurface 却与「一个账户都没有」共用一个答案");
    QVERIFY2(why.contains(QStringLiteral("op=accountSurface")), qPrintable(why));
    QVERIFY2(why != staleBeforeSurface,
             "读不出来的那一格交回的是上一格的旧串，不是这次的判定");

    // 恢复对照
    QVERIFY(probeExec(probe, kUsersDdl).isEmpty());
    QVERIFY2(db->accountSurface() == AccountSurface::NoneYet,
             "表建回来应重新读得出 NoneYet");

    probe.close();
    QSqlDatabase::removeDatabase(QStringLiteral("p13_probe_surface"));
}

// authenticateUser 的 false 有两种来历，界面只能显示其中一种。
// 反向对照只差「库能不能读」这一处：健康库上的错口令必须仍然报 readFailed=no。
void DatabaseTest::testReadFailureIsNotMistakenForWrongPassword()
{
    AppDatabase *db = AppDatabase::instance();
    QSqlDatabase probe = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                  QStringLiteral("p13_probe_auth"));
    probe.setDatabaseName(db->databasePath());
    QVERIFY2(probe.open(), qPrintable(probe.lastError().text()));

    QVERIFY(probeExec(probe, QStringLiteral("DROP TABLE IF EXISTS users")).isEmpty());
    bool readFailedBad = false;
    const bool rejectedNoDb = !db->authenticateUser(QStringLiteral("admin"),
                                                   QStringLiteral("whatever"), &readFailedBad);
    QVERIFY2(rejectedNoDb, "库读不到却登录成功");
    QVERIFY2(readFailedBad,
             "读不到用户表被报成「口令错」：现场会把一次数据库故障当成操作员打错字");

    // 反向对照：表在、账户在、口令打错 —— 这时 readFailed 必须为假
    QVERIFY(probeExec(probe, kUsersDdl).isEmpty());
    QVERIFY(probeExec(probe, QStringLiteral(
        "INSERT INTO users (name, password_hash, role) VALUES ('admin', '%1', 'Admin')")
        .arg(AppDatabase::createPasswordHash(QStringLiteral("S3cret!NotFactory")))).isEmpty());
    bool readFailedWrong = true;
    const bool rejectedWrongPw = !db->authenticateUser(QStringLiteral("admin"),
                                                      QStringLiteral("wrong-password"),
                                                      &readFailedWrong);
    bool readFailedOk = true;
    const bool accepted = db->authenticateUser(QStringLiteral("admin"),
                                              QStringLiteral("S3cret!NotFactory"),
                                              &readFailedOk);
    qDebug().noquote() << QStringLiteral("P13-AUTH-OUTLET")
                       << QStringLiteral("nodb_readfailed=%1 wrongpw_readfailed=%2 "
                                         "wrongpw_rejected=%3 good_accepted=%4 good_readfailed=%5")
                              .arg(readFailedBad ? "yes" : "no")
                              .arg(readFailedWrong ? "yes" : "no")
                              .arg(rejectedWrongPw ? "yes" : "no")
                              .arg(accepted ? "yes" : "no")
                              .arg(readFailedOk ? "yes" : "no");
    QVERIFY2(rejectedWrongPw && !readFailedWrong,
             "口令打错却被报成读不到（那是把另一个方向折了，同样是不分三态）");
    QVERIFY2(accepted && !readFailedOk, "健康库上的正确口令应登录成功且不带读失败");

    probe.close();
    QSqlDatabase::removeDatabase(QStringLiteral("p13_probe_auth"));
}

void DatabaseTest::testLegacyHashesAuthenticateAndUpgrade()
{
    AppDatabase *db = AppDatabase::instance();
    const QString path = db->databasePath();
    QVERIFY2(!path.isEmpty(), "databasePath 为空，本腿无从定位库文件");

    // 遗留格式二：无盐单轮 SHA-256（最早的形态）
    const QString legacy0 = QString::fromLatin1(
        QCryptographicHash::hash(QStringLiteral("Legacy0!pw").toUtf8(),
                                 QCryptographicHash::Sha256).toHex());
    // 遗留格式一：盐 + 12000 轮自制迭代链。测试侧按历史算法复算同一串，
    // 复算步数与 AppDatabase.cpp 里 stretchHashLegacy 的常量同值：抬那格要同批抬这里。
    const QByteArray salt =
        QByteArray::fromHex(QByteArrayLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90"));
    QByteArray chain = QCryptographicHash::hash(salt + QStringLiteral("Legacy1!pw").toUtf8(),
                                                QCryptographicHash::Sha256);
    for (int i = 1; i < 12000; ++i) {
        chain = QCryptographicHash::hash(chain + QStringLiteral("Legacy1!pw").toUtf8(),
                                         QCryptographicHash::Sha256);
    }
    const QString legacy1 = QString::fromLatin1(salt.toHex()) + QLatin1Char(':')
                          + QString::fromLatin1(chain.toHex());

    {
        QSqlDatabase probe = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                       QStringLiteral("p13_probe_kdf"));
        probe.setDatabaseName(path);
        QVERIFY2(probe.open(), qPrintable(probe.lastError().text()));
        const QString dropErr = probeExec(probe, QStringLiteral("DROP TABLE IF EXISTS users"));
        QVERIFY2(dropErr.isEmpty(), qPrintable(dropErr));
        const QString ddlErr = probeExec(probe, kUsersDdl);
        QVERIFY2(ddlErr.isEmpty(), qPrintable(ddlErr));
        const QString ins0Err = probeExec(probe, QStringLiteral(
            "INSERT INTO users (name, password_hash, role) VALUES ('legacy0', '%1', 'Admin')")
            .arg(legacy0));
        QVERIFY2(ins0Err.isEmpty(), qPrintable(ins0Err));
        const QString ins1Err = probeExec(probe, QStringLiteral(
            "INSERT INTO users (name, password_hash, role) VALUES ('legacy1', '%1', 'Admin')")
            .arg(legacy1));
        QVERIFY2(ins1Err.isEmpty(), qPrintable(ins1Err));
        probe.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("p13_probe_kdf"));

    // 两种旧格式都要能登进来：KDF 换构造不许把现场已存的账户锁在门外
    bool readFailedL0 = true;
    const bool okL0 = db->authenticateUser(QStringLiteral("legacy0"),
                                           QStringLiteral("Legacy0!pw"), &readFailedL0);
    QVERIFY2(okL0 && !readFailedL0, "旧无盐哈希登不进（KDF 换构造必须带走旧账）");
    bool readFailedL1 = true;
    const bool okL1 = db->authenticateUser(QStringLiteral("legacy1"),
                                           QStringLiteral("Legacy1!pw"), &readFailedL1);
    QVERIFY2(okL1 && !readFailedL1, "旧自制迭代链哈希登不进（KDF 换构造必须带走旧账）");

    // 登录成功那一刻两行都被换写成 PBKDF2 token
    {
        QSqlDatabase probe = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                       QStringLiteral("p13_probe_kdf2"));
        probe.setDatabaseName(path);
        QVERIFY2(probe.open(), qPrintable(probe.lastError().text()));
        QSqlQuery check(probe);
        QVERIFY2(check.exec(QStringLiteral(
                     "SELECT name, password_hash FROM users ORDER BY name")),
                 qPrintable(check.lastError().text()));
        int seen = 0;
        bool sawL0 = false;
        bool sawL1 = false;
        while (check.next()) {
            const QString token = check.value(1).toString();
            const QString name = check.value(0).toString();
            QVERIFY2(token.startsWith(QStringLiteral("pbkdf2$")),
                     qPrintable(QStringLiteral("%1 未被换写成 PBKDF2 token：%2")
                                    .arg(name, token)));
            if (name == QStringLiteral("legacy0")) sawL0 = true;
            if (name == QStringLiteral("legacy1")) sawL1 = true;
            ++seen;
        }
        QCOMPARE(seen, 2);
        QVERIFY2(sawL0 && sawL1, "换写后两行旧账缺行");
        probe.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("p13_probe_kdf2"));

    // 换写后的第二遍登录走新 token：同一口令原样登进
    bool readFailedAgain = true;
    QVERIFY2(db->authenticateUser(QStringLiteral("legacy0"), QStringLiteral("Legacy0!pw"),
                                  &readFailedAgain) && !readFailedAgain,
             "换写成 PBKDF2 token 后登不进");
    QVERIFY2(db->authenticateUser(QStringLiteral("legacy1"), QStringLiteral("Legacy1!pw"),
                                  &readFailedAgain) && !readFailedAgain,
             "换写成 PBKDF2 token 后登不进");

    // 收自己：users 表退回空表形状（与邻腿约定的状态）
    {
        QSqlDatabase probe = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                       QStringLiteral("p13_probe_kdf3"));
        probe.setDatabaseName(path);
        QVERIFY2(probe.open(), qPrintable(probe.lastError().text()));
        const QString dropErr = probeExec(probe, QStringLiteral("DROP TABLE IF EXISTS users"));
        QVERIFY2(dropErr.isEmpty(), qPrintable(dropErr));
        const QString ddlErr = probeExec(probe, kUsersDdl);
        QVERIFY2(ddlErr.isEmpty(), qPrintable(ddlErr));
        probe.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("p13_probe_kdf3"));
}

void DatabaseTest::testWriteFailureIsVisibleAndNotMisleading()
{
    AppDatabase *db = AppDatabase::instance();
    const QString path = db->databasePath();
    QVERIFY2(!path.isEmpty(), "databasePath 为空，本腿无从定位库文件");

    {
        QSqlDatabase probe = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                      QStringLiteral("p1_probe_conn"));
        probe.setDatabaseName(path);
        QVERIFY2(probe.open(), qPrintable(probe.lastError().text()));
        QSqlQuery drop(probe);
        QVERIFY2(drop.exec(QStringLiteral("DROP TABLE IF EXISTS inspection_results")),
                 qPrintable(drop.lastError().text()));
        probe.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("p1_probe_conn"));

    // ①② 的判据共用一个谓词：真原因必须在，误导性那句必须不在。
    // stage 为什么不钉死：实测同一条 INSERT 在同一条连接上**第一次**失败在 exec()
    // （driver="no such table: ... Unable to fetch row"），**第二次**失败在 prepare()
    // （driver="no such table: ... Unable to execute statement"）——因为 SQLite 的模式缓存
    // 要等第一次 step 失败才刷新。这正好是"prepare 与 exec 两个返回值都必须查"的实证：
    // 只查其中一个，另一条路径就会退化成历史日志里那句 "Parameter count mismatch"。
    const auto realReason = [](const QString &text, const QString &op) {
        const bool stageKnown = text.contains(QStringLiteral("stage=prepare"))
                             || text.contains(QStringLiteral("stage=exec"));
        return text.contains(QStringLiteral("op=") + op)
            && stageKnown
            && text.contains(QStringLiteral("no such table: inspection_results"))
            && !text.contains(QStringLiteral("Parameter count mismatch"));
    };

    // ① 单条写（「数据记录」算子走的那条）
    QVERIFY2(!db->saveInspectionResult(QStringLiteral("p1Flow"), QStringLiteral("p1Node"),
                                       true, QStringLiteral("1.0")),
             "检测结果表已不存在，单条写却报成功");
    const QString single = db->lastDatabaseError();
    qDebug().noquote() << QStringLiteral("P1-MISSING-TABLE single:") << single;
    QVERIFY2(realReason(single, QStringLiteral("saveInspectionResult")), qPrintable(single));

    // ② 整批写（执行器轮末走的那条），op 里要带本轮条数——丢了几个必须能从原因里读出来
    QList<InspectionRecord> batch;
    InspectionRecord r;
    r.flowName = QStringLiteral("p1Flow");
    r.nodeName = QStringLiteral("p1Node");
    r.passed = true;
    r.value = QStringLiteral("2.0");
    batch.append(r);
    QVERIFY2(!db->saveInspectionResults(batch), "检测结果表已不存在，批量写却报成功");
    const QString grouped = db->lastDatabaseError();
    qDebug().noquote() << QStringLiteral("P1-MISSING-TABLE batch:") << grouped;
    QVERIFY2(realReason(grouped, QStringLiteral("saveInspectionResults(1)")), qPrintable(grouped));

    // ③ 算子侧：这种库上「数据记录」绝不绿灯，且原因能被界面显示
    RecordNode node;
    node.init();
    node.setParam(QStringLiteral("flowName"), QStringLiteral("p1RecordFlow"));
    node.setInputData(0, QSharedPointer<DataObject>::create(DataObject::DataType::String,
                                                            QVariant(QStringLiteral("9.9"))));
    QVERIFY2(!node.execute(), "落库失败时数据记录算子仍判成功（检测结果静默丢失）");
    QVERIFY2(!node.getParam(QStringLiteral("moduleStatus")).toBool(),
             "判红却没写 moduleStatus，界面看不到红灯");
    const QString reason = node.getParam(QStringLiteral("lastError")).toString();
    qDebug().noquote() << QStringLiteral("P1-MISSING-TABLE recordNodeReason:") << reason;
    QVERIFY2(!reason.isEmpty(), "判红没有原因文本");
    QVERIFY2(reason.contains(QStringLiteral("检测结果未写入数据库")), qPrintable(reason));
    // 原因里必须带着"为什么"（而不是只有节点名）：这是现场能否自查的分界
    QVERIFY2(reason.contains(QStringLiteral("no such table: inspection_results")), qPrintable(reason));

    // ④ prepare 阶段：把本线程那条连接关掉再写一次。
    // 这才是现场那句 "Parameter count mismatch" 的形状：连接没打开 ⇒ prepare() 直接失败，
    // 而旧实现不查它的返回值 ⇒ exec() 拿 0 个占位符去配 4 个绑定值 ⇒ 报出一句与真因无关的话。
    // 连接名不硬编码：按"库文件路径"从登记表里找 AppDatabase 自己建的那条。
    QString ownerConn;
    const QStringList names = QSqlDatabase::connectionNames();
    for (const QString &n : names) {
        if (QSqlDatabase::database(n, false).databaseName() == path) {
            ownerConn = n;
            break;
        }
    }
    QVERIFY2(!ownerConn.isEmpty(), "没找到 AppDatabase 为这个库文件建的连接");
    QSqlDatabase::database(ownerConn, false).close();
    QVERIFY2(!db->saveInspectionResult(QStringLiteral("p1Flow2"), QStringLiteral("p1Node2"),
                                       true, QStringLiteral("3.0")),
             "连接已关闭，写库却报成功");
    const QString notOpen = db->lastDatabaseError();
    qDebug().noquote() << QStringLiteral("P1-NOT-OPEN single:") << notOpen;
    QVERIFY2(notOpen.contains(QStringLiteral("stage=prepare")), qPrintable(notOpen));
    QVERIFY2(notOpen.contains(QStringLiteral("open=no")), qPrintable(notOpen));
    QVERIFY2(!notOpen.contains(QStringLiteral("Parameter count mismatch")), qPrintable(notOpen));
    QSqlDatabase::database(ownerConn, false).open();   // 开回去：本腿排最后，但析构路径仍会读库
}

QTEST_MAIN(DatabaseTest)
#include "database_test.moc"
