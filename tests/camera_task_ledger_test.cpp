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

#include <QtTest/QtTest>
#include <QElapsedTimer>
#include <QThread>
#include <atomic>

class CameraTaskLedgerTest : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void drainedWaitsForInFlightTask();
    void stuckTaskTimesOut();
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
    QVERIFY2(elapsed >= 250,
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
    QVERIFY2(elapsed >= 150,
             qPrintable(QStringLiteral("超时等待只用了 %1ms，粒度不对").arg(elapsed)));
}

QTEST_MAIN(CameraTaskLedgerTest)
#include "camera_task_ledger_test.moc"
