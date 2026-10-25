// 中途停止的一轮不得写成「整轮 OK」（2026-10-25 复核 P2-1）。
//
// 现象：轮内收到停止请求时，主遍历 break 出节点循环，随后轮末汇总照常执行；只要已完成的
// 节点没有失败，m_roundHadFailure 仍为 false ⇒ 这半截流程被记成 passed=true 的整轮汇总行。
// 而良率监控（YieldMonitor）只读这类汇总行 ⇒ 一次人为中断被算成一件 OK 产品，良率虚高。
//
// 修法：轮内因停止而 break 时置 m_roundAborted，轮末跳过汇总行（已执行节点的记录照常落库，
// 追溯不丢）。本用例钉住两侧：
//   ① 被中止的轮：库里不得出现整轮汇总行；
//   ② 正常跑完的轮：汇总行必须存在且 passed=true（证明不是把汇总整个关掉了）。
#include "FlowScene.h"
#include "FlowExecutor.h"
#include "FlowExecutorToken.h"
#include "AppDatabase.h"
#include "NodeBase.h"
#include "NodeGraphicsItem.h"   // 选中节点定义子流程需要图元
#include "NodeRegistry.h"
#include "InspectionRecord.h"

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QPointF>

class RoundAbortSummaryTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void abortedRoundHasNoSummaryRow();
    void completeRoundHasSummaryRow();
    // P2-1 残留（2026-10-25 复核）：中止只覆盖主遍历时，循环体/子流程路径仍会写错汇总
    void abortedLoopAtTailHasNoSummaryRow();
    void abortedSubFlowIsNotReportedAsFailure();
};

namespace {

NodeBase *addById(FlowScene *scene, const char *typeId, const QPointF &pos)
{
    NodeBase *n = scene->createNodeByTypeIdOrName(QString::fromLatin1(typeId),
                                                  NodeBase::LOGIC,
                                                  QString::fromLatin1(typeId));
    if (n)
        scene->adoptNode(n, pos);
    return n;
}

int summaryRows(const QDateTime &from, const QDateTime &to)
{
    int n = 0;
    const QList<InspectionRecord> rows =
        AppDatabase::instance()->queryResults(from, to, 5000);
    for (const InspectionRecord &r : rows) {
        if (r.nodeName == kRoundSummaryNodeName)
            ++n;
    }
    return n;
}

QString dbPath()
{
    return QDir::tempPath() + QStringLiteral("/round_abort_summary_test.db");
}

} // namespace

void RoundAbortSummaryTest::initTestCase()
{
    registerAllNodes();
    QFile::remove(dbPath());
    QVERIFY2(AppDatabase::instance()->initialize(dbPath()),
             qPrintable(AppDatabase::instance()->lastDatabaseError()));
}

void RoundAbortSummaryTest::cleanupTestCase()
{
    AppDatabase::instance()->initialize(QString());
    QFile::remove(dbPath());
}

void RoundAbortSummaryTest::abortedRoundHasNoSummaryRow()
{
    FlowScene scene;
    // 第一个节点故意拖 2 秒：停止请求必然落在"轮内"（节点正在跑），
    // 这样才复现"break 之后轮末照常汇总"的路径。
    NodeBase *slow = addById(&scene, "DelayNode", QPointF(0, 0));
    NodeBase *sink = addById(&scene, "FormulaNode", QPointF(220, 0));
    QVERIFY(slow && sink);
    slow->setParam(QStringLiteral("delayMs"), 2000);
    sink->setParam(QStringLiteral("expression"), QStringLiteral("p0"));
    QVERIFY(scene.createConnection(slow->outputPorts().first(),
                                   sink->inputPorts().first(), true));

    auto token = FlowExecutorToken::create(new FlowExecutor());
    FlowExecutor &exec = *token->executor();
    exec.setFlowName(QStringLiteral("abortFlow"));
    exec.setFlowMode(FlowMode::SoftwareTrigger);
    exec.setFlowScene(&scene);
    exec.startExecution();
    QTest::qWait(500);          // 0.5s < 2s：停止落在轮内
    exec.stopExecution();
    bool finished = exec.wait(15000);
    if (!finished) {
        exec.stopExecution();
        exec.wait(3000);
    }
    exec.setFlowScene(nullptr);
    QCoreApplication::processEvents();

    const QDateTime from = QDateTime::currentDateTime().addDays(-1);
    const QDateTime to = QDateTime::currentDateTime().addSecs(60);
    QCOMPARE(summaryRows(from, to), 0);
}

