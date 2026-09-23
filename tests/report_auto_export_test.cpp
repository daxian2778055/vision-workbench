#include <QtTest>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QTime>

#include "ReportAutoExport.h"
#include "StatisticsReport.h"
#include "InspectionRecord.h"

// 定时导出（P1-11 收尾）契约测试：
//  ① **到点判定**：未启用不导、从未导过先出一份、未到间隔不导、**时钟回拨**（现场对时）不能卡死；
//  ② **落盘**：CSV/HTML 都写出、CSV 带 BOM、HTML 离线自包含、**不留临时文件**（原子替换）、
//     目录不可用时**如实失败**（不许假装成功——定时任务没人看着）；
//  ③ **保留策略**：按"时间戳整份"保留/删除，不能留下"有 HTML 没 CSV"的半份报告；
//     只动本程序生成的 report_*，不碰目录里别的文件；
//  ④ 配置往返用**注入的 QSettings**（测试不污染用户配置）。
class ReportAutoExportTest : public QObject
{
    Q_OBJECT
private slots:
    void testShouldExportDecision();
    void testInvalidConfigNeverExports();
    void testExportWritesAtomicFiles();
    void testExportReportsFailure();
    void testPruneKeepsNewestWholeSets();
    void testPruneIgnoresForeignFiles();
    void testConfigRoundTripWithInjectedSettings();
    void testLastExportPersisted();
};

namespace {

QDateTime stamp(int dayOfMonth, int hour, int minute)
{
    return QDateTime(QDate(2026, 9, dayOfMonth), QTime(hour, minute, 0));
}

/// 一份便于断言的小汇总：4 轮里 3 OK（良率 75%），跨 2 个小时
StatisticsReport::Summary sampleSummary()
{
    QList<InspectionRecord> records;
    const auto round = [](bool passed, const QDateTime &ts) {
        InspectionRecord r;
        r.flowName = QStringLiteral("流程A");
        r.nodeName = kRoundSummaryNodeName;
        r.passed = passed;
        r.timestamp = ts;
        return r;
    };
    records << round(true, stamp(23, 8, 5));
    records << round(true, stamp(23, 8, 35));
    records << round(false, stamp(23, 9, 5));
    records << round(true, stamp(23, 9, 25));
    return StatisticsReport::compute(records, QString(), StatisticsReport::Granularity::ByHour);
}

QStringList fileNamesIn(const QString &dir)
{
    return QDir(dir).entryList(QDir::Files, QDir::Name);
}
}   // namespace

void ReportAutoExportTest::testShouldExportDecision()
{
    ReportAutoExport::Config cfg;
    QVERIFY2(!cfg.enabled, "定时导出必须默认关闭（定时写盘属现场策略，不替用户默认决定）");
    QVERIFY(cfg.isValid());

    const qint64 now = QDateTime(QDate(2026, 9, 23), QTime(12, 0)).toMSecsSinceEpoch();

    // 未启用：任何时刻都不导
    QVERIFY(!ReportAutoExport::shouldExport(cfg, now, 0));

    cfg.enabled = true;
    // 从未导出过 → 立刻出一份（开机后先有报表，不用等满一个间隔）
    QVERIFY(ReportAutoExport::shouldExport(cfg, now, 0));

    // 未到间隔（默认每天一次）
    QVERIFY2(!ReportAutoExport::shouldExport(cfg, now, now - 60 * 1000), "不到间隔不该重复导出");
    QVERIFY2(!ReportAutoExport::shouldExport(cfg, now, now - (1440 * 60 * 1000 - 1000)),
             "差 1 秒也不该导（间隔判定要严）");
    // 到点
    QVERIFY(ReportAutoExport::shouldExport(cfg, now, now - 1440 * 60 * 1000));

    // 时钟回拨（现场对时/NTP）：不能因为 now < last 就"永远还没到点"
    QVERIFY2(ReportAutoExport::shouldExport(cfg, now, now + 3600 * 1000),
             "时钟回拨后必须仍会导出，否则定时导出静默死掉");

    // 间隔可配：改成 5 分钟
    cfg.intervalMinutes = 5;
    QVERIFY(ReportAutoExport::shouldExport(cfg, now, now - 5 * 60 * 1000));
    QVERIFY(!ReportAutoExport::shouldExport(cfg, now, now - 4 * 60 * 1000));
}

