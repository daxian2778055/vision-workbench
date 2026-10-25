// 被拒的连线不得在撤销栈里留步（2026-10-25 复核 P3-11）。
//
// 缺陷：FlowScene::createConnection 先 recordUndo() 再做语义/闭环校验。一次被拒的连线
// （自连、类型不匹配、成环）也会往撤销栈压一步快照，并清空 redo 栈——现场表现是
// "线没连上，但按一次 Ctrl+Z 却撤掉了别的东西 / 重做莫名其妙没了"。
//
// 修法：recordUndo 移到校验通过之后。本用例钉住两侧：① 被拒连线后 canUndo 仍为 false；
// ② 成功连线照旧记撤销、能撤能重做（不是把撤销关掉）。
#include "FlowScene.h"
#include "NodeBase.h"
#include "NodeRegistry.h"
#include "Port.h"

#include <QtTest/QtTest>
#include <QPointF>

class RejectedConnectionUndoTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void rejectedConnectionLeavesNoUndoStep();
    void acceptedConnectionStillRecordsUndo();
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

void RejectedConnectionUndoTest::initTestCase()
{
    registerAllNodes();
}

void RejectedConnectionUndoTest::rejectedConnectionLeavesNoUndoStep()
{
    FlowScene scene;
    NodeBase *a = addNode(&scene, QPointF(0, 0));
    QVERIFY(a);

    int rejections = 0;
    QObject::connect(&scene, &FlowScene::connectionRejected, &scene,
                     [&rejections](const QString &) { ++rejections; });

    // 自连：同一算子的输出接自己的输入（非放宽语义 = 用户拖拽那条路）
    MyProject::Connection *c = scene.createConnection(a->outputPorts().first(),
                                                      a->inputPorts().first(), false);
    QVERIFY2(c == nullptr, "自连本应被拒（用例前提不成立）");
    QCOMPARE(rejections, 1);
    QCOMPARE(scene.connections().size(), 0);

    // 核心断言：被拒的连线不该留下任何撤销步
    QVERIFY2(!scene.canUndo(),
             "被拒的连线在撤销栈里留了步：按 Ctrl+Z 会撤掉别的东西（P3-11）");
    QVERIFY2(!scene.canRedo(), "被拒的连线还清空了 redo 栈");
}

void RejectedConnectionUndoTest::acceptedConnectionStillRecordsUndo()
{
    FlowScene scene;
    NodeBase *a = addNode(&scene, QPointF(0, 0));
    NodeBase *b = addNode(&scene, QPointF(200, 0));
    QVERIFY(a && b);

    QVERIFY(scene.createConnection(a->outputPorts().first(),
                                   b->inputPorts().first(), false));
    QCOMPARE(scene.connections().size(), 1);
    QVERIFY2(scene.canUndo(), "成功的连线必须记撤销（修复不能把撤销功能关掉）");

    QVERIFY(scene.undo());
    QCOMPARE(scene.connections().size(), 0);
    QVERIFY(scene.canRedo());
    QVERIFY(scene.redo());
    QCOMPARE(scene.connections().size(), 1);
}

QTEST_MAIN(RejectedConnectionUndoTest)
#include "rejected_connection_undo_test.moc"
