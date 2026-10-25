#pragma once

#include <QString>

/// 相机线程池任务的在飞账（2026-10-25 复核 P2-3）。
///
/// 为什么需要它：MvsImageSourceNode 的开关相机、应用参数、枚举设备都投到
/// QThreadPool::globalInstance()，任务在**锁外**拿裸句柄调 MVS SDK；而 closeCamera 会
/// MV_CC_DestroyHandle 拆掉那个句柄。没有这道账，"点应用参数后立刻关窗/切方案"就是
/// 在工作线程仍可能持有句柄时拆句柄（use-after-free）。
///
/// 用法：任务进 run() 时 enter()、离开时 leave()（RAII）；关相机前 waitDrained(超时)，
/// 超时未排空就**跳过关闭**（句柄留给进程退出时回收），而不是硬拆。
///
/// 抽成独立小类是为了能单测：账目的进/出/计数/有界等待都可直接验，不必真相机。
class CameraTaskLedger
{
public:
    static void enter();
    static void leave();
    /// 当前在飞任务数（测试/诊断用）
    static int inFlight();
    /// 有界等待排空；返回 false = 超时仍有任务在飞（调用方应跳过关相机）
    static bool waitDrained(int timeoutMs);
    /// 排空等待的超时上限（关相机路径用）
    static int drainTimeoutMs();
};
