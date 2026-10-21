// FlowExecutorTokenTest（词条切片 C P3a 期：令牌与持有面）——执行器生命周期令牌的行为座。
// 四腿：构造即接管（解除父子）、释放即回收本体、多个持有者只收尾一次、持有期间本体可调用。
// 观测手段用 QPointer（本体被回收即置空），不靠日志读数。
// 红臂破坏位 = 令牌的收尾（等待成功即回收本体）——回收腿当场红，其余腿臂内原样绿。
#include <QtTest>
#include <QPointer>

#include "FlowExecutor.h"
#include "FlowExecutorToken.h"

class FlowExecutorTokenTest : public QObject
{
    Q_OBJECT

private slots:
    void testTokenTakesOwnership();       // 构造即接管并解除父子
    void testReleaseReclaimsExecutor();   // 最后一个令牌释放即回收本体
    void testSharedTokenCleansUpOnce();   // 多个持有者只收尾一次
    void testExecutorUsableWhileHeld();   // 持有期间本体可调用
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

QTEST_MAIN(FlowExecutorTokenTest)
#include "flow_executor_token_test.moc"
