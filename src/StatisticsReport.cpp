#include "StatisticsReport.h"

#include <QStringList>
#include <algorithm>

namespace {

/// CSV 字段转义：含逗号/引号/换行时用双引号包起来，内部引号翻倍（Excel 口径）
QString csvField(const QString &s)
{
    if (s.contains(QLatin1Char(',')) || s.contains(QLatin1Char('"'))
        || s.contains(QLatin1Char('\n')) || s.contains(QLatin1Char('\r'))) {
        QString t = s;
        t.replace(QLatin1Char('"'), QLatin1String("\"\""));
        return QLatin1Char('"') + t + QLatin1Char('"');
    }
    return s;
}

QString csvLine(const QStringList &fields)
{
    QStringList out;
    out.reserve(fields.size());
    for (const QString &f : fields)
        out << csvField(f);
    return out.join(QLatin1Char(',')) + QLatin1String("\r\n");
}

/// HTML 转义（报表里会出现流程名/节点名，可能含 < > &）
QString htmlEsc(const QString &s)
{
    QString t = s;
    t.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    t.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    t.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    return t;
}

/// 简易条形图（纯 HTML/CSS，不引外部资源）
QString barRow(const QString &label, int value, int maxValue)
{
    const double pct = (maxValue > 0) ? (100.0 * value / maxValue) : 0.0;
    return QStringLiteral(
               "<tr><td class=\"lbl\">%1</td>"
               "<td class=\"bar\"><div style=\"width:%2%%\"></div></td>"
               "<td class=\"num\">%3</td></tr>\n")
        .arg(htmlEsc(label))
        .arg(pct, 0, 'f', 1)
        .arg(value);
}

/// 按次数降序；次数相同按名称升序（保证输出确定性，便于单测与 diff）
QList<QPair<QString, int>> sortedDesc(const QMap<QString, int> &m)
{
    QList<QPair<QString, int>> out;
    out.reserve(m.size());
    for (auto it = m.cbegin(); it != m.cend(); ++it)
        out.append(qMakePair(it.key(), it.value()));
    std::sort(out.begin(), out.end(), [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
        if (a.second != b.second)
            return a.second > b.second;
        return a.first < b.first;
    });
    return out;
}

} // namespace

StatisticsReport::Summary StatisticsReport::compute(const QList<InspectionRecord> &records,
                                                    const QString &flowFilter)
{
    Summary s;
    QMap<QDate, DayBucket> dayMap;
    QMap<QString, int> ngByNode;
    QMap<QString, int> recordsByFlow;
    bool haveTime = false;

    for (const InspectionRecord &r : records) {
        if (!flowFilter.isEmpty() && r.flowName != flowFilter)
            continue;

        ++s.totalRecords;
        if (!r.passed)
            ++s.ngRecords;
        recordsByFlow[r.flowName] += 1;

        const QDateTime ts = r.timestamp;
        DayBucket *bucket = nullptr;
        if (ts.isValid()) {
            if (!haveTime || ts < s.firstSeen)
                s.firstSeen = ts;
            if (!haveTime || ts > s.lastSeen)
                s.lastSeen = ts;
            haveTime = true;
            bucket = &dayMap[ts.date()];
            bucket->date = ts.date();
            bucket->totalRecords += 1;
            if (!r.passed)
                bucket->ngRecords += 1;
        }
        // 时间戳无效：计入总计，但不进日桶（避免造出 1970-01-01 的假数据点）

        if (r.nodeName == kRoundSummaryNodeName) {
            s.hasRounds = true;
            ++s.totalRounds;
            if (r.passed) {
                ++s.okRounds;
                if (bucket)
                    bucket->okRounds += 1;
            } else {
                ++s.ngRounds;
                if (bucket)
                    bucket->ngRounds += 1;
            }
        } else if (!r.passed) {
            ngByNode[r.nodeName] += 1;   // 整轮汇总记录不计入节点分布，否则重复计数
        }
    }

    s.yieldPercent = (s.totalRounds > 0) ? (100.0 * double(s.okRounds) / double(s.totalRounds)) : 0.0;

    for (auto it = dayMap.cbegin(); it != dayMap.cend(); ++it)
        s.days.append(it.value());       // QMap 按 key(QDate) 升序 → 天然按日期升序
    s.ngByNode = sortedDesc(ngByNode);
    s.recordsByFlow = sortedDesc(recordsByFlow);
    return s;
}

