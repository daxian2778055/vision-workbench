#include "ReportAutoExport.h"
#include "RecoveryStore.h"

#include <QDir>
#include <QSaveFile>
#include <QSettings>
#include <algorithm>
#include <functional>

namespace {

/// 配置键前缀（与报表窗口共用 `reporting/` 前缀；目标良率键也是这个前缀）
const QString kKeyPrefix = QStringLiteral("reporting/autoExport");
const QString kLastMsKey = QStringLiteral("reporting/autoExportLastMs");

/// 生成的文件名前缀；清理时只认这个前缀，不动目录里的其它文件
const QString kFilePrefix = QStringLiteral("report_");

/// 取文件名里的时间戳部分（report_<stamp>.<ext>）；不匹配返回空
QString stampOf(const QString &fileName)
{
    if (!fileName.startsWith(kFilePrefix))
        return QString();
    const int dot = fileName.lastIndexOf(QLatin1Char('.'));
    if (dot <= kFilePrefix.size())
        return QString();
    return fileName.mid(kFilePrefix.size(), dot - kFilePrefix.size());
}
}   // namespace

QString ReportAutoExport::Config::effectiveDir() const
{
    return dir.trimmed().isEmpty() ? defaultDir() : dir.trimmed();
}

bool ReportAutoExport::Config::isValid() const
{
    return intervalMinutes >= 1 && rangeHours >= 1 && keepFiles >= 1 && (csv || html);
}

ReportAutoExport::Config ReportAutoExport::loadConfig(QSettings *settings)
{
    QSettings local;
    QSettings &s = settings ? *settings : local;

    Config c;
    c.enabled = s.value(kKeyPrefix + QStringLiteral("Enabled"), c.enabled).toBool();
    c.intervalMinutes =
        s.value(kKeyPrefix + QStringLiteral("IntervalMinutes"), c.intervalMinutes).toInt();
    c.rangeHours = s.value(kKeyPrefix + QStringLiteral("RangeHours"), c.rangeHours).toInt();
    c.dir = s.value(kKeyPrefix + QStringLiteral("Dir"), QString()).toString();
    c.keepFiles = s.value(kKeyPrefix + QStringLiteral("KeepFiles"), c.keepFiles).toInt();
    c.csv = s.value(kKeyPrefix + QStringLiteral("Csv"), c.csv).toBool();
    c.html = s.value(kKeyPrefix + QStringLiteral("Html"), c.html).toBool();
    return c;
}

void ReportAutoExport::saveConfig(const Config &c, QSettings *settings)
{
    QSettings local;
    QSettings &s = settings ? *settings : local;

    s.setValue(kKeyPrefix + QStringLiteral("Enabled"), c.enabled);
    s.setValue(kKeyPrefix + QStringLiteral("IntervalMinutes"), c.intervalMinutes);
    s.setValue(kKeyPrefix + QStringLiteral("RangeHours"), c.rangeHours);
    s.setValue(kKeyPrefix + QStringLiteral("Dir"), c.dir);
    s.setValue(kKeyPrefix + QStringLiteral("KeepFiles"), c.keepFiles);
    s.setValue(kKeyPrefix + QStringLiteral("Csv"), c.csv);
    s.setValue(kKeyPrefix + QStringLiteral("Html"), c.html);
    s.sync();
}

qint64 ReportAutoExport::lastExportMs(QSettings *settings)
{
    QSettings local;
    QSettings &s = settings ? *settings : local;
    return s.value(kLastMsKey, qint64(0)).toLongLong();
}

void ReportAutoExport::setLastExportMs(qint64 ms, QSettings *settings)
{
    QSettings local;
    QSettings &s = settings ? *settings : local;
    s.setValue(kLastMsKey, ms);
    s.sync();
}

QString ReportAutoExport::defaultDir()
{
    // 与恢复文件同一套"运行时数据随安装目录走 + 可写探针回落"的约定（见 RecoveryStore）：
    // 路径逻辑只写一份，判错的表现是"目标机上定时导出静默不落盘"。
    const QString dir = RecoveryStore::runtimeDataRoot() + QStringLiteral("/reports");
    QDir().mkpath(dir);
    return dir;
}

