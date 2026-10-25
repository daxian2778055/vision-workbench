// 打开方案后撤销栈必须为空（2026-10-25 复核 P2-2）。
//
// 现象：sceneFromJson 内部每次 createNode / createConnection 都会 recordUndo
// （m_restoring 只在 restoreSnapshot 内置位，载入路径没置），于是一个 20 节点的方案载入后
// 撤销栈里约堆着 39 个半成品快照（上限 50）。用户打开方案后按一次 Ctrl+Z，刚载入的流程
// 就被拆掉最后一条线/一个节点——"一打开就坏"的观感。
//
// 修法：ProjectManager::sceneFromJson 末尾调 FlowScene::clearUndoHistory()（放在更里层，
//       打开方案/崩溃恢复/片段/测试所有调用方都经过它）。
// 本用例钉住两侧：① 载入后 canUndo/canRedo 都为 false；② 之后正常编辑仍能记录、能撤销
//（证明不是把撤销功能整个关掉）。
#include "FlowScene.h"
#include "ProjectManager.h"
#include "NodeBase.h"
#include "NodeRegistry.h"

#include <QtTest/QtTest>
#include <QPointF>

class UndoHistoryAfterLoadTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void loadClearsUndoHistory();
    void editingStillRecordsUndoAfterLoad();
};

namespace {

NodeBase *addNode(FlowScene *scene, const QPointF &pos)
{
    NodeBase *n = scene->createNodeByTypeIdOrName(QStringLiteral("DelayNode"),
                                                  NodeBase::LOGIC,
                                                  QStringLiteral("Delay"));
    if (n)
        scene->adoptNode(n, pos);
    return n;
}

} // namespace

void UndoHistoryAfterLoadTest::initTestCase()
{
    registerAllNodes();
}

void UndoHistoryAfterLoadTest::loadClearsUndoHistory()
{
    FlowScene scene;
    NodeBase *a = addNode(&scene, QPointF(0, 0));
    NodeBase *b = addNode(&scene, QPointF(200, 0));
    NodeBase *c = addNode(&scene, QPointF(400, 0));
    QVERIFY(a && b && c);
    QVERIFY(scene.createConnection(a->outputPorts().first(), b->inputPorts().first(), true));
    QVERIFY(scene.createConnection(b->outputPorts().first(), c->inputPorts().first(), true));

    // 先在原场景上制造几步真实撤销历史，证明下面"栈空"不是一开始就空
    QVERIFY(scene.canUndo());

    ProjectManager pm;
    const QJsonObject json = pm.sceneToJson(&scene);

    FlowScene restored;
    pm.sceneFromJson(json, &restored);
    QCOMPARE(restored.nodes().size(), 3);
    QCOMPARE(restored.connections().size(), 2);

    // 核心断言：载入后撤销/重做栈必须为空
    QVERIFY2(!restored.canUndo(),
             "载入后撤销栈非空：第一次 Ctrl+Z 会拆掉刚打开的流程（半成品快照堆栈）");
    QVERIFY2(!restored.canRedo(), "载入后重做栈非空");
}

void UndoHistoryAfterLoadTest::editingStillRecordsUndoAfterLoad()
{
    FlowScene scene;
    NodeBase *a = addNode(&scene, QPointF(0, 0));
    NodeBase *b = addNode(&scene, QPointF(200, 0));
    QVERIFY(a && b);

    ProjectManager pm;
    FlowScene restored;
    pm.sceneFromJson(pm.sceneToJson(&scene), &restored);
    QVERIFY(!restored.canUndo());
    QCOMPARE(restored.nodes().size(), 2);
    QCOMPARE(restored.connections().size(), 0);

    // 载入后做一次**会记撤销**的真实编辑（连线：createConnection 内部 recordUndo）：
    // 必须能记、能撤、能重做——证明清空的是"载入残留"，不是把撤销功能关掉。
    const QList<NodeBase *> nodes = restored.nodes();
    QCOMPARE(nodes.size(), 2);
    NodeBase *n1 = nodes.value(0);
    NodeBase *n2 = nodes.value(1);
    QVERIFY(n1 && n2);
    QVERIFY(restored.createConnection(n1->outputPorts().first(),
                                      n2->inputPorts().first(), true));
    QCOMPARE(restored.connections().size(), 1);
    QVERIFY(restored.canUndo());

    QVERIFY(restored.undo());
    QCOMPARE(restored.connections().size(), 0);
    QVERIFY(restored.canRedo());

    QVERIFY(restored.redo());
    QCOMPARE(restored.connections().size(), 1);
}

QTEST_MAIN(UndoHistoryAfterLoadTest)
#include "undo_history_after_load_test.moc"
