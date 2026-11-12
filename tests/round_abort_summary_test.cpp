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
#include <atomic>

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
#include <QPointF>
#include <QTemporaryDir>

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
    // P2-A/P2-B（2026-10-25 复核第二批）：真失败不当中止；局部执行不污染下一轮
    void subFlowNestedLoopFailureIsStillNg();
    void localExecutionFailureDoesNotPoisonNextRound();

private:
    /// 每次运行独占一个临时目录（2026-11-12）：旧实现用固定的
    /// QDir::tempPath()/round_abort_summary_test.db，Windows 上若 SQLite 句柄未释放，
    /// QFile::remove 会静默失败，上一轮的汇总行残留；而 summaryRows 用
    /// addDays(-1) 的窗口会把它们一起查出来 ⇒ -j4 下低频偶发红。
    QTemporaryDir m_tempDir;
    QString dbPath() const
    {
        return m_tempDir.filePath(QStringLiteral("round_abort.db"));
    }
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

} // namespace

void RoundAbortSummaryTest::initTestCase()
{
    registerAllNodes();
    QVERIFY2(m_tempDir.isValid(), "无法为本次运行创建唯一的临时数据库目录");
    // 不再 QFile::remove：目录是本次运行独占的，且析构时自动清理，
    // 从根上排除「上一轮 DB 残留 + Windows 文件占用导致删不掉」的干扰。
    QVERIFY2(AppDatabase::instance()->initialize(dbPath()),
             qPrintable(AppDatabase::instance()->lastDatabaseError()));
}

