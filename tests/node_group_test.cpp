#include <QtTest>
#include <QCoreApplication>
#include <algorithm>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QList>

#include "FlowScene.h"
#include "FlowExecutor.h"
#include "NodeBase.h"
#include "NodeGraphicsItem.h"
#include "NodeGroupItem.h"
#include "ProjectManager.h"
#include "NodeRegistry.h"

// 算子分组（FR1.9，Group）契约测试。
//
// 分组最容易"看着像做完了、实际埋雷"的三处，正是本文件要钉的：
//  ① **成员身份**：分组必须记住"是哪几个算子"，而不是"第几个"。方案里的节点下标来自
//     FlowScene::nodes()（按指针地址排序），换会话/加载一遍就变——按下标存，分组会移花接木。
//     故用模块号（moduleId）做成员标识，并在此验证"存盘 → 加载"后成员仍是同一批算子（按参数值核对）。
//  ② **不参与执行**：分组只是视觉容器。若它改了图结构或发了 nodeAdded/connection* 信号，
//     会污染执行器的图结构缓存。用例真跑流程，比较"分组前/后每轮执行次数"。
//  ③ **撤销时机**：分组拖动在"移动前"记录撤销（一次 Ctrl+Z 即复原）。节点拖动当前是
//     "释放时才记"（第一次撤销看起来没反应）——本轮不改该约定，但在此**钉住现状**，
//     改约定时这里会亮，提醒同步文档。
class NodeGroupTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void testCreateRequiresTwoSelectedNodes();
    void testMembersAndFrameGeometry();
    void testMembersSurviveSaveLoad();
    void testMoveGroupMovesMembers();
    void testNodeMovedRefitsFrame();
    void testRemoveNodeMaintainsGroup();
    void testDissolveKeepsNodes();
    void testDeleteSelectedGroupKeepsNodes();
    void testEditLockedBlocksGroupEdits();
    void testUndoRedoRestoresGroups();
    void testExecutionUnaffectedByGroup();
    void testModuleIdRestoredOnLoad();
    void testNodeMoveUndoConvention();
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
}   // namespace

void NodeGroupTest::initTestCase()
{
    registerAllNodes();
}

void NodeGroupTest::testCreateRequiresTwoSelectedNodes()
{
    FlowScene scene;
    NodeBase *a = addNode(&scene, QStringLiteral("Delay"), QPointF(0, 0));
    NodeBase *b = addNode(&scene, QStringLiteral("Delay"), QPointF(320, 0));
    QVERIFY(a && b);

    // 没选东西 → 不建组（返回 nullptr，而不是建一个空框）
    QVERIFY(scene.createGroupFromSelection() == nullptr);
    QVERIFY(scene.groups().isEmpty());

    // 只选中 1 个 → 不建组（框只会贴住它自己，没有意义）
    selectOnly(&scene, {a});
    QVERIFY(scene.createGroupFromSelection() == nullptr);
    QVERIFY(scene.groups().isEmpty());

    // 选中 2 个 → 建组
    selectOnly(&scene, {a, b});
    NodeGroupItem *group = scene.createGroupFromSelection(QStringLiteral("测试分组"));
    QVERIFY(group);
    QCOMPARE(scene.groups().size(), 1);
    QCOMPARE(group->memberCount(), 2);
    QCOMPARE(group->title(), QStringLiteral("测试分组"));
}

void NodeGroupTest::testMembersAndFrameGeometry()
{
    FlowScene scene;
    NodeBase *a = addNode(&scene, QStringLiteral("Delay"), QPointF(100, 100));
    NodeBase *b = addNode(&scene, QStringLiteral("Delay"), QPointF(420, 300));
    QVERIFY(a && b);
    selectOnly(&scene, {a, b});

    NodeGroupItem *group = scene.createGroupFromSelection();
    QVERIFY(group);

    // 框体必须把两个算子的图形都包住（各留内边距，上方留标题栏）
    QRectF memberBox = scene.getGraphicsItemForNode(a)->sceneBoundingRect();
    memberBox = memberBox.united(scene.getGraphicsItemForNode(b)->sceneBoundingRect());
    const QRectF frame = group->mapRectToScene(group->boundingRect());
    QVERIFY2(frame.contains(memberBox.adjusted(-1, -1, 1, 1)),
             qPrintable(QStringLiteral("组框未包含全部成员：框=%1,%2 %3x%4 成员=%5,%6 %7x%8")
                            .arg(frame.x()).arg(frame.y()).arg(frame.width()).arg(frame.height())
                            .arg(memberBox.x()).arg(memberBox.y())
                            .arg(memberBox.width()).arg(memberBox.height())));

    // 标题栏判定：只有顶部带状区域可拖动（其余区域要放行给框选）
    QVERIFY(group->hitTitleBar(QPointF(20, 2)));
    QVERIFY(!group->hitTitleBar(QPointF(20, group->boundingRect().height() - 5)));
    QVERIFY(!group->hitTitleBar(QPointF(-5, 2)));
}

