// 每线程数据库连接的生命周期（2026-10-25 复核 P3-10）。
//
// 现象：AppDatabase::threadDatabase() 按线程 ID 命名连接（visionflow_conn_<tid>），
// 但从不 removeDatabase。线程退出后 ID 可能被复用：新线程 contains(name) 为真，拿到的却是
// 已死线程建的连接（Qt 只告警，随后查询失败）——长期运行/反复起停流程时是"连到旧库"一类
// 难查现象。
//
// 修法：建连时给当前 QThread 挂 finished，线程结束时摘除本线程的连接。
// 本用例：一个 QThread 写库成功 → 退出 → 它的连接必须已从注册表移除。
#include "AppDatabase.h"
#include "InspectionRecord.h"

#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QThread>

class DbThreadConnectionCleanupTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void workerThreadConnectionIsRemovedOnExit();
};

namespace {

/// 在自己的线程里写一条记录，并记下本线程连接名（必须在 run() 内取 currentThreadId）
class DbWriter : public QThread
{
public:
    QString connName;
    bool writeOk = false;
    QString error;

protected:
    void run() override
    {
        connName = QStringLiteral("visionflow_conn_%1")
                       .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
        writeOk = AppDatabase::instance()->saveInspectionResult(
            QStringLiteral("cleanupFlow"), QStringLiteral("cleanupNode"), true, QStringLiteral("1"));
        error = AppDatabase::instance()->lastDatabaseError();
    }
};

} // namespace

void DbThreadConnectionCleanupTest::initTestCase()
{
    const QString path = QDir::tempPath() + QStringLiteral("/db_thread_conn_cleanup_test.db");
    QFile::remove(path);
    QVERIFY2(AppDatabase::instance()->initialize(path),
             qPrintable(AppDatabase::instance()->lastDatabaseError()));
}

void DbThreadConnectionCleanupTest::cleanupTestCase()
{
    AppDatabase::instance()->initialize(QString());
    QFile::remove(QDir::tempPath() + QStringLiteral("/db_thread_conn_cleanup_test.db"));
}

void DbThreadConnectionCleanupTest::workerThreadConnectionIsRemovedOnExit()
{
    DbWriter w;
    w.start();
    QVERIFY2(w.wait(15000), "写库线程没在 15 秒内退出");
    QVERIFY2(w.writeOk, qPrintable(QStringLiteral("线程内写库失败：%1").arg(w.error)));

    // 核心断言：线程已退出 ⇒ 它的连接必须已摘除（否则 ID 复用时新线程拿到旧连接）
    QVERIFY2(!w.connName.isEmpty(), "没拿到本线程连接名（用例前提不成立）");
    QVERIFY2(!QSqlDatabase::contains(w.connName),
             qPrintable(QStringLiteral("线程退出后连接 %1 仍挂在注册表里（P3-10）").arg(w.connName)));
}

QTEST_MAIN(DbThreadConnectionCleanupTest)
#include "db_thread_connection_cleanup_test.moc"
