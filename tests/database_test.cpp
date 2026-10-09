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
#include <QList>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>   // lastError().text()：只含前两个头时 QSqlError 只有前向声明

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
    QVERIFY(hash.contains(QLatin1Char(':')));
    QVERIFY(AppDatabase::verifyPassword(QStringLiteral("p@ssw0rd"), hash));
    QVERIFY(!AppDatabase::verifyPassword(QStringLiteral("wrong"), hash));
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
