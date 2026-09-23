#include "YieldMonitor.h"

#include <algorithm>

YieldMonitor::YieldMonitor(const Config &config)
    : m_config(config)
{
}

void YieldMonitor::setConfig(const Config &config)
{
    const bool targetChanged = !qFuzzyCompare(config.targetPercent + 1.0, m_config.targetPercent + 1.0)
                               || config.windowRounds != m_config.windowRounds
                               || config.minSamples != m_config.minSamples;
    m_config = config;
    if (targetChanged)
        m_latched = false;   // 口径变了就按新口径重判（见头文件说明）
}

void YieldMonitor::reset()
{
    m_latched = false;
}

QString YieldMonitor::Result::text() const
{
    if (!evaluated)
        return QString();

    const QString stats = QStringLiteral("最近 %1 轮良率 %2%（%3/%4 轮 OK，目标 %5%）")
                              .arg(samples)
                              .arg(yieldPercent, 0, 'f', 1)
                              .arg(okRounds)
                              .arg(samples)
                              .arg(targetPercent, 0, 'f', 1);
    if (alarmRaised)
        return QStringLiteral("良率低于目标：") + stats;
    if (alarmCleared)
        return QStringLiteral("良率已恢复到目标线以上：") + stats;
    if (belowTarget)
        return QStringLiteral("良率仍低于目标（已报警，不重复报警）：") + stats;
    return QStringLiteral("良率正常：") + stats;
}

YieldMonitor::Result YieldMonitor::evaluate(const QList<bool> &rounds)
{
    Result result;

    if (!m_config.isValid())
        return result;   // 未设目标 = 关闭：不判定、不报警（也不动闭锁，改了目标可续用）

    // 只取最近 windowRounds 轮
    const int take = qMin(m_config.windowRounds, int(rounds.size()));
    if (take < m_config.minSamples)
        return result;   // 样本不足：不判定（刚开线不该报）

    int okRounds = 0;
    for (int i = int(rounds.size()) - take; i < int(rounds.size()); ++i) {
        if (rounds.at(i))
            ++okRounds;
    }

    result.evaluated = true;
    result.samples = take;
    result.okRounds = okRounds;
    result.yieldPercent = 100.0 * double(okRounds) / double(take);
    result.targetPercent = m_config.targetPercent;
    result.belowTarget = result.yieldPercent < m_config.targetPercent;

    if (result.belowTarget && !m_latched) {
        result.alarmRaised = true;   // 由达标跌到不达标：报一次
        m_latched = true;
    } else if (!result.belowTarget && m_latched) {
        result.alarmCleared = true;  // 恢复：通知一次，并解除闭锁（之后再跌破才会再报）
        m_latched = false;
    }
    return result;
}
