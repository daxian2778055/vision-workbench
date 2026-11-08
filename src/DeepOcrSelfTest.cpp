#include "DeepOcrSelfTest.h"

#include "DeepOcrNode.h"
#include "OpencvUtil.h"

#include <QDir>
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
    QDir dir(info.absoluteFilePath());
    QStringList names;
    for (const QString &f : imageFilters())
        names << dir.entryList(QStringList{f}, QDir::Files, QDir::Name);
    names.removeDuplicates();
    names.sort();
    for (const QString &n : names) {
        if (out.size() >= maxCount)
            break;
        out << dir.absoluteFilePath(n);
    }
    return out;
}

ImageResult runOne(const QString &imagePath, int mode, double minConfidence)
{
    ImageResult r;
    r.path = imagePath;

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
    for (const ImageResult &r : results) {
        if (r.ok)
            ++okCount;
        totalMs += r.elapsedMs;
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
            lines << QStringLiteral("      提示: %1 判成功但未读出任何词——若用的是 auto/detection "
                                    "模式，请改 --mode 2（recognition）再试")
                         .arg(QFileInfo(r.path).fileName());
            break;
        }
    }
    lines << QStringLiteral("==== 汇总: %1/%2 成功，总耗时 %3 ms ====")
                 .arg(okCount)
                 .arg(results.size())
                 .arg(totalMs);
    return lines.join(QLatin1Char('\n'));
}

QString formatCsv(const QVector<ImageResult> &results)
{
    QStringList lines;
    lines << QStringLiteral("file,ok,words,avg_confidence,max_confidence,elapsed_ms,text,error");
    for (const ImageResult &r : results) {
        QString text = r.text;
        text.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        QString err = r.error;
        err.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        lines << QStringLiteral("\"%1\",%2,%3,%4,%5,%6,\"%7\",\"%8\"")
                     .arg(QFileInfo(r.path).fileName())
                     .arg(r.ok ? 1 : 0)
                     .arg(r.wordCount)
                     .arg(r.avgConfidence, 0, 'f', 4)
                     .arg(r.maxConfidence, 0, 'f', 4)
                     .arg(r.elapsedMs)
                     .arg(text, err);
    }
    return lines.join(QLatin1Char('\n'));
}

}   // namespace DeepOcrSelfTest
