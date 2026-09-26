// FR15.10 运行期子流程（方案 B：方案内命名子图）契约测试。
//
// 对应 docs/子流程复用设计草案.md 的验收口径：
//  ① 定义校验：<2 算子 / 重名 / 成员已属于其它定义 / 跨边界连线 / 入口或出口不唯一 → 显式失败；
//  ② 定义随方案 extras 往返一致；
//  ③ 端到端执行：成员被主遍历跳过、由调用点内联调度恰好一遍（执行次数是硬指标），
//     出口结果回写调用点输出端口并传到下游；
//  ④ 隔离：两个调用点各调同一子流程一次，各自结果互不污染（草案"命名空间回归"条目）；
//  ⑤ 递归被拒绝且给出清晰原因；未定义名字可见报错（不静默跑旧逻辑）。
#include "FlowScene.h"
#include "FlowExecutor.h"
#include "NodeBase.h"
#include "NodeRegistry.h"
#include "NodeGraphicsItem.h"
#include "DataObject.h"
#include "Port.h"
#include "SubFlowDefs.h"

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QPointF>

class SubFlowTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void testDefineValidation();
    void testDefJsonRoundTrip();
    void testExecutionEndToEnd();
    void testTwoCallersIsolation();
    void testRecursionRejected();
    void testUndefinedRejected();
    void testUnfedSubFlowCallerStaysGreenAndEmpty();
};

namespace {

NodeBase *addNodeById(FlowScene *scene, const QString &typeId, const QPointF &pos)
{
    // createNodeByTypeIdOrName 只创建 + init()（建端口），**不入场景**——
    // 必须再走 adoptNode（挂图元 + m_nodeItems + nodeAdded），否则选择/定义都看不到它
    NodeBase *n = scene->createNodeByTypeIdOrName(typeId, NodeBase::LOGIC, typeId);
    if (n)
        scene->adoptNode(n, pos);
    return n;
}

NodeBase *addFormula(FlowScene *scene, const QString &expression, const QPointF &pos)
{
    NodeBase *n = addNodeById(scene, QStringLiteral("FormulaNode"), pos);
    if (n)
        n->setParam(QStringLiteral("expression"), expression);
    return n;
}

NodeBase *addDelay(FlowScene *scene, const QPointF &pos)
{
    NodeBase *n = addNodeById(scene, QStringLiteral("DelayNode"), pos);
    if (n)
        n->setParam(QStringLiteral("delayMs"), 1);
    return n;
}

NodeBase *addCaller(FlowScene *scene, const QString &subFlowName, const QPointF &pos)
{
    NodeBase *n = addNodeById(scene, QStringLiteral("SubFlowNode"), pos);
    if (n)
        n->setParam(QStringLiteral("subFlowName"), subFlowName);
    return n;
}

void connectNodes(FlowScene *scene, NodeBase *from, NodeBase *to)
{
    QVERIFY(scene->createConnection(from->outputPorts().first(),
                                    to->inputPorts().first(), true));
}

void selectOnly(FlowScene *scene, const QList<NodeBase *> &nodes)
{
    scene->clearSelection();
    for (NodeBase *n : nodes) {
        if (NodeGraphicsItem *item = scene->getGraphicsItemForNode(n))
            item->setSelected(true);
    }
}

struct RoundResult {
    int okRuns = 0;
    QHash<QString, bool> lastByNode;   // fullName → 最近一次成功与否
    QStringList errors;
};

RoundResult runOneRound(FlowExecutor &exec, FlowScene *scene)
{
    RoundResult r;
    const auto c1 = QObject::connect(&exec, &FlowExecutor::nodeExecuted, &exec,
                                     [&](NodeBase *n, bool ok) {
                                         if (!n)
                                             return;
                                         r.lastByNode.insert(n->fullName(), ok);
                                         if (ok)
                                             ++r.okRuns;
                                     },
                                     Qt::DirectConnection);
    const auto c2 = QObject::connect(&exec, &FlowExecutor::executionError, &exec,
                                     [&](const QString &msg) { r.errors << msg; },
                                     Qt::DirectConnection);
    exec.setFlowName(QStringLiteral("SubFlowTest"));
    exec.setFlowMode(FlowMode::SoftwareTrigger);
    exec.setFlowScene(scene);
    exec.startExecution();
    bool finished = exec.wait(15000);
    if (!finished) {
        exec.stopExecution();
        exec.wait(3000);
    }
    exec.setFlowScene(nullptr);
    QCoreApplication::processEvents();
    QObject::disconnect(c1);
    QObject::disconnect(c2);
    return r;
}

double readOutNumber(NodeBase *node)
{
    if (!node)
        return qQNaN();
    const auto out = node->getOutputData(0);
    if (!out)
        return qQNaN();
    return out->getData().toDouble();
}

} // namespace