void NodeGroupTest::testMembersSurviveSaveLoad()
{
    // ① 的核心：存盘往返后成员仍是同一批算子（按模块号 + 参数值核对，不只看数量）
    FlowScene scene;
    NodeBase *a = addNode(&scene, QStringLiteral("Delay"), QPointF(100, 100));
    NodeBase *b = addNode(&scene, QStringLiteral("Delay"), QPointF(420, 300));
    QVERIFY(a && b);
    a->setParam(QStringLiteral("delayMs"), 111);
    b->setParam(QStringLiteral("delayMs"), 222);
    selectOnly(&scene, {a, b});
    NodeGroupItem *group = scene.createGroupFromSelection(QStringLiteral("往返分组"));
    QVERIFY(group);

    ProjectManager pm;
    const QJsonObject json = pm.sceneToJson(&scene);
    const QJsonArray groupsJson = json.value(QStringLiteral("nodeGroups")).toArray();
    QCOMPARE(groupsJson.size(), 1);
    QCOMPARE(groupsJson.first().toObject().value(QStringLiteral("members")).toArray().size(), 2);

    FlowScene restored;
    pm.sceneFromJson(json, &restored);
    QCOMPARE(restored.groups().size(), 1);
    NodeGroupItem *restoredGroup = restored.groups().first();
    QCOMPARE(restoredGroup->title(), QStringLiteral("往返分组"));
    QCOMPARE(restoredGroup->memberCount(), 2);

    QList<int> delays;
    for (int id : restoredGroup->memberIds()) {
        NodeBase *n = restored.nodeByModuleId(id);
        QVERIFY2(n, "分组成员的模块号在恢复后的场景里找不到（身份没跨存盘保持）");
        delays.append(n->getParam(QStringLiteral("delayMs")).toInt());
    }
    std::sort(delays.begin(), delays.end());
    QCOMPARE(delays, QList<int>({111, 222}));
}

void NodeGroupTest::testMoveGroupMovesMembers()
{
    FlowScene scene;
    NodeBase *a = addNode(&scene, QStringLiteral("Delay"), QPointF(100, 100));
    NodeBase *b = addNode(&scene, QStringLiteral("Delay"), QPointF(420, 300));
    QVERIFY(a && b);
    selectOnly(&scene, {a, b});
    NodeGroupItem *group = scene.createGroupFromSelection();
    QVERIFY(group);

    const int aId = a->moduleId();
    const int bId = b->moduleId();
    const QPointF beforeA = a->position();
    const QPointF beforeB = b->position();
    const QPointF beforeGroup = group->pos();
    const QPointF delta(60, -25);

    // 与交互路径一致：移动前记录撤销（见用例 ③ 的说明）
    scene.recordUndo();
    group->moveGroupBy(delta);

    QCOMPARE(a->position(), beforeA + delta);
    QCOMPARE(b->position(), beforeB + delta);
    QCOMPARE(group->pos(), beforeGroup + delta);

    // 一次撤销即整体复原（撤销会重建场景，故用模块号重新取算子）
    QVERIFY(scene.undo());
    NodeBase *a2 = scene.nodeByModuleId(aId);
    NodeBase *b2 = scene.nodeByModuleId(bId);
    QVERIFY(a2 && b2);
    QCOMPARE(a2->position(), beforeA);
    QCOMPARE(b2->position(), beforeB);
}

