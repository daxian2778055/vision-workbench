#include <QtTest>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QSignalSpy>
#include <algorithm>

#include "FlowScene.h"
#include "FlowExecutor.h"
#include "FlowSnippet.h"
#include "NodeBase.h"
#include "NodeGraphicsItem.h"
#include "NodeGroupItem.h"
#include "Port.h"
#include "Connection.h"
#include "NodeRegistry.h"

// 子图片段（多算子 + 内部连线 [+ 分组]）的复制/粘贴与片段文件契约测试。
//
// 这条链路最容易"看着像做完了、实际埋雷"的地方：
//  ① **身份**：插入必须重新发号（moduleId 是方案内身份，跨方案/同场景粘贴都会撞），
//     并且名字要去重（变量引用按名字定位）；
//  ② **拓扑**：内部连线要按序号正确重接；跨边界连线要**丢弃且如实计数**（否则用户以为粘全了）；
//  ③ **原子性**：片段里有一个算子建不出来，就整段放弃（半截子图比不粘贴更糟）；
//  ④ **一步撤销**：一次粘贴 = 一次 Ctrl+Z。
class FlowSnippetTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void testCaptureNeedsSceneNodes();
    void testDuplicateNodeKeepsPorts();
    void testCaptureKeepsInternalDropsBoundary();
    void testRoundTripTopologyAndPositions();
    void testInsertFreshIdsAndUniqueNames();
    void testInsertIsAtomicOnUnbuildableNode();
    void testRejectInvalidSnippet();
    void testTextRoundTrip();
    void testGroupTravelsOnlyWhenFullySelected();
    void testPastedSnippetExecutes();
    void testSingleUndoStep();
    void testInsertIntoLockedSceneRejected();
    void testCanvasCtrlCVEmitsRequests();
    void testSuggestedInsertTopLeftCentersCluster();
};

namespace {

NodeBase *addNode(FlowScene *scene, const QString &name, const QPointF &pos)
{
    return scene->createNode(NodeBase::LOGIC, pos, name);
}

void selectOnly(FlowScene *scene, const QList<NodeBase *> &nodes)
{
    scene->clearSelection();
    for (NodeBase *n : nodes) {
        if (NodeGraphicsItem *item = scene->getGraphicsItemForNode(n))
            item->setSelected(true);
    }
}

/// 场景里是否存在"from 的第 fromPort 个输出 → to 的第 toPort 个输入"这条连线
bool hasConnection(FlowScene *scene, NodeBase *from, int fromPort, NodeBase *to, int toPort)
{
    if (!from || !to)
        return false;
    for (MyProject::Connection *conn : scene->connections()) {
        Port *sp = conn->sourcePort();
        Port *tp = conn->targetPort();
        if (!sp || !tp)
            continue;
        if (sp->node() == from && tp->node() == to
            && from->outputPorts().indexOf(sp) == fromPort
            && to->inputPorts().indexOf(tp) == toPort) {
            return true;
        }
    }
    return false;
}

QSet<int> moduleIdsOf(FlowScene *scene)
{
    QSet<int> ids;
    for (NodeBase *n : scene->nodes())
        ids.insert(n->moduleId());
    return ids;
}
}   // namespace

void FlowSnippetTest::initTestCase()
{
    registerAllNodes();
}

void FlowSnippetTest::testCaptureNeedsSceneNodes()
{
    FlowScene scene;
    NodeBase *a = addNode(&scene, QStringLiteral("Delay"), QPointF(0, 0));
    QVERIFY(a);

    // 空选择 → 空片段
    QVERIFY(FlowSnippet::capture(&scene, {}).isEmpty());
    // 不在场景里的算子不能进片段（否则会拷出悬垂引用）
    FlowScene other;
    NodeBase *foreign = addNode(&other, QStringLiteral("Delay"), QPointF(0, 0));
    QVERIFY(foreign);
    QVERIFY(FlowSnippet::capture(&scene, {foreign}).isEmpty());
    // nullptr 场景
    QVERIFY(FlowSnippet::capture(nullptr, {a}).isEmpty());
}

