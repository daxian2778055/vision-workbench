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
#include "FlowSceneAliveBlock.h"   // 切片 C 段二：块腿完整类型（显式引，不靠 FlowScene.h 传递）
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
    void testAliveBlockGoneMarkedOnSceneDestruction();  // ⑦ 段二：场景亡⇒其块落墓碑（售票判走；红臂靶）
    void testAliveBlockPerSceneBindingServe();  // ⑧ 段二：每场景自代各自的块（在世服务面，死亡面由⑦照管）
void testAliveBlockBoundViaProductionSetFlowScene();  // ⑨ 段三：生产 setFlowScene 自带 bind（发布合同语义半边；红臂靶）
    void testSameSceneRebindSkipsCacheChurn();  // ⑩ 段四·U-217：同场景重绑不走慢路径（切页对运行中执行器的竞态源）
};

void FlowExecutorOwnershipTest::testCurrentSingletonLifecycle()
{
    FlowExecutor *alive = nullptr;
    {
        auto executorToken = FlowExecutorToken::create(new FlowExecutor());
        FlowExecutor &executor = *executorToken->executor();
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
    auto executorToken = FlowExecutorToken::create(new FlowExecutor());
    FlowExecutor &executor = *executorToken->executor();
    executor.stopExecution();
    QCOMPARE(executor.getState(), ExecutionState::Stopped);
    QVERIFY(!executor.interruptibleSleep(50));
}

void FlowExecutorOwnershipTest::testAliveBlockGoneMarkedOnSceneDestruction()
{
    // 段二（立项书 §13.1）：场景亡 ⇒ 析构体首在自己块上落墓碑（markGone）⇒ 之后售票判走
    // （不再可借）。座纪律照旧：不启动工作线程、不把执行器带进悬垂面——腿只借块本身说话。
    FlowScene *scene = new FlowScene;
    std::shared_ptr<FlowSceneAliveBlock> block = scene->aliveBlockShared();
    QVERIFY(block != nullptr);
    block->bind(scene);   // 执行器 setFlowScene 的同款绑定
    QCOMPARE(block->generation(), quint64(1));
    {
        FlowSceneAliveBlock::CaptureTicket inLife = block->acquireForCapture();
        QVERIFY(inLife.alive());
        QCOMPARE(inLife.scene(), scene);
    }   // 在世窗口服务正常
    delete scene;   // 无在飞快照（M-2 安静）；析构体首动作＝落墓碑
    {
        FlowSceneAliveBlock::CaptureTicket afterDeath = block->acquireForCapture();
        QVERIFY2(!afterDeath.alive(),
                 "scene death must tombstone its alive block (markGone first action missing?)");
        QCOMPARE(afterDeath.scene(), static_cast<FlowScene *>(nullptr));
    }   // 票先还锁、块才收核（座位纪律：块不得带着未归还的借锁收核——QMutex 悬锁即挂死）
    block.reset();   // 最后一个引用放 ⇒ 块真析构（此时其互斥不在任何活票手上）
}

void FlowExecutorOwnershipTest::testAliveBlockPerSceneBindingServe()
{
    // 段二生产形：每场景自建自己的块（执行器 setFlowScene 取当值场景自代的块）。
    // 两场景两块、绑定后各自在世售票：alive 且 scene() 各指当值场景；换绑计数逐块独立。
    // 死亡面的断言不在本腿（由腿⑦照管）——本腿保持死亡断言零依赖 ⇒ 红臂只打⑦。
    FlowScene *s1 = new FlowScene;
    FlowScene *s2 = new FlowScene;
    std::shared_ptr<FlowSceneAliveBlock> b1 = s1->aliveBlockShared();
    std::shared_ptr<FlowSceneAliveBlock> b2 = s2->aliveBlockShared();
    QVERIFY(b1 != nullptr);
    QVERIFY(b2 != nullptr);
    QVERIFY2(b1 != b2, "each scene must own its own alive block");
    b1->bind(s1);   // 执行器 setFlowScene(s1) 的同款动作
    b2->bind(s2);   // 执行器 setFlowScene(s2) 的同款动作
    {
        FlowSceneAliveBlock::CaptureTicket t1 = b1->acquireForCapture();
        FlowSceneAliveBlock::CaptureTicket t2 = b2->acquireForCapture();
        QVERIFY(t1.alive());
        QCOMPARE(t1.scene(), s1);
        QVERIFY(t2.alive());
        QCOMPARE(t2.scene(), s2);
    }   // 票先还锁：generation() 须取同一把块锁，票在手上时再取＝同线程死等（座内已踩一课）
    QCOMPARE(b1->generation(), quint64(1));
    QCOMPARE(b2->generation(), quint64(1));
    // 清座：块随 shared_ptr 落底（块本身不拒收场景退出）；两场景各自析构落各自的墓碑（无断言依赖）。
    b1.reset();
    b2.reset();
    delete s1;
    delete s2;
}

void FlowExecutorOwnershipTest::testAliveBlockBoundViaProductionSetFlowScene()
{
    // 段三回归腿（U-213，r11 复核 P1-6 同轮）：生产 setFlowScene 必须自带 bind——座位世界不再
    // 手写 bind、只走生产路径，块仍能售票（发布合同运行时可见的一半：bind 落笔）。
    // 锁内发布/锁外减是形状判据（普查直读，运行时无确定性数据竞争判据）；本腿红臂＝撤 bind 调用。
    FlowScene scene;
    auto token = FlowExecutorToken::create(new FlowExecutor());
    FlowExecutor *exec = token->executor();
    exec->setFlowName(QStringLiteral("ownership_publish_probe"));
    exec->setFlowScene(&scene);   // 生产路径：无任何手写 bind
    const std::shared_ptr<FlowSceneAliveBlock> block = scene.aliveBlockShared();
    QVERIFY(block != nullptr);
    QCOMPARE(block->generation(), quint64(1));   // bind 已由生产路径落笔（换绑计数）
    {
        FlowSceneAliveBlock::CaptureTicket inLife = block->acquireForCapture();
        QVERIFY2(inLife.alive(),
                 "production setFlowScene must bind the scene's own block (bind missing from publish path?)");
        QCOMPARE(inLife.scene(), &scene);
    }
    // 换绑面：换第二场景 ⇒ 新块同票形（换绑计数逐块独立；换绑即换天然精准）。
    FlowScene scene2;
    exec->setFlowScene(&scene2);
    const std::shared_ptr<FlowSceneAliveBlock> b2 = scene2.aliveBlockShared();
    QCOMPARE(b2->generation(), quint64(1));
    {
        FlowSceneAliveBlock::CaptureTicket t2 = b2->acquireForCapture();
        QVERIFY(t2.alive());
        QCOMPARE(t2.scene(), &scene2);
    }
}

void FlowExecutorOwnershipTest::testSameSceneRebindSkipsCacheChurn()
{
    // 段四回归腿（U-217，r12 复核 P1-6 余量）：同场景重绑必须走幂等快速路——**不走慢路径**。
    // 机制：切回已绑定的同场景页签（executorForScene 复用每场景执行器）内容恒等，慢路径的
    // 断/接信号与整块图缓存清空是纯盖章；且目标执行器可能在 Running（多流程自启后切页），
    // worker 正在轮内无锁读循环体/子流集合——「锁内清 vs 无锁读」＝U-213 收口后残余的混锁
    // 竞态源。观测用只读计数器（不参与执行决策：慢路径清空一次 +1，快速路不计）。
    // 红臂破坏位＝同场景快速路守卫撤除（慢路径无条件执行 ⇒ 计数第二次自增，本腿当场红）。
    FlowScene scene;
    NodeBase *first = scene.createNode(NodeBase::LOGIC, QPointF(0, 0), QStringLiteral("churn_first"));
    QVERIFY(first != nullptr);
    auto token = FlowExecutorToken::create(new FlowExecutor());
    FlowExecutor *exec = token->executor();
    exec->setFlowScene(&scene);
    const quint64 afterFirst = exec->graphCacheClearCount();
    QCOMPARE(afterFirst, quint64(1));   // 首绑＝慢路径一次清

    // 同场景重绑（切回 tab 的真实形状）：计数不得再涨——归属与 current() 语义照旧保留。
    exec->setFlowScene(&scene);
    QCOMPARE(exec->graphCacheClearCount(), afterFirst);
    NodeBase *late = scene.createNode(NodeBase::LOGIC, QPointF(10, 10), QStringLiteral("churn_late"));
    QVERIFY(late != nullptr);
    exec->setFlowScene(&scene);   // 快速路仍须补挂晚到节点的归属
    QCOMPARE(late->ownerExecutor(), exec);
    QCOMPARE(late->ownerToken(), token);
    QCOMPARE(FlowExecutor::current(), exec);   // 最近激活执行器登记照旧

    // 跨场景换绑与解绑仍走慢路径（清空语义属它们）：计数继续自增。
    FlowScene other;
    exec->setFlowScene(&other);
    QCOMPARE(exec->graphCacheClearCount(), afterFirst + 1);
    exec->setFlowScene(nullptr);
    QCOMPARE(exec->graphCacheClearCount(), afterFirst + 2);

    // 复绑任意新场景后，同场景幂等再走快速路（换场景后的第一次重绑必须先慢一次）：
    exec->setFlowScene(&scene);
    QCOMPARE(exec->graphCacheClearCount(), afterFirst + 3);
    exec->setFlowScene(&scene);
    QCOMPARE(exec->graphCacheClearCount(), afterFirst + 3);
}

QTEST_MAIN(FlowExecutorOwnershipTest)
#include "flow_executor_ownership_test.moc"