void SubFlowTest::initTestCase()
{
    registerAllNodes();
}

void SubFlowTest::testDefineValidation()
{
    FlowScene scene;
    NodeBase *gen = addFormula(&scene, QStringLiteral("1"), QPointF(0, 0));
    NodeBase *m1 = addDelay(&scene, QPointF(220, 0));
    NodeBase *m2 = addDelay(&scene, QPointF(440, 0));
    NodeBase *m3 = addDelay(&scene, QPointF(660, 0));
    QVERIFY(gen && m1 && m2 && m3);

    // <2 个算子
    selectOnly(&scene, { m1 });
    QString err;
    QVERIFY(!scene.defineSubFlowFromSelection(QStringLiteral("S1"), &err));
    QVERIFY(err.contains(QStringLiteral("2 个算子")));

    // 正常定义（无任何外部连线的两节点链）
    QVERIFY(scene.createConnection(m1->outputPorts().first(), m2->inputPorts().first(), true));
    selectOnly(&scene, { m1, m2 });
    QCOMPARE(scene.selectedNodes().size(), 2);   // 探针：选择是否生效
    QVERIFY2(scene.defineSubFlowFromSelection(QStringLiteral("S1"), &err), qPrintable(err));
    QCOMPARE(scene.subFlows().size(), 1);

    // 重名
    selectOnly(&scene, { m1, m2 });
    QVERIFY(!scene.defineSubFlowFromSelection(QStringLiteral("S1"), &err));
    QVERIFY(err.contains(QStringLiteral("已存在")));

    // 成员已属于其它子流程
    selectOnly(&scene, { m2, m3 });
    QVERIFY(!scene.defineSubFlowFromSelection(QStringLiteral("S2"), &err));
    QVERIFY(err.contains(QStringLiteral("已属于子流程")));

    // 入口/出口不唯一：两个互不相连、且都不属于任何定义的算子
    NodeBase *m4 = addDelay(&scene, QPointF(880, 0));
    QVERIFY(m4);
    selectOnly(&scene, { m3, m4 });
    QVERIFY(!scene.defineSubFlowFromSelection(QStringLiteral("S3"), &err));
    QVERIFY(err.contains(QStringLiteral("不唯一")));

    // 跨边界连线：成员与成员外算子相连
    QVERIFY(scene.createConnection(gen->outputPorts().first(), m3->inputPorts().first(), true));
    selectOnly(&scene, { m3, m4 });
    QVERIFY(!scene.defineSubFlowFromSelection(QStringLiteral("S4"), &err));
    QVERIFY(err.contains(QStringLiteral("边界不允许连线")));
}

void SubFlowTest::testDefJsonRoundTrip()
{
    FlowScene scene;
    NodeBase *m1 = addDelay(&scene, QPointF(0, 0));
    NodeBase *m2 = addDelay(&scene, QPointF(220, 0));
    QVERIFY(m1 && m2);
    QVERIFY(scene.createConnection(m1->outputPorts().first(), m2->inputPorts().first(), true));
    selectOnly(&scene, { m1, m2 });
    QString err;
    QVERIFY2(scene.defineSubFlowFromSelection(QStringLiteral("预处理"), &err), qPrintable(err));

    const QJsonObject extras = scene.extrasToJson();
    const QJsonArray arr = extras.value(QStringLiteral("subFlows")).toArray();
    QCOMPARE(arr.size(), 1);
    const QJsonObject o = arr.first().toObject();
    QCOMPARE(o.value(QStringLiteral("name")).toString(), QStringLiteral("预处理"));
    QCOMPARE(o.value(QStringLiteral("members")).toArray().size(), 2);
    QCOMPARE(o.value(QStringLiteral("input")).toInt(), m1->moduleId());
    QCOMPARE(o.value(QStringLiteral("output")).toInt(), m2->moduleId());

    // 往返：新场景恢复同名同成员定义
    FlowScene scene2;
    scene2.extrasFromJson(extras);
    QCOMPARE(scene2.subFlows().size(), 1);
    const SubFlowDef restored = scene2.subFlowByName(QStringLiteral("预处理"));
    QVERIFY(restored.isValid());
    // 成员顺序取自 selectedItems()（无序），按集合比较；入口/出口是显式 id
    const QSet<int> restoredSet(restored.members.cbegin(), restored.members.cend());
    QCOMPARE(restoredSet, QSet<int>({ m1->moduleId(), m2->moduleId() }));
    QCOMPARE(restored.input, m1->moduleId());
    QCOMPARE(restored.output, m2->moduleId());

    // 删除定义（画布算子保留；再删 = 名字不存在）
    QVERIFY(scene.removeSubFlow(QStringLiteral("预处理")));
    QVERIFY(scene.subFlows().isEmpty());
    QVERIFY(!scene.removeSubFlow(QStringLiteral("预处理")));
    QCOMPARE(scene.nodes().size(), 2);
}

