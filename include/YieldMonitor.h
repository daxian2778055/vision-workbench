#pragma once

#include <QList>
#include <QString>

/// 良率目标监控（P1-11 的"目标线 + 报警联动"里的**判定部分**：纯逻辑、可单测、不碰界面与数据库）。
///
/// 现场口径（勿凭直觉改，改了要在用例里同步）：
///  · 只看**最近 windowRounds 轮**（滚动窗口）：产线上"整体良率还行、但刚才连续 NG"才是要报警的事——
///    按累计良率报警会把问题淹没在历史数据里；
///  · 窗口内样本不足 `minSamples` 时**不判定**：否则刚开线第一轮 NG 就报警；
///  · 判据 `良率 < 目标`（**等于目标算达标**）；
///  · **闭锁**：同一段低良率只报一次——`alarmRaised` 仅在"由达标跌到不达标"的那一次为真；
///    恢复到目标线以上时给出一次 `alarmCleared`，之后再跌破才会再报。连续模式下每轮都判定，
///    没有闭锁就会刷屏（报警历史也会被灌满，等于没有报警）；
///  · `targetPercent <= 0`（或 > 100）＝**未设目标 = 关闭监控**（默认关闭，需现场显式设置）。
///
/// 数据由调用方提供（`rounds` 按时间**升序**、true=OK），且应与统计报表取自同一批「整轮汇总」记录
/// （见 InspectionRecord.h 的 kRoundSummaryNodeName）：口径不一致会出现"报表显示正常但报警在响"，
/// 现场无法解释，故本类只做判定、不做取数。
class YieldMonitor
{
public:
    struct Config {
        double targetPercent = 0.0;   ///< ≤0 或 >100 = 关闭监控
        int windowRounds = 50;        ///< 滚动窗口（最近 N 轮）
        int minSamples = 10;          ///< 窗口内最少样本数，不足不判定
        bool isValid() const
        {
            return targetPercent > 0.0 && targetPercent <= 100.0
                   && windowRounds > 0 && minSamples > 0 && minSamples <= windowRounds;
        }
    };

    struct Result {
        bool evaluated = false;       ///< 是否真的判定了（关闭 / 样本不足 → false）
        int samples = 0;              ///< 参与判定的轮数
        int okRounds = 0;
        double yieldPercent = 0.0;    ///< 0..100（仅当 evaluated 时有意义）
        double targetPercent = 0.0;   ///< 本次判定用的目标（仅用于文案；不在这里再读配置）
        bool belowTarget = false;
        bool alarmRaised = false;     ///< 本次**新触发**（闭锁后的第一次）
        bool alarmCleared = false;    ///< 本次**恢复**到目标线以上
        QString text() const;         ///< 一句话（日志 / 报警历史 / 状态栏共用）
    };

    YieldMonitor() = default;
    explicit YieldMonitor(const Config &config);

    /// 改配置会**重置闭锁**：否则用户把目标调严之后，若本就处于闭锁状态就不再报警
    /// （表现为"改了目标不生效"，很难查）。宁可在改配置后按新口径重新判一次。
    void setConfig(const Config &config);
    Config config() const { return m_config; }
    bool alarmLatched() const { return m_latched; }
    /// 清闭锁（流程重启 / 手动复位）
    void reset();

    /// 判定一次。rounds 按时间升序，true=OK；内部只取最后 windowRounds 个。
    Result evaluate(const QList<bool> &rounds);

private:
    Config m_config;
    bool m_latched = false;
};