void FlowSnippetTest::testDuplicateNodeKeepsPorts()
{
    // 与片段插入同源的坑：端口是在各算子 init() 里建的，而 createById/NodeFactory 都不调它。
    // 若"右键复制算子"出来的副本没有端口，现场表现是"复制一个算子后连不上线"，
    // 而流程仍能跑（更隐蔽）——故这里把副本的端口数与原件对齐作为硬契约。
    FlowScene scene;
    NodeBase *origin = addNode(&scene, QStringLiteral("Delay"), QPointF(100, 100));
    QVERIFY(origin);
    QVERIFY2(!origin->inputPorts().isEmpty() && !origin->outputPorts().isEmpty(),
             "原点子没有端口，前置条件不成立");

    NodeBase *clone = scene.duplicateNode(origin);
    QVERIFY2(clone, "复制算子失败");
    QCOMPARE(clone->inputPorts().size(), origin->inputPorts().size());
    QCOMPARE(clone->outputPorts().size(), origin->outputPorts().size());
    // 副本必须真的能接线（端口数相同但接不上，等于没修）
    NodeBase *mate = addNode(&scene, QStringLiteral("Delay"), QPointF(400, 100));
    QVERIFY(mate);
    QVERIFY2(scene.createConnection(clone->outputPorts().first(), mate->inputPorts().first(), true),
             "复制出来的算子无法接线");
}

void FlowSnippetTest::testCaptureKeepsInternalDropsBoundary()
{
    FlowScene scene;
    NodeBase *a = addNode(&scene, QStringLiteral("Delay"), QPointF(0, 0));
    NodeBase *b = addNode(&scene, QStringLiteral("Delay"), QPointF(200, 0));
    NodeBase *c = addNode(&scene, QStringLiteral("Delay"), QPointF(400, 0));
    NodeBase *outside = addNode(&scene, QStringLiteral("Formula"), QPointF(600, 0));
    QVERIFY(a && b && c && outside);

    QVERIFY(scene.createConnection(a->outputPorts().first(), b->inputPorts().first(), true));
    QVERIFY(scene.createConnection(b->outputPorts().first(), c->inputPorts().first(), true));
    // 跨边界那条连到 a 的输入（a 的输入还是空的；一个输入端口只能接一条线，别接到 b 上）
    QVERIFY(scene.createConnection(outside->outputPorts().first(), a->inputPorts().first(), true));

    int dropped = -1;
    const QJsonObject snippet = FlowSnippet::capture(&scene, {a, b, c}, &dropped);
    QVERIFY2(!snippet.isEmpty(), "抓取片段失败");
    QCOMPARE(snippet.value(QStringLiteral("nodes")).toArray().size(), 3);
    QCOMPARE(snippet.value(QStringLiteral("connections")).toArray().size(), 2);
    // 跨边界那条必须被丢弃**并且如实计数**（用户要知道少了一根线）
    QCOMPARE(snippet.value(QStringLiteral("droppedBoundaryConnections")).toInt(), 1);
    QCOMPARE(dropped, 1);
}

void FlowSnippetTest::testRoundTripTopologyAndPositions()
{
    FlowScene source;
    NodeBase *a = addNode(&source, QStringLiteral("Delay"), QPointF(100, 100));
    NodeBase *b = addNode(&source, QStringLiteral("Delay"), QPointF(320, 100));
    QVERIFY(a && b);
    a->setParam(QStringLiteral("delayMs"), 37);
    QVERIFY(source.createConnection(a->outputPorts().first(), b->inputPorts().first(), true));

    const QJsonObject snippet = FlowSnippet::capture(&source, {a, b});
    QVERIFY(!snippet.isEmpty());

    FlowScene target;
    QStringList rejections;   // 连线被拒的原因（诊断用：插入失败时能直接看到为什么）
    QObject::connect(&target, &FlowScene::connectionRejected, &target,
                     [&rejections](const QString &reason) { rejections << reason; });
    const QPointF at(500, 300);
    QString error;
    const QList<NodeBase *> created = FlowSnippet::insert(&target, snippet, at, &error);
    QVERIFY2(created.size() == 2, qPrintable(error));
    QVERIFY2(target.connections().size() == 1,
             qPrintable(QStringLiteral("插入后连线数=%1（期望 1）拒绝原因=[%2] 片段连线=%3 "
                                       "端口数=[源出 %4 / 目标入 %5] 类型=[%6 / %7]")
                            .arg(target.connections().size())
                            .arg(rejections.join(QStringLiteral(" | ")),
                                 QString::fromUtf8(QJsonDocument(
                                     snippet.value(QStringLiteral("connections")).toArray())
                                                       .toJson(QJsonDocument::Compact)))
                            .arg(created.at(0)->outputPorts().size())
                            .arg(created.at(1)->inputPorts().size())
                            .arg(created.at(0)->property("vfpNodeTypeId").toString(),
                                 created.at(1)->property("vfpNodeTypeId").toString())));

    // 拓扑按序号重接：from 的第 0 个输出 → to 的第 0 个输入
    QVERIFY2(hasConnection(&target, created.at(0), 0, created.at(1), 0),
             "插入后的连线没有接到对应端口上");

    // 相对位置整体平移到插入点（片段左上角落在 at）
    QCOMPARE(created.at(0)->position(), at);
    QCOMPARE(created.at(1)->position(), at + QPointF(220, 0));

    // 参数原样带过来（"粘过来参数丢了"是最难查的一类）
    QCOMPARE(created.at(0)->getParam(QStringLiteral("delayMs")).toInt(), 37);
}

