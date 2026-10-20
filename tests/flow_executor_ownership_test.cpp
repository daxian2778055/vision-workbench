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
#include <QPointF>
#include <QString>

#include "FlowExecutor.h"
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
        FlowExecutor executor;
        executor.setFlowScene(&scene);
        // 挂上场景即建立归属
        QCOMPARE(node->ownerExecutor(), static_cast<FlowExecutor *>(&executor));
    }

    // 执行器已析构：节点的归属指针必须被清空（红臂破坏位就在这一位）
    QVERIFY(node->ownerExecutor() == nullptr);
}

void FlowExecutorOwnershipTest::testUnbindKeepsOwnership()
{
    FlowScene scene;
    NodeBase *node = scene.createNode(NodeBase::LOGIC, QPointF(0, 0), QStringLiteral("own_node"));
    QVERIFY(node != nullptr);

    FlowExecutor executor;
    executor.setFlowScene(&scene);
    QCOMPARE(node->ownerExecutor(), static_cast<FlowExecutor *>(&executor));

    // 解绑（置空场景）刻意不清归属：节点可能已转由别的执行器接管，清成空反而退回全局单例兜底
    executor.setFlowScene(nullptr);
    QCOMPARE(node->ownerExecutor(), static_cast<FlowExecutor *>(&executor));
}

void FlowExecutorOwnershipTest::testTriggerBindingUnregisteredOnDestruction()
{
    FlowScene scene;
    const QString flowName = QStringLiteral("ownership_probe_flow");
    {
        FlowExecutor executor;
        GlobalTriggerManager::instance()->registerFlow(flowName, &scene, &executor);
        QCOMPARE(GlobalTriggerManager::instance()->executorForFlow(flowName),
                 static_cast<FlowExecutor *>(&executor));
    }
    // 执行器析构内按身份注销：绑定不再指向已死执行器
    QVERIFY(GlobalTriggerManager::instance()->executorForFlow(flowName) == nullptr);
}

QTEST_MAIN(FlowExecutorOwnershipTest)
#include "flow_executor_ownership_test.moc"
