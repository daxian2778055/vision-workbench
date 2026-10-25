// 相机线程池任务在飞账（2026-10-25 复核 P2-3）。
//
// 缺陷：开关相机/应用参数/枚举设备都投到 QThreadPool 的任务里，任务在锁外拿裸句柄调 SDK；
// closeCamera 却直接 MV_CC_DestroyHandle。没有在飞账时，"点应用参数后立刻关窗/切方案"就是
// 在工作线程仍可能持有句柄时拆句柄（use-after-free）。
//
// 本用例钉住账目本身（真机不可用也能验）：
//   ① 有人 enter 时 waitDrained 必须等它 leave（不能提前返回）；
//   ② 超时仍在飞 → waitDrained 返回 false（调用方据此跳过关相机，而不是硬拆）。
#include "CameraTaskLedger.h"
#include "MvsImageSourceNode.h"   // 排队记账腿：需要一个能投相机任务的节点

#include <QtTest/QtTest>
#include <QElapsedTimer>
#include <QSemaphore>
#include <QThread>
#include <QThreadPool>
#include <atomic>

class CameraTaskLedgerTest : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void drainedWaitsForInFlightTask();
    void stuckTaskTimesOut();
    /// P2-3 残留：排队中的任务也必须计入账（修复前只数已进入 run() 的）
    void queuedCameraTaskIsCounted();
};

namespace {

/// 占账的任务：跑 ms 毫秒后 leave
class HoldTask : public QThread
{
public:
    explicit HoldTask(int ms) : m_ms(ms) {}
    void run() override
    {
        CameraTaskLedger::enter();
        QThread::msleep(m_ms);
        CameraTaskLedger::leave();
    }
private:
    int m_ms;
};

} // namespace

void CameraTaskLedgerTest::init()
{
    // 用例间不串味：等前面遗留的账清空（不应有遗留，属自检）
    QVERIFY2(CameraTaskLedger::waitDrained(3000),
             qPrintable(QStringLiteral("用例开始时账上仍有 %1 个在飞任务")
                            .arg(CameraTaskLedger::inFlight())));
}

void CameraTaskLedgerTest::drainedWaitsForInFlightTask()
{
    HoldTask t(350);
    t.start();
    QTRY_COMPARE_WITH_TIMEOUT(CameraTaskLedger::inFlight(), 1, 2000);

    QElapsedTimer timer;
    timer.start();
    QVERIFY2(CameraTaskLedger::waitDrained(3000),
             "任务在 350ms 内会 leave，waitDrained 不该超时");
    const qint64 elapsed = timer.elapsed();
    QVERIFY2(elapsed >= 200,
             qPrintable(QStringLiteral("waitDrained 提前返回（耗时 %1ms）：它没有等在飞任务")
                            .arg(elapsed)));
    QVERIFY(t.wait(3000));
}

void CameraTaskLedgerTest::stuckTaskTimesOut()
{
    // 模拟 SDK 调用卡死：进账后不 leave
    CameraTaskLedger::enter();
    QElapsedTimer timer;
    timer.start();
    const bool drained = CameraTaskLedger::waitDrained(200);
    const qint64 elapsed = timer.elapsed();
    CameraTaskLedger::leave();   // 收尾，别污染后续用例

    QVERIFY2(!drained, "卡死的任务必须让 waitDrained 返回 false（调用方据此跳过拆句柄）");
    QVERIFY2(elapsed >= 100,
             qPrintable(QStringLiteral("超时等待只用了 %1ms，粒度不对").arg(elapsed)));
}

void CameraTaskLedgerTest::queuedCameraTaskIsCounted()
{
    QThreadPool *pool = QThreadPool::globalInstance();
    const int savedMax = pool->maxThreadCount();
    pool->setMaxThreadCount(1);

    // 占住唯一的线程：之后投的相机任务必然排队
    QSemaphore release;
    QSemaphore started;
    pool->start(QRunnable::create([&release, &started]() {
        started.release();
        release.acquire();
    }));
    QVERIFY2(started.tryAcquire(1, 5000), "占位任务没跑起来，用例前提不成立");

    MvsImageSourceNode node;      // 不入场景，仅用来投任务
    node.applyCameraParams();     // 投到已饱和的池 → 排队
    // 关键断言：排队中的任务必须已经计入账。修复前它要等 run() 才开始记账，
    // 于是 closeCamera 的 waitDrained 会看到 0 —— 拆句柄与排队任务抢句柄的窗口（P2-3 残留）。
    QTRY_VERIFY_WITH_TIMEOUT(CameraTaskLedger::inFlight() >= 1, 3000);

    release.release();
    QVERIFY2(CameraTaskLedger::waitDrained(5000), "占位任务释放后账未排空");
    pool->setMaxThreadCount(savedMax);
}

QTEST_MAIN(CameraTaskLedgerTest)
#include "camera_task_ledger_test.moc"
