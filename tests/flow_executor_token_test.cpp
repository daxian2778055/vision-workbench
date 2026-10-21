// FlowExecutorTokenTest（词条切片 C P3a/P3c 期：令牌与持有面）——执行器生命周期令牌的行为座。
// 腿：构造即接管（解除父子）、释放即回收本体、多个持有者只收尾一次、持有期间本体可调用、
// 释放即同步收尾；P3c 增两腿——节点面经令牌借本体（收尾后借不到）、触发面经令牌取用
//（路由副本强持期间本体不得被收、绑定在收尾后失联）。
// 观测手段用 QPointer（本体被回收即置空），不靠日志读数。
// 红臂破坏位 = 令牌的收尾（等待成功即回收本体）——回收腿当场红，其余腿臂内原样绿。
#include <QtTest>
#include <QPointer>

#include "FlowExecutor.h"
#include "FlowExecutorToken.h"
#include "FlowScene.h"
#include "GlobalTriggerManager.h"
#include "NodeBase.h"

class FlowExecutorTokenTest : public QObject
{
    Q_OBJECT

private slots:
    void testTokenTakesOwnership();       // 构造即接管并解除父子
    void testReleaseReclaimsExecutor();   // 最后一个令牌释放即回收本体
    void testSharedTokenCleansUpOnce();   // 多个持有者只收尾一次
    void testExecutorUsableWhileHeld();   // 持有期间本体可调用
    void testReleaseIsSynchronous();      // 释放返回时本体已收干净（P3b 界面侧依赖的同步语义）
    void testNodeBorrowsExecutorThroughToken();     // P3c：节点面经令牌借本体，收尾后借不到
    void testTriggerBindingBorrowsThroughToken();   // P3c：触发面经令牌取用，路由副本强持期间本体不死
};

void FlowExecutorTokenTest::testTokenTakesOwnership()
{
    FlowExecutor *ex = new FlowExecutor();     // 无父对象
    QPointer<FlowExecutor> watch(ex);
    QVERIFY(watch != nullptr);
    {
        FlowExecutorToken token(ex);
        QCOMPARE(token.executor(), ex);
        // 令牌接管 ⇒ 解除父子（删除权归令牌，父对象不再替它删除）
        QCOMPARE(ex->parent(), nullptr);
    }
    // 令牌析构 ⇒ 本体被回收
    QVERIFY(watch.isNull());
}

void FlowExecutorTokenTest::testReleaseReclaimsExecutor()
{
    QPointer<FlowExecutor> watch;
    {
        FlowExecutor *ex = new FlowExecutor();
        watch = ex;
        auto token = std::make_shared<FlowExecutorToken>(ex);
        QVERIFY(watch != nullptr);
        token.reset();                          // 最后一个令牌释放
    }
    QVERIFY(watch.isNull());
}

void FlowExecutorTokenTest::testSharedTokenCleansUpOnce()
{
    QPointer<FlowExecutor> watch;
    {
        FlowExecutor *ex = new FlowExecutor();
        watch = ex;
        auto token = std::make_shared<FlowExecutorToken>(ex);
        auto holder = token;                    // 第二个持有者
        token.reset();                          // 还有一个持有者 ⇒ 不得回收
        QVERIFY(watch != nullptr);
        holder.reset();                         // 最后一个释放 ⇒ 回收
    }
    QVERIFY(watch.isNull());
}

void FlowExecutorTokenTest::testExecutorUsableWhileHeld()
{
    FlowExecutor *ex = new FlowExecutor();
    QPointer<FlowExecutor> watch(ex);
    {
        FlowExecutorToken token(ex);
        // 持有令牌期间本体必存活且可调用
        QCOMPARE(token.executor()->getState(), ExecutionState::Idle);
        QCOMPARE(token.executor()->getFlowMode(), FlowMode::SoftwareTrigger);
        QVERIFY(watch != nullptr);
    }
    QVERIFY(watch.isNull());
}

void FlowExecutorTokenTest::testReleaseIsSynchronous()
{
    // P3b（销毁权归一）：界面侧的释放点要求「调用方等它停干净」⇒ 释放令牌必须**同步**收尾，
    // 不能把回收推迟到别处（否则界面侧先删本体就与令牌的收尾抢同一个对象）。
    QPointer<FlowExecutor> watch;
    {
        FlowExecutor *ex = new FlowExecutor();
        watch = ex;
        auto token = std::make_shared<FlowExecutorToken>(ex);
        token.reset();                       // 释放
        // 释放返回时本体已收干净（不等事件循环、不等别的持有者）
        QVERIFY(watch.isNull());
    }
    QVERIFY(watch.isNull());
}

void FlowExecutorTokenTest::testNodeBorrowsExecutorThroughToken()
{
    // P3c：节点残余面改持令牌（弱引用）——挂场景后经令牌借得到本体；令牌收尾后借不到，
    // 且**不会**留下悬垂（这正是「收尾后执行器不可再被调用」的节点侧形状）。
    FlowScene scene;
    NodeBase *node = scene.createNode(NodeBase::LOGIC, QPointF(0, 0), QStringLiteral("p3c_node"));
    QVERIFY(node != nullptr);

    auto token = FlowExecutorToken::create(new FlowExecutor());
    FlowExecutor *ex = token->executor();
    ex->setFlowScene(&scene);

    QCOMPARE(node->ownerExecutor(), ex);
    QVERIFY(node->ownerToken() == token);

    token.reset();   // 最后一个强令牌释放 ⇒ 收尾
    QVERIFY(node->ownerExecutor() == nullptr);
    QVERIFY(node->ownerToken() == nullptr);
    QVERIFY(node->ownerContext() == nullptr);
}

void FlowExecutorTokenTest::testTriggerBindingBorrowsThroughToken()
{
    // P3c：触发绑定持令牌（弱引用）——路由取用前先 lock 拿强引用副本 ⇒ 调用期间本体不会被收；
    // 绑定本身不延长寿命 ⇒ 最后一个强令牌释放后绑定失联，绝不指向已死本体。
    FlowScene scene;
    const QString flow = QStringLiteral("p3c_route_probe");
    auto token = FlowExecutorToken::create(new FlowExecutor());
    FlowExecutor *ex = token->executor();
    auto *gtm = GlobalTriggerManager::instance();

    gtm->registerFlow(flow, &scene, token);
    QCOMPARE(gtm->executorForFlow(flow), ex);

    std::shared_ptr<FlowExecutorToken> route = gtm->tokenForFlow(flow);
    QVERIFY(route != nullptr);
    QCOMPARE(route->executor(), ex);

    // 路由副本强持期间放掉界面侧令牌：本体必须还在（结构性保证，不靠线程时序）
    token.reset();
    QCOMPARE(route->executor(), ex);
    QCOMPARE(route->executor()->getState(), ExecutionState::Idle);

    route.reset();   // 最后一个强引用释放 ⇒ 收尾
    QVERIFY(gtm->executorForFlow(flow) == nullptr);   // 绑定失联（弱引用过期）
}

QTEST_MAIN(FlowExecutorTokenTest)
#include "flow_executor_token_test.moc"