QString StatisticsReport::toCsv(const Summary &s, const QDateTime &generatedAt)
{
    QString out;
    out.reserve(2048);
    out += QChar(0xFEFF);   // UTF-8 BOM：Excel 双击打开不乱码

    out += csvLine({ QStringLiteral("VisionFlowPlatform 统计报表"),
                     QStringLiteral("生成时间"),
                     generatedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) });
    out += csvLine({ QString() });

    out += csvLine({ QStringLiteral("汇总") });
    out += csvLine({ QStringLiteral("指标"), QStringLiteral("数值") });
    out += csvLine({ QStringLiteral("总记录数"), QString::number(s.totalRecords) });
    out += csvLine({ QStringLiteral("NG 记录数"), QString::number(s.ngRecords) });
    out += csvLine({ QStringLiteral("总轮次"), QString::number(s.totalRounds) });
    out += csvLine({ QStringLiteral("OK 轮次"), QString::number(s.okRounds) });
    out += csvLine({ QStringLiteral("NG 轮次"), QString::number(s.ngRounds) });
    out += csvLine({ QStringLiteral("良率(%)"),
                     s.hasRounds ? QString::number(s.yieldPercent, 'f', 2) : QStringLiteral("无轮次数据") });
    out += csvLine({ QStringLiteral("最早记录"),
                     s.firstSeen.isValid() ? s.firstSeen.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                                           : QStringLiteral("-") });
    out += csvLine({ QStringLiteral("最晚记录"),
                     s.lastSeen.isValid() ? s.lastSeen.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                                          : QStringLiteral("-") });
    out += csvLine({ QString() });

    out += csvLine({ QStringLiteral("按天序列") });
    out += csvLine({ QStringLiteral("日期"), QStringLiteral("OK 轮次"), QStringLiteral("NG 轮次"),
                     QStringLiteral("记录数"), QStringLiteral("NG 记录数") });
    for (const DayBucket &d : s.days) {
        out += csvLine({ d.date.toString(QStringLiteral("yyyy-MM-dd")), QString::number(d.okRounds),
                         QString::number(d.ngRounds), QString::number(d.totalRecords),
                         QString::number(d.ngRecords) });
    }
    out += csvLine({ QString() });

    out += csvLine({ QStringLiteral("NG 按节点分布") });
    out += csvLine({ QStringLiteral("节点"), QStringLiteral("NG 次数") });
    for (const auto &p : s.ngByNode)
        out += csvLine({ p.first, QString::number(p.second) });
    out += csvLine({ QString() });

    out += csvLine({ QStringLiteral("按流程记录数") });
    out += csvLine({ QStringLiteral("流程"), QStringLiteral("记录数") });
    for (const auto &p : s.recordsByFlow)
        out += csvLine({ p.first, QString::number(p.second) });

    return out;
}