void RoundAbortSummaryTest::completeRoundHasSummaryRow()
{
    FlowScene scene;
    NodeBase *gen = addById(&scene, "FormulaNode", QPointF(0, 0));
    NodeBase *sink = addById(&scene, "FormulaNode", QPointF(220, 0));
    QVERIFY(gen && sink);
    gen->setParam(QStringLiteral("expression"), QStringLiteral("1"));
    sink->setParam(QStringLiteral("expression"), QStringLiteral("p0"));
    QVERIFY(scene.createConnection(gen->outputPorts().first(),
                                   sink->inputPorts().first(), true));

    auto token = FlowExecutorToken::create(new FlowExecutor());
    FlowExecutor &exec = *token->executor();
    exec.setFlowName(QStringLiteral("completeFlow"));
    exec.setFlowMode(FlowMode::SoftwareTrigger);
    exec.setFlowScene(&scene);
    exec.startExecution();
    bool finished = exec.wait(15000);
    if (!finished) {
        exec.stopExecution();
        exec.wait(3000);
    }
    exec.setFlowScene(nullptr);
    QCoreApplication::processEvents();

    const QDateTime from = QDateTime::currentDateTime().addDays(-1);
    const QDateTime to = QDateTime::currentDateTime().addSecs(60);
    const QList<InspectionRecord> rows =
        AppDatabase::instance()->queryResults(from, to, 5000);
    int summary = 0;
    bool anyPassed = false;
    for (const InspectionRecord &r : rows) {
        if (r.nodeName == kRoundSummaryNodeName) {
            ++summary;
            anyPassed = r.passed;
        }
    }
    QCOMPARE(summary, 1);
    QVERIFY2(anyPassed, "正常跑完的一轮，汇总行必须是 passed=true");
}

// 循环节点恰好是拓扑末节点、轮内被停止：executeLoop 在迭代/节点边界直接 return，
// 主遍历因此自然结束（不走那两个 break 点）——轮末必须认这一轮为中止（P2-1 残留）。
void RoundAbortSummaryTest::abortedLoopAtTailHasNoSummaryRow()
{
    FlowScene scene;
    NodeBase *loop = addById(&scene, "LoopNode", QPointF(0, 0));
    NodeBase *body = addById(&scene, "DelayNode", QPointF(220, 0));
    QVERIFY(loop && body);
    loop->setParam(QStringLiteral("loopCount"), 3);
    body->setParam(QStringLiteral("delayMs"), 600);
    QVERIFY(scene.createConnection(loop->outputPorts().first(),
                                   body->inputPorts().first(), true));

    const int before = summaryRows(QDateTime::currentDateTime().addDays(-1),
                                   QDateTime::currentDateTime().addSecs(60));

    auto token = FlowExecutorToken::create(new FlowExecutor());
    FlowExecutor &exec = *token->executor();
    // 断言停止确实落在轮内（否则"没有汇总"可能只是因为压根没跑）
    bool bodyStarted = false;
    QObject::connect(&exec, &FlowExecutor::nodeExecuted, &exec,
                     [&bodyStarted, body](NodeBase *n, bool) {
                         if (n == body)
                             bodyStarted = true;
                     },
                     Qt::DirectConnection);
    exec.setFlowName(QStringLiteral("loopAbortFlow"));
    exec.setFlowMode(FlowMode::SoftwareTrigger);
    exec.setFlowScene(&scene);
    exec.startExecution();
    QTest::qWait(300);          // 落在第一轮迭代的 600ms 延时里
    exec.stopExecution();
    bool finished = exec.wait(15000);
    if (!finished) {
        exec.stopExecution();
        exec.wait(3000);
    }
    exec.setFlowScene(nullptr);
    QCoreApplication::processEvents();

    const int after = summaryRows(QDateTime::currentDateTime().addDays(-1),
                                  QDateTime::currentDateTime().addSecs(60));
    QVERIFY2(bodyStarted, "循环体没跑起来：本腿没验到'轮内中止'这条路径");
    QCOMPARE(after, before);   // 中止轮不得写汇总行
}

