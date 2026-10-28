// 训练评估（2026-10-27）：evaluateFromDir 的正确性与可复现性腿。
//   ① 同 seed 两次评估结果必须一致（可复现是"训练前后对比"的前提）；
//   ② 可分小数据集上准确率应当高（证明训练+特征+评估链路通）；
//   ③ 空/单类数据集给可读错误。
#include "ClassifierTrainer.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <opencv2/imgcodecs.hpp>

class TrainingEvalTest : public QObject
{
    Q_OBJECT
private slots:
    void trainAndEvaluateOnSeparableData();
    void evaluateIsReproducible();
    void badDatasetReportsError();
    void augmentIsDeterministic();
    void augmentationExpandsDataset();
    void holdoutExcludesValidationFromTraining();
    void augmentationNoiseIsZeroMean();
    void evalUsesTrainedRatioNotSlider();

private:
    static cv::Mat makeSample(int cls, int seed);
    static void buildDataset(const QString &root, int perClass);
};

cv::Mat TrainingEvalTest::makeSample(int cls, int seed)
{
    // 类别 0：亮背景 + 暗方块；类别 1：暗背景 + 亮圆。可分但有随机纹理。
    cv::Mat img(64, 64, CV_8UC1, cv::Scalar(cls == 0 ? 210 : 40));
    unsigned s = unsigned(seed) * 2654435761u + 7u;
    for (int y = 0; y < img.rows; ++y)
        for (int x = 0; x < img.cols; ++x) {
            s = s * 1664525u + 1013904223u;
            img.at<uchar>(y, x) = cv::saturate_cast<uchar>(
                int(img.at<uchar>(y, x)) + int((s >> 16) % 21) - 10);
        }
    if (cls == 0)
        cv::rectangle(img, cv::Rect(20, 20, 24, 24), cv::Scalar(30), -1);
    else
        cv::circle(img, cv::Point(32, 32), 16, cv::Scalar(240), -1);
    return img;
}

void TrainingEvalTest::buildDataset(const QString &root, int perClass)
{
    for (int cls = 0; cls < 2; ++cls) {
        const QString d = QDir(root).filePath(QStringLiteral("c%1").arg(cls));
        QDir().mkpath(d);
        for (int i = 0; i < perClass; ++i) {
            const cv::Mat img = makeSample(cls, i);
            cv::imwrite(QDir(d).filePath(QStringLiteral("%1.png").arg(i, 3, 10, QLatin1Char('0')))
                            .toStdString(),
                        img);
        }
    }
}

void TrainingEvalTest::trainAndEvaluateOnSeparableData()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    buildDataset(dir.path(), 12);

    const std::string model = QDir(dir.path()).filePath(QStringLiteral("m.yaml")).toStdString();
    const ClsTrainer::TrainStats st = ClsTrainer::trainFromDir(
        dir.path().toStdString(), model, /*featureMode=*/0, /*hidden=*/32, /*iters=*/300);
    QVERIFY2(st.ok, qPrintable(QString::fromStdString(st.error)));

    const ClsTrainer::EvalResult ev = ClsTrainer::evaluateFromDir(
        model, dir.path().toStdString(), 0, 0.3, 42);
    QVERIFY2(ev.ok, qPrintable(QString::fromStdString(ev.error)));
    QVERIFY2(ev.total > 0, "验证集为空");
    // 可分数据上应当显著高于瞎猜（0.5）
    QVERIFY2(ev.accuracy >= 0.8,
             qPrintable(QStringLiteral("可分数据准确率过低：%1%").arg(ev.accuracy * 100.0, 0, 'f', 1)));
    // 混淆矩阵尺寸与类别数一致
    QCOMPARE(int(ev.confusion.size()), 2);
    QCOMPARE(int(ev.confusion[0].size()), 2);
}

void TrainingEvalTest::evaluateIsReproducible()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    buildDataset(dir.path(), 10);
    const std::string model = QDir(dir.path()).filePath(QStringLiteral("m.yaml")).toStdString();
    QVERIFY(ClsTrainer::trainFromDir(dir.path().toStdString(), model, 1, 16, 200).ok);

    const auto a = ClsTrainer::evaluateFromDir(model, dir.path().toStdString(), 1, 0.3, 42);
    const auto b = ClsTrainer::evaluateFromDir(model, dir.path().toStdString(), 1, 0.3, 42);
    QVERIFY(a.ok && b.ok);
    QCOMPARE(a.total, b.total);
    QCOMPARE(a.correct, b.correct);
    QVERIFY2(a.confusion == b.confusion, "同 seed 两次混淆矩阵不一致：评估不可复现");
}

void TrainingEvalTest::badDatasetReportsError()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // 只有一个类别
    buildDataset(dir.path(), 4);
    QDir(dir.filePath(QStringLiteral("c1"))).removeRecursively();

    const std::string model = QDir(dir.path()).filePath(QStringLiteral("m.yaml")).toStdString();
    const ClsTrainer::TrainStats st = ClsTrainer::trainFromDir(dir.path().toStdString(), model, 0, 16, 100);
    QVERIFY2(!st.ok, "单类别却训练成功");
    QVERIFY2(!st.error.empty(), "单类别失败却没有可读原因");

    // 不存在的模型
    const ClsTrainer::EvalResult ev = ClsTrainer::evaluateFromDir(
        QDir(dir.path()).filePath(QStringLiteral("nope.yaml")).toStdString(),
        dir.path().toStdString(), 0, 0.3, 42);
    QVERIFY2(!ev.ok, "不存在的模型却评估成功");
    QVERIFY2(!ev.error.empty(), "模型加载失败没有可读原因");
}

