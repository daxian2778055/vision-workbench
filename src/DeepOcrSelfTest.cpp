#include "DeepOcrSelfTest.h"

#include "DeepOcrNode.h"
#include "OpencvUtil.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTextStream>

#include <opencv2/imgcodecs.hpp>

namespace DeepOcrSelfTest {

namespace {
const QStringList &imageFilters()
{
    static const QStringList filters = {
        QStringLiteral("*.png"),  QStringLiteral("*.jpg"),  QStringLiteral("*.jpeg"),
        QStringLiteral("*.bmp"),  QStringLiteral("*.tif"),  QStringLiteral("*.tiff")
    };
    return filters;
}
}   // namespace

QStringList collectImages(const QString &path, int maxCount)
{
    QStringList out;
    const QFileInfo info(path);
    if (info.isFile()) {
        out << info.absoluteFilePath();
        return out;
    }
    if (!info.isDir())
        return out;
    // 递归子目录：现场样本常按批次/日期分层存放，只扫一层会漏掉大部分图片。
    QDirIterator it(info.absoluteFilePath(), imageFilters(), QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext() && out.size() < maxCount)
        out << it.next();
    out.sort();   // 全路径排序，保证多次运行顺序稳定
    return out;
}

ImageResult runOne(const QString &imagePath, int mode, double minConfidence)
{
    ImageResult r;
    r.path = imagePath;
    r.mode = mode;

    // 按原图读取（不强制灰度）：DeepOCR 支持彩色输入，强制转灰度会丢信息
    const cv::Mat mat = cv::imread(imagePath.toStdString(), cv::IMREAD_COLOR);
    if (mat.empty()) {
        r.error = QStringLiteral("图片无法读取（路径或格式不支持）");
        return r;
    }

    DeepOcrNode node;
    node.init();
    node.setParam(QStringLiteral("mode"), mode);
    node.setParam(QStringLiteral("minConfidence"), minConfidence);
    node.setInputImage(OpencvUtil::matToHimage(mat));

    QElapsedTimer timer;
    timer.start();
    node.run(false);
    r.elapsedMs = timer.elapsed();

    r.ok = node.getParam(QStringLiteral("moduleStatus")).toBool();
    r.text = node.getParam(QStringLiteral("ocrText")).toString();
    r.wordCount = node.getParam(QStringLiteral("ocrCount")).toInt();
    r.avgConfidence = node.getParam(QStringLiteral("ocrAvgConfidence")).toDouble();
    r.maxConfidence = node.getParam(QStringLiteral("ocrConfidence")).toDouble();
    if (!r.ok) {
        r.error = node.getParam(QStringLiteral("lastError")).toString();
        if (r.error.isEmpty())
            r.error = QStringLiteral("节点判失败但未给出原因");
    }
    return r;
}

QVector<ImageResult> runAll(const QStringList &images, int mode, double minConfidence)
{
    QVector<ImageResult> results;
    results.reserve(images.size());
    for (const QString &p : images)
        results.append(runOne(p, mode, minConfidence));
    return results;
}

QString formatReport(const QVector<ImageResult> &results)
{
    QStringList lines;
    int okCount = 0;
    qint64 totalMs = 0;
    qint64 maxMs = 0;
    double confSum = 0.0;
    int confN = 0;
    int totalWords = 0;
    for (const ImageResult &r : results) {
        if (r.ok) {
            ++okCount;
            totalWords += r.wordCount;
            if (r.avgConfidence > 0.0) {
                confSum += r.avgConfidence;
                ++confN;
            }
        }
        totalMs += r.elapsedMs;
        maxMs = qMax(maxMs, r.elapsedMs);
        const QString name = QFileInfo(r.path).fileName();
        if (r.ok) {
            lines << QStringLiteral("[OK]   %1  words=%2  avg=%3  max=%4  %5 ms  \"%6\"")
                         .arg(name)
                         .arg(r.wordCount)
                         .arg(r.avgConfidence, 0, 'f', 3)
                         .arg(r.maxConfidence, 0, 'f', 3)
                         .arg(r.elapsedMs)
                         .arg(r.text);
        } else {
            lines << QStringLiteral("[FAIL] %1  %2").arg(name, r.error);
        }
    }
    // 实测：auto/detection 模式只返回检测结果、不含 word 数组 ⇒ 节点判成功但文本为空。
    // 这不是"OCR 坏了"，但用户看到空文本会当成坏了，所以这里显式提示。
    for (const ImageResult &r : results) {
        if (r.ok && r.wordCount == 0) {
            if (r.mode == 2) {
                // 复核 P1-6 残留：旧版在已经是 recognition 时仍建议「改 --mode 2」，误导。
                lines << QStringLiteral("      提示: %1 在 recognition 模式下仍读出 0 个词——"
                                        "请检查图片是否含文字、--min-conf 阈值与 dl/ 模型")
                             .arg(QFileInfo(r.path).fileName());
            } else {
                lines << QStringLiteral("      提示: %1 判成功但未读出任何词——若用的是 auto/detection "
                                        "模式，请改 --mode 2（recognition）再试")
                             .arg(QFileInfo(r.path).fileName());
            }
            break;
        }
    }
    const int failCount = results.size() - okCount;
    const double avgMs = results.isEmpty() ? 0.0 : double(totalMs) / results.size();
    const double avgConf = confN > 0 ? confSum / confN : 0.0;
    lines << QStringLiteral("==== 汇总: %1/%2 成功 (%3 失败)，总耗时 %4 ms，平均 %5 ms，"
                            "最慢 %6 ms，词数 %7，平均置信度 %8 ====")
                 .arg(okCount)
                 .arg(results.size())
                 .arg(failCount)
                 .arg(totalMs)
                 .arg(avgMs, 0, 'f', 1)
                 .arg(maxMs)
                 .arg(totalWords)
                 .arg(avgConf, 0, 'f', 3);
    // 失败明细单独列，避免几十行日志里翻找
    if (failCount > 0) {
        lines << QStringLiteral("---- 失败明细 (%1) ----").arg(failCount);
        for (const ImageResult &r : results) {
            if (!r.ok)
                lines << QStringLiteral("  %1  %2")
                             .arg(r.relativePath.isEmpty() ? QFileInfo(r.path).fileName()
                                                           : r.relativePath,
                                  r.error);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

QString formatCsv(const QVector<ImageResult> &results)
{
    QStringList lines;
    // mode 追加在末尾：保持既有 CSV 表头前缀不变（下游解析不破）。
    lines << QStringLiteral("file,ok,words,avg_confidence,max_confidence,elapsed_ms,text,error,mode");
    for (const ImageResult &r : results) {
        QString text = r.text;
        text.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        QString err = r.error;
        err.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        // 递归后不同子目录可能有同名文件，优先写相对路径
        const QString displayPath =
            r.relativePath.isEmpty() ? QFileInfo(r.path).fileName() : r.relativePath;
        lines << QStringLiteral("\"%1\",%2,%3,%4,%5,%6,\"%7\",\"%8\",%9")
                     .arg(displayPath)
                     .arg(r.ok ? 1 : 0)
                     .arg(r.wordCount)
                     .arg(r.avgConfidence, 0, 'f', 4)
                     .arg(r.maxConfidence, 0, 'f', 4)
                     .arg(r.elapsedMs)
                     .arg(text, err)
                     .arg(r.mode);
    }
    return lines.join(QLatin1Char('\n'));
}

}   // namespace DeepOcrSelfTest
