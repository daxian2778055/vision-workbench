// DiagnosticsCollectorTest（P2 工具菜单「异常信息收集」数据面）：
// 日志尾部裁剪（行数/字符数/CRLF 归一）、报告字段与未知项标注、UTF-8 导出往返、错误上下文。
// 纯文本处理，不进 MainWindow；QTEST_MAIN 链接 Widgets 后自动建 QApplication。
#include <QFile>
#include <QStringConverter>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtTest>

#include "DiagnosticsCollector.h"

class DiagnosticsCollectorTest : public QObject
{
    Q_OBJECT

private slots:
    void testTailLinesByLinesAndChars();
    void testReportContainsFields();
    void testReportUnknownMarkers();
    void testWriteReportRoundTrip();
    void testWriteReportErrors();
};

namespace {

DiagnosticsInput fullInput()
{
    DiagnosticsInput input;
    input.appVersion = QStringLiteral("VisionFlowPlatform 1.0.0 (abc1234 / 2026-11-07)");
    input.osVersion = QStringLiteral("Windows 10 Version 22H2");
    input.qtVersion = QStringLiteral("6.11.0");
    input.halconVersion = QStringLiteral("24.11.0.0");
    input.cpuArchitecture = QStringLiteral("x86_64");
    input.appDir = QStringLiteral("E:/halcon/2/xin1/build/bin/Release");
    input.schemePath = QStringLiteral("E:/schemes/demo.vfp");
    input.logTail = QStringLiteral("[2026-11-07 10:00:00] 流程启动");
    input.cpuPercent = 35;
    input.memoryPercent = 62;
    input.diskPercent = 71;
    return input;
}

QString readTextFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    return stream.readAll();
}

} // namespace

void DiagnosticsCollectorTest::testTailLinesByLinesAndChars()
{
    // 行数限制：只保留末尾 3 行
    QCOMPARE(DiagnosticsCollector::tailLines(QStringLiteral("a\nb\nc\nd\ne"), 3, 1000),
             QStringLiteral("c\nd\ne"));

    // 字符数限制：优先保留最靠近结尾的内容
    QCOMPARE(DiagnosticsCollector::tailLines(QStringLiteral("abcdef"), 10, 2),
             QStringLiteral("ef"));

    // CRLF / CR 统一归一为 LF（避免导出后行尾混排）
    QCOMPARE(DiagnosticsCollector::tailLines(QStringLiteral("a\r\nb\rc"), 10, 1000),
             QStringLiteral("a\nb\nc"));

    // 空日志返回空串；下限保护（0/负数按 1 处理）
    QVERIFY(DiagnosticsCollector::tailLines(QString(), 5, 100).isEmpty());
    QCOMPARE(DiagnosticsCollector::tailLines(QStringLiteral("x\ny"), 0, 0), QStringLiteral("y"));
}

void DiagnosticsCollectorTest::testReportContainsFields()
{
    const QString report = DiagnosticsCollector::buildReport(fullInput());

    QVERIFY2(report.startsWith(QStringLiteral("VisionFlowPlatform 异常信息收集报告")),
             qPrintable(report.left(80)));
    QVERIFY2(report.contains(QStringLiteral("生成时间: ")), "缺少生成时间");
    // 各采集字段必须原样出现在报告里
    QVERIFY2(report.contains(QStringLiteral("版本: VisionFlowPlatform 1.0.0")), "缺少程序版本");
    QVERIFY2(report.contains(QStringLiteral("操作系统: Windows 10 Version 22H2")), "缺少操作系统");
    QVERIFY2(report.contains(QStringLiteral("Qt: 6.11.0")), "缺少 Qt 版本");
    QVERIFY2(report.contains(QStringLiteral("HALCON: 24.11.0.0")), "缺少 HALCON 版本");
    QVERIFY2(report.contains(QStringLiteral("CPU 架构: x86_64")), "缺少 CPU 架构");
    QVERIFY2(report.contains(QStringLiteral("CPU 占用: 35%")), "缺少 CPU 占用");
    QVERIFY2(report.contains(QStringLiteral("内存占用: 62%")), "缺少内存占用");
    QVERIFY2(report.contains(QStringLiteral("方案卷占用: 71%")), "缺少磁盘占用");
    QVERIFY2(report.contains(QStringLiteral("demo.vfp")), "缺少方案路径");
    QVERIFY2(report.contains(QStringLiteral("流程启动")), "缺少日志内容");
    // 明确告知报告不含二进制内容，避免用户误以为已打包图像/模型
    QVERIFY2(report.contains(QStringLiteral("不包含图像、模型或方案内容")), "缺少范围说明");
}

void DiagnosticsCollectorTest::testReportUnknownMarkers()
{
    DiagnosticsInput input;   // 全空 + 百分比 -1
    const QString report = DiagnosticsCollector::buildReport(input);

    QVERIFY2(report.contains(QStringLiteral("版本: 未获取")), "空版本未标注未获取");
    QVERIFY2(report.contains(QStringLiteral("HALCON: 未获取")), "空 HALCON 未标注未获取");
    QVERIFY2(report.contains(QStringLiteral("CPU 占用: 未获取")), "未知 CPU 未标注未获取");
    QVERIFY2(report.contains(QStringLiteral("内存占用: 未获取")), "未知内存未标注未获取");
    QVERIFY2(report.contains(QStringLiteral("未命名方案（尚未保存）")), "未命名方案标注缺失");
    QVERIFY2(report.contains(QStringLiteral("（无日志）")), "空日志标注缺失");
}

void DiagnosticsCollectorTest::testWriteReportRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "无法创建临时目录");

    const QString report = DiagnosticsCollector::buildReport(fullInput());
    const QString pathWithoutSuffix = dir.filePath(QStringLiteral("diagnostics"));

    QString error;
    QVERIFY2(DiagnosticsCollector::writeReport(pathWithoutSuffix, report, &error),
             qPrintable(error));

    const QString txtPath = dir.filePath(QStringLiteral("diagnostics.txt"));
    QVERIFY2(QFile::exists(txtPath), "未自动补 .txt 扩展名");
    QCOMPARE(readTextFile(txtPath), report);
}

void DiagnosticsCollectorTest::testWriteReportErrors()
{
    const QString report = QStringLiteral("报告");
    QString error;

    QVERIFY(!DiagnosticsCollector::writeReport(QString(), report, &error));
    QVERIFY2(error.contains(QStringLiteral("路径为空")), qPrintable(error));

    error.clear();
    QVERIFY(!DiagnosticsCollector::writeReport(QStringLiteral("report.txt"), QString(), &error));
    QVERIFY2(error.contains(QStringLiteral("内容为空")), qPrintable(error));

    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "无法创建临时目录");
    error.clear();
    QVERIFY(!DiagnosticsCollector::writeReport(dir.filePath(QStringLiteral("missing/report.txt")),
                                               report, &error));
    QVERIFY2(error.contains(QStringLiteral("目录不存在")), qPrintable(error));
}

QTEST_MAIN(DiagnosticsCollectorTest)
#include "diagnostics_collector_test.moc"
