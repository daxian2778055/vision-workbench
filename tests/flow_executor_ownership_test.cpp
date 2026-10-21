// FlowExecutorOwnershipTest（切片 C 立项书 P0 期：所有权契约断言座）
//
// 目的：把「谁持谁、谁必须先释放」从注释里的约定变成可执行断言——P1 至 P4 都要动所有权面
// （触发注册、全局单例、节点归属指针、快照与墓碑），本座是那些期次的「行为零变更」基线护栏。
//
// 四条所有权事实（每条一腿，断言的是既有行为，本轮零行为改动）：
//  ① 全局单例：执行器构造即登记为当前实例，析构即置空（不再指向已死对象）。
//  ② 节点归属指针：执行器析构时把仍活着的节点的归属指针清空（已死执行器不留悬垂指向）。
//  ③ 解绑不清归属：解绑场景（置空）刻意保留节点归属指针——节点可能已转由别的执行器接管，
//     清成空反而退回全局单例兜底。
//  ④ 触发面配对：按执行器身份注册在触发管理器里的绑定，随执行器析构一并注销。
//
// fixture：真 FlowScene + 真节点（场景自建节点）+ 真执行器（不启动工作线程——本座只验所有权面）。
// 红臂破坏位 = 析构内的归属指针后置清空段（移除即「已死执行器仍在节点上留指向」）。
#include <QtTest>
#include <QCoreApplication>
#include <QPointF>
#include <QString>
#include <QThread>

#include "CommunicationManager.h"
#include "FlowExecutor.h"
#include "FlowExecutorToken.h"
#include "FlowScene.h"
#include "GlobalTriggerManager.h"
#include "NodeBase.h"

class FlowExecutorOwnershipTest : public QObject
{
    Q_OBJECT

private slots:
    void testCurrentSingletonLifecycle();   // ① 构造即登记、析构即置空
    void testOwnerPointerClearedOnDestruction();  // ② 析构后置清空活节点归属指针
    void testUnbindKeepsOwnership();        // ③ 解绑不清归属
    void testTriggerBindingUnregisteredOnDestruction();  // ④ 触发面按执行器身份配对注销
    void testTriggerRoutingRunsOnAppThread();  // ⑤ 触发路由与执行器析构同线程（线程前提）
    void testExecutorSleepStopsWithExecutor();  // ⑥ 执行器停后其可取消等待立即返回未完成（P2b 接线）
};

void FlowExecutorOwnershipTest::testCurrentSingletonLifecycle()
{
    FlowExecutor *alive = nullptr;
    {
        FlowExecutor executor;
        alive = &executor;
        // 构造即登记为当前实例（节点侧查运行状态走这条）
        QCOMPARE(FlowExecutor::current(), static_cast<FlowExecutor *>(&executor));
    }
    // 析构即置空：全局单例不得再指向已死执行器
    QVERIFY(FlowExecutor::current() != alive);
}

void FlowExecutorOwnershipTest::testOwnerPointerClearedOnDestruction()
{
    FlowScene scene;
    NodeBase *node = scene.createNode(NodeBase::LOGIC, QPointF(0, 0), QStringLiteral("own_node"));
    QVERIFY(node != nullptr);

    {
        // P3c：节点面改持令牌 ⇒ 本体由令牌拥有（栈上本体没有令牌，节点借不到本体）
        auto token = FlowExecutorToken::create(new FlowExecutor());
        FlowExecutor *executor = token->executor();
        executor->setFlowScene(&scene);
        // 挂上场景即建立归属
        QCOMPARE(node->ownerExecutor(), executor);
        QVERIFY(node->ownerToken() == token);
    }   // 最后一个令牌释放 ⇒ 收尾（本体析构）

    // 执行器已析构：节点的归属必须已失联（令牌过期后借不到本体；红臂破坏位就在这一位）
    QVERIFY(node->ownerExecutor() == nullptr);
    QVERIFY(node->ownerToken() == nullptr);
}

