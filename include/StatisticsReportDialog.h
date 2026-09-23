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

/// 统计报表对话框（P1-11）：**良率 / 按天趋势 / NG 按节点分布**，支持导出 CSV / HTML / 图表 PNG。
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

private:
    void updateCharts();
    QDateTime rangeStart() const;
    QString currentFlowFilter() const;      ///< 空 = 全部流程

    QComboBox *m_rangeCombo = nullptr;
    QComboBox *m_flowCombo = nullptr;
    QLabel *m_kpiLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
    QChartView *m_trendView = nullptr;
    QChartView *m_ngView = nullptr;

    QList<InspectionRecord> m_records;      ///< 当前范围内的全部记录（导出时按过滤重算）
    StatisticsReport::Summary m_summary;    ///< 当前过滤下的汇总
    QString m_lastError;
};