void FlowSnippetTest::testInsertFreshIdsAndUniqueNames()
{
    FlowScene scene;
    NodeBase *a = addNode(&scene, QStringLiteral("Delay"), QPointF(0, 0));
    NodeBase *b = addNode(&scene, QStringLiteral("Delay"), QPointF(200, 0));
    QVERIFY(a && b);
    QVERIFY(scene.createConnection(a->outputPorts().first(), b->inputPorts().first(), true));
    const QSet<int> idsBefore = moduleIdsOf(&scene);
    QSet<QString> namesBefore;
    for (NodeBase *n : scene.nodes())
        namesBefore.insert(n->name());

    // 命名去重是单一实现（模板插入与片段插入共用），先在"只有两个同名算子"的干净状态下钉契约：
    // 没被占用 → 原样返回；被占用 → 加 _2 后缀。
    // 注意用**算子实际名字**（createNode 的入参可能是英文别名，落地后是显示名）。
    const QString takenName = a->name();
    QVERIFY2(namesBefore.contains(takenName), "夹具前置不成立：名字应该被占用");
    QCOMPARE(scene.makeUniqueNodeName(takenName), takenName + QStringLiteral("_2"));
    QCOMPARE(scene.makeUniqueNodeName(QStringLiteral("绝无此名_zzz")),
             QStringLiteral("绝无此名_zzz"));

    // 粘到**同一个场景**：必须重新发号（不然一号两算子，执行器缓存与引用会互相污染）
    const QJsonObject snippet = FlowSnippet::capture(&scene, {a, b});
    QString error;
    const QList<NodeBase *> created = FlowSnippet::insert(&scene, snippet, QPointF(600, 400), &error);
    QVERIFY2(created.size() == 2, qPrintable(error));
    for (NodeBase *n : created)
        QVERIFY2(!idsBefore.contains(n->moduleId()), "粘贴出来的算子复用了已有模块号");

    // 全场景无重复模块号；**新插入的算子**不与任何已有算子重名、彼此也不重名
    // （变量引用按名字定位，撞名会让"引用指向哪个算子"变得不确定）。
    // 注意口径：createNode 本身允许重名，所以这里只钉"插入方的责任"，不去断言全场景唯一。
    QSet<int> allIds;
    for (NodeBase *n : scene.nodes())
        allIds.insert(n->moduleId());
    QCOMPARE(allIds.size(), int(scene.nodes().size()));

    QSet<QString> insertedNames;
    for (NodeBase *n : created) {
        QVERIFY2(!namesBefore.contains(n->name()),
                 qPrintable(QStringLiteral("插入的算子沿用了已有名字：%1").arg(n->name())));
        insertedNames.insert(n->name());
    }
    QCOMPARE(insertedNames.size(), int(created.size()));


}

