// NodeRunContextTest（词条切片 C P2c 期：算子侧改持块）——归属接线与读面的真图座。
// 四腿：节点上下文与执行器同源、执行器析构后节点上下文清空、未接管节点归属槽为空、
// 单例兜底（静态当前上下文与执行器同源）。fixture 用真场景与真节点（场景自建节点）。
// 红臂破坏位 = 析构内的归属清空段（移除即已死执行器在节点上留指向）——清空腿当场红，余腿原样绿。
#include <QtTest>
#include "FlowExecutorToken.h"
#include <QPointF>

#include "FlowExecutor.h"
#include "FlowScene.h"
#include "NodeBase.h"

class NodeRunContextTest : public QObject
{
    Q_OBJECT

private slots:
    void testNodeContextSameSourceAsExecutor();  // 挂场景后节点上下文与执行器同源
    void testContextClearedOnExecutorDeath();   // 执行器析构后节点上下文清空
    void testNodeWithoutContextHasEmptySlot();  // 未被接管的节点归属槽为空
    void testCurrentContextFallbackHolds();     // 单例兜底：静态当前上下文与执行器同源
};

void NodeRunContextTest::testNodeContextSameSourceAsExecutor()
{
    FlowScene scene;
    NodeBase *node = scene.createNode(NodeBase::LOGIC, QPointF(0, 0), QStringLiteral("ctx_node"));
    QVERIFY(node != nullptr);

    auto executorToken = FlowExecutorToken::create(new FlowExecutor());
    FlowExecutor &executor = *executorToken->executor();
    executor.setFlowScene(&scene);

    // 归属上下文已交到节点上，且与执行器同源（同一块 ⇒ 状态读数一致）
    QVERIFY(node->ownerContext() != nullptr);
    QCOMPARE(node->ownerContext()->state(), executor.getState());
    QCOMPARE(node->ownerContext()->flowMode(), executor.getFlowMode());

    // 执行器状态一变，节点侧读面同步（对照断言：块状态＝主体状态）
    executor.stopExecution();
    QCOMPARE(node->ownerContext()->state(), ExecutionState::Stopped);
    QCOMPARE(node->ownerContext()->state(), executor.getState());
}

void NodeRunContextTest::testContextClearedOnExecutorDeath()
{
    FlowScene scene;
    NodeBase *node = scene.createNode(NodeBase::LOGIC, QPointF(0, 0), QStringLiteral("ctx_node"));
    QVERIFY(node != nullptr);
    {
        auto executorToken = FlowExecutorToken::create(new FlowExecutor());
        FlowExecutor &executor = *executorToken->executor();
        executor.setFlowScene(&scene);
        QVERIFY(node->ownerContext() != nullptr);
    }
    // 执行器已死：节点上不得留指向（今天的语义原样）
    QVERIFY(node->ownerContext() == nullptr);
    QVERIFY(node->ownerExecutor() == nullptr);
}

void NodeRunContextTest::testNodeWithoutContextHasEmptySlot()
{
    FlowScene scene;
    NodeBase *node = scene.createNode(NodeBase::LOGIC, QPointF(0, 0), QStringLiteral("lonely"));
    QVERIFY(node != nullptr);
    // 设计期与自检路径：没有执行器上下文 ⇒ 归属槽为空
    QVERIFY(node->ownerContext() == nullptr);
    QVERIFY(node->ownerExecutor() == nullptr);
}

void NodeRunContextTest::testCurrentContextFallbackHolds()
{
    auto executorToken = FlowExecutorToken::create(new FlowExecutor());
    FlowExecutor &executor = *executorToken->executor();
    const auto ctx = FlowExecutor::currentContext();
    QVERIFY(ctx != nullptr);
    // 兜底块与执行器同源
    QCOMPARE(ctx->state(), executor.getState());
    QCOMPARE(ctx->flowMode(), executor.getFlowMode());
}

QTEST_MAIN(NodeRunContextTest)
#include "node_run_context_test.moc"