void SubFlowTest::testExecutionEndToEnd()
{
    FlowScene scene;
    // 主图：gen(42) → caller → sink；子流程成员：m1 → m2（与主图无连线）
    NodeBase *gen = addFormula(&scene, QStringLiteral("42"), QPointF(0, 0));
    NodeBase *caller = addCaller(&scene, QStringLiteral("预处理"), QPointF(220, 0));
    NodeBase *sink = addFormula(&scene, QStringLiteral("p0"), QPointF(440, 0));
    NodeBase *m1 = addDelay(&scene, QPointF(0, 220));
    NodeBase *m2 = addDelay(&scene, QPointF(220, 220));
    QVERIFY(gen && caller && sink && m1 && m2);
    connectNodes(&scene, gen, caller);
    connectNodes(&scene, caller, sink);
    QVERIFY(scene.createConnection(m1->outputPorts().first(), m2->inputPorts().first(), true));

    selectOnly(&scene, { m1, m2 });
    QString err;
    QVERIFY2(scene.defineSubFlowFromSelection(QStringLiteral("预处理"), &err), qPrintable(err));

    FlowExecutor exec;
    const RoundResult r = runOneRound(exec, &scene);

    // 成员被主遍历跳过、由调用点内联各执行恰一遍；主图 3 + 成员 2 = 5 次成功执行
    // （若成员被主遍历重复执行，okRuns 会多出 2 —— 调度互斥的硬指标）
    QCOMPARE(r.okRuns, 5);
    QVERIFY(r.lastByNode.value(caller->fullName()));
    QVERIFY(r.lastByNode.value(m1->fullName()));
    QVERIFY(r.lastByNode.value(m2->fullName()));
    QVERIFY(r.errors.isEmpty());

    // 出口结果 → 调用点输出端口 0 → 下游
    QVERIFY(qAbs(readOutNumber(caller) - 42.0) < 1e-9);
}

void SubFlowTest::testTwoCallersIsolation()
{
    FlowScene scene;
    NodeBase *genA = addFormula(&scene, QStringLiteral("11"), QPointF(0, 0));
    NodeBase *callerA = addCaller(&scene, QStringLiteral("共享段"), QPointF(220, 0));
    NodeBase *sinkA = addFormula(&scene, QStringLiteral("p0"), QPointF(440, 0));
    NodeBase *genB = addFormula(&scene, QStringLiteral("22"), QPointF(0, 220));
    NodeBase *callerB = addCaller(&scene, QStringLiteral("共享段"), QPointF(220, 220));
    NodeBase *sinkB = addFormula(&scene, QStringLiteral("p0"), QPointF(440, 220));
    NodeBase *m1 = addDelay(&scene, QPointF(0, 440));
    NodeBase *m2 = addDelay(&scene, QPointF(220, 440));
    QVERIFY(genA && callerA && sinkA && genB && callerB && sinkB && m1 && m2);
    connectNodes(&scene, genA, callerA);
    connectNodes(&scene, callerA, sinkA);
    connectNodes(&scene, genB, callerB);
    connectNodes(&scene, callerB, sinkB);
    QVERIFY(scene.createConnection(m1->outputPorts().first(), m2->inputPorts().first(), true));

    selectOnly(&scene, { m1, m2 });
    QString err;
    QVERIFY2(scene.defineSubFlowFromSelection(QStringLiteral("共享段"), &err), qPrintable(err));

    FlowExecutor exec;
    const RoundResult r = runOneRound(exec, &scene);

    // 主图 6 次执行 + 成员被两个调用点各内联一遍（2×2）= 10
    QCOMPARE(r.okRuns, 10);
    QVERIFY(r.errors.isEmpty());
    // 隔离：两个调用点各拿各的结果（同一组成员，互不污染）
    QVERIFY(qAbs(readOutNumber(callerA) - 11.0) < 1e-9);
    QVERIFY(qAbs(readOutNumber(callerB) - 22.0) < 1e-9);
}