void ReportAutoExportTest::testInvalidConfigNeverExports()
{
    ReportAutoExport::Config cfg;
    cfg.enabled = true;
    const qint64 now = 1000000000LL;

    cfg.intervalMinutes = 0;
    QVERIFY(!cfg.isValid());
    QVERIFY2(!ReportAutoExport::shouldExport(cfg, now, 0), "配置非法时宁可不导，也不能乱写盘");

    cfg.intervalMinutes = 60;
    cfg.keepFiles = 0;
    QVERIFY(!cfg.isValid());
    QVERIFY(!ReportAutoExport::shouldExport(cfg, now, 0));

    cfg.keepFiles = 30;
    cfg.csv = false;
    cfg.html = false;
    QVERIFY(!cfg.isValid());
    QVERIFY(!ReportAutoExport::shouldExport(cfg, now, 0));

    // 导出接口本身也要挡住非法配置（不能只靠调用方自觉）
    QString error;
    QStringList written;
    QVERIFY(!ReportAutoExport::exportNow(cfg, sampleSummary(), QString(), 0.0, QDateTime::currentDateTime(),
                                         &written, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(written.isEmpty());
}

void ReportAutoExportTest::testExportWritesAtomicFiles()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ReportAutoExport::Config cfg;
    cfg.enabled = true;
    cfg.dir = tmp.path();

    const QDateTime now = stamp(23, 10, 30);
    QStringList written;
    QString error;
    QVERIFY2(ReportAutoExport::exportNow(cfg, sampleSummary(), QStringLiteral("自动报表"), 98.0, now,
                                         &written, &error),
             qPrintable(error));
    QCOMPARE(written.size(), 2);   // CSV + HTML

    // 文件名带时间戳（保留策略按它分组）
    QVERIFY2(QFileInfo(written.at(0)).fileName().contains(QStringLiteral("20260923_1030")),
             qPrintable(written.at(0)));

    // **不留临时文件**：QSaveFile 的原子替换若失败会留下 .tmp，这里必须干净
    const QStringList files = fileNamesIn(tmp.path());
    QCOMPARE(files.size(), 2);
    for (const QString &name : files)
        QVERIFY2(!name.endsWith(QStringLiteral(".tmp")), qPrintable(name));

    // CSV：带 UTF-8 BOM（Excel 双击不乱码）+ 按小时分段
    const QString csvPath = (QFileInfo(written.at(0)).suffix() == QStringLiteral("csv")) ? written.at(0)
                                                                                        : written.at(1);
    QFile csv(csvPath);
    QVERIFY(csv.open(QIODevice::ReadOnly));
    const QByteArray csvBytes = csv.readAll();
    csv.close();
    QVERIFY2(csvBytes.startsWith("\xEF\xBB\xBF"), "CSV 缺少 UTF-8 BOM");
    const QString csvText = QString::fromUtf8(csvBytes);
    QVERIFY2(csvText.contains(QStringLiteral("按小时序列")), "定时导出应按小时分段（默认范围是最近若干小时）");
    QVERIFY2(csvText.contains(QStringLiteral("良率目标")), "定时导出应带上目标良率与达标结论");

    // HTML：离线自包含
    const QString htmlPath = (QFileInfo(written.at(0)).suffix() == QStringLiteral("html"))
                                 ? written.at(0)
                                 : written.at(1);
    QFile html(htmlPath);
    QVERIFY(html.open(QIODevice::ReadOnly));
    const QString htmlText = QString::fromUtf8(html.readAll());
    html.close();
    QVERIFY(htmlText.contains(QStringLiteral("自动报表")));
    QVERIFY2(!htmlText.contains(QStringLiteral("http")), "HTML 必须自包含（离线环境打开不能依赖网络）");

    // 目录已存在时也能正常导出（幂等）
    QStringList again;
    QVERIFY(ReportAutoExport::exportNow(cfg, sampleSummary(), QString(), 0.0, now, &again, &error));
    QCOMPARE(again.size(), 2);
    QCOMPARE(fileNamesIn(tmp.path()).size(), 2);   // 同一时间戳：覆盖写，不产生第 3 个文件
}

void ReportAutoExportTest::testExportReportsFailure()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    // 把一个"普通文件"当作导出目录路径：mkpath 必然失败
    const QString blocker = QDir(tmp.path()).filePath(QStringLiteral("blocker"));
    QFile f(blocker);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("not a dir");
    f.close();

    ReportAutoExport::Config cfg;
    cfg.enabled = true;
    cfg.dir = blocker;

    QStringList written;
    QString error;
    QVERIFY2(!ReportAutoExport::exportNow(cfg, sampleSummary(), QString(), 0.0, QDateTime::currentDateTime(),
                                          &written, &error),
             "目录不可用时必须失败");
    QVERIFY2(!error.isEmpty(), "失败必须给出原因（定时任务没人看着，静默失败等于没有功能）");
    QVERIFY(written.isEmpty());
}

