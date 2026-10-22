// NodeStateBodyTest（切片 C 立项书 P2 期段一：身份体与参数内核随快照共享化）——壳亡语义座。
//
// 两腿纯逻辑断言（① 身份腿＝红臂靶　② 参数内核腿）：
//  ① 身份体随快照列入列、与壳同值可读 → 刻意壳先亡（场景销毁，M-2 断言在用例内临时关闭
//     并恢复）→ 经快照列仍可读（立项书 §13.1 净收益：壳亡后状态仍在）→ 句柄释放后引用
//     归零（身份体真析构在壳的生命之外发生，且身份体非 QObject，可在释放线程析构）。
//  ② 参数内核（ThreadSafeParams 自带读写锁的 Core）随快照列入列、加锁读同值 → 壳先亡
//     仍加锁可读 → 释放后归零。
//
// fixture：真 FlowScene + 真节点（LOGIC 类目映射为裸 HalconNode——带参数表家族）；
// 场景与快照句柄都走堆，显式控制「先杀壳、再释句柄」的顺序（栈对象析构逆序做不到）。
// 座侧自纠（如实记）：座内的硬引用拷贝（body/core 局部 shared_ptr）须在壳亡前出块收掉——
// 否则引用归零断言被座侧自己的拷贝托住（首跑 Totals 2 passed 2 failed，全红在 expired 断言）；
// 出块后快照列是壳亡后的唯一残余持有者，判定面才真正落在机制上。
//
// 红臂破坏位 = captureGraphSnapshot 的身份列填充（塞空指针 ⇒ 状态体失去共享所有权）
// ——身份腿当场红，参数内核腿原样绿。
#include <QtTest>
#include <QPointF>
#include <QString>
#include <QReadLocker>
#include <memory>

#include "FlowScene.h"
#include "NodeBase.h"
#include "NodeStateBody.h"
#include "ThreadSafeParams.h"

class NodeStateBodyTest : public QObject
{
    Q_OBJECT

private slots:
    void testIdentityBodySharedOwnership();   // ① 身份腿（红臂靶）
    void testParamsCoreSharedOwnership();     // ② 参数内核腿
};

void NodeStateBodyTest::testIdentityBodySharedOwnership()
{
    const QString kName = QStringLiteral("身份体座");
    std::weak_ptr<NodeStateBody> wId;
    {
        auto *scene = new FlowScene;
        NodeBase *node = scene->createNode(NodeBase::LOGIC, QPointF(0, 0), kName);
        QVERIFY(node != nullptr);
        node->setName(kName);   // NodeFactory 默认类目会就地命名（LOGIC→「逻辑」），显式重命名后再比对
        const int idSave = node->moduleId();
        auto guard = std::make_unique<FlowScene::GraphSnapshotGuard>(scene->captureGraphSnapshot());
        QVERIFY(!guard->snapshot().nodeStates.isEmpty());
        {
            std::shared_ptr<NodeStateBody> body = guard->snapshot().nodeStates.first();
            QVERIFY(body != nullptr);                // 非共享破坏位在此现形（红臂）
            wId = body;
            QVERIFY(wId.lock() != nullptr);          // 持有期：身份体随快照活着
            QCOMPARE(body->moduleId, idSave);
            QCOMPARE(body->name, kName);
        }   // 座侧硬引用在此出块；此后日期读面一律走快照列与弱引用（快照列是唯一残余持有者）
        // 刻意壳先亡（M-2 断言在用例内临时关闭，与本用例族同口径；收尾恢复）
        FlowScene::setDanglingSnapshotAssertEnabled(false);
        delete scene;                                // 壳随 QObject 父子树全亡；身份体由快照列扛住
        QVERIFY(wId.lock() != nullptr);              // §13.1 净收益：壳亡后仍可读
        QCOMPARE(wId.lock()->moduleId, idSave);
        QCOMPARE(wId.lock()->name, kName);
        guard.reset();                               // 释句柄（场景已亡：releaseHeld 空转）
        QVERIFY(wId.expired());                      // 引用归零：真析构发生在壳的生命之外
        FlowScene::setDanglingSnapshotAssertEnabled(true);
    }
}

void NodeStateBodyTest::testParamsCoreSharedOwnership()
{
    std::weak_ptr<ThreadSafeParams::Core> wCore;
    const QVariant kVal(12345);
    {
        auto *scene = new FlowScene;
        NodeBase *node = scene->createNode(NodeBase::LOGIC, QPointF(0, 0), QStringLiteral("参数内核座"));
        QVERIFY(node != nullptr);
        node->setParam(QStringLiteral("params_seat_key"), kVal);
        auto guard = std::make_unique<FlowScene::GraphSnapshotGuard>(scene->captureGraphSnapshot());
        QVERIFY(!guard->snapshot().paramCores.isEmpty());
        {
            std::shared_ptr<ThreadSafeParams::Core> core = guard->snapshot().paramCores.first();
            QVERIFY(core != nullptr);                // 裸 HalconNode 自带参数表内核
            wCore = core;
            QVERIFY(wCore.lock() != nullptr);
            {
                QReadLocker locker(&core->lock);
                QCOMPARE(core->map.value(QStringLiteral("params_seat_key")), kVal);
            }
        }   // 座侧硬引用在此出块；此后读面一律走弱引用（快照列是唯一残余持有者）
        FlowScene::setDanglingSnapshotAssertEnabled(false);
        delete scene;
        QVERIFY(wCore.lock() != nullptr);            // 壳亡后参数内核仍活
        {
            QReadLocker locker(&wCore.lock()->lock);
            QCOMPARE(wCore.lock()->map.value(QStringLiteral("params_seat_key")), kVal);
        }
        guard.reset();
        QVERIFY(wCore.expired());
        FlowScene::setDanglingSnapshotAssertEnabled(true);
    }
}

QTEST_MAIN(NodeStateBodyTest)
#include "node_state_body_test.moc"
