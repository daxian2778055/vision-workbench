// 训练评估（2026-10-27）：evaluateFromDir 的正确性与可复现性腿。
//   ① 同 seed 两次评估结果必须一致（可复现是"训练前后对比"的前提）；
//   ② 可分小数据集上准确率应当高（证明训练+特征+评估链路通）；
//   ③ 空/单类数据集给可读错误。
#include "ClassifierTrainer.h"

#include <QDir>
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

QTEST_MAIN(TrainingEvalTest)
#include "training_eval_test.moc"