void FlowSnippetTest::testInsertIsAtomicOnUnbuildableNode()
{
    FlowScene source;
    NodeBase *a = addNode(&source, QStringLiteral("Delay"), QPointF(0, 0));
    QVERIFY(a);
    const int baseline = int(source.nodes().size());

    QJsonObject snippet = FlowSnippet::capture(&source, {a});
    QVERIFY(!snippet.isEmpty());
    // 人为塞一个"建不出来"的算子（类型既不在注册表、类型枚举也非法）
    QJsonArray nodes = snippet.value(QStringLiteral("nodes")).toArray();
    QJsonObject bogus;
    bogus[QStringLiteral("typeId")] = QStringLiteral("NoSuchNodeType_zzz");
    bogus[QStringLiteral("nodeType")] = 9999;
    bogus[QStringLiteral("nodeName")] = QStringLiteral("幽灵算子");
    bogus[QStringLiteral("relativeX")] = 200;
    bogus[QStringLiteral("relativeY")] = 0;
    nodes.append(bogus);
    snippet[QStringLiteral("nodes")] = nodes;

    QString error;
    const QList<NodeBase *> created = FlowSnippet::insert(&source, snippet, QPointF(800, 800), &error);
    QVERIFY2(created.isEmpty(), "含非法算子的片段不应插入任何东西");
    QVERIFY2(!error.isEmpty(), "失败时必须给出原因");
    // 原子性：场景必须保持原样（半截子图比不粘贴更糟）
    QCOMPARE(int(source.nodes().size()), baseline);
}

void FlowSnippetTest::testRejectInvalidSnippet()
{
    QString error;

    // 非 JSON 文本
    QVERIFY(FlowSnippet::fromText(QStringLiteral("not json at all"), &error).isEmpty());
    QVERIFY(!error.isEmpty());

    // 合法 JSON 但不是片段
    const QJsonObject plain = FlowSnippet::fromText(QStringLiteral("{\"hello\":1}"), &error);
    QVERIFY(!plain.isEmpty());
    QVERIFY(!FlowSnippet::isValid(plain, &error));
    QVERIFY(!error.isEmpty());

    // kind 对、版本过高 → 拒绝（避免"看不懂的字段被静默忽略"）
    QJsonObject future;
    future[QStringLiteral("kind")] = QStringLiteral("vfp.flowSnippet");
    future[QStringLiteral("version")] = 99;
    future[QStringLiteral("nodes")] = QJsonArray{QJsonObject{{QStringLiteral("nodeType"), 1}}};
    QVERIFY(!FlowSnippet::isValid(future, &error));

    // 没有任何算子
    QJsonObject emptyNodes;
    emptyNodes[QStringLiteral("kind")] = QStringLiteral("vfp.flowSnippet");
    emptyNodes[QStringLiteral("version")] = 1;
    emptyNodes[QStringLiteral("nodes")] = QJsonArray{};
    QVERIFY(!FlowSnippet::isValid(emptyNodes, &error));

    // 插入非法片段：场景不动
    FlowScene scene;
    NodeBase *a = addNode(&scene, QStringLiteral("Delay"), QPointF(0, 0));
    QVERIFY(a);
    const int baseline = int(scene.nodes().size());
    QVERIFY(FlowSnippet::insert(&scene, plain, QPointF(0, 0), &error).isEmpty());
    QCOMPARE(int(scene.nodes().size()), baseline);
}

void FlowSnippetTest::testTextRoundTrip()
{
    FlowScene scene;
    NodeBase *a = addNode(&scene, QStringLiteral("Delay"), QPointF(10, 20));
    NodeBase *b = addNode(&scene, QStringLiteral("Delay"), QPointF(210, 20));
    QVERIFY(a && b);
    QVERIFY(scene.createConnection(a->outputPorts().first(), b->inputPorts().first(), true));

    const QJsonObject snippet = FlowSnippet::capture(&scene, {a, b});
    const QString text = FlowSnippet::toText(snippet);
    QVERIFY(!text.isEmpty());

    QString error;
    const QJsonObject back = FlowSnippet::fromText(text, &error);
    QVERIFY2(!back.isEmpty(), qPrintable(error));
    QVERIFY(FlowSnippet::isValid(back, &error));
    QCOMPARE(back.value(QStringLiteral("nodes")).toArray().size(), 2);
    QCOMPARE(back.value(QStringLiteral("connections")).toArray().size(), 1);
}

