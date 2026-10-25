// executeNode 异常出口的收口（2026-10-25 复核 P2-5）。
//
// 缺陷：executeNode 的 try 里，"失败分支"会清三类执行缓存并置 m_roundHadFailure；但异常
// 路径（catch）只把 success 置假。若异常发生在输出已发布之后：
//   · 三类缓存里留着失败节点的半成品输出 ⇒ 下游可能读到脏数据；
//   · m_roundHadFailure 不置位 ⇒ 轮末汇总仍写 passed=true ⇒ 良率虚高（报了错却算 OK）。
// 触发面窄：NodeBase::execute 已吞掉算子自身异常，能逃到 catch 的多是直连槽、缓存写入、
// 落库、全局变量——本用例用"直连槽抛异常"这条真实路径来触发。
//
// 断言（用户可见的一半）：异常节点的这一轮，整轮汇总必须是 NG，不能是 OK。
#include "FlowScene.h"
#include "FlowExecutor.h"
#include "FlowExecutorToken.h"
#include "AppDatabase.h"
#include "NodeBase.h"
#include "NodeRegistry.h"
#include "InspectionRecord.h"

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QPointF>
#include <stdexcept>

class NodeExceptionExitTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void exceptionInDirectSlotMakesRoundNg();
    void controlRoundWithoutExceptionIsOk();
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

QString dbPath()
{
    return QDir::tempPath() + QStringLiteral("/node_exception_exit_test.db");
}

/// 跑一轮，返回库里全部整轮汇总行（调用方按 before/after 差分，避免用例间串味）
QList<InspectionRecord> roundSummaries(FlowExecutor &exec, FlowScene &scene)
{
    exec.setFlowName(QStringLiteral("excFlow"));
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

    QList<InspectionRecord> rows;
    const QList<InspectionRecord> all =
        AppDatabase::instance()->queryResults(QDateTime::currentDateTime().addDays(-1),
                                              QDateTime::currentDateTime().addSecs(60), 5000);
    for (const InspectionRecord &r : all) {
        if (r.nodeName == kRoundSummaryNodeName)
            rows.append(r);
    }
    return rows;
}

int summaryCount()
{
    int n = 0;
    const QList<InspectionRecord> all =
        AppDatabase::instance()->queryResults(QDateTime::currentDateTime().addDays(-1),
                                              QDateTime::currentDateTime().addSecs(60), 5000);
    for (const InspectionRecord &r : all) {
        if (r.nodeName == kRoundSummaryNodeName)
            ++n;
    }
    return n;
}

} // namespace

void NodeExceptionExitTest::initTestCase()
{
    registerAllNodes();
    QFile::remove(dbPath());
    QVERIFY2(AppDatabase::instance()->initialize(dbPath()),
             qPrintable(AppDatabase::instance()->lastDatabaseError()));
}

void NodeExceptionExitTest::cleanupTestCase()
{
    AppDatabase::instance()->initialize(QString());
    QFile::remove(dbPath());
}

void NodeExceptionExitTest::exceptionInDirectSlotMakesRoundNg()
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

    // 直连槽：目标节点一发 nodeExecuted 就抛 —— 异常落在 executeNode 的 try 内
    // （emit 就在 try 里），由 catch 收口。只对第一个节点抛，第二节点照常。
    bool thrown = false;
    QObject::connect(&exec, &FlowExecutor::nodeExecuted, &exec,
                     [&thrown, gen](NodeBase *n, bool ok) {
                         if (!thrown && n == gen) {
                             thrown = true;
                             throw std::runtime_error("direct slot boom");
                         }
                     },
                     Qt::DirectConnection);

    const QList<InspectionRecord> summaries = roundSummaries(exec, scene);
    QVERIFY2(thrown, "直连槽没有抛出去（用例前提不成立）");
    QCOMPARE(summaries.size(), 1);
    QVERIFY2(!summaries.first().passed,
             "异常节点的这一轮被写成整轮 OK：异常出口没有置轮失败位（P2-5）");

    // P3-1 残留（2026-10-25 复核）：异常出口还要与失败分支同口径——
    // ① 清输出端口（否则 propagateData 回落到 getOutputData 仍读到半成品）
    // ② 失败节点落库（否则追溯里只剩一条 NG 汇总，看不出是哪个算子）
    QVERIFY2(gen->getOutputData(0).isNull(),
             "异常节点的输出端口未清空：下游可能读到半成品输出（P3-1 残留）");
    bool genRecorded = false;
    const QList<InspectionRecord> all =
        AppDatabase::instance()->queryResults(QDateTime::currentDateTime().addDays(-1),
                                              QDateTime::currentDateTime().addSecs(60), 5000);
    for (const InspectionRecord &r : all) {
        if (r.nodeName == gen->fullName() && !r.passed)
            genRecorded = true;
    }
    QVERIFY2(genRecorded, "异常节点没有落库：追溯只剩一条 NG 汇总（P3-1 残留）");
}

void NodeExceptionExitTest::controlRoundWithoutExceptionIsOk()
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
    const int before = summaryCount();
    const QList<InspectionRecord> summaries = roundSummaries(exec, scene);
    QCOMPARE(summaries.size(), before + 1);   // 本轮只该多出 exactly 一行
    // 取 id 最大的那一行（不依赖查询排序假设）：本轮新写的汇总
    const InspectionRecord *newest = nullptr;
    for (const InspectionRecord &r : summaries) {
        if (!newest || r.id > newest->id)
            newest = &r;
    }
    QVERIFY2(newest && newest->passed, "对照轮（无异常）必须是 OK，否则说明判据本身坏了");
}

QTEST_MAIN(NodeExceptionExitTest)
#include "node_exception_exit_test.moc"
