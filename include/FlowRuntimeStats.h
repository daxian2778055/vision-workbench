#pragma once

#include <QHash>
#include <QString>

/// 单节点运行统计（累计值，供现场长跑诊断）
struct NodeRuntimeStat
{
    quint64 executions = 0;   ///< 实际执行次数（不含跳过）
    quint64 failures = 0;     ///< 执行失败次数
    quint64 skipped = 0;      ///< 被跳过次数（未激活分支 / 循环体由外层调度）
    qint64  totalMs = 0;      ///< 累计执行耗时
    qint64  maxMs = 0;        ///< 单次最大耗时
};

/// 流程运行期统计：轮次 / 每节点耗时与失败 / 进程句柄与内存。
///
/// 用途：现场连续运行（8~24h）时观测节拍是否漂移、失败率、句柄与内存是否持续增长。
/// 说明：计数器为累计值，跨轮保留；线程安全由持有者（FlowExecutor）加锁保证。
class FlowRuntimeStats
{
public:
    // ---- 流程级 ----
    quint64 rounds = 0;          ///< 已完成轮次
    quint64 failedRounds = 0;    ///< 含失败节点的轮次
    qint64  lastRoundMs = 0;     ///< 最近一轮耗时
    qint64  maxRoundMs = 0;      ///< 单轮最大耗时
    qint64  totalRoundMs = 0;    ///< 累计轮次耗时

    // ---- 进程级（最近一次采样）----
    quint64 processHandleCount = 0;      ///< 进程句柄数：持续增长通常意味着句柄泄漏
    quint64 processWorkingSetBytes = 0;  ///< 进程工作集字节数

    // ---- 节点级 ----
    QHash<QString, NodeRuntimeStat> nodes;  ///< 节点全名 -> 统计

    void reset();

    /// 采样当前进程资源（Windows 生效；其它平台置 0）
    void sampleProcessResources();

    /// 单行摘要（日志输出用）
    QString summary() const;

    /// 平均轮次耗时（ms）
    double avgRoundMs() const;

    // ---- 记录入口（由执行器调用）----
    void onNodeExecuted(const QString &nodeName, bool success, qint64 elapsedMs);
    void onNodeSkipped(const QString &nodeName);
    void onRoundFinished(qint64 roundMs, bool hadFailure);

private:
    static QString formatBytes(quint64 bytes);
};
