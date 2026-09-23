#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

#include "StatisticsReport.h"

class QSettings;

/// 定时导出（P1-11 收尾）：**决策**（到点没有）+ **落盘**（CSV / 自包含 HTML）+ **保留策略**。
///
/// 为什么单独成类：MainWindow 无法在 CI 实例化（重窗口 + HALCON/相机/数据库初始化），
/// 留在窗口里就只剩"手动点一次"这一种验证方式；而定时导出的典型故障恰恰是"没人看着"才发生：
/// 目录不可写、重复导出、把磁盘写满、导出内容与界面看的不是同一口径。
///
/// 设计要点（都有代价）：
///  · 决策是纯逻辑（`shouldExport`），只与配置/时刻有关 ⇒ 可单测；
///  · 落盘与统计口径复用 `StatisticsReport`（与报表窗口**同一套计算与导出字符串**）：
///    定时导出的文件与手动导出的内容必须一致，否则"界面看是 98%、机器导出来是 96%"无法解释；
///  · 目录默认 `RecoveryStore::runtimeDataRoot()/reports`（与恢复文件同一套可写性探针，见 RecoveryStore）；
///  · **保留最近 N 份**（按文件名里的时间戳分组，一份含 csv/html/png 全部文件）：无人值守机不会写满盘；
///  · 写文件用 QSaveFile **原子替换**：不留半个报表（半个 CSV 比没有更糟——看着像有数据）；
///  · 默认**关闭**（`enabled=false`）：定时写盘属现场策略，不替用户默认决定。
class ReportAutoExport
{
public:
    struct Config {
        bool enabled = false;         ///< 默认关闭，需现场显式开启
        int intervalMinutes = 1440;   ///< 导出间隔（分钟）；默认每天一次
        int rangeHours = 24;          ///< 每次导出的统计范围（最近 N 小时）
        QString dir;                  ///< 输出目录；空 = defaultDir()
        int keepFiles = 30;           ///< 保留最近 N 份
        bool csv = true;
        bool html = true;

        QString effectiveDir() const;
        /// 合法性：间隔/范围/保留数均为正、且至少选了一种格式（非法配置按"未启用"处理，不乱写盘）
        bool isValid() const;
    };

    /// 读/写配置（键前缀 `reporting/autoExport*`）。settings 为空时用默认 QSettings；
    /// 传入自定义实例便于测试（不污染用户配置）。
    static Config loadConfig(QSettings *settings = nullptr);
    static void saveConfig(const Config &c, QSettings *settings = nullptr);
    /// 最近一次导出时刻（毫秒时间戳；0 = 从未导出）。持久化，避免重启后立刻又导一份。
    static qint64 lastExportMs(QSettings *settings = nullptr);
    static void setLastExportMs(qint64 ms, QSettings *settings = nullptr);

    static QString defaultDir();

    /// 现在是否该导出：未启用 → 否；从未导出过 → **是**（开机后先出一份，别等满一个间隔才第一次出）
    static bool shouldExport(const Config &c, qint64 nowMs, qint64 lastMs);

    /// 立即导出一次（目录不存在则创建）。written 返回实际写出的绝对路径；
    /// 失败返回 false 并填 error。单个格式失败不影响另一种（各自原子写）。
    static bool exportNow(const Config &c, const StatisticsReport::Summary &summary,
                          const QString &title, double targetPercent, const QDateTime &now,
                          QStringList *written, QString *error);

    /// 清理旧报告：按"文件名里的时间戳"分组，只保留最新 keep 份，返回删除的文件数。
    /// 只动本程序生成的 `report_*.*`，不碰目录里的其它文件。
    static int pruneOldFiles(const QString &dir, int keep);
};
