// 特征检索（2026-10-27）：图库持久化与"以图查图"的正确性腿。
//   ① 入库→检索，自己必排第一（排序正确性）；
//   ② 序列化往返：存进去的描述子取出来逐字节一致（持久化不失真）；
//   ③ 不同图的相似度必须低于自己（区分度）；
//   ④ 空库/无命中给出可读错误（与"识别不到内容判红"同口径）。
#include "FeatureIndex.h"
#include "AppDatabase.h"

#include <opencv2/imgproc.hpp>
#include <QTemporaryDir>
#include <QtTest/QtTest>

class FeatureIndexTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void persistenceRoundTrip();
    void searchRanksSelfFirst();
    void emptyLibraryReportsError();
    void deleteAndClearWork();
    void thumbnailRoundTrip();
    void searchAnnotatesTop1();

private:
    static cv::Mat pattern(int seed, int w = 320, int h = 240);
};


cv::Mat FeatureIndexTest::pattern(int seed, int w, int h)
{
    // 用可复现的伪随机纹理：不同 seed → 不同图，同 seed → 完全相同
    cv::Mat img(h, w, CV_8UC1);
    unsigned s = unsigned(seed) * 2654435761u + 12345u;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            s = s * 1664525u + 1013904223u;
            img.at<uchar>(y, x) = uchar((s >> 16) & 0xFF);
        }
    // 再画几个几何块，保证有稳定的角点
    cv::rectangle(img, cv::Rect(30 + seed * 5, 40, 90, 60), cv::Scalar(255), 2);
    cv::circle(img, cv::Point(200, 120 + seed * 3), 45, cv::Scalar(255), 3);
    cv::line(img, cv::Point(10, 10), cv::Point(300, 200), cv::Scalar(128), 2);
    return img;
}

void FeatureIndexTest::initTestCase()
{
    static QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY2(AppDatabase::instance()->initialize(dir.filePath(QStringLiteral("test.db"))),
             "测试库初始化失败");
}

void FeatureIndexTest::cleanupTestCase()
{
    AppDatabase::instance()->initialize(QString());
}

void FeatureIndexTest::persistenceRoundTrip()
{
    std::vector<cv::KeyPoint> kps;
    cv::Mat desc;
    QVERIFY(FeatureIndex::extract(QStringLiteral("ORB"), pattern(1), kps, desc));
    QVERIFY(!desc.empty());

    FeatureSample s;
    s.setFrom(QStringLiteral("alpha"), QStringLiteral("ORB"), kps, desc, QByteArray());
    QVERIFY(s.isValid());
    QVERIFY2(AppDatabase::instance()->saveFeatureSample(s), "入库失败");

    const auto lib = AppDatabase::instance()->loadFeatureSamples(QStringLiteral("ORB"));
    QVERIFY2(!lib.isEmpty(), "库里取不到样本");
    const FeatureSample &back = lib.last();
    QCOMPARE(back.name, QStringLiteral("alpha"));
    QCOMPARE(back.descRows, s.descRows);
    QCOMPARE(back.descCols, s.descCols);
    QCOMPARE(back.descType, s.descType);
    QVERIFY2(back.descriptors == s.descriptors, "描述子往返不一致");
    const QVector<cv::KeyPoint> bk = back.keyPoints();
    QCOMPARE(bk.size(), int(kps.size()));
    if (!kps.empty()) {
        QVERIFY(qAbs(bk.first().pt.x - kps[0].pt.x) < 1e-3);
        QVERIFY(qAbs(bk.first().pt.y - kps[0].pt.y) < 1e-3);
    }
}

