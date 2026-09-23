// 统计报表（P1-11）测试：良率口径、节点 NG 分布、按天序列、流程过滤、边界与导出内容
//
// 重点钉住的**口径**（都是容易含糊的地方）：
//   · 良率来自「整轮汇总」记录；没有这类记录时 hasRounds=false，界面必须显示"无轮次数据"
//     而不是 0% 良率（把"没数据"说成"全 NG"）；
//   · 节点 NG 分布**不含**整轮汇总记录（否则重复计数）；
//   · 时间戳无效的记录计入总数但不进日桶（否则会造出 1970-01-01 的假数据点）；
//   · 导出必须离线自包含（HTML 里不得出现 http，CSV 带 BOM 且字段正确转义）。
#include <QtTest/QtTest>
#include <QObject>
#include <QString>
#include <QDateTime>
#include <QList>
#include <QDate>
#include <QTime>

#include "StatisticsReport.h"
#include "InspectionRecord.h"

class StatisticsReportTest : public QObject
{
    Q_OBJECT
private slots:
    void testYieldFromRoundSummary();
    void testNoRoundsDataIsNotZeroYield();
    void testNgByNodeDistribution();
    void testFlowFilterAndSorting();
    void testInvalidTimestampHandling();
    void testCsvExport();
    void testHtmlExportOfflineSelfContained();
};

namespace {

QDateTime at(int dayOfMonth, int hour = 10, int minute = 0)
{
    return QDateTime(QDate(2026, 9, dayOfMonth), QTime(hour, minute, 0));
}

InspectionRecord nodeRec(const QString &flow, const QString &node, bool passed, const QDateTime &ts)
{
    InspectionRecord r;
    r.flowName = flow;
    r.nodeName = node;
    r.passed = passed;
    r.value = passed ? QStringLiteral("OK") : QStringLiteral("NG");
    r.timestamp = ts;
    return r;
}

InspectionRecord roundRec(const QString &flow, bool passed, const QDateTime &ts)
{
    return nodeRec(flow, kRoundSummaryNodeName, passed, ts);
}

} // namespace

void StatisticsReportTest::testYieldFromRoundSummary()
{
    QList<InspectionRecord> records;
    for (int i = 0; i < 2; ++i) {                 // 2 个 OK 轮
        records << nodeRec(QStringLiteral("流程A"), QStringLiteral("找边"), true, at(10, 9, i));
        records << roundRec(QStringLiteral("流程A"), true, at(10, 9, i));
    }
    records << nodeRec(QStringLiteral("流程A"), QStringLiteral("找边"), false, at(10, 11, 0));
    records << roundRec(QStringLiteral("流程A"), false, at(10, 11, 0));   // 1 个 NG 轮

    const StatisticsReport::Summary s = StatisticsReport::compute(records);
    QVERIFY(s.hasRounds);
    QCOMPARE(s.totalRounds, 3);
    QCOMPARE(s.okRounds, 2);
    QCOMPARE(s.ngRounds, 1);
    QVERIFY2(qAbs(s.yieldPercent - 66.6667) < 0.01,
             qPrintable(QStringLiteral("良率 %1（应≈66.67）").arg(s.yieldPercent)));
    QCOMPARE(s.totalRecords, 6);
    QCOMPARE(s.ngRecords, 2);

    QCOMPARE(s.days.size(), 1);
    QCOMPARE(s.days.first().date, QDate(2026, 9, 10));
    QCOMPARE(s.days.first().okRounds, 2);
    QCOMPARE(s.days.first().ngRounds, 1);
    QCOMPARE(s.days.first().totalRecords, 6);
}

void StatisticsReportTest::testNoRoundsDataIsNotZeroYield()
{
    // 只有节点记录（例如数据库里是尚未升级前写入的旧数据）：良率必须标为"无数据"
    QList<InspectionRecord> records;
    records << nodeRec(QStringLiteral("流程A"), QStringLiteral("找边"), true, at(11));
    records << nodeRec(QStringLiteral("流程A"), QStringLiteral("Blob"), false, at(11));
    records << nodeRec(QStringLiteral("流程A"), QStringLiteral("Blob"), false, at(11));

    const StatisticsReport::Summary s = StatisticsReport::compute(records);
    QVERIFY2(!s.hasRounds, "没有整轮汇总记录时 hasRounds 必须为 false");
    QCOMPARE(s.totalRounds, 0);
    QCOMPARE(s.totalRecords, 3);
    QCOMPARE(s.ngRecords, 2);
    // 节点分布仍然可用
    QCOMPARE(s.ngByNode.size(), 1);
    QCOMPARE(s.ngByNode.first().first, QStringLiteral("Blob"));
    QCOMPARE(s.ngByNode.first().second, 2);
}