void NodeGroupTest::testNodeMovedRefitsFrame()
{
    // 成员被单独拖走后，框体必须跟着长大/收拢；否则组框与成员对不上（视觉错位）
    FlowScene scene;
    NodeBase *a = addNode(&scene, QStringLiteral("Delay"), QPointF(100, 100));
    NodeBase *b = addNode(&scene, QStringLiteral("Delay"), QPointF(420, 300));
    QVERIFY(a && b);
    selectOnly(&scene, {a, b});
    NodeGroupItem *group = scene.createGroupFromSelection();
    QVERIFY(group);
    const QRectF before = group->mapRectToScene(group->boundingRect());

    // 复刻 NodeGraphicsItem::mouseReleaseEvent 里"记录撤销 + 重算组框"的调用序列
    scene.recordUndo();
    NodeGraphicsItem *itemB = scene.getGraphicsItemForNode(b);
    QVERIFY(itemB);
    itemB->setPos(QPointF(1000, 900));
    itemB->syncNodeGeometry();
    group->fitToMembers();

    const QRectF after = group->mapRectToScene(group->boundingRect());
    QVERIFY2(after.width() > before.width() && after.height() > before.height(),
             qPrintable(QStringLiteral("组框未跟随成员变大：%1x%2 → %3x%4")
                            .arg(before.width()).arg(before.height())
                            .arg(after.width()).arg(after.height())));
    QVERIFY2(after.contains(itemB->sceneBoundingRect().adjusted(-1, -1, 1, 1)),
             "组框没有包住被拖走的成员");
}

void NodeGroupTest::testRemoveNodeMaintainsGroup()
{
    FlowScene scene;
    NodeBase *a = addNode(&scene, QStringLiteral("Delay"), QPointF(0, 0));
    NodeBase *b = addNode(&scene, QStringLiteral("Delay"), QPointF(320, 0));
    NodeBase *c = addNode(&scene, QStringLiteral("Delay"), QPointF(640, 0));
    QVERIFY(a && b && c);
    const int bId = b->moduleId();
    selectOnly(&scene, {a, b, c});
    NodeGroupItem *group = scene.createGroupFromSelection();
    QVERIFY(group);
    QCOMPARE(group->memberCount(), 3);

    // 删掉一个成员：分组要跟着减员（否则框里指向一个已不存在的算子）
    scene.removeNode(b);
    QCOMPARE(group->memberCount(), 2);
    QVERIFY(!group->hasMember(bId));

    // 成员删光：分组自动解散（不留一个永远空着的框）
    scene.removeNode(a);
    scene.removeNode(c);
    QVERIFY2(scene.groups().isEmpty(), "成员删光后分组未自动解散");
}

void NodeGroupTest::testDissolveKeepsNodes()
{
    FlowScene scene;
    NodeBase *a = addNode(&scene, QStringLiteral("Delay"), QPointF(0, 0));
    NodeBase *b = addNode(&scene, QStringLiteral("Delay"), QPointF(320, 0));
    QVERIFY(a && b);
    selectOnly(&scene, {a, b});
    NodeGroupItem *group = scene.createGroupFromSelection();
    QVERIFY(group);
    const int aId = a->moduleId();
    const int bId = b->moduleId();

    scene.removeGroup(group);   // 右键菜单"解散分组"走这条
    QVERIFY(scene.groups().isEmpty());
    QCOMPARE(scene.nodes().size(), 2);
    QVERIFY(scene.nodeByModuleId(aId) && scene.nodeByModuleId(bId));

    // 菜单/快捷键入口：先选中分组框再解散
    selectOnly(&scene, {a, b});
    NodeGroupItem *group2 = scene.createGroupFromSelection();
    QVERIFY(group2);
    scene.clearSelection();
    group2->setSelected(true);
    QCOMPARE(scene.dissolveSelectedGroups(), 1);
    QVERIFY(scene.groups().isEmpty());
    QCOMPARE(scene.nodes().size(), 2);
}

void NodeGroupTest::testDeleteSelectedGroupKeepsNodes()
{
    // Delete 键路径：选中分组框按 Delete = 解散（只删框，不连带删算子——
    // "删框顺带删算子"会让误点标题栏变成批量删除，风险远大于收益）
    FlowScene scene;
    NodeBase *a = addNode(&scene, QStringLiteral("Delay"), QPointF(0, 0));
    NodeBase *b = addNode(&scene, QStringLiteral("Delay"), QPointF(320, 0));
    QVERIFY(a && b);
    selectOnly(&scene, {a, b});
    NodeGroupItem *group = scene.createGroupFromSelection();
    QVERIFY(group);

    scene.clearSelection();
    group->setSelected(true);
    scene.deleteSelectedItems();

    QVERIFY(scene.groups().isEmpty());
    QCOMPARE(scene.nodes().size(), 2);
}