void FlowSnippetTest::testGroupTravelsOnlyWhenFullySelected()
{
    FlowScene scene;
    NodeBase *a = addNode(&scene, QStringLiteral("Delay"), QPointF(0, 0));
    NodeBase *b = addNode(&scene, QStringLiteral("Delay"), QPointF(200, 0));
    NodeBase *c = addNode(&scene, QStringLiteral("Delay"), QPointF(400, 0));
    QVERIFY(a && b && c);

    selectOnly(&scene, {a, b});
    NodeGroupItem *group = scene.createGroupFromSelection(QStringLiteral("成对分组"));
    QVERIFY(group);
    QCOMPARE(group->memberCount(), 2);

    // ① 只选中组内一个成员 → 组**不**随片段走（避免把一个组拆成两半）
    const QJsonObject partial = FlowSnippet::capture(&scene, {a});
    QVERIFY(!partial.isEmpty());
    QCOMPARE(partial.value(QStringLiteral("groups")).toArray().size(), 0);

    // ② 选中组内全部成员（再加一个组外算子）→ 组随片段走，成员映射到新算子
    const QJsonObject full = FlowSnippet::capture(&scene, {a, b, c});
    QCOMPARE(full.value(QStringLiteral("groups")).toArray().size(), 1);

    FlowScene target;
    QString error;
    const QList<NodeBase *> created = FlowSnippet::insert(&target, full, QPointF(700, 500), &error);
    QVERIFY2(created.size() == 3, qPrintable(error));
    QCOMPARE(target.groups().size(), 1);
    NodeGroupItem *pasted = target.groups().first();
    QCOMPARE(pasted->title(), QStringLiteral("成对分组"));
    QCOMPARE(pasted->memberCount(), 2);
    // 成员必须是**新插入**的那两个算子（映射到新模块号，而不是源场景的号）
    for (int id : pasted->memberIds()) {
        NodeBase *n = target.nodeByModuleId(id);
        QVERIFY2(n, "粘贴后的分组成员指向了不存在的模块号");
        QVERIFY2(created.contains(n), "分组成员没有映射到新插入的算子");
    }
}

void FlowSnippetTest::testPastedSnippetExecutes()
{
    // 粘出来的图必须真的能跑：真跑一轮流程（连线接错/端口接反会在这里暴露）
    FlowScene source;
    NodeBase *a = addNode(&source, QStringLiteral("Delay"), QPointF(0, 0));
    NodeBase *b = addNode(&source, QStringLiteral("Delay"), QPointF(220, 0));
    QVERIFY(a && b);
    a->setParam(QStringLiteral("delayMs"), 5);
    b->setParam(QStringLiteral("delayMs"), 5);
    QVERIFY(source.createConnection(a->outputPorts().first(), b->inputPorts().first(), true));

    const QJsonObject snippet = FlowSnippet::capture(&source, {a, b});

    FlowScene target;
    QString error;
    const QList<NodeBase *> created = FlowSnippet::insert(&target, snippet, QPointF(0, 0), &error);
    QVERIFY2(created.size() == 2, qPrintable(error));

    FlowExecutor exec;
    exec.setFlowName(QStringLiteral("SnippetPasteExecution"));
    int okRuns = 0;
    const auto conn = QObject::connect(
        &exec, &FlowExecutor::nodeExecuted, &exec,
        [&okRuns](NodeBase *, bool ok) {
            if (ok)
                ++okRuns;
        },
        Qt::DirectConnection);

    exec.setFlowScene(&target);
    exec.setFlowMode(FlowMode::SoftwareTrigger);
    exec.startExecution();
    bool finished = exec.wait(10000);
    if (!finished) {
        exec.stopExecution();
        finished = exec.wait(3000);
    }
    exec.setFlowScene(nullptr);
    QCoreApplication::processEvents();
    QObject::disconnect(conn);

    QVERIFY2(finished, "粘贴出的流程未能在 10 秒内跑完");
    QCOMPARE(okRuns, 2);   // 两个延时算子各成功执行一次
}

void FlowSnippetTest::testSingleUndoStep()
{
    FlowScene source;
    NodeBase *a = addNode(&source, QStringLiteral("Delay"), QPointF(0, 0));
    NodeBase *b = addNode(&source, QStringLiteral("Delay"), QPointF(220, 0));
    QVERIFY(a && b);
    QVERIFY(source.createConnection(a->outputPorts().first(), b->inputPorts().first(), true));

    FlowScene target;
    NodeBase *existing = addNode(&target, QStringLiteral("Formula"), QPointF(0, 0));
    QVERIFY(existing);
    const int baseline = int(target.nodes().size());
    const QJsonObject snippet = FlowSnippet::capture(&source, {a, b});

    target.recordUndo();   // 与界面入口一致：插入前记录一次
    QString error;
    QVERIFY(FlowSnippet::insert(&target, snippet, QPointF(400, 400), &error).size() == 2);
    QCOMPARE(int(target.nodes().size()), baseline + 2);

    // 一次粘贴 = 一步撤销（内部必须批量抑制分步记录）
    QVERIFY(target.undo());
    QCOMPARE(int(target.nodes().size()), baseline);
    QCOMPARE(int(target.connections().size()), 0);
}

