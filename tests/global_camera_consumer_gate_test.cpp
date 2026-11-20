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
#include <QMutex>
#include <atomic>
#include <thread>

#include <QtTest/QtTest>

class GlobalCameraConsumerGateTest : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void closeRefusedWhileConsumersExist();
    void forceOverridesConsumerGate();
    void closeAllowedAfterLastConsumerLeaves();
    // r19 P1-4：同一句柄的 SDK 调用必须串行（并发取图是 UB）
    void sdkMutexIsPerHandle();
    void pixelFormatCacheKeepsOtherParams();   // P5：像素格式回填不得重置曝光/增益/帧率
    void deviceBindingRejectsDuplicate();       // P5：同一物理设备不能被两台相机同时绑定
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

void GlobalCameraConsumerGateTest::sdkMutexIsPerHandle()
{
    // ① 同一句柄 → 同一把锁
    void *h1 = reinterpret_cast<void *>(0x1000);
    void *h2 = reinterpret_cast<void *>(0x2000);
    QVERIFY(&GlobalCameraManager::sdkMutexFor(h1) == &GlobalCameraManager::sdkMutexFor(h1));
    // ② 不同句柄 → 不同锁（一把全局锁会把两台相机也串成一台）
    QVERIFY(&GlobalCameraManager::sdkMutexFor(h1) != &GlobalCameraManager::sdkMutexFor(h2));

    // ③ 两线程抢同一句柄的锁：临界区必须互斥
    std::atomic<int> inside(0);
    std::atomic<int> overlaps(0);
    auto worker = [&]() {
        for (int i = 0; i < 200; ++i) {
            QMutexLocker locker(&GlobalCameraManager::sdkMutexFor(h1));
            if (inside.fetch_add(1) != 0)
                ++overlaps;
            std::this_thread::yield();
            inside.fetch_sub(1);
        }
    };
    std::thread a(worker);
    std::thread b(worker);
    a.join();
    b.join();
    QCOMPARE(int(overlaps), 0);   // 同一句柄的临界区从不重叠
}

void GlobalCameraConsumerGateTest::pixelFormatCacheKeepsOtherParams()
{
    auto *m = GlobalCameraManager::instance();
    // 先造一份"非默认"的参数快照（默认曝光 10000/增益 0/帧率 30）
    CameraParams full;
    full.exposureTime = 4567.0;
    full.gain = 12.5;
    full.frameRate = 60.0;
    full.triggerMode = QStringLiteral("On");
    full.triggerSource = QStringLiteral("Software");
    m->updateCameraCache(kCam, full);

    // P5 修前：MVS 用只填 pixelFormat 的默认 CameraParams 整体覆盖 → 全被重置
    m->updateCameraPixelFormatCache(kCam, QStringLiteral("Mono10Packed"));
    const CameraParams after = m->cameras().value(kCam).params;
    QCOMPARE(after.pixelFormat, QStringLiteral("Mono10Packed"));
    QVERIFY2(qAbs(after.exposureTime - 4567.0) < 1e-6, "像素格式回填把曝光重置了");
    QVERIFY2(qAbs(after.gain - 12.5) < 1e-6, "像素格式回填把增益重置了");
    QVERIFY2(qAbs(after.frameRate - 60.0) < 1e-6, "像素格式回填把帧率重置了");
    QCOMPARE(after.triggerMode, QStringLiteral("On"));
    QCOMPARE(after.triggerSource, QStringLiteral("Software"));
}

void GlobalCameraConsumerGateTest::deviceBindingRejectsDuplicate()
{
    auto *m = GlobalCameraManager::instance();
    // 用两个默认相机（Camera1/Camera2）验证占用闸
    const QString other = QStringLiteral("Camera2");
    const QString dev = QStringLiteral("GigE: TEST-CAM (IP: 192.168.1.99)");
    // 先清掉两相机原有绑定，避免测试间互相污染
    m->setCameraDevice(kCam, QString());
    m->setCameraDevice(other, QString());
    QVERIFY(m->setCameraDevice(kCam, dev));
    QVERIFY2(!m->setCameraDevice(other, dev), "同一物理设备被两台相机绑定，UI 的[已被占用]只是装饰");
    // 同相机重复设同一设备允许（绑定刷新路径）
    QVERIFY(m->setCameraDevice(kCam, dev));
    // 清理
    m->setCameraDevice(kCam, QString());
    m->setCameraDevice(other, QString());
}

QTEST_MAIN(GlobalCameraConsumerGateTest)
#include "global_camera_consumer_gate_test.moc"
