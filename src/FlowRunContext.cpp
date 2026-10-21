#include "FlowRunContext.h"

#include <QElapsedTimer>

// 运行上下文块（切片 C 立项书 P2a 期：块与承载）
// 取消等待的语义与今天执行器内那段同形（逐式复刻）：停止即醒并返回未完成；暂停期间不扣延时预算；
// 恢复后继续扣；状态一变就唤醒等待者。

FlowRunContext::FlowRunContext() = default;

ExecutionState FlowRunContext::state() const
{
    return m_state.load(std::memory_order_acquire);
}

void FlowRunContext::setState(ExecutionState state)
{
    {
        QMutexLocker locker(&m_mutex);
        m_state.store(state, std::memory_order_release);
    }
    // 状态一变就唤醒等待者：暂停与恢复都要叫醒挂着的等待（不叫醒则恢复后仍挂着）。
    m_waitCondition.wakeAll();
}

FlowMode FlowRunContext::flowMode() const
{
    return m_flowMode.load(std::memory_order_acquire);
}

void FlowRunContext::setFlowMode(FlowMode mode)
{
    m_flowMode.store(mode, std::memory_order_release);
}

bool FlowRunContext::established() const
{
    return m_established.load(std::memory_order_acquire);
}

void FlowRunContext::markEstablished()
{
    m_established.store(true, std::memory_order_release);
}

void FlowRunContext::requestStop()
{
    setState(ExecutionState::Stopped);
}

bool FlowRunContext::interruptibleSleep(int ms)
{
    if (ms <= 0) return true;
    QMutexLocker locker(&m_mutex);
    if (m_state.load(std::memory_order_relaxed) == ExecutionState::Stopped) return false;
    // 可取消等待：维护剩余时间，暂停期间不消耗延时预算，避免恢复时提前结束（E4/P5）
    qint64 remaining = ms;
    while (remaining > 0) {
        if (m_state.load(std::memory_order_relaxed) == ExecutionState::Stopped) return false;
        if (m_state.load(std::memory_order_relaxed) == ExecutionState::Paused) {
            m_waitCondition.wait(&m_mutex);  // 暂停时一直等待，直到恢复唤醒
            continue;                        // 不扣减 remaining（暂停时间不计入延时）
        }
        QElapsedTimer t;
        t.start();
        m_waitCondition.wait(&m_mutex, remaining);
        remaining -= t.elapsed();            // 仅扣减实际等待时间
    }
    return true;
}
