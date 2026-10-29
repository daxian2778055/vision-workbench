// 训练环境探测（2026-10-29）：产线机没装训练环境时必须"说清缺什么"，不能装作可用。
#include "TrainingRunner.h"

#include <QStandardPaths>
#include <QtTest/QtTest>

class TrainingRunnerTest : public QObject
{
    Q_OBJECT
private slots:
    void probeRejectsEmptyPath();
    void probeReportsMissingPython();
    void probeOnSystemPythonGivesReadableResult();
    void runnerIsNotRunningBeforeStart();
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

QTEST_MAIN(TrainingRunnerTest)
#include "training_runner_test.moc"
