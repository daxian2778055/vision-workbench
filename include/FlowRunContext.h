#pragma once

#include "FlowExecutionEnums.h"

#include <QMutex>
#include <QWaitCondition>
#include <atomic>

/// 运行上下文块（切片 C 立项书 P2a 期：块与承载）——**非对象**的共享值型件：
/// 不进任何父子树、可被多线程引用、**不持有任何裸指针**（不放场景、不放节点、不放执行器主体）。
///
/// 存在理由（实测收敛）：算子侧从执行器身上只取四样东西——运行态、运行模式、上下文存在性、
/// 取消信号；**没有一处需要执行器主体的方法**。把这四样搬进本块后，算子只持块不持执行器，
/// 执行器主体可以先死而算子仍安全（终局三条件之 T1 在节点侧达成）。
///
/// 契约钉：
///  · 字段就四项：**运行态**（原子）、**运行模式**（原子）、**上下文存在性**（原子）、
///    **取消信号**（互斥量 ＋ 等待条件）；值域与序沿用执行状态与运行模式那两套枚举，不新增第三套；
///  · **互斥量是叶子**：持本块锁期间不得再取任何别的锁（含图锁与账叶锁）；
///    本块锁与账叶锁同级，两者**不得嵌套持有**；
///  · 写面收敛：状态与模式由执行器侧写；算子侧只读，**唯一可写的动作是「请求取消」**；
///  · 取消等待的语义与今天执行器内那段同形：停止即醒并返回未完成；暂停期间不扣延时预算；
///    恢复后继续扣；状态一变就唤醒等待者。
class FlowRunContext final {
public:
    FlowRunContext();

    /// 运行态（原子读；初值空闲，与执行器初值同）。
    ExecutionState state() const;
    /// 运行态（**执行器侧**写；写后会唤醒等待者）。
    void setState(ExecutionState state);

    /// 运行模式（原子读；初值软触发，与执行器初值同）。
    FlowMode flowMode() const;
    /// 运行模式（**执行器侧**写）。
    void setFlowMode(FlowMode mode);

    /// 上下文存在性：有没有执行器上下文（算子侧用它判「设计期或自检」那一路）。
    bool established() const;
    /// 标记上下文已建立（**执行器侧**写）。
    void markEstablished();

    /// 请求取消（**算子侧唯一可写的动作**）：置停止并唤醒等待者。
    void requestStop();

    /// 可取消等待：睡满返回真；期间被取消返回假。
    /// 暂停期间挂住且**不扣延时预算**，恢复后继续扣（与今天执行器内那段同形）。
    bool interruptibleSleep(int ms);

private:
    std::atomic<ExecutionState> m_state{ExecutionState::Idle};
    std::atomic<FlowMode> m_flowMode{FlowMode::SoftwareTrigger};
    std::atomic<bool> m_established{false};
    QMutex m_mutex;              // 叶子锁：持锁期间不得再取任何别的锁
    QWaitCondition m_waitCondition;
};
