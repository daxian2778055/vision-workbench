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
#include <QtTest/QtTest>
#include <QObject>
#include <QThread>
#include <QTemporaryDir>
#include <QDateTime>
#include <QList>

#include "AppDatabase.h"

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

QTEST_MAIN(DatabaseTest)
#include "database_test.moc"