void RoundAbortSummaryTest::cleanupTestCase()
{
    AppDatabase::instance()->initialize(QString());
    // m_tempDir 析构时自动删除整个唯一目录，不需要显式 remove
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
    // 2026-11-12 flaky 根因：旧版监听 nodeExecuted（节点**执行完**才 emit）置
    // slowRan，可本腿验证的是「停止落在轮内、节点还在跑」——stop 越成功，
    // slowRan 越可能是 false，断言语义自相矛盾。build/Testing 里两次失败日志
    // 都指向这一条（'slowRan' returned FALSE），高负载下稳定翻车。
    // 改为监听 executionStarted：整轮启动、在第一个节点之前 emit，不受中止影响；
    // DirectConnection 在工作线程写、主线程读，用 atomic 消掉数据竞争。
    std::atomic_bool roundStarted{false};
    QObject::connect(&exec, &FlowExecutor::executionStarted, &exec,
                     [&roundStarted]() { roundStarted.store(true); },
                     Qt::DirectConnection);
    const int beforeRows = summaryRows(QDateTime::currentDateTime().addDays(-1),
                                       QDateTime::currentDateTime().addSecs(60));
    exec.setFlowName(QStringLiteral("abortFlow"));
    exec.setFlowMode(FlowMode::SoftwareTrigger);
    exec.setFlowScene(&scene);
    exec.startExecution();
    // 先确定性等整轮启动：消掉「启动延迟」对 500ms 窗口的侵蚀，
    // 同时这一句本身承担「防空转」职责（流程真的启动过才继续）。
    QTRY_VERIFY_WITH_TIMEOUT(roundStarted.load(), 5000);
    QTest::qWait(500);          // 0.5s < 2s：此时 slow 仍在延时中，stop 落在轮内
    exec.stopExecution();
    bool finished = exec.wait(15000);
    if (!finished) {
        exec.stopExecution();
        exec.wait(3000);
    }
    exec.setFlowScene(nullptr);
    QCoreApplication::processEvents();

    // 不再断言 slowRan（见上方注释：它与「轮内中止」语义相反）。
    // 上面的 QTRY 已保证流程确实启动过，本腿不会空转。
    // 差分断言：这一轮不得新增整轮汇总行（绝对值会被其它用例/残留干扰）
    const int afterRows = summaryRows(QDateTime::currentDateTime().addDays(-1),
                                      QDateTime::currentDateTime().addSecs(60));
    QCOMPARE(afterRows, beforeRows);
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
    std::atomic_bool bodyStarted{false};   // 同上：工作线程写、主线程读
    QObject::connect(&exec, &FlowExecutor::nodeExecuted, &exec,
                     [&bodyStarted, body](NodeBase *n, bool) {
                         if (n == body)
                             bodyStarted.store(true);
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
    QVERIFY2(bodyStarted.load(), "循环体没跑起来：本腿没验到'轮内中止'这条路径");
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
    // 2 秒窗口：原 600ms 在 ctest -j4 并行负载下容易被固定等待错过，
    // 使停止落在轮末、反而写出正常汇总行。窗口放宽到 2s 后抖动不再影响。
    m1->setParam(QStringLiteral("delayMs"), 2000);
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
    std::atomic_bool callerFailed{false};   // 工作线程写、主线程读
    // worker 线程（DirectConnection）写、主线程读：用 atomic 消掉数据竞争。
    std::atomic<bool> memberStarted { false };   // 断言停止确实落在"子流程正在执行"里
    QStringList errors;
    QObject::connect(&exec, &FlowExecutor::nodeExecuted, &exec,
                     [&callerFailed, &memberStarted, caller, m1](NodeBase *n, bool ok) {
                         if (n == m1)
                             memberStarted = true;
                         if (n == caller && !ok)
                             callerFailed.store(true);
                     },
                     Qt::DirectConnection);
    QObject::connect(&exec, &FlowExecutor::executionError, &exec,
                     [&errors](const QString &e) { errors << e; }, Qt::DirectConnection);

    exec.setFlowName(QStringLiteral("subAbortFlow"));
    exec.setFlowMode(FlowMode::SoftwareTrigger);
    exec.setFlowScene(&scene);
    exec.startExecution();
    // 不能轮询等 memberStarted：该标志在 nodeExecuted（m1 执行完）才置位，
    // 等它等于让流程跑完再停，会写出正常汇总行。保持固定短等待，靠 m1 的
    // 2 秒延时窗口保证「停止落在轮内」；实测单跑与 -j4 并行都稳定。
    QTest::qWait(300);
    exec.stopExecution();
    bool finished = exec.wait(15000);
    if (!finished) {
        exec.stopExecution();
        exec.wait(3000);
    }
    exec.setFlowScene(nullptr);
    QCoreApplication::processEvents();

    QVERIFY2(memberStarted, "子流程成员没跑起来：本腿没验到'轮内中止'这条路径");
    // 中止不得报成节点失败，也不得出现"节点 … 执行失败"的错误
    QVERIFY2(!callerFailed.load(), "子流程被中止时调用点被记成执行失败（P2-1 残留）");
    for (const QString &e : errors) {
        QVERIFY2(!e.contains(QStringLiteral("执行失败")),
                 qPrintable(QStringLiteral("中止被报成了执行失败：%1").arg(e)));
    }
    // 同样不得写汇总；且中止的调用点不得落 OK 行（P3-1：中止不是成功）
    const int after = summaryRows(QDateTime::currentDateTime().addDays(-1),
                                  QDateTime::currentDateTime().addSecs(60));
    QCOMPARE(after, before);
    const QList<InspectionRecord> all =
        AppDatabase::instance()->queryResults(QDateTime::currentDateTime().addDays(-1),
                                              QDateTime::currentDateTime().addSecs(60), 5000);
    for (const InspectionRecord &r : all) {
        QVERIFY2(!(r.nodeName == caller->fullName() && r.passed),
                 "中止的调用点落了 passed=1 的节点行（P3-1：中止不是成功）");
    }
    // 运行期腿：中止的调用点不得被标成"输出有效/可复用"（2026-10-25 复核 P3-2）——
    // 此前只有"没有 OK 行"的断言，有效位这条修复没有腿。
    QVERIFY2(!exec.isOutputMarkedValid(caller),
             "中止的调用点被标成了输出有效：局部执行的复用判定会把它当成有结果（P3-2）");

    // 第三态腿（2026-10-28 P3-14）：中止的调用点画布不得显示绿色（成功）
    QVERIFY2(caller->executionAborted(),
             "中止的调用点没有标记为中止：画布会显示绿色（跑成功了）");
}

// P2-A（2026-10-25 复核第二批）：子流程里嵌套的循环体**真失败**，必须还是 NG——
// 不能因为 executeLoop 失败分支也会置 Stopped，就被当中止抹掉。
void RoundAbortSummaryTest::subFlowNestedLoopFailureIsStillNg()
{
    FlowScene scene;
    NodeBase *gen = addById(&scene, "FormulaNode", QPointF(0, 0));
    NodeBase *caller = addById(&scene, "SubFlowNode", QPointF(220, 0));
    NodeBase *loop = addById(&scene, "LoopNode", QPointF(0, 220));
    NodeBase *bad = addById(&scene, "SubFlowNode", QPointF(220, 220));
    QVERIFY(gen && caller && loop && bad);
    gen->setParam(QStringLiteral("expression"), QStringLiteral("1"));
    loop->setParam(QStringLiteral("loopCount"), 1);
    // 循环体里的节点必然失败（引用不存在的子流程）
    bad->setParam(QStringLiteral("subFlowName"), QStringLiteral("不存在"));
    QVERIFY(scene.createConnection(gen->outputPorts().first(),
                                   caller->inputPorts().first(), true));
    QVERIFY(scene.createConnection(loop->outputPorts().first(),
                                   bad->inputPorts().first(), true));

    scene.clearSelection();
    for (NodeBase *n : { loop, bad }) {
        if (NodeGraphicsItem *item = scene.getGraphicsItemForNode(n))
            item->setSelected(true);
    }
    QString err;
    QVERIFY2(scene.defineSubFlowFromSelection(QStringLiteral("坏循环段"), &err), qPrintable(err));
    caller->setParam(QStringLiteral("subFlowName"), QStringLiteral("坏循环段"));

    auto token = FlowExecutorToken::create(new FlowExecutor());
    FlowExecutor &exec = *token->executor();
    std::atomic_bool callerFailed{false};   // 工作线程写、主线程读
    QHash<QString, bool> nodeStates;   // 诊断：每个节点最后一次 nodeExecuted
    QStringList errors;
    QObject::connect(&exec, &FlowExecutor::nodeExecuted, &exec,
                     [&callerFailed, &nodeStates, caller](NodeBase *n, bool ok) {
                         if (n)
                             nodeStates.insert(n->fullName(), ok);
                         if (n == caller && !ok)
                             callerFailed.store(true);
                     },
                     Qt::DirectConnection);
    QObject::connect(&exec, &FlowExecutor::executionError, &exec,
                     [&errors](const QString &e) { errors << e; }, Qt::DirectConnection);

    const int before = summaryRows(QDateTime::currentDateTime().addDays(-1),
                                   QDateTime::currentDateTime().addSecs(60));
    exec.setFlowName(QStringLiteral("nestedLoopFailFlow"));
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

    // 真失败：调用点判失败、写 NG 汇总（不得被当成中止而跳过汇总/调用点变绿）
    {
        // 诊断：把每个节点最后一次 nodeExecuted 打出来（谁绿谁红一目了然）
        QStringList stateList;
        for (auto it = nodeStates.constBegin(); it != nodeStates.constEnd(); ++it)
            stateList << (it.key() + QLatin1String("=") + (it.value() ? "ok" : "fail"));
        QVERIFY2(callerFailed.load(),
                 qPrintable(QStringLiteral("子流程内循环体失败时调用点没有判失败（P2-A）"
                                           " finished=%1 nodes=[%2] errors=[%3]")
                                .arg(finished ? 1 : 0)
                                .arg(stateList.join(QStringLiteral(" , ")))
                                .arg(errors.join(QStringLiteral(" | ")))));

        // 本腿必须真的走到"嵌套循环体失败"这条路径（2026-10-25 复核 P3-3）：
        // 若内部循环体又被判成"定义损坏"，调用点同样失败、同样写 NG —— 上面那两条断言
        // 照样绿，等于没钉住 P2-A × P3-8 的冲突修复。这里用"循环体节点真的执行过"+
        // "错误里是循环体失败而不是定义损坏"两条把它钉死。
        QVERIFY2(nodeStates.contains(bad->fullName()),
                 qPrintable(QStringLiteral("循环体节点没执行（多半又被判成定义损坏）：nodes=[%1]")
                                .arg(stateList.join(QStringLiteral(" , ")))));
        bool sawBrokenDef = false;
        for (const QString &e : errors) {
            if (e.contains(QStringLiteral("定义损坏")))
                sawBrokenDef = true;
        }
        QVERIFY2(!sawBrokenDef,
                 qPrintable(QStringLiteral("定义被判成损坏，本腿没验到嵌套循环路径：%1")
                                .arg(errors.join(QStringLiteral(" | ")))));
        // 循环**体内**的节点必须真的执行过且判失败（说明失败确实来自那次嵌套执行）。
        // 注意循环节点本身在子流程里是 executeLoop 直接调度的，不走 executeNode，故不发
        // nodeExecuted——不能拿它当判据。
        QVERIFY2(nodeStates.contains(bad->fullName()) && !nodeStates.value(bad->fullName()),
                 qPrintable(QStringLiteral("循环体节点没有执行/没判失败：nodes=[%1]")
                                .arg(stateList.join(QStringLiteral(" , ")))));
    }
    const int after = summaryRows(QDateTime::currentDateTime().addDays(-1),
                                  QDateTime::currentDateTime().addSecs(60));
    QCOMPARE(after, before + 1);   // 必须写汇总行
    const QList<InspectionRecord> all =
        AppDatabase::instance()->queryResults(QDateTime::currentDateTime().addDays(-1),
                                              QDateTime::currentDateTime().addSecs(60), 5000);
    bool newestIsNg = false;
    int newestId = -1;
    for (const InspectionRecord &r : all) {
        if (r.nodeName == kRoundSummaryNodeName && r.id > newestId) {
            newestId = r.id;
            newestIsNg = !r.passed;
        }
    }
    QVERIFY2(newestIsNg, "真失败的那一轮汇总不是 NG：被当中止抹掉了（P2-A）");
}

// P2-B（2026-10-25 复核）：同步局部执行（executeUpTo/executeFrom）失败后，
// 下一次完整轮即使全绿，汇总仍会写 NG——轮级标志被局部执行污染且没人复位。
void RoundAbortSummaryTest::localExecutionFailureDoesNotPoisonNextRound()
{
    // ① 局部执行一个必然失败的算子
    FlowScene badScene;
    NodeBase *genBad = addById(&badScene, "FormulaNode", QPointF(0, 0));
    NodeBase *bad = addById(&badScene, "SubFlowNode", QPointF(220, 0));
    QVERIFY(genBad && bad);
    genBad->setParam(QStringLiteral("expression"), QStringLiteral("1"));
    bad->setParam(QStringLiteral("subFlowName"), QStringLiteral("不存在"));
    QVERIFY(badScene.createConnection(genBad->outputPorts().first(),
                                      bad->inputPorts().first(), true));

    auto token = FlowExecutorToken::create(new FlowExecutor());
    FlowExecutor &exec = *token->executor();
    exec.setFlowName(QStringLiteral("localExecFlow"));
    exec.setFlowScene(&badScene);
    exec.executeUpTo(bad);   // 同步局部执行：失败置 m_roundHadFailure，但没有轮末
    exec.setFlowScene(nullptr);
    QCoreApplication::processEvents();

    // ② 换一个全绿的场景跑完整轮：汇总必须 OK
    FlowScene goodScene;
    NodeBase *gen = addById(&goodScene, "FormulaNode", QPointF(0, 0));
    NodeBase *sink = addById(&goodScene, "FormulaNode", QPointF(220, 0));
    QVERIFY(gen && sink);
    gen->setParam(QStringLiteral("expression"), QStringLiteral("1"));
    sink->setParam(QStringLiteral("expression"), QStringLiteral("p0"));
    QVERIFY(goodScene.createConnection(gen->outputPorts().first(),
                                       sink->inputPorts().first(), true));

    exec.setFlowName(QStringLiteral("goodRoundAfterLocal"));
    exec.setFlowMode(FlowMode::SoftwareTrigger);
    exec.setFlowScene(&goodScene);
    exec.startExecution();
    bool finished = exec.wait(15000);
    if (!finished) {
        exec.stopExecution();
        exec.wait(3000);
    }
    exec.setFlowScene(nullptr);
    QCoreApplication::processEvents();

    const QList<InspectionRecord> all =
        AppDatabase::instance()->queryResults(QDateTime::currentDateTime().addDays(-1),
                                              QDateTime::currentDateTime().addSecs(60), 5000);
    int newestId = -1;
    bool newestIsOk = false;
    for (const InspectionRecord &r : all) {
        if (r.nodeName == kRoundSummaryNodeName && r.id > newestId) {
            newestId = r.id;
            newestIsOk = r.passed;
        }
    }
    QVERIFY2(newestIsOk,
             "局部执行失败后，下一次全绿轮的汇总仍写成 NG：轮级标志被局部执行污染（P2-B）");
}

QTEST_MAIN(RoundAbortSummaryTest)
#include "round_abort_summary_test.moc"