void TrainingEvalTest::augmentIsDeterministic()
{
    const cv::Mat src = makeSample(0, 3);
    ClsTrainer::AugmentOptions aug;
    aug.enabled = true;
    aug.factor = 3;
    aug.seed = 7;

    const cv::Mat a = ClsTrainer::augmentSample(src, 1, aug, 100);
    const cv::Mat b = ClsTrainer::augmentSample(src, 1, aug, 100);
    QVERIFY2(a.size() == b.size(), "增广尺寸不稳定");
    QVERIFY2(cv::countNonZero(a != b) == 0, "同 seed 增广结果不一致：评估可复现性会被破坏");

    const cv::Mat c = ClsTrainer::augmentSample(src, 2, aug, 100);
    // 不同变体应当有差异（否则增广没起作用）
    QVERIFY2(cv::countNonZero(a != c) > 0, "不同变体完全相同：增广没有起作用");
}

void TrainingEvalTest::augmentationExpandsDataset()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    buildDataset(dir.path(), 6);   // 2 类 × 6 = 12 张

    ClsTrainer::AugmentOptions aug;
    aug.enabled = true;
    aug.factor = 3;               // 扩成 3 倍 ≈ 36
    aug.seed = 42;

    const std::string model = QDir(dir.path()).filePath(QStringLiteral("m.yaml")).toStdString();
    const ClsTrainer::TrainStats st = ClsTrainer::trainFromDir(
        dir.path().toStdString(), model, 0, 24, 250, aug);
    QVERIFY2(st.ok, qPrintable(QString::fromStdString(st.error)));
    QVERIFY2(st.samples >= 30,
             qPrintable(QStringLiteral("增广未扩样：samples=%1（应接近 36）").arg(st.samples)));

    // 增广训练后评估仍应可分
    const auto ev = ClsTrainer::evaluateFromDir(model, dir.path().toStdString(), 0, 0.3, 42);
    QVERIFY2(ev.ok, qPrintable(QString::fromStdString(ev.error)));
    QVERIFY2(ev.accuracy >= 0.8,
             qPrintable(QStringLiteral("增广后准确率过低：%1%").arg(ev.accuracy * 100.0, 0, 'f', 1)));
}

void TrainingEvalTest::holdoutExcludesValidationFromTraining()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    buildDataset(dir.path(), 10);   // 20 张

    const std::string model = QDir(dir.path()).filePath(QStringLiteral("m.yaml")).toStdString();
    // 留出 30% → 训练只应见到 ~14 张（20 - 6）
    const ClsTrainer::TrainStats st = ClsTrainer::trainFromDir(
        dir.path().toStdString(), model, 0, 16, 150, ClsTrainer::AugmentOptions(), 0.3, 42);
    QVERIFY2(st.ok, qPrintable(QString::fromStdString(st.error)));
    QVERIFY2(st.samples <= 16,
             qPrintable(QStringLiteral("验证样本参与了训练：samples=%1（20 张留 30%% 应 ≤16）")
                            .arg(st.samples)));
}

void TrainingEvalTest::augmentationNoiseIsZeroMean()
{
    // 零均值噪声（2026-10-27 P3-1）：单边钳位会把负值压成 0，训练样本被系统性提亮
    const cv::Mat src(128, 128, CV_8UC1, cv::Scalar(128));
    ClsTrainer::AugmentOptions aug;
    aug.enabled = true;
    aug.rotateDeg = 0.0;      // 只留噪声
    aug.brightness = 0.0;
    aug.horizontalFlip = false;
    aug.noiseSigma = 10.0;
    // 多个变体取平均，抵消单次随机
    double sum = 0.0;
    const int N = 8;
    for (int k = 1; k <= N; ++k) {
        const cv::Mat a = ClsTrainer::augmentSample(src, k, aug, 1000 + k);
        sum += cv::mean(a)[0];
    }
    const double mean = sum / N;
    QVERIFY2(qAbs(mean - 128.0) < 1.5,
             qPrintable(QStringLiteral("噪声不是零均值：均值 %1（应≈128）").arg(mean, 0, 'f', 2)));
}
void TrainingEvalTest::evalUsesTrainedRatioNotSlider()
{
    // P2（2026-10-28）：评估必须用**训练时**的验证划分。用户训练用 0.3、评估前把滑块拖到 0.5，
    // 原实现会用 0.5 重新划分 → 部分训练样本被算进"留出验证集"，摘要却仍写"未参与训练"。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    buildDataset(dir.path(), 10);
    const std::string model = QDir(dir.path()).filePath(QStringLiteral("m.yaml")).toStdString();

    QVERIFY(ClsTrainer::trainFromDir(dir.path().toStdString(), model, 0, 16, 150,
                                     ClsTrainer::AugmentOptions(), 0.3, 42).ok);
    QVERIFY2(QFile::exists(QString::fromStdString(ClsTrainer::modelMetaPath(model))),
             "训练没有写出模型元数据");

    // 故意传 0.5：应被 meta 里的 0.3 覆盖
    const auto ev = ClsTrainer::evaluateFromDir(model, dir.path().toStdString(), 0, 0.5, 42);
    QVERIFY2(ev.ok, qPrintable(QString::fromStdString(ev.error)));
    QVERIFY2(qAbs(ev.usedValidationRatio - 0.3) < 1e-6,
             qPrintable(QStringLiteral("评估用了外部比例 %1，应改用训练的 0.3")
                            .arg(ev.usedValidationRatio)));
}

QTEST_MAIN(TrainingEvalTest)
#include "training_eval_test.moc"
