#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QByteArray>
#include <QDateTime>

#include <opencv2/core.hpp>
#include <opencv2/features2d.hpp>

/// 特征库样本（2026-10-27）：一张图 + 它的关键点与描述子，用于"以图查图/少样本检索"。
/// 序列化形状：keypoints 为 N*4 float32（x,y,size,angle），descriptors 为 N*D 连续字节，
/// 与 descRows/descCols/descType 一起存进 SQLite（AppDatabase 独占出入口）。
struct FeatureSample {
    int id = -1;
    QString name;                 // 样本名（检索结果里显示）
    QString detector;             // ORB / SIFT
    QByteArray imagePng;          // 可选缩略图（PNG 字节），供界面预览
    QByteArray keypoints;         // N*4 float32
    QByteArray descriptors;       // N*D 连续字节
    int descRows = 0;
    int descCols = 0;
    int descType = 0;             // CV_8U / CV_32F ...
    QDateTime createdAt;

    /// 形状校验（2026-10-27 P2-4）：DB 行可能被截断/半写/旧格式，直接按 rows/cols/type
    /// 把 blob 包成 cv::Mat 会让 Mat 容量小于声明尺寸 → BFMatcher 越界读。
    /// 三件事必须同时成立：描述子字节数 == rows*cols*elemSize、关键点字节数是 16 的倍数、
    /// 关键点数 == 行数。
    bool isValid() const
    {
        if (descRows <= 0 || descCols <= 0 || descType < 0)
            return false;
        const size_t elem = (descType == CV_8U) ? 1
                            : (descType == CV_32F) ? 4
                            : (descType == CV_64F) ? 8 : 0;
        if (elem == 0)
            return false;
        const size_t want = size_t(descRows) * size_t(descCols) * elem;
        if (size_t(descriptors.size()) != want)
            return false;
        if (keypoints.size() % 16 != 0)   // 每点 4×float32
            return false;
        return size_t(keypoints.size() / 16) == size_t(descRows);
    }

    /// 反序列化成 OpenCV 结构（失败返回空）
    cv::Mat descriptorMat() const;
    QVector<cv::KeyPoint> keyPoints() const;
    /// 由 OpenCV 结构填充序列化字段
    void setFrom(const QString &name_, const QString &detector_,
                 const std::vector<cv::KeyPoint> &kps, const cv::Mat &desc,
                 const QByteArray &imagePng_);
};

/// 检索命中
struct FeatureMatchHit {
    int sampleId = -1;
    QString name;
    int goodMatches = 0;      // 比值检验后的匹配数
    int inliers = 0;          // 几何一致性内点数
    double score = 0.0;       // 综合分（0..1，越大越像）
    QVector<cv::Point2f> inlierPoints;   // 查询图上的内点坐标（可视化用）
};

/// 特征检索（2026-10-27）：图库索引 + 以图查图。
/// 为什么值得做：VM 4.4 有"特征检索"能力，我们此前只能逐个算子跑模板；
/// 少样本/换型场景下"库里找最像的"比"重新训"快得多。
class FeatureIndex
{
public:
    /// 检测器：ORB（默认，快且无专利）/ SIFT（更稳，慢）
    static cv::Ptr<cv::Feature2D> createDetector(const QString &name);

    /// 对一张图提取关键点+描述子（灰度化内部做；失败返回 false）
    static bool extract(const QString &detector, const cv::Mat &bgrOrGray,
                        std::vector<cv::KeyPoint> &kps, cv::Mat &descriptors);

    /// 检索：在库里找与 query 最像的 topK 个样本。
    /// 打分 = 比值检验好匹配数 + 单应内点数（几何一致性），归一化到 0..1。
    /// annotatedOut 非空时画出 Top1 的匹配内点（现场调 ratio/topK 用，2026-10-27）
    static QVector<FeatureMatchHit> search(const QString &detector,
                                           const cv::Mat &queryImage,
                                           int topK, double ratio,
                                           QStringList *errors = nullptr,
                                           cv::Mat *annotatedOut = nullptr);
};
