#pragma once

#include <QDialog>
#include <QDateTime>
#include <QList>

#include "StatisticsReport.h"
#include "InspectionRecord.h"

class QComboBox;
class QLabel;
class QChartView;
class QChart;
class QDoubleSpinBox;
class QCheckBox;
class QSpinBox;

/// 统计报表对话框（P1-11）：**良率 / 时间趋势 / NG 按节点分布**，支持导出 CSV / HTML / 图表 PNG，
/// 以及**定时导出**设置（策略与落盘见 ReportAutoExport，本类只做界面与配置存取）。
/// 时间序列粒度可选：自动（今天/24h 内按小时，更长按天）/ 按小时 / 按天。
///
/// 数据来自 `AppDatabase::queryResults()`（本地 SQLite，离线可用）；全部统计与导出逻辑在
/// `StatisticsReport`（纯逻辑、可单测），本类只做取数、展示与落盘。
///
/// 口径提醒：良率来自执行器每轮追加的「整轮汇总」记录（见 InspectionRecord.h）。
/// 若所选范围里没有这类记录（例如是升级前的旧数据），界面会明确显示"无轮次数据"，
/// **不会**把它显示成 0% 良率。
class StatisticsReportDialog : public QDialog
{
    Q_OBJECT
public:
    explicit StatisticsReportDialog(QWidget *parent = nullptr);

private slots:
    void reload();                 ///< 按当前范围/流程过滤重新取数并刷新
    void exportCsv();
    void exportHtml();
    void exportChartPng();
    void exportScheduledNow();     ///< 「立即导出」：走定时导出同一套落盘与保留策略
    void saveAutoExportSettings(); ///< 定时导出设置变更后写入配置

private:
    void updateCharts();
    /// 当前时间序列粒度（"自动"按范围推断：范围 ≤ 2 天按小时，否则按天）
    StatisticsReport::Granularity currentGranularity() const;
    /// KPI 与提示文案（与取数分离：改目标值时只需重画这两处，不必重新查库）
    void updateKpiAndHint();
    QDateTime rangeStart() const;
    QString currentFlowFilter() const;      ///< 空 = 全部流程
    /// 目标良率（0 = 未设目标）；运行期报警监控读的是同一个配置键（reporting/yieldTargetPercent）
    double targetPercent() const;
    void saveTargetSetting();

    QComboBox *m_rangeCombo = nullptr;
    QComboBox *m_flowCombo = nullptr;
    QComboBox *m_granCombo = nullptr;       ///< 时间序列粒度：自动 / 按小时 / 按天
    QDoubleSpinBox *m_targetSpin = nullptr;
    QLabel *m_kpiLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
    QLabel *m_trendTitle = nullptr;         ///< 趋势图标题（随粒度变化）
    QLabel *m_yieldTitle = nullptr;
    QCheckBox *m_autoExportCheck = nullptr; ///< 定时导出开关
    QSpinBox *m_autoExportInterval = nullptr;
    QLabel *m_autoExportDirLabel = nullptr;
    QChartView *m_trendView = nullptr;
    QChartView *m_yieldView = nullptr;      ///< 良率(%) + 目标线
    QChartView *m_ngView = nullptr;

    QList<InspectionRecord> m_records;      ///< 当前范围内的全部记录（导出时按过滤重算）
    StatisticsReport::Summary m_summary;    ///< 当前过滤下的汇总
    QString m_lastError;
};