void NodeGroupTest::testEditLockedBlocksGroupEdits()
{
    FlowScene scene;
    NodeBase *a = addNode(&scene, QStringLiteral("Delay"), QPointF(0, 0));
    NodeBase *b = addNode(&scene, QStringLiteral("Delay"), QPointF(320, 0));
    QVERIFY(a && b);
    selectOnly(&scene, {a, b});

    scene.setEditLocked(true);
    QVERIFY2(scene.createGroupFromSelection() == nullptr, "编辑锁定时仍能创建分组");
    QVERIFY(scene.groups().isEmpty());
    scene.setEditLocked(false);

    NodeGroupItem *group = scene.createGroupFromSelection();
    QVERIFY(group);

    // 锁定后分组框不可拖动，也不能被解散（与节点"锁定即不可编辑"一致）
    scene.setEditLocked(true);
    QVERIFY(!(group->flags() & QGraphicsItem::ItemIsMovable));
    group->setSelected(true);
    QCOMPARE(scene.dissolveSelectedGroups(), 0);
    QCOMPARE(scene.groups().size(), 1);

    scene.setEditLocked(false);
    QVERIFY(group->flags() & QGraphicsItem::ItemIsMovable);
}

void NodeGroupTest::testUndoRedoRestoresGroups()
{
    FlowScene scene;
    NodeBase *a = addNode(&scene, QStringLiteral("Delay"), QPointF(100, 100));
    NodeBase *b = addNode(&scene, QStringLiteral("Delay"), QPointF(420, 300));
    QVERIFY(a && b);
    selectOnly(&scene, {a, b});
    NodeGroupItem *group = scene.createGroupFromSelection(QStringLiteral("可撤销分组"));
    QVERIFY(group);
    QList<int> ids = group->memberIds();
    std::sort(ids.begin(), ids.end());

    // 撤销建组：分组消失（分组数据随撤销快照走，无需额外维护）
    QVERIFY(scene.undo());
    QVERIFY2(scene.groups().isEmpty(), "撤销后分组未消失");

    // 重做：分组回来，且成员身份与最初一致
    QVERIFY(scene.redo());
    QCOMPARE(scene.groups().size(), 1);
    QList<int> ids2 = scene.groups().first()->memberIds();
    std::sort(ids2.begin(), ids2.end());
    QCOMPARE(ids2, ids);
    for (int id : ids2)
        QVERIFY2(scene.nodeByModuleId(id), "重做后分组成员找不到对应算子");
}

void NodeGroupTest::testExecutionUnaffectedByGroup()
{
    // ② 的核心：分组不得改变执行。真跑两轮（分组前/分组后），比较每轮节点执行次数。
    FlowScene scene;
    FlowExecutor exec;
    exec.setFlowName(QStringLiteral("GroupExecutionRegression"));

    NodeBase *delay = scene.createNode(NodeBase::LOGIC, QPointF(200, 200),
                                       QStringLiteral("Delay"));
    NodeBase *mate = scene.createNode(NodeBase::LOGIC, QPointF(420, 200),
                                      QStringLiteral("Delay"));
    QVERIFY(delay && mate);
    delay->setParam(QStringLiteral("delayMs"), 5);
    mate->setParam(QStringLiteral("delayMs"), 5);

    int okRuns = 0;
    const auto conn = QObject::connect(
        &exec, &FlowExecutor::nodeExecuted, &exec,
        [&okRuns](NodeBase *, bool ok) {
            if (ok)
                ++okRuns;
        },
        Qt::DirectConnection);

    auto runOnce = [&exec, &scene]() {
        exec.setFlowScene(&scene);
        exec.setFlowMode(FlowMode::SoftwareTrigger);
        exec.startExecution();
        bool finished = exec.wait(10000);
        if (!finished) {
            exec.stopExecution();
            finished = exec.wait(3000);
        }
        exec.setFlowScene(nullptr);
        QCoreApplication::processEvents();
        return finished;
    };

    QVERIFY2(runOnce(), "分组前的流程未跑完");
    const int runsBefore = okRuns;
    QVERIFY2(runsBefore >= 1, "分组前流程没有任何节点成功执行，前置条件不成立");

    selectOnly(&scene, {delay, mate});
    NodeGroupItem *group = scene.createGroupFromSelection();
    QVERIFY2(group, "分组创建失败（需选中 ≥2 个算子）");
    QCOMPARE(group->memberCount(), 2);

    QVERIFY2(runOnce(), "分组后的流程未跑完");
    QCOMPARE(okRuns - runsBefore, runsBefore);

    QObject::disconnect(conn);
}