QString StatisticsReport::toHtml(const Summary &s, const QString &title, const QDateTime &generatedAt)
{
    const QString t = title.isEmpty() ? QStringLiteral("VisionFlowPlatform 统计报表") : title;
    QString html;
    html.reserve(8192);
    html += QStringLiteral(
                "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
                "<title>%1</title><style>"
                "body{font-family:'Microsoft YaHei',Arial,sans-serif;margin:24px;color:#222}"
                "h1{font-size:20px}h2{font-size:15px;margin-top:22px;border-left:4px solid #2b7;padding-left:8px}"
                "table{border-collapse:collapse;margin-top:6px;min-width:420px}"
                "td,th{border:1px solid #ddd;padding:4px 8px;font-size:13px}"
                "th{background:#f5f5f5}"
                ".kpi{display:inline-block;margin-right:18px;font-size:13px}"
                ".kpi b{font-size:18px;color:#2b7}"
                "td.bar{width:260px;padding:0}td.bar div{height:14px;background:#2b7}"
                "td.lbl{max-width:260px}td.num{text-align:right}"
                "</style></head><body>"
                "<h1>%1</h1><div>生成时间：%2</div>")
                .arg(htmlEsc(t), generatedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));

    html += QStringLiteral("<h2>汇总</h2><div>");
    html += QStringLiteral("<span class=\"kpi\">总记录数 <b>%1</b></span>")
                .arg(s.totalRecords);
    html += QStringLiteral("<span class=\"kpi\">NG 记录 <b>%1</b></span>").arg(s.ngRecords);
    if (s.hasRounds) {
        html += QStringLiteral("<span class=\"kpi\">总轮次 <b>%1</b></span>").arg(s.totalRounds);
        html += QStringLiteral("<span class=\"kpi\">OK / NG 轮次 <b>%1 / %2</b></span>")
                    .arg(s.okRounds).arg(s.ngRounds);
        html += QStringLiteral("<span class=\"kpi\">良率 <b>%1%</b></span>")
                    .arg(s.yieldPercent, 0, 'f', 2);
    } else {
        html += QStringLiteral("<span class=\"kpi\">良率 <b>无轮次数据</b>"
                               "（未产生「整轮汇总」记录）</span>");
    }
    html += QStringLiteral("</div>");

    if (!s.days.isEmpty()) {
        int maxOk = 1;
        int maxNg = 1;
        for (const DayBucket &d : s.days) {
            maxOk = std::max(maxOk, d.okRounds);
            maxNg = std::max(maxNg, d.ngRounds);
        }
        html += QStringLiteral("<h2>按天趋势（OK 轮次 / NG 轮次）</h2><table>");
        html += QStringLiteral("<tr><th>日期</th><th>OK 轮次</th><th>NG 轮次</th>"
                               "<th>记录数</th><th>NG 记录</th></tr>");
        for (const DayBucket &d : s.days) {
            html += QStringLiteral("<tr><td>%1</td><td class=\"num\">%2</td><td class=\"num\">%3</td>"
                                   "<td class=\"num\">%4</td><td class=\"num\">%5</td></tr>")
                        .arg(d.date.toString(QStringLiteral("yyyy-MM-dd")))
                        .arg(d.okRounds).arg(d.ngRounds).arg(d.totalRecords).arg(d.ngRecords);
        }
        html += QStringLiteral("</table><table>");
        for (const DayBucket &d : s.days)
            html += barRow(d.date.toString(QStringLiteral("MM-dd")), d.ngRounds, maxNg);
        html += QStringLiteral("</table>");
    }

    if (!s.ngByNode.isEmpty()) {
        const int maxNg = s.ngByNode.first().second;
        html += QStringLiteral("<h2>NG 按节点分布（Top %1）</h2><table>")
                    .arg(std::min(20, int(s.ngByNode.size())));
        int shown = 0;
        for (const auto &p : s.ngByNode) {
            if (++shown > 20)
                break;
            html += barRow(p.first, p.second, maxNg);
        }
        html += QStringLiteral("</table>");
    }

    if (!s.recordsByFlow.isEmpty()) {
        html += QStringLiteral("<h2>按流程记录数</h2><table><tr><th>流程</th><th>记录数</th></tr>");
        for (const auto &p : s.recordsByFlow)
            html += QStringLiteral("<tr><td>%1</td><td class=\"num\">%2</td></tr>")
                        .arg(htmlEsc(p.first)).arg(p.second);
        html += QStringLiteral("</table>");
    }

    html += QStringLiteral("</body></html>");
    return html;
}

QString StatisticsReport::suggestedFileName(const QString &ext, const QDateTime &now)
{
    const QString stamp = now.toString(QStringLiteral("yyyyMMdd_HHmm"));
    return QStringLiteral("report_%1.%2").arg(stamp, ext);
}
