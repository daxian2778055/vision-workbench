#include "FeatureIndex.h"

#include "AppDatabase.h"

#include <opencv2/features2d.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <QBuffer>

// ---------------- FeatureSample 序列化 ----------------

cv::Mat FeatureSample::descriptorMat() const
{
    if (descRows <= 0 || descCols <= 0 || descriptors.isEmpty())
        return cv::Mat();
    cv::Mat m(descRows, descCols, descType,
              const_cast<char *>(descriptors.constData()));
    return m.clone();
}

QVector<cv::KeyPoint> FeatureSample::keyPoints() const
{
    QVector<cv::KeyPoint> out;
    const int n = keypoints.size() / int(sizeof(float) * 4);
    const float *p = reinterpret_cast<const float *>(keypoints.constData());
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        cv::KeyPoint kp;
        kp.pt.x = p[i * 4 + 0];
        kp.pt.y = p[i * 4 + 1];
        kp.size = p[i * 4 + 2];
        kp.angle = p[i * 4 + 3];
        out.append(kp);
    }
    return out;
}

void FeatureSample::setFrom(const QString &name_, const QString &detector_,
                            const std::vector<cv::KeyPoint> &kps, const cv::Mat &desc,
                            const QByteArray &imagePng_)
{
    name = name_;
    detector = detector_;
    imagePng = imagePng_;
    descRows = desc.rows;
    descCols = desc.cols;
    descType = desc.type();
    // 描述子必须连续（非连续则复制）
    cv::Mat cont = desc.isContinuous() ? desc : desc.clone();
    descriptors = QByteArray(reinterpret_cast<const char *>(cont.data),
                             int(cont.total() * cont.elemSize()));
    QByteArray kpBytes;
    kpBytes.resize(int(kps.size() * sizeof(float) * 4));
    float *p = reinterpret_cast<float *>(kpBytes.data());
    for (size_t i = 0; i < kps.size(); ++i) {
        p[i * 4 + 0] = kps[i].pt.x;
        p[i * 4 + 1] = kps[i].pt.y;
        p[i * 4 + 2] = kps[i].size;
        p[i * 4 + 3] = kps[i].angle;
    }
    keypoints = kpBytes;
    createdAt = QDateTime::currentDateTimeUtc();
}

// ---------------- FeatureIndex ----------------

cv::Ptr<cv::Feature2D> FeatureIndex::createDetector(const QString &name)
{
    if (name.compare(QStringLiteral("SIFT"), Qt::CaseInsensitive) == 0)
        return cv::SIFT::create();
    return cv::ORB::create();   // 默认
}

bool FeatureIndex::extract(const QString &detector, const cv::Mat &bgrOrGray,
                           std::vector<cv::KeyPoint> &kps, cv::Mat &descriptors)
{
    if (bgrOrGray.empty())
        return false;
    cv::Mat gray;
    if (bgrOrGray.channels() == 1)
        gray = bgrOrGray;
    else
        cv::cvtColor(bgrOrGray, gray, cv::COLOR_BGR2GRAY);
    // ORB/SIFT 都要求 8 位单通道；16 位先线性压到 8 位（保留相对灰阶）
    if (gray.depth() != CV_8U) {
        double lo = 0, hi = 0;
        cv::minMaxLoc(gray, &lo, &hi);
        const double scale = (hi > lo) ? 255.0 / (hi - lo) : 1.0;
        gray.convertTo(gray, CV_8U, scale, -lo * scale);
    }
    auto det = createDetector(detector);
    if (det.empty())
        return false;
    det->detectAndCompute(gray, cv::noArray(), kps, descriptors);
    return !kps.empty() && !descriptors.empty();
}