void NodeGroupTest::testModuleIdRestoredOnLoad()
{
    // 本轮顺带修复：加载方案时**从不**恢复模块号 ⇒ 号按"文件里节点的出现顺序"重发，而该顺序来自
    // FlowScene::nodes()（按指针地址排序），与创建顺序不保证一致（删过节点/地址复用即错位）
    // ⇒ {模块号.参数名} 引用、结果表键、配方键会静默指向**另一个**算子。分组也靠模块号认成员，
    // 所以这条是分组功能的前提，单独钉住。
    FlowScene scene;
    NodeBase *a = addNode(&scene, QStringLiteral("Delay"), QPointF(10, 10));
    NodeBase *b = addNode(&scene, QStringLiteral("Formula"), QPointF(300, 10));
    QVERIFY(a && b);
    QVERIFY2(a->moduleId() != b->moduleId(), "同一场景里出现重复模块号");

    QMap<QString, int> before;
    for (NodeBase *n : scene.nodes())
        before[n->name()] = n->moduleId();

    ProjectManager pm;
    const QJsonObject json = pm.sceneToJson(&scene);
    FlowScene restored;
    pm.sceneFromJson(json, &restored);

    QMap<QString, int> after;
    for (NodeBase *n : restored.nodes())
        after[n->name()] = n->moduleId();
    QCOMPARE(after, before);

    // 脏文件防御：人为把两个节点的模块号改成同一个 → 加载后不得出现"一号两算子"
    QJsonObject dirty = json;
    QJsonArray nodes = dirty.value(QStringLiteral("nodes")).toArray();
    if (nodes.size() >= 2) {
        QJsonObject first = nodes.at(0).toObject();
        QJsonObject second = nodes.at(1).toObject();
        second[QStringLiteral("moduleId")] = first.value(QStringLiteral("moduleId"));
        nodes[1] = second;
        dirty[QStringLiteral("nodes")] = nodes;

        FlowScene dup;
        ProjectManager pm2;
        pm2.sceneFromJson(dirty, &dup);
        QSet<int> ids;
        QStringList loadedIds;
        for (NodeBase *n : dup.nodes()) {
            ids.insert(n->moduleId());
            loadedIds << QString::number(n->moduleId());
        }
        QStringList fileIds;
        for (const QJsonValue &v : dirty.value(QStringLiteral("nodes")).toArray())
            fileIds << QString::number(v.toObject().value(QStringLiteral("moduleId")).toInt());
        QVERIFY2(ids.size() == int(dup.nodes().size()),
                 qPrintable(QStringLiteral("脏文件加载后出现重复模块号：加载后=[%1] 文件内=[%2]")
                                .arg(loadedIds.join(QStringLiteral(",")),
                                     fileIds.join(QStringLiteral(",")))));
    }
}

void NodeGroupTest::testNodeMoveUndoConvention()
{
    // 现状钉子（**已知缺陷**，本轮不改，改的是全员交互约定，需单独评审）：
    // 节点拖动在"释放时"才记录撤销 ⇒ 快照是**移动之后**的状态 ⇒ 用户移动算子后第一次
    // Ctrl+Z 看起来"没反应"，要按两次才回到原位。分组拖动已按"移动前记录"实现（一次即复原）。
    // 哪天这条不再成立（第一次撤销即复原），说明约定改了：请同步更新本用例与文档说明。
    //
    // 撤销栈推算：每次 createNode 会先记一条快照，故栈尾依次是
    //   [..., S(建节点A之前), S(建节点B之前), S(移动之后)]
    // 用"被测节点先建、再建一个陪跑节点"来保证第二次撤销后被测节点仍然存在。
    FlowScene scene;
    NodeBase *tested = addNode(&scene, QStringLiteral("Delay"), QPointF(100, 100));
    QVERIFY(tested);
    const int testedId = tested->moduleId();
    NodeBase *scratch = addNode(&scene, QStringLiteral("Delay"), QPointF(500, 500));
    QVERIFY(scratch);

    NodeGraphicsItem *item = scene.getGraphicsItemForNode(tested);
    QVERIFY(item);
    const QPointF origin = tested->position();

    // 复刻现有交互序列：移动 → 释放时记录
    item->setPos(QPointF(500, 400));
    item->syncNodeGeometry();
    scene.recordUndo();

    QVERIFY(scene.undo());
    NodeBase *afterFirstUndo = scene.nodeByModuleId(testedId);
    QVERIFY(afterFirstUndo);
    QCOMPARE(afterFirstUndo->position(), QPointF(500, 400));   // 第一次撤销：位置没变（现状）

    QVERIFY(scene.undo());
    NodeBase *afterSecondUndo = scene.nodeByModuleId(testedId);
    QVERIFY(afterSecondUndo);
    QCOMPARE(afterSecondUndo->position(), origin);             // 第二次才回到原位
}

QTEST_MAIN(NodeGroupTest)
#include "node_group_test.moc"