void FlowSnippetTest::testInsertIntoLockedSceneRejected()
{
    FlowScene source;
    NodeBase *a = addNode(&source, QStringLiteral("Delay"), QPointF(0, 0));
    QVERIFY(a);
    const QJsonObject snippet = FlowSnippet::capture(&source, {a});

    FlowScene target;
    target.setEditLocked(true);
    QString error;
    QVERIFY(FlowSnippet::insert(&target, snippet, QPointF(0, 0), &error).isEmpty());
    QVERIFY2(error.contains(QStringLiteral("锁定")), qPrintable(error));
    QVERIFY(target.nodes().isEmpty());
}

void FlowSnippetTest::testCanvasCtrlCVEmitsRequests()
{
    // 画布聚焦时 Ctrl+C / Ctrl+V 由场景发请求（剪贴板与粘贴位置在窗口侧），
    // 这样参数框/日志里的 Ctrl+C 仍是原生文本复制
    FlowScene scene;
    NodeBase *a = addNode(&scene, QStringLiteral("Delay"), QPointF(0, 0));
    QVERIFY(a);

    QSignalSpy copySpy(&scene, &FlowScene::copySelectionRequested);
    QSignalSpy pasteSpy(&scene, &FlowScene::pasteRequested);
    QVERIFY(copySpy.isValid() && pasteSpy.isValid());

    QKeyEvent copyEvent(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
    QCoreApplication::sendEvent(&scene, &copyEvent);
    QCOMPARE(copySpy.count(), 1);

    QKeyEvent pasteEvent(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
    QCoreApplication::sendEvent(&scene, &pasteEvent);
    QCOMPARE(pasteSpy.count(), 1);

    // 普通按键不得被当成复制粘贴（防止"按 C 就复制"这类误触）
    QKeyEvent plainEvent(QEvent::KeyPress, Qt::Key_C, Qt::NoModifier);
    QCoreApplication::sendEvent(&scene, &plainEvent);
    QCOMPARE(copySpy.count(), 1);
}

void FlowSnippetTest::testSuggestedInsertTopLeftCentersCluster()
{
    // 建议插入点让"片段整体"落在视图中心附近：否则大片段会有一多半跑到视口外，用户以为没粘上
    FlowScene source;
    NodeBase *a = addNode(&source, QStringLiteral("Delay"), QPointF(0, 0));
    NodeBase *b = addNode(&source, QStringLiteral("Delay"), QPointF(400, 200));
    QVERIFY(a && b);
    const QJsonObject snippet = FlowSnippet::capture(&source, {a, b});

    const QPointF center(1000, 800);
    const QPointF topLeft = FlowSnippet::suggestedInsertTopLeft(snippet, center);

    FlowScene target;
    QString error;
    const QList<NodeBase *> created = FlowSnippet::insert(&target, snippet, topLeft, &error);
    QVERIFY2(created.size() == 2, qPrintable(error));

    // 插入后的整体外接矩形中心应落在 center 附近（各算子尺寸固定，容差按尺寸给）
    QRectF box = target.getGraphicsItemForNode(created.at(0))->sceneBoundingRect();
    box = box.united(target.getGraphicsItemForNode(created.at(1))->sceneBoundingRect());
    QVERIFY2(qAbs(box.center().x() - center.x()) < 150.0
                 && qAbs(box.center().y() - center.y()) < 150.0,
             qPrintable(QStringLiteral("片段未落在视图中心：外接中心=(%1,%2) 期望≈(%3,%4)")
                            .arg(box.center().x()).arg(box.center().y())
                            .arg(center.x()).arg(center.y())));
}

QTEST_MAIN(FlowSnippetTest)
#include "flow_snippet_test.moc"