void SubFlowTest::testRecursionRejected()
{
    FlowScene scene;
    // A：innerAB → mA；B：innerBA → mB；innerAB 调 B、innerBA 调 A（环）
    NodeBase *gen = addFormula(&scene, QStringLiteral("1"), QPointF(0, 0));
    NodeBase *callerA = addCaller(&scene, QStringLiteral("A"), QPointF(220, 0));
    NodeBase *innerAB = addCaller(&scene, QStringLiteral("B"), QPointF(0, 220));
    NodeBase *mA = addDelay(&scene, QPointF(220, 220));
    NodeBase *innerBA = addCaller(&scene, QStringLiteral("A"), QPointF(0, 440));
    NodeBase *mB = addDelay(&scene, QPointF(220, 440));
    QVERIFY(gen && callerA && innerAB && mA && innerBA && mB);
    connectNodes(&scene, gen, callerA);
    QVERIFY(scene.createConnection(innerAB->outputPorts().first(),
                                   mA->inputPorts().first(), true));
    QVERIFY(scene.createConnection(innerBA->outputPorts().first(),
                                   mB->inputPorts().first(), true));

    selectOnly(&scene, { innerAB, mA });
    QString err;
    QVERIFY2(scene.defineSubFlowFromSelection(QStringLiteral("A"), &err), qPrintable(err));
    selectOnly(&scene, { innerBA, mB });
    QVERIFY2(scene.defineSubFlowFromSelection(QStringLiteral("B"), &err), qPrintable(err));

    FlowExecutor exec;
    const RoundResult r = runOneRound(exec, &scene);

    // 递归被拒绝：调用点失败，错误链里明确写"递归"
    QVERIFY(!r.lastByNode.value(callerA->fullName()));
    bool hasRecursionError = false;
    for (const QString &e : r.errors)
        if (e.contains(QStringLiteral("递归")))
            hasRecursionError = true;
    QVERIFY(hasRecursionError);
}

void SubFlowTest::testUndefinedRejected()
{
    FlowScene scene;
    NodeBase *gen = addFormula(&scene, QStringLiteral("1"), QPointF(0, 0));
    NodeBase *caller = addCaller(&scene, QStringLiteral("不存在"), QPointF(220, 0));
    QVERIFY(gen && caller);
    connectNodes(&scene, gen, caller);

    FlowExecutor exec;
    const RoundResult r = runOneRound(exec, &scene);

    QVERIFY(!r.lastByNode.value(caller->fullName()));
    bool hasUndefinedError = false;
    for (const QString &e : r.errors)
        if (e.contains(QStringLiteral("未定义")))
            hasUndefinedError = true;
    QVERIFY(hasUndefinedError);
}

// A5 口径（有执行器侧）：子流程没有喂入时，调用点允许绿灯，但**不得凭空造数据**。
// 与 DelayNode 的空载豁免同族（时间门控/透传：没有可传的东西 ⇒ 空就是诚实结果）。
// 这条同时挡两个方向：把豁免改成"空载必红"会在这里变红；把空输出填成上一轮残留值也会变红。
void SubFlowTest::testUnfedSubFlowCallerStaysGreenAndEmpty()
{
    FlowScene scene;
    // 调用点故意**不接上游**：唯一能让它"成功却零产出"的就是这条无喂入路径
    NodeBase *caller = addCaller(&scene, QStringLiteral("空转段"), QPointF(0, 0));
    NodeBase *m1 = addDelay(&scene, QPointF(0, 220));
    NodeBase *m2 = addDelay(&scene, QPointF(220, 220));
    QVERIFY(caller && m1 && m2);
    QVERIFY(scene.createConnection(m1->outputPorts().first(), m2->inputPorts().first(), true));

    selectOnly(&scene, { m1, m2 });
    QString err;
    QVERIFY2(scene.defineSubFlowFromSelection(QStringLiteral("空转段"), &err), qPrintable(err));

    FlowExecutor exec;
    const RoundResult r = runOneRound(exec, &scene);

    qDebug().noquote()
        << QStringLiteral("A5-SUBFLOW caller=%1 member1=%2 member2=%3 outputEmpty=%4 errors=%5")
               .arg(r.lastByNode.value(caller->fullName()) ? QStringLiteral("green") : QStringLiteral("red"))
               .arg(r.lastByNode.value(m1->fullName()) ? QStringLiteral("green") : QStringLiteral("red"))
               .arg(r.lastByNode.value(m2->fullName()) ? QStringLiteral("green") : QStringLiteral("red"))
               .arg(caller->getOutputData(0).isNull() ? QStringLiteral("true") : QStringLiteral("false"))
               .arg(r.errors.size());

    QVERIFY2(r.errors.isEmpty(), qPrintable(r.errors.join(QStringLiteral(" | "))));
    QVERIFY2(r.lastByNode.value(caller->fullName()),
             "无喂入的子流程调用点被判失败：豁免被改坏了（延时段空转是合法用法）");
    QVERIFY2(caller->getOutputData(0).isNull(),
             "无喂入的子流程调用点吐出了数据对象（凭空结果 / 上一轮残留）");
}

QTEST_MAIN(SubFlowTest)
#include "subflow_test.moc"
