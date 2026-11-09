#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

/// DeepOCR 现场自检（2026-11-08）：换机部署后确认「HALCON 运行时 + dl/ 预训练模型」
/// 真的可用，而不只是节点能被构造。逐张跑 DeepOcrNode，输出识别文本与置信度。
///
/// 用法：
///   VisionFlowPlatform.exe --selftest-deepocr <图片或目录> [--mode 0|1|2]
///                          [--min-conf 0.3] [--csv 结果.csv]
/// 退出码：0 = 全部成功；1 = 有失败；2 = 未找到图片。
namespace DeepOcrSelfTest {

struct ImageResult {
    QString path;
    /// 相对根目录的路径（递归子目录后 CSV 用它避免同名文件混淆；空则用 fileName）
    QString relativePath;
    /// 本次推理的模式（0=auto / 1=detection / 2=recognition）。报告要按它给提示，
    /// 否则在已经是 recognition 时还建议「改 --mode 2」会误导（复核 P1-6 残留）。
    int mode = -1;
    bool ok = false;
    QString text;
    int wordCount = 0;
    double avgConfidence = 0.0;
    double maxConfidence = 0.0;
    qint64 elapsedMs = 0;
    QString error;
};

/// path 是文件 → 单个；是目录 → **递归子目录**收集常见图片扩展名
/// （按全路径排序，最多 maxCount 张）。现场样本常按批次放子目录，
/// 旧实现只扫一层且注释写「递归」，属行为与文档不符（2026-11-09 复核）。
QStringList collectImages(const QString &path, int maxCount = 200);

/// 跑单张：mode 0=auto / 1=detection / 2=recognition（与节点参数一致）
ImageResult runOne(const QString &imagePath, int mode, double minConfidence);

QVector<ImageResult> runAll(const QStringList &images, int mode, double minConfidence);

/// 人类可读报告（含汇总行）；同时用于 stdout 与 --csv 之外的场景
QString formatReport(const QVector<ImageResult> &results);

/// CSV 行（UTF-8，带表头），供批量统计
QString formatCsv(const QVector<ImageResult> &results);

}   // namespace DeepOcrSelfTest