void ReportAutoExportTest::testPruneKeepsNewestWholeSets()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // 三份报告，每份含 csv+html；中间那份多一个 png（手动导出的图表）
    const QStringList stamps{ QStringLiteral("20260920_0800"), QStringLiteral("20260921_0800"),
                              QStringLiteral("20260922_0800") };
    for (const QString &s : stamps) {
        for (const QString &ext : { QStringLiteral("csv"), QStringLiteral("html") }) {
            QFile f(QDir(tmp.path()).filePath(QStringLiteral("report_%1.%2").arg(s, ext)));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x");
            f.close();
        }
    }
    {
        QFile f(QDir(tmp.path()).filePath(QStringLiteral("report_20260921_0800.png")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
    }
    QCOMPARE(fileNamesIn(tmp.path()).size(), 7);

    // 只留最近 2 份 ⇒ 删除最旧那份（csv+html 两个文件）
    const int removed = ReportAutoExport::pruneOldFiles(tmp.path(), 2);
    QCOMPARE(removed, 2);

    const QStringList left = fileNamesIn(tmp.path());
    QCOMPARE(left.size(), 5);
    for (const QString &name : left)
        QVERIFY2(!name.contains(QStringLiteral("20260920_0800")), qPrintable(name));
    // 整份保留：中间那份的 png 也在（不然会留下"有 png 没 csv"的残片）
    QVERIFY(left.contains(QStringLiteral("report_20260921_0800.csv")));
    QVERIFY(left.contains(QStringLiteral("report_20260921_0800.html")));
    QVERIFY(left.contains(QStringLiteral("report_20260921_0800.png")));

    // 再清一次：数量已达标 → 不再删（幂等）
    QCOMPARE(ReportAutoExport::pruneOldFiles(tmp.path(), 2), 0);
    // keep 至少按 1 处理（配置写坏了也不能把目录清空）
    QCOMPARE(ReportAutoExport::pruneOldFiles(tmp.path(), 0), 3);
    QCOMPARE(fileNamesIn(tmp.path()).size(), 2);
}

void ReportAutoExportTest::testPruneIgnoresForeignFiles()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QStringList foreign{ QStringLiteral("notes.txt"), QStringLiteral("visionflow.db"),
                               QStringLiteral("report.csv") };
    for (const QString &name : foreign) {
        QFile f(QDir(tmp.path()).filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
    }

    QCOMPARE(ReportAutoExport::pruneOldFiles(tmp.path(), 1), 0);
    QCOMPARE(fileNamesIn(tmp.path()).size(), 3);   // 目录里别的文件一个都不能动
}

void ReportAutoExportTest::testConfigRoundTripWithInjectedSettings()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    // 用注入的 ini 文件做配置后端：测试不污染用户真实配置
    const QString iniPath = QDir(tmp.path()).filePath(QStringLiteral("settings.ini"));

    ReportAutoExport::Config out;
    out.enabled = true;
    out.intervalMinutes = 90;
    out.rangeHours = 8;
    out.dir = QStringLiteral("D:/reports");
    out.keepFiles = 7;
    out.csv = true;
    out.html = false;
    {
        QSettings settings(iniPath, QSettings::IniFormat);
        ReportAutoExport::saveConfig(out, &settings);
        QCOMPARE(ReportAutoExport::lastExportMs(&settings), qint64(0));   // 未记录过 = 0
        ReportAutoExport::setLastExportMs(1712345678000LL, &settings);
    }

    QSettings settings(iniPath, QSettings::IniFormat);
    const ReportAutoExport::Config in = ReportAutoExport::loadConfig(&settings);
    QCOMPARE(in.enabled, out.enabled);
    QCOMPARE(in.intervalMinutes, out.intervalMinutes);
    QCOMPARE(in.rangeHours, out.rangeHours);
    QCOMPARE(in.dir, out.dir);
    QCOMPARE(in.keepFiles, out.keepFiles);
    QCOMPARE(in.csv, out.csv);
    QCOMPARE(in.html, out.html);
    QCOMPARE(ReportAutoExport::lastExportMs(&settings), qint64(1712345678000LL));
}

void ReportAutoExportTest::testLastExportPersisted()
{
    // 默认（空）配置：未启用 → 不导出；且 lastExportMs 为 0 表示"从未导出"
    QTemporaryDir tmp;
    const QString iniPath = QDir(tmp.path()).filePath(QStringLiteral("empty.ini"));
    QSettings settings(iniPath, QSettings::IniFormat);
    QCOMPARE(ReportAutoExport::lastExportMs(&settings), qint64(0));

    const ReportAutoExport::Config cfg = ReportAutoExport::loadConfig(&settings);
    QVERIFY(!cfg.enabled);
    QVERIFY(cfg.isValid());                                  // 默认值本身必须合法
    QVERIFY(!cfg.dir.trimmed().isEmpty() || cfg.dir.isEmpty());   // effectiveDir 负责兜底默认目录
    QVERIFY(!cfg.effectiveDir().isEmpty());
}

QTEST_MAIN(ReportAutoExportTest)
#include "report_auto_export_test.moc"