bool ReportAutoExport::shouldExport(const Config &c, qint64 nowMs, qint64 lastMs)
{
    if (!c.enabled || !c.isValid())
        return false;   // 未启用 / 配置非法：宁可不导，也不乱写盘
    if (lastMs <= 0)
        return true;    // 从未导出过：开机后先出一份，别等满一个间隔才第一次出
    if (nowMs < lastMs)
        return true;    // 时钟回拨（对时/NTP）：立刻补一份，否则会一直"还没到点"再也不导
    return (nowMs - lastMs) >= qint64(c.intervalMinutes) * 60LL * 1000LL;
}

bool ReportAutoExport::exportNow(const Config &c, const StatisticsReport::Summary &summary,
                                 const QString &title, double targetPercent, const QDateTime &now,
                                 QStringList *written, QString *error)
{
    if (written)
        written->clear();
    const auto fail = [error](const QString &reason) {
        if (error)
            *error = reason;
        return false;
    };

    if (!c.isValid())
        return fail(QStringLiteral("定时导出配置非法（间隔/范围/保留份数需为正，且至少选一种格式）"));

    const QString dir = c.effectiveDir();
    if (!QDir().mkpath(dir))
        return fail(QStringLiteral("无法创建导出目录：%1").arg(dir));

    const QString stamp = now.toString(QStringLiteral("yyyyMMdd_HHmm"));
    int writtenCount = 0;
    QStringList failures;

    // 原子写：不留半个报表。半个 CSV 比没有更糟——现场看着"有数据"，实际是截断的。
    const auto writeFile = [&](const QString &ext, const QString &content) {
        const QString path = QDir(dir).filePath(QStringLiteral("report_%1.%2").arg(stamp, ext));
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            failures << QStringLiteral("%1 打开失败：%2").arg(path, file.errorString());
            return;
        }
        const QByteArray payload = content.toUtf8();
        if (file.write(payload) != payload.size()) {
            failures << QStringLiteral("%1 写入不完整").arg(path);
            file.cancelWriting();
            return;
        }
        if (!file.commit()) {
            failures << QStringLiteral("%1 提交失败：%2").arg(path, file.errorString());
            return;
        }
        if (written)
            written->append(path);
        ++writtenCount;
    };

    if (c.csv)
        writeFile(QStringLiteral("csv"), StatisticsReport::toCsv(summary, now, targetPercent));
    if (c.html)
        writeFile(QStringLiteral("html"), StatisticsReport::toHtml(summary, title, now, targetPercent));

    if (writtenCount == 0)
        return fail(failures.isEmpty() ? QStringLiteral("未选择导出格式") : failures.join(QStringLiteral("；")));
    if (!failures.isEmpty() && error)
        *error = failures.join(QStringLiteral("；"));   // 部分成功：如实回报，不假装全好
    return true;
}

int ReportAutoExport::pruneOldFiles(const QString &dir, int keep)
{
    if (keep < 1)
        keep = 1;

    QDir d(dir);
    // 按"时间戳"分组：一份报告可能同时有 csv/html/png，必须整份保留或整份删除，
    // 否则会留下"有 HTML 没 CSV"的半份报告，比没有更让人困惑。
    QMap<QString, QStringList> byStamp;
    const QStringList names =
        d.entryList(QStringList{ QStringLiteral("report_*.*") }, QDir::Files, QDir::Name);
    for (const QString &name : names) {
        const QString stamp = stampOf(name);
        if (!stamp.isEmpty())
            byStamp[stamp] << name;
    }
    if (byStamp.size() <= keep)
        return 0;

    // 时间戳格式为 yyyyMMdd_HHmm ⇒ 字典序即时间序（倒序 = 从新到旧）
    QStringList stamps = byStamp.keys();
    std::sort(stamps.begin(), stamps.end(), std::greater<QString>());

    int removed = 0;
    for (int i = keep; i < stamps.size(); ++i) {
        const QStringList files = byStamp.value(stamps.at(i));
        for (const QString &name : files) {
            if (d.remove(name))
                ++removed;
        }
    }
    return removed;
}