void FeatureIndexTest::searchRanksSelfFirst()
{
    // 三张互不相同的图入库
    for (int seed = 1; seed <= 3; ++seed) {
        std::vector<cv::KeyPoint> kps;
        cv::Mat desc;
        QVERIFY(FeatureIndex::extract(QStringLiteral("ORB"), pattern(seed), kps, desc));
        FeatureSample s;
        s.setFrom(QStringLiteral("img%1").arg(seed), QStringLiteral("ORB"), kps, desc, QByteArray());
        QVERIFY(AppDatabase::instance()->saveFeatureSample(s));
    }

    QStringList errs;
    const auto hits = FeatureIndex::search(QStringLiteral("ORB"), pattern(2), 3, 0.75, &errs);
    QVERIFY2(!hits.isEmpty(), qPrintable(errs.join(QStringLiteral("；"))));
    QCOMPARE(hits.first().name, QStringLiteral("img2"));   // 自己必须第一
    QVERIFY(hits.first().score >= hits.last().score);      // 降序

    // 不同图的相似度应低于自己（区分度）
    double selfScore = 0, otherScore = 0;
    for (const auto &h : hits) {
        if (h.name == QStringLiteral("img2")) selfScore = h.score;
        if (h.name == QStringLiteral("img3")) otherScore = h.score;
    }
    QVERIFY2(selfScore > otherScore,
             qPrintable(QStringLiteral("区分度不足：自己 %1 不高于他者 %2").arg(selfScore).arg(otherScore)));
}

void FeatureIndexTest::emptyLibraryReportsError()
{
    QVERIFY(AppDatabase::instance()->clearFeatureSamples());
    QStringList errs;
    const auto hits = FeatureIndex::search(QStringLiteral("ORB"), pattern(7), 5, 0.75, &errs);
    QVERIFY2(hits.isEmpty(), "空库却有命中");
    QVERIFY2(!errs.isEmpty(), "空库必须给出可读错误");
}

void FeatureIndexTest::deleteAndClearWork()
{
    std::vector<cv::KeyPoint> kps;
    cv::Mat desc;
    QVERIFY(FeatureIndex::extract(QStringLiteral("ORB"), pattern(5), kps, desc));
    FeatureSample s;
    s.setFrom(QStringLiteral("toDelete"), QStringLiteral("ORB"), kps, desc, QByteArray());
    QVERIFY(AppDatabase::instance()->saveFeatureSample(s));
    QCOMPARE(AppDatabase::instance()->loadFeatureSamples(QStringLiteral("ORB")).size(), 1);

    QVERIFY(AppDatabase::instance()->deleteFeatureSample(AppDatabase::instance()
                                                            ->loadFeatureSamples(QStringLiteral("ORB"))
                                                            .first().id));
    QVERIFY(AppDatabase::instance()->loadFeatureSamples(QStringLiteral("ORB")).isEmpty());
}

void FeatureIndexTest::thumbnailRoundTrip()
{
    std::vector<cv::KeyPoint> kps;
    cv::Mat desc;
    QVERIFY(FeatureIndex::extract(QStringLiteral("ORB"), pattern(9), kps, desc));
    const QByteArray png = QByteArray::fromHex("89504e470d0a1a0a");   // PNG 头即可（不校验图像）
    FeatureSample s;
    s.setFrom(QStringLiteral("withThumb"), QStringLiteral("ORB"), kps, desc, png);
    QVERIFY(AppDatabase::instance()->saveFeatureSample(s));
    const auto lib = AppDatabase::instance()->loadFeatureSamples(QStringLiteral("ORB"));
    QVERIFY(!lib.isEmpty());
    QVERIFY2(lib.last().imagePng == png, "缩略图往返不一致");
    QVERIFY(AppDatabase::instance()->clearFeatureSamples());
}

void FeatureIndexTest::searchAnnotatesTop1()
{
    for (int seed = 11; seed <= 12; ++seed) {
        std::vector<cv::KeyPoint> kps;
        cv::Mat desc;
        QVERIFY(FeatureIndex::extract(QStringLiteral("ORB"), pattern(seed), kps, desc));
        FeatureSample s;
        s.setFrom(QStringLiteral("viz%1").arg(seed), QStringLiteral("ORB"), kps, desc, QByteArray());
        QVERIFY(AppDatabase::instance()->saveFeatureSample(s));
    }
    const cv::Mat q = pattern(11);
    cv::Mat annotated;
    QStringList errs;
    const auto hits = FeatureIndex::search(QStringLiteral("ORB"), q, 2, 0.75, &errs, &annotated);
    QVERIFY2(!hits.isEmpty(), qPrintable(errs.join(QStringLiteral("；"))));
    QVERIFY2(!annotated.empty(), "命中却没有可视化输出");
    QCOMPARE(annotated.rows, q.rows);
    QCOMPARE(annotated.cols, q.cols);
    QVERIFY(AppDatabase::instance()->clearFeatureSamples());
}

QTEST_MAIN(FeatureIndexTest)
#include "feature_index_test.moc"