void StatisticsReportTest::testNgByNodeDistribution()
{
    QList<InspectionRecord> records;
    records << nodeRec(QStringLiteral("流程A"), QStringLiteral("节点甲"), false, at(12));
    records << nodeRec(QStringLiteral("流程A"), QStringLiteral("节点甲"), false, at(12));
    records << nodeRec(QStringLiteral("流程A"), QStringLiteral("节点甲"), false, at(12));
    records << nodeRec(QStringLiteral("流程A"), QStringLiteral("节点乙"), false, at(12));
    records << nodeRec(QStringLiteral("流程A"), QStringLiteral("节点乙"), true, at(12));
    records << nodeRec(QStringLiteral("流程A"), QStringLiteral("节点丙"), true, at(12));
    records << roundRec(QStringLiteral("流程A"), false, at(12));   // 整轮 NG：不得进节点分布

    const StatisticsReport::Summary s = StatisticsReport::compute(records);
    QCOMPARE(s.ngByNode.size(), 2);
    QCOMPARE(s.ngByNode.at(0).first, QStringLiteral("节点甲"));
    QCOMPARE(s.ngByNode.at(0).second, 3);
    QCOMPARE(s.ngByNode.at(1).first, QStringLiteral("节点乙"));
    QCOMPARE(s.ngByNode.at(1).second, 1);
    for (const auto &p : s.ngByNode)
        QVERIFY2(p.first != kRoundSummaryNodeName, "整轮汇总记录混进了节点 NG 分布");
}

void StatisticsReportTest::testFlowFilterAndSorting()
{
    QList<InspectionRecord> records;
    records << nodeRec(QStringLiteral("流程A"), QStringLiteral("同次数节点"), false, at(13));
    records << nodeRec(QStringLiteral("流程B"), QStringLiteral("同次数节点"), false, at(13));
    records << nodeRec(QStringLiteral("流程B"), QStringLiteral("另一个"), false, at(13));
    records << roundRec(QStringLiteral("流程A"), true, at(13));
    records << roundRec(QStringLiteral("流程B"), false, at(13));

    // 不过滤：两个流程都在，按记录数降序（B=3 条 > A=2 条）
    const StatisticsReport::Summary all = StatisticsReport::compute(records);
    QCOMPARE(all.recordsByFlow.size(), 2);
    QCOMPARE(all.recordsByFlow.at(0).first, QStringLiteral("流程B"));
    QCOMPARE(all.recordsByFlow.at(0).second, 3);
    QCOMPARE(all.totalRounds, 2);

    // 过滤到流程A：良率 100%，节点分布只有一条
    const StatisticsReport::Summary a = StatisticsReport::compute(records, QStringLiteral("流程A"));
    QCOMPARE(a.totalRecords, 2);
    QCOMPARE(a.totalRounds, 1);
    QCOMPARE(a.okRounds, 1);
    QVERIFY2(qAbs(a.yieldPercent - 100.0) < 1e-9, "流程A 良率应为 100");
    QCOMPARE(a.ngByNode.size(), 1);

    // 确定性：同次数时按**名称的 Unicode 码点升序**。别凭直觉写死顺序——
    // 实测 "另"(U+53E6) < "同"(U+540C)，与"看着哪个顺眼"无关。这里直接借用 Qt 的排序规则，
    // 让用例表达的是"输出稳定且与 Qt 规则一致"，而不是我猜的一个字面顺序。
    QStringList expectedOrder { QStringLiteral("同次数节点"), QStringLiteral("另一个") };
    expectedOrder.sort();
    const StatisticsReport::Summary b = StatisticsReport::compute(records, QStringLiteral("流程B"));
    QCOMPARE(b.ngByNode.size(), 2);
    QCOMPARE(b.ngByNode.at(0).first, expectedOrder.at(0));
    QCOMPARE(b.ngByNode.at(1).first, expectedOrder.at(1));
}