// 子流程成员执行中被人为停止：不得把调用点报成"执行失败"，也不得写汇总（P2-1 残留）。
void RoundAbortSummaryTest::abortedSubFlowIsNotReportedAsFailure()
{
    FlowScene scene;
    NodeBase *gen = addById(&scene, "FormulaNode", QPointF(0, 0));
    NodeBase *caller = addById(&scene, "SubFlowNode", QPointF(220, 0));
    NodeBase *m1 = addById(&scene, "DelayNode", QPointF(0, 220));
    NodeBase *m2 = addById(&scene, "DelayNode", QPointF(220, 220));
    QVERIFY(gen && caller && m1 && m2);
    gen->setParam(QStringLiteral("expression"), QStringLiteral("1"));
    m1->setParam(QStringLiteral("delayMs"), 600);
    m2->setParam(QStringLiteral("delayMs"), 10);
    QVERIFY(scene.createConnection(gen->outputPorts().first(),
                                   caller->inputPorts().first(), true));
    QVERIFY(scene.createConnection(m1->outputPorts().first(),
                                   m2->inputPorts().first(), true));
    scene.clearSelection();
    for (NodeBase *n : { m1, m2 }) {
        if (NodeGraphicsItem *item = scene.getGraphicsItemForNode(n))
            item->setSelected(true);
    }
    QString err;
    QVERIFY2(scene.defineSubFlowFromSelection(QStringLiteral("慢段"), &err), qPrintable(err));
    caller->setParam(QStringLiteral("subFlowName"), QStringLiteral("慢段"));

    const int before = summaryRows(QDateTime::currentDateTime().addDays(-1),
                                   QDateTime::currentDateTime().addSecs(60));

    auto token = FlowExecutorToken::create(new FlowExecutor());
    FlowExecutor &exec = *token->executor();
    bool callerFailed = false;
    QStringList errors;
    QObject::connect(&exec, &FlowExecutor::nodeExecuted, &exec,
                     [&callerFailed, caller](NodeBase *n, bool ok) {
                         if (n == caller && !ok)
                             callerFailed = true;
                     },
                     Qt::DirectConnection);
    QObject::connect(&exec, &FlowExecutor::executionError, &exec,
                     [&errors](const QString &e) { errors << e; }, Qt::DirectConnection);

    exec.setFlowName(QStringLiteral("subAbortFlow"));
    exec.setFlowMode(FlowMode::SoftwareTrigger);
    exec.setFlowScene(&scene);
    exec.startExecution();
    QTest::qWait(300);          // 落在子流程成员 m1 的 600ms 延时里
    exec.stopExecution();
    bool finished = exec.wait(15000);
    if (!finished) {
        exec.stopExecution();
        exec.wait(3000);
    }
    exec.setFlowScene(nullptr);
    QCoreApplication::processEvents();

    // 中止不得报成节点失败，也不得出现"节点 … 执行失败"的错误
    QVERIFY2(!callerFailed, "子流程被中止时调用点被记成执行失败（P2-1 残留）");
    for (const QString &e : errors) {
        QVERIFY2(!e.contains(QStringLiteral("执行失败")),
                 qPrintable(QStringLiteral("中止被报成了执行失败：%1").arg(e)));
    }
    // 同样不得写汇总
    const int after = summaryRows(QDateTime::currentDateTime().addDays(-1),
                                  QDateTime::currentDateTime().addSecs(60));
    QCOMPARE(after, before);
}

QTEST_MAIN(RoundAbortSummaryTest)
#include "round_abort_summary_test.moc"
