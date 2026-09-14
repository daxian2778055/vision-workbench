#include "FlowRuntimeStats.h"

#if defined(Q_OS_WIN)
#  include <windows.h>
#  include <psapi.h>
#endif

void FlowRuntimeStats::reset()
{
    rounds = 0;
    failedRounds = 0;
    lastRoundMs = 0;
    maxRoundMs = 0;
    totalRoundMs = 0;
    processHandleCount = 0;
    processWorkingSetBytes = 0;
    nodes.clear();
}

void FlowRuntimeStats::sampleProcessResources()
{
#if defined(Q_OS_WIN)
    DWORD handles = 0;
    if (GetProcessHandleCount(GetCurrentProcess(), &handles)) {
        processHandleCount = handles;
    }

    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        processWorkingSetBytes = static_cast<quint64>(pmc.WorkingSetSize);
    }
#else
    // 非 Windows 暂不采样（保留字段，便于后续接入 /proc 等实现）
    processHandleCount = 0;
    processWorkingSetBytes = 0;
#endif
}

void FlowRuntimeStats::onNodeExecuted(const QString &nodeName, bool success, qint64 elapsedMs)
{
    NodeRuntimeStat &stat = nodes[nodeName];
    ++stat.executions;
    if (!success) {
        ++stat.failures;
    }
    if (elapsedMs > 0) {
        stat.totalMs += elapsedMs;
        if (elapsedMs > stat.maxMs) {
            stat.maxMs = elapsedMs;
        }
    }
}

void FlowRuntimeStats::onNodeSkipped(const QString &nodeName)
{
    ++nodes[nodeName].skipped;
}

void FlowRuntimeStats::onRoundFinished(qint64 roundMs, bool hadFailure)
{
    ++rounds;
    if (hadFailure) {
        ++failedRounds;
    }
    lastRoundMs = roundMs;
    totalRoundMs += roundMs;
    if (roundMs > maxRoundMs) {
        maxRoundMs = roundMs;
    }
}

double FlowRuntimeStats::avgRoundMs() const
{
    return rounds > 0 ? double(totalRoundMs) / double(rounds) : 0.0;
}

QString FlowRuntimeStats::formatBytes(quint64 bytes)
{
    const double mb = double(bytes) / (1024.0 * 1024.0);
    return QString::number(mb, 'f', 1) + QStringLiteral("MB");
}

QString FlowRuntimeStats::summary() const
{
    quint64 totalFails = 0;
    quint64 totalSkips = 0;
    QString slowestNode;
    qint64 slowestMaxMs = 0;
    for (auto it = nodes.cbegin(); it != nodes.cend(); ++it) {
        totalFails += it.value().failures;
        totalSkips += it.value().skipped;
        if (it.value().maxMs > slowestMaxMs) {
            slowestMaxMs = it.value().maxMs;
            slowestNode = it.key();
        }
    }

    QString text = QStringLiteral("rounds=%1 avg=%2ms last=%3ms max=%4ms failRounds=%5 fails=%6 skips=%7 "
                                  "handles=%8 mem=%9 nodes=%10")
                       .arg(rounds)
                       .arg(QString::number(avgRoundMs(), 'f', 1))
                       .arg(lastRoundMs)
                       .arg(maxRoundMs)
                       .arg(failedRounds)
                       .arg(totalFails)
                       .arg(totalSkips)
                       .arg(processHandleCount)
                       .arg(formatBytes(processWorkingSetBytes))
                       .arg(nodes.size());

    if (slowestMaxMs > 0) {
        text += QStringLiteral(" slowest=%1(%2ms)").arg(slowestNode).arg(slowestMaxMs);
    }
    return text;
}