void StatisticsReportTest::testInvalidTimestampHandling()
{
    QList<InspectionRecord> records;
    records << nodeRec(QStringLiteral("流程A"), QStringLiteral("找边"), true, QDateTime());  // 无效时间戳
    records << roundRec(QStringLiteral("流程A"), true, QDateTime());
    records << nodeRec(QStringLiteral("流程A"), QStringLiteral("找边"), false, at(14));

    const StatisticsReport::Summary s = StatisticsReport::compute(records);
    QCOMPARE(s.totalRecords, 3);          // 无效时间戳仍计入总数
    QVERIFY(s.hasRounds);
    QCOMPARE(s.totalRounds, 1);           // 本 fixture 只有 1 条整轮记录（另一条是普通节点记录）
    QCOMPARE(s.days.size(), 1);           // 只有一条落进日桶，不造 1970 假数据点
    QCOMPARE(s.days.first().date, QDate(2026, 9, 14));
    QVERIFY(s.firstSeen.isValid());
    QCOMPARE(s.firstSeen.date(), QDate(2026, 9, 14));
}

void StatisticsReportTest::testCsvExport()
{
    QList<InspectionRecord> records;
    const QString trickyFlow = QStringLiteral("流程,含逗号\"与引号");
    records << nodeRec(trickyFlow, QStringLiteral("节点甲"), false, at(15, 8, 30));
    records << roundRec(trickyFlow, false, at(15, 8, 30));   // NG 轮
    records << roundRec(trickyFlow, true, at(15, 8, 31));    // 再补一个 OK 轮 → 良率恰为 50.00
    const StatisticsReport::Summary s = StatisticsReport::compute(records);

    const QDateTime gen = at(15, 9, 0);
    const QString csv = StatisticsReport::toCsv(s, gen);
    QVERIFY2(csv.startsWith(QChar(0xFEFF)), "CSV 必须以 UTF-8 BOM 开头（否则 Excel 打开中文乱码）");
    QVERIFY2(csv.contains(QStringLiteral("良率(%)")), "缺少良率行");
    QVERIFY2(csv.contains(QStringLiteral("50.00")), "良率数值缺失（1 OK / 1 NG 应为 50.00）");
    QVERIFY2(csv.contains(QStringLiteral("按天序列")), "缺少按天段");
    QVERIFY2(csv.contains(QStringLiteral("NG 按节点分布")), "缺少节点分布段");
    QVERIFY2(csv.contains(QStringLiteral("\"流程,含逗号\"\"与引号\"")), "含逗号/引号的字段未按 CSV 规则转义");
    QVERIFY2(csv.contains(QStringLiteral("\r\n")), "CSV 行尾应为 CRLF");
    QVERIFY2(csv.contains(gen.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))), "缺少生成时间");
}

void StatisticsReportTest::testHtmlExportOfflineSelfContained()
{
    QList<InspectionRecord> records;
    records << nodeRec(QStringLiteral("流程<script>"), QStringLiteral("节点<甲>"), false, at(16));
    records << roundRec(QStringLiteral("流程<script>"), false, at(16));
    const StatisticsReport::Summary s = StatisticsReport::compute(records);

    const QString html = StatisticsReport::toHtml(s, QStringLiteral("生产报表"), at(16, 12, 0));
    QVERIFY(html.contains(QStringLiteral("<!DOCTYPE html>")));
    QVERIFY(html.contains(QStringLiteral("生产报表")));
    QVERIFY2(!html.contains(QStringLiteral("http")), "HTML 必须自包含（离线可用），不得引用任何外部资源");
    QVERIFY2(html.contains(QStringLiteral("&lt;甲&gt;")), "节点名未做 HTML 转义");
    QVERIFY2(!html.contains(QStringLiteral("<script>")), "流程名中的标签未转义（会破坏报表结构）");
    QVERIFY2(html.contains(QStringLiteral("0.00%")), "应显示 0.00% 良率（1 个 NG 轮）");
    QVERIFY2(html.contains(QStringLiteral("按天趋势")), "缺少按天趋势段");
}

QTEST_MAIN(StatisticsReportTest)
#include "statistics_report_test.moc"
