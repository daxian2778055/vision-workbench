// 训练环境探测（2026-10-29）：产线机没装训练环境时必须"说清缺什么"，不能装作可用。
#include "TrainingRunner.h"

#include <QStandardPaths>
#include <QElapsedTimer>
#include <QtTest/QtTest>

class TrainingRunnerTest : public QObject
{
    Q_OBJECT
private slots:
    void probeRejectsEmptyPath();
    void probeReportsMissingPython();
    void probeOnSystemPythonGivesReadableResult();
    void runnerIsNotRunningBeforeStart();
    void stubbornProcessIsKilledAfterGrace();
    void restartWithinWindowIsNotKilled();
};

void TrainingRunnerTest::probeRejectsEmptyPath()
{
    const auto st = TrainingRunner::probe(QString());
    QVERIFY(!st.ok);
    QVERIFY2(st.detail.contains(QStringLiteral("未配置")), qPrintable(st.detail));
}

void TrainingRunnerTest::probeReportsMissingPython()
{
    const auto st = TrainingRunner::probe(QStringLiteral("C:/__no_such_python__/python.exe"));
    QVERIFY(!st.ok);
    QVERIFY2(st.detail.contains(QStringLiteral("不存在")), qPrintable(st.detail));
}

void TrainingRunnerTest::probeOnSystemPythonGivesReadableResult()
{
    // 本机 python 未必装了 torch：两种情况都接受，但**必须给可读结论**，
    // 不能"既不说可用也不说原因"。这一条正是产线机现场的判据。
    const QString py = QStandardPaths::findExecutable(QStringLiteral("python"));
    if (py.isEmpty())
        QSKIP("本机没有 python，跳过");
    const auto st = TrainingRunner::probe(py, 30000);
    QVERIFY2(!st.detail.isEmpty(), "探测没有给出任何说明");
    if (!st.ok)
        QVERIFY2(st.detail.contains(QStringLiteral("torch"))
                     || st.detail.contains(QStringLiteral("超时"))
                     || st.detail.contains(QStringLiteral("无法启动")),
                 qPrintable(st.detail));
}

void TrainingRunnerTest::runnerIsNotRunningBeforeStart()
{
    TrainingRunner r;
    QVERIFY(!r.running());
    r.cancel();               // 未启动时取消不得崩
    QVERIFY(!r.running());
}

void TrainingRunnerTest::stubbornProcessIsKilledAfterGrace()
{
    // 顽固进程（忽略 WM_CLOSE）5 秒后必须被强杀（2026-10-29 P2-3/P3-2）
    TrainingRunner r;
    bool done = false;
    connect(&r, &TrainingRunner::finished, this, [&done](bool, const QString &) { done = true; });
    QStringList seen;
    connect(&r, &TrainingRunner::logLine, this,
            [&seen](const QString &l) { seen << l; });
    // ping 是控制台进程，terminate(WM_CLOSE) 对它无效
    r.start(QStringLiteral("ping"), {QStringLiteral("-n"), QStringLiteral("60"), QStringLiteral("127.0.0.1")},
            QString());
    QVERIFY(r.running());
    r.requestCancel();
    QElapsedTimer t;
    t.start();
    while (!done && t.elapsed() < 15000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QVERIFY2(done, "15 秒内没有结束：5 秒强杀兜底没开火");
    QVERIFY2(!r.running(), "强杀后进程仍在跑");
    // 下界（2026-10-29 复核 P3-2）：若 terminate() 将来对某类进程生效，这条腿会在
    // 1 秒内变绿——定时器坏了也看不出来。强杀路径必须走满约 5 秒。
    QVERIFY2(t.elapsed() >= 4500,
             qPrintable(QStringLiteral("结束太快（%1 ms）：terminate 生效了或定时器没走满 5 秒，"
                                        "这条腿守的是 5 秒强杀路径").arg(t.elapsed())));
    // 直接证明定时器回调走到了（比耗时断言更贴机制）
    bool sawKillLog = false;
    for (const QString &l : seen) {
        if (l.contains(QStringLiteral("强制结束")))
            sawKillLog = true;
    }
    QVERIFY2(sawKillLog, "日志里没有 5 秒强杀记录：定时器回调没走");
}

void TrainingRunnerTest::restartWithinWindowIsNotKilled()
{
    // 2026-10-29 P2-1 回归：取消后 5 秒内重启，新进程不得被上一趟的定时器误杀
    TrainingRunner r;
    // 第一趟：约 1 秒后自然结束，结束前请求取消（= 复现 P2-1 的场景）
    r.start(QStringLiteral("ping"), {QStringLiteral("-n"), QStringLiteral("1"), QStringLiteral("127.0.0.1")},
            QString());
    r.requestCancel();
    QElapsedTimer t0;
    t0.start();
    while (r.running() && t0.elapsed() < 4000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QVERIFY2(!r.running(), "第一趟没有按时结束");
    // 第二趟：在上一趟 5 秒强杀窗口内重启 30 秒长任务
    r.start(QStringLiteral("ping"), {QStringLiteral("-n"), QStringLiteral("30"), QStringLiteral("127.0.0.1")},
            QString());
    QVERIFY2(r.running(), "第二趟没起来");
    QTest::qWait(5500);   // 跨过上一趟的 5 秒强杀窗口
    QVERIFY2(r.running(), "第二趟被上一趟遗留的强杀定时器误杀（P2-1 回归）");
    r.cancel();
    // 收尾：等它被杀干净，别把 ping 留给下一条用例
    QElapsedTimer t;
    t.start();
    while (r.running() && t.elapsed() < 15000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

QTEST_MAIN(TrainingRunnerTest)
#include "training_runner_test.moc"
