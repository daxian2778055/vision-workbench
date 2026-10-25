// 全局相机关闭的消费者闸（2026-10-25 复核 P2-4）。
//
// 缺陷：GlobalCameraManager::closeCamera 无条件拆句柄；消费者账（markCameraUsed /
// isCameraUsed / consumersForCamera）虽在，但查询侧没有任何生产调用点。相机管理对话框是
// 非模态的——运行中可以直接解绑；自动重连也会关掉一台可能被别的节点共享的相机。别的消费者
// 正拿着裸句柄在 MV_CC_GetOneFrameTimeout 里 ⇒ UAF / 驱动崩溃。
//
// 修法：closeCamera(name, force=false) 在仍有消费者时拒绝并返回 false；只有明确知道自己在
// 拆全场（关机清理）或自己是唯一消费者（自动重连）时才 force。
// 本用例不需要真机：闸在"取句柄"之前执行，账目可直接验。
#include "GlobalCameraManager.h"

#include <QtTest/QtTest>

class GlobalCameraConsumerGateTest : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void closeRefusedWhileConsumersExist();
    void forceOverridesConsumerGate();
    void closeAllowedAfterLastConsumerLeaves();
};

namespace {
// 用管理器构造时自带的默认相机名（Camera1..8 已在 initializeDefaultCameras 注册）；
// 自造的名字会先被"相机不存在"分支拒掉，验不出消费者闸。
const QString kCam = QStringLiteral("Camera1");
}

void GlobalCameraConsumerGateTest::init()
{
    auto *m = GlobalCameraManager::instance();
    // 清干净上一个用例可能留下的账
    for (const QString &c : m->consumersForCamera(kCam))
        m->markCameraUnused(kCam, c);
}

void GlobalCameraConsumerGateTest::closeRefusedWhileConsumersExist()
{
    auto *m = GlobalCameraManager::instance();
    QVERIFY(m->markCameraUsed(kCam, QStringLiteral("consumer-A")));
    QVERIFY(m->markCameraUsed(kCam, QStringLiteral("consumer-B")));
    QCOMPARE(m->consumersForCamera(kCam).size(), 2);

    // 核心断言：还有消费者 ⇒ 拒绝关闭（不得拆句柄）
    QVERIFY2(!m->closeCamera(kCam),
             "仍有消费者时 closeCamera 返回了 true：句柄可能在别人取图时被拆（P2-4）");
}

void GlobalCameraConsumerGateTest::forceOverridesConsumerGate()
{
    auto *m = GlobalCameraManager::instance();
    QVERIFY(m->markCameraUsed(kCam, QStringLiteral("consumer-A")));

    // force：调用方明确知道自己在拆全场（关机清理 / 唯一消费者）。相机本就没打开，
    // 因此这里应返回 true（"未打开"是成功态），而不是被消费者闸拦成 false。
    QVERIFY2(m->closeCamera(kCam, /*force*/ true),
             "force=true 时仍被消费者闸拦住（关机清理会关不掉相机）");
}

void GlobalCameraConsumerGateTest::closeAllowedAfterLastConsumerLeaves()
{
    auto *m = GlobalCameraManager::instance();
    QVERIFY(m->markCameraUsed(kCam, QStringLiteral("consumer-A")));
    QVERIFY(!m->closeCamera(kCam));            // 有消费者 → 拒
    m->markCameraUnused(kCam, QStringLiteral("consumer-A"));
    QVERIFY(m->consumersForCamera(kCam).isEmpty());
    QVERIFY2(m->closeCamera(kCam), "最后一个消费者离开后仍拒绝关闭（闸把相机永久锁死了）");
}

QTEST_MAIN(GlobalCameraConsumerGateTest)
#include "global_camera_consumer_gate_test.moc"
