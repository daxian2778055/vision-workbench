#include "DiagnosticsCollector.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringConverter>
#include <QStringList>
#include <QTextStream>

namespace {

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

} // namespace

QString DiagnosticsCollector::tailLines(const QString &text, int maxLines, int maxChars)
{
    const int keepLines = qMax(1, maxLines);
    const int keepChars = qMax(1, maxChars);

    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    QStringList lines = normalized.split(QLatin1Char('\n'));
    if (lines.size() > keepLines)
        lines = lines.mid(lines.size() - keepLines);

    QString tail = lines.join(QLatin1Char('\n'));
    if (tail.size() > keepChars)
        tail = tail.right(keepChars);
    return tail;
}

QString DiagnosticsCollector::buildReport(const DiagnosticsInput &input)
{
    auto orUnknown = [](const QString &value) {
        return value.trimmed().isEmpty() ? QStringLiteral("未获取") : value;
    };
    auto percentText = [](int percent) {
        return percent < 0 ? QStringLiteral("未获取") : QStringLiteral("%1%").arg(percent);
    };

    QStringList lines;
    lines << QStringLiteral("VisionFlowPlatform 异常信息收集报告");
    lines << QStringLiteral("生成时间: %1").arg(
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    lines << QStringLiteral("----------------------------------------");
    lines << QStringLiteral("[程序]");
    lines << QStringLiteral("版本: %1").arg(orUnknown(input.appVersion));
    lines << QStringLiteral("程序目录: %1")
                 .arg(orUnknown(QDir::toNativeSeparators(input.appDir)));
    lines << QString();
    lines << QStringLiteral("[运行环境]");
    lines << QStringLiteral("操作系统: %1").arg(orUnknown(input.osVersion));
    lines << QStringLiteral("CPU 架构: %1").arg(orUnknown(input.cpuArchitecture));
    lines << QStringLiteral("Qt: %1").arg(orUnknown(input.qtVersion));
    lines << QStringLiteral("HALCON: %1").arg(orUnknown(input.halconVersion));
    lines << QString();
    lines << QStringLiteral("[资源]");
    lines << QStringLiteral("CPU 占用: %1").arg(percentText(input.cpuPercent));
    lines << QStringLiteral("内存占用: %1").arg(percentText(input.memoryPercent));
    lines << QStringLiteral("方案卷占用: %1").arg(percentText(input.diskPercent));
    lines << QString();
    lines << QStringLiteral("[当前方案]");
    lines << QStringLiteral("路径: %1")
                 .arg(input.schemePath.trimmed().isEmpty()
                          ? QStringLiteral("未命名方案（尚未保存）")
                          : QDir::toNativeSeparators(input.schemePath));
    lines << QString();
    lines << QStringLiteral("[运行日志（最近记录，可能被截断）]");
    lines << (input.logTail.isEmpty() ? QStringLiteral("（无日志）") : input.logTail);
    lines << QString();
    lines << QStringLiteral("说明: 本报告仅含文本信息，不包含图像、模型或方案内容。");
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

bool DiagnosticsCollector::writeReport(const QString &filePath, const QString &report,
                                       QString *error)
{
    if (filePath.trimmed().isEmpty()) {
        setError(error, QStringLiteral("保存路径为空"));
        return false;
    }
    if (report.isEmpty()) {
        setError(error, QStringLiteral("报告内容为空"));
        return false;
    }

    QString path = filePath;
    QFileInfo info(path);
    if (info.suffix().isEmpty()) {
        path += QStringLiteral(".txt");
        info.setFile(path);
    }
    if (!info.dir().exists()) {
        setError(error, QStringLiteral("目标目录不存在：%1")
                             .arg(QDir::toNativeSeparators(info.absolutePath())));
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        setError(error, QStringLiteral("无法写入文件：%1（%2）")
                             .arg(QDir::toNativeSeparators(path), file.errorString()));
        return false;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << report;
    stream.flush();
    const bool ok = (stream.status() == QTextStream::Ok);
    file.close();
    if (!ok) {
        setError(error, QStringLiteral("写入文件失败：%1")
                             .arg(QDir::toNativeSeparators(path)));
        return false;
    }
    return true;
}
