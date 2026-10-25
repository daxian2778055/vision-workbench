#include "CameraTaskLedger.h"

#include <QDeadlineTimer>
#include <QThread>
#include <atomic>

namespace {
std::atomic<int> s_inFlight{0};
constexpr int kDrainTimeoutMs = 3000;   // 与 cleanupCameras 等执行线程的 3 秒同口径
} // namespace

void CameraTaskLedger::enter()
{
    s_inFlight.fetch_add(1, std::memory_order_relaxed);
}

void CameraTaskLedger::leave()
{
    s_inFlight.fetch_sub(1, std::memory_order_relaxed);
}

int CameraTaskLedger::inFlight()
{
    return s_inFlight.load(std::memory_order_relaxed);
}

bool CameraTaskLedger::waitDrained(int timeoutMs)
{
    const QDeadlineTimer deadline(timeoutMs);
    while (s_inFlight.load(std::memory_order_relaxed) > 0) {
        if (deadline.hasExpired())
            return false;
        QThread::msleep(5);   // 5ms 粒度：足够快，又不至于烧一个核
    }
    return true;
}

int CameraTaskLedger::drainTimeoutMs()
{
    return kDrainTimeoutMs;
}