QVector<FeatureMatchHit> FeatureIndex::search(const QString &detector,
                                              const cv::Mat &queryImage,
                                              int topK, double ratio,
                                              QStringList *errors,
                                              cv::Mat *annotatedOut)
{
    QVector<FeatureMatchHit> hits;
    std::vector<cv::KeyPoint> qkps;
    cv::Mat qdesc;
    if (!extract(detector, queryImage, qkps, qdesc)) {
        if (errors) *errors << QStringLiteral("查询图提取不到特征");
        return hits;
    }

    const QList<FeatureSample> lib = AppDatabase::instance()->loadFeatureSamples(detector);
    if (lib.isEmpty()) {
        if (errors) *errors << QStringLiteral("特征库为空（detector=%1）").arg(detector);
        return hits;
    }

    // 描述子类型必须一致才能比较（ORB=8U 汉明 / SIFT=32F L2）
    const bool binary = (qdesc.type() == CV_8U);
    const auto normType = binary ? cv::NORM_HAMMING : cv::NORM_L2;
    cv::BFMatcher matcher(normType);

    for (const FeatureSample &s : lib) {
        if (s.descType != qdesc.type() || s.descCols != qdesc.cols)
            continue;
        const cv::Mat libDesc = s.descriptorMat();
        if (libDesc.empty())
            continue;

        std::vector<std::vector<cv::DMatch>> knn;
        try {
            matcher.knnMatch(qdesc, libDesc, knn, 2);
        } catch (const cv::Exception &e) {
            if (errors) *errors << QString::fromUtf8(e.what());
            continue;
        }
        std::vector<cv::DMatch> good;
        for (const auto &pair : knn) {
            if (pair.size() >= 2 && pair[0].distance < ratio * pair[1].distance)
                good.push_back(pair[0]);
        }

        // 几何一致性：单应内点数（>=4 对才试）
        int inliers = 0;
        std::vector<cv::Point2f> inlierPts;
        if (good.size() >= 4) {
            const QVector<cv::KeyPoint> lkps = s.keyPoints();
            std::vector<cv::Point2f> qp, lp;
            for (const cv::DMatch &m : good) {
                qp.push_back(qkps[size_t(m.queryIdx)].pt);
                if (m.trainIdx < lkps.size())
                    lp.push_back(lkps[m.trainIdx].pt);
            }
            if (qp.size() == lp.size() && qp.size() >= 4) {
                cv::Mat mask;
                try {
                    cv::findHomography(qp, lp, cv::RANSAC, 3.0, mask);
                    inliers = mask.empty() ? 0 : cv::countNonZero(mask);
                    if (!mask.empty()) {
                        for (int i = 0; i < mask.rows; ++i)
                            if (mask.at<uchar>(i))
                                inlierPts.push_back(qp[size_t(i)]);
                    }
                } catch (const cv::Exception &) {
                    inliers = 0;
                }
            }
        }

        FeatureMatchHit h;
        h.sampleId = s.id;
        h.name = s.name;
        h.goodMatches = int(good.size());
        h.inliers = inliers;
        h.inlierPoints = QVector<cv::Point2f>(inlierPts.begin(), inlierPts.end());
        // 综合分：匹配数与内点数各占一半，各自按经验上限归一
        const double partMatch = qMin(1.0, double(good.size()) / 50.0);
        const double partInlier = qMin(1.0, double(inliers) / 30.0);
        h.score = 0.5 * partMatch + 0.5 * partInlier;
        hits.append(h);
    }

    std::sort(hits.begin(), hits.end(),
              [](const FeatureMatchHit &a, const FeatureMatchHit &b) {
                  return a.score > b.score;
              });
    if (hits.size() > topK)
        hits.resize(topK);

    // 可视化（2026-10-27）：把 Top1 的内点画在查询图上，现场据此调 ratio/topK
    if (annotatedOut && !hits.isEmpty()) {
        cv::Mat canvas;
        if (queryImage.channels() == 1)
            cv::cvtColor(queryImage, canvas, cv::COLOR_GRAY2BGR);
        else
            canvas = queryImage.clone();
        for (const cv::Point2f &p : hits.first().inlierPoints)
            cv::circle(canvas, p, 6, cv::Scalar(0, 255, 0), 2);
        const QString title = QStringLiteral("%1  score=%2  matches=%3 inliers=%4")
                                  .arg(hits.first().name)
                                  .arg(hits.first().score, 0, 'f', 3)
                                  .arg(hits.first().goodMatches)
                                  .arg(hits.first().inliers);
        cv::putText(canvas, title.toStdString(), cv::Point(8, 24),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 255), 2);
        *annotatedOut = canvas;
    }
    return hits;
}
