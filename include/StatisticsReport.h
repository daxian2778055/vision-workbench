#pragma once

#include <QString>
#include <QList>
#include <QMap>
#include <QPair>
#include <QDate>
#include <QDateTime>

#include "InspectionRecord.h"

/// 统计报表的**纯计算 + 导出**（不依赖 QtWidgets，便于单测）。
///
/// 口径说明（重要，勿凭直觉改）：
/// · **良率 = OK 轮数 / 总轮数**，数据来自 FlowExecutor 轮末追加的「整轮汇总」记录
///   （nodeName == kRoundSummaryNodeName）。没有这类记录时 `hasRounds` 为 false、良率记 0，
///   报表必须显式标注"无轮次数据"，而不是把它当成 0% 良率（那会把"没数据"说成"全 NG"）。
/// · **节点 NG 分布**来自各节点记录（passed == false），不含整轮汇总记录（否则会重复计数）。
/// · **时间序列桶**只使用**有效时间戳**的记录；无效时间戳（老数据/未落库时间）计入总数但不进桶，
///   避免造出 1970-01-01 的假数据点。
/// · **粒度**：`ByDay`（长周期看趋势）/ `ByHour`（单班、单日排产时才有意义——按天会把
///   "下午开始连续 NG"这类事抹平）。同一份记录换粒度只是重分桶，口径与汇总数字不变。
class StatisticsReport
{
public:
    enum class Granularity {
        ByDay,    ///< 按自然日分桶
        ByHour    ///< 按自然小时分桶
    };

    struct Bucket {
        QDateTime begin;          ///< 桶起点（按天=当日 00:00；按小时=该小时 00 分，本地时间）
        int okRounds = 0;
        int ngRounds = 0;
        int totalRecords = 0;
        int ngRecords = 0;
    };

    struct Summary {
        int totalRecords = 0;
        int ngRecords = 0;
        int totalRounds = 0;
        int okRounds = 0;
        int ngRounds = 0;
        bool hasRounds = false;
        double yieldPercent = 0.0;              ///< 0..100；hasRounds 为 false 时无效
        QDateTime firstSeen;                    ///< 有效时间戳中的最早/最晚
        QDateTime lastSeen;
        Granularity granularity = Granularity::ByDay;
        QList<Bucket> buckets;                  ///< 按时间升序（粒度见 granularity）
        QList<QPair<QString, int>> ngByNode;    ///< 按 NG 次数降序（同为 0 时按名称升序，保证确定性）
        QList<QPair<QString, int>> recordsByFlow; ///< 按记录数降序

        /// 桶的显示标签（shortForm：图表轴用短标签；否则导出用完整标签）
        QString labelOf(const Bucket &b, bool shortForm) const;
        /// 粒度对应的中文名（"天"/"小时"），用于标题与导出段落名
        QString granularityName() const;
    };

    /// 计算汇总；flowFilter 非空时只统计该流程；granularity 决定时间序列的分桶粒度
    static Summary compute(const QList<InspectionRecord> &records,
                           const QString &flowFilter = QString(),
                           Granularity granularity = Granularity::ByDay);

    /// 良率是否达标（P1-11 的"目标线"）：**等于目标算达标**。
    /// targetPercent ≤ 0 或 > 100（未设目标）、或没有轮次数据时返回 false——
    /// "没有数据"不等于"不达标"，调用方需要区分时请自行先看 hasRounds。
    static bool meetsTarget(const Summary &s, double targetPercent);

    /// CSV 导出（**带 UTF-8 BOM**，Excel 双击即可正确显示中文；含汇总/时间序列/按节点/按流程四段）。
    /// 时间序列段的标题与列名随粒度变化（"按天序列"/"日期" vs "按小时序列"/"时间"）。
    /// targetPercent > 0 时额外汇出"良率目标/达标"两行；默认 0 = 不加（保持既有输出不变）。
    static QString toCsv(const Summary &s, const QDateTime &generatedAt = QDateTime::currentDateTime(),
                         double targetPercent = 0.0);

    /// 自包含 HTML 报告：**无任何外部资源**（离线可看），含简易条形图与三段明细。
    /// targetPercent > 0 时在 KPI 区显示目标与达标结论（未达标标红）。
    static QString toHtml(const Summary &s, const QString &title = QString(),
                          const QDateTime &generatedAt = QDateTime::currentDateTime(),
                          double targetPercent = 0.0);

    /// 建议文件名（不含目录），如 report_20260923_1030.csv
    static QString suggestedFileName(const QString &ext,
                                     const QDateTime &now = QDateTime::currentDateTime());
};