void FlowExecutorOwnershipTest::testUnbindKeepsOwnership()
{
    FlowScene scene;
    NodeBase *node = scene.createNode(NodeBase::LOGIC, QPointF(0, 0), QStringLiteral("own_node"));
    QVERIFY(node != nullptr);

    auto token = FlowExecutorToken::create(new FlowExecutor());
    FlowExecutor *executor = token->executor();
    executor->setFlowScene(&scene);
    QCOMPARE(node->ownerExecutor(), executor);

    // 解绑（置空场景）刻意不清归属：节点可能已转由别的执行器接管，清成空反而退回全局单例兜底
    executor->setFlowScene(nullptr);
    QCOMPARE(node->ownerExecutor(), executor);
}

void FlowExecutorOwnershipTest::testTriggerBindingUnregisteredOnDestruction()
{
    FlowScene scene;
    const QString flowName = QStringLiteral("ownership_probe_flow");
    auto token = FlowExecutorToken::create(new FlowExecutor());
    FlowExecutor *executor = token->executor();
    GlobalTriggerManager::instance()->registerFlow(flowName, &scene, token);
    QCOMPARE(GlobalTriggerManager::instance()->executorForFlow(flowName), executor);

    // P3c：绑定只持令牌的弱引用 ⇒ 释放最后一个强令牌即收尾，本体析构内按身份注销
    token.reset();
    QVERIFY(GlobalTriggerManager::instance()->executorForFlow(flowName) == nullptr);
}

void FlowExecutorOwnershipTest::testTriggerRoutingRunsOnAppThread()
{
    // 线程前提：触发管理器单例的归属线程 = 首次创建它的线程（现状＝界面线程，
    // 因为首个调用发生在界面线程构造执行器时）。触发路由在该线程执行，故它与执行器
    // 析构同线程——路由里「出锁后解引用执行器指针」的那一小段窗口不会与析构并发
    // （析构的注销动作也要取触发管理器这把锁）。
    QThread *appThread = QCoreApplication::instance()->thread();
    QCOMPARE(GlobalTriggerManager::instance()->thread(), appThread);

    FlowScene scene;
    auto token = FlowExecutorToken::create(new FlowExecutor());
    FlowExecutor *executor = token->executor();
    // 硬触发模式：路由匹配即通知，但不受理外部触发、不起工作线程（观测线程用）
    executor->setFlowMode(FlowMode::HardwareTrigger);

    const QString flowName = QStringLiteral("ownership_route_probe");
    const QString trigger = QStringLiteral("PROBE_TRIGGER");
    GlobalTriggerManager::instance()->registerFlow(flowName, &scene, token);
    QVERIFY(GlobalTriggerManager::instance()->setStringTrigger(trigger, flowName));

    QThread *observed = nullptr;
    QObject::connect(GlobalTriggerManager::instance(), &GlobalTriggerManager::triggerFired,
                     executor,
                     [&observed](const QString &, const QString &) {
                         observed = QThread::currentThread();
                     },
                     Qt::DirectConnection);

    // 走生产路径：通信数据到达 → 触发管理器路由（不直接调槽，以保留真实连接语义）
    emit CommunicationManager::instance()->dataReceived(QStringLiteral("probe_device"),
                                                        trigger.toUtf8());
    QTRY_VERIFY(observed != nullptr);
    QCOMPARE(observed, appThread);

    GlobalTriggerManager::instance()->removeStringTrigger(trigger);
    GlobalTriggerManager::instance()->unregisterFlow(flowName);
}

void FlowExecutorOwnershipTest::testExecutorSleepStopsWithExecutor()
{
    // P2b 接线：执行器的可取消等待已委给运行上下文块（状态与取消信号在块内）。
    // 停后等待必须立即返回「未完成」——块被停在状态上时同形。
    FlowExecutor executor;
    executor.stopExecution();
    QCOMPARE(executor.getState(), ExecutionState::Stopped);
    QVERIFY(!executor.interruptibleSleep(50));
}

QTEST_MAIN(FlowExecutorOwnershipTest)
#include "flow_executor_ownership_test.moc"