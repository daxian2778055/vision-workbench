// 异常检测（G-P1-2 后端 A：OK 样本逐像素统计基线）功能测试
//
// 覆盖：Welford 累积正确性 + 热图定标 + 植入缺陷检出 + σ 下限（平坦区防爆分）
//       + 基线落盘/重载等价 + 尺寸不符不得静默 resize + 两类未见异常的检出 + ROI/掩膜作用域。
//
// 全部用合成图（真值已知）：μ/σ 的期望值可以直接算出来，用例才谈得上"钉住"而不是"看着像"。
#include <QtTest/QtTest>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QImage>

#include "AnomalyDetectNode.h"
#include "AnomalyBaseline.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <HalconCpp.h>

namespace {

cv::RNG g_rng(20260924);

/// 有结构的"工件"图：纯色图既测不出统计也测不出定位
cv::Mat makePart(int w = 128, int h = 128)
{
    cv::Mat img(h, w, CV_8UC1, cv::Scalar(40));
    cv::rectangle(img, cv::Rect(30, 30, 40, 40), cv::Scalar(200), cv::FILLED);
    cv::circle(img, cv::Point(90, 40), 12, cv::Scalar(120), cv::FILLED);
    cv::line(img, cv::Point(20, 100), cv::Point(108, 92), cv::Scalar(230), 3);
    cv::rectangle(img, cv::Rect(60, 80, 25, 20), cv::Scalar(90), cv::FILLED);
    return img;
}

/// 叠加高斯噪声（OK 样本之间的波动就是这个模型，σ 已知 ⇒ 断言有真值可比）
cv::Mat addNoise(const cv::Mat &src, double sigma)
{
    cv::Mat n(src.size(), CV_32FC1);
    g_rng.fill(n, cv::RNG::NORMAL, 0.0, sigma);
    cv::Mat out;
    src.convertTo(out, CV_32F);
    out += n;
    out.convertTo(out, CV_8U);   // 饱和截断
    return out;
}

cv::Mat plantRect(const cv::Mat &src, cv::Rect r, int delta)
{
    cv::Mat out = src.clone();
    cv::add(out(r), cv::Scalar(delta), out(r));   // 8U 加法自带饱和
    return out;
}

/// 整图平移（模拟工件在传送带上的位置抖动）
cv::Mat shiftImage(const cv::Mat &src, double dx, double dy)
{
    const cv::Mat M = (cv::Mat_<double>(2, 3) << 1.0, 0.0, dx, 0.0, 1.0, dy);
    cv::Mat out;
    cv::warpAffine(src, out, M, src.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    return out;
}

struct NodeRun {
    int count = 0;
    double area = 0.0;
    double scoreMax = 0.0;
    bool status = false;
    bool alignApplied = false;
    QString baselineStatus;
    QString trainStatus;
    QVector<double> boxes;
};

NodeRun runOnce(AnomalyDetectNode &node, const cv::Mat &img)
{
    node.setInputImage(OpencvUtil::matToHimage(img));
    node.run();
    NodeRun r;
    r.count = node.getParam(QStringLiteral("anomalyCount")).toInt();
    r.area = node.getParam(QStringLiteral("anomalyArea")).toDouble();
    r.scoreMax = node.getParam(QStringLiteral("scoreMax")).toDouble();
    r.status = node.getParam(QStringLiteral("moduleStatus")).toBool();
    r.alignApplied = node.getParam(QStringLiteral("alignApplied")).toBool();
    r.baselineStatus = node.getParam(QStringLiteral("baselineStatus")).toString();
    r.trainStatus = node.getParam(QStringLiteral("trainStatus")).toString();
    if (const auto obj = node.getOutputData(1))
        r.boxes = obj->getMeasureResult().extraValues;
    return r;
}

/// 累加教学 n 个样本（trainMode=2 ⇒ 连续跑 n 轮就完成建模，与现场用法一致）
void teach(AnomalyDetectNode &node, const cv::Mat &clean, int n, double sigma)
{
    node.setParam(QStringLiteral("trainMode"), 2);
    for (int i = 0; i < n; ++i) {
        const NodeRun r = runOnce(node, addNoise(clean, sigma));
        QVERIFY2(r.status, qPrintable(QStringLiteral("教学第 %1 轮失败").arg(i)));
    }
    node.setParam(QStringLiteral("trainMode"), 0);
}

void configure(AnomalyDetectNode &node, const QString &path)
{
    node.init();
    node.setParam(QStringLiteral("baselinePath"), path);
    node.setParam(QStringLiteral("overlayMode"), 2);   // 原图+框：本文件只测几何与判定，不测伪彩
}

}   // namespace

class AnomalyDetectTest : public QObject
{
    Q_OBJECT
private slots:
    void testWelfordStats();
    void testHeatScaling();
    void testPlantedDefect();
    void testSigmaFloorGuardsFlatRegions();
    void testSaveLoadEquivalence();
    void testSizeMismatchNeverResizes();
    void testUnseenAnomalyTypes();
    void testRoiAndMaskScope();
    void testAlignSuppressesShiftFalsePositives();
    void testPcaBackendCatchesWhatPixelBaselineMisses();
};

void AnomalyDetectTest::testWelfordStats()
{
    const cv::Mat clean = makePart(96, 96);
    AnomalyBaseline b;
    QVERIFY(b.isEmpty());
    QCOMPARE(b.sampleCount(), 0);

    const int n = 200;
    for (int i = 0; i < n; ++i)
        QVERIFY(b.addSample(addNoise(clean, 4.0)));
    QCOMPARE(b.sampleCount(), n);
    QVERIFY(!b.isEmpty());
    QCOMPARE(b.size(), clean.size());

    // 均值：n=200、σ=4 ⇒ 每像素标准误 ≈0.28，取 1.0 作上界（含 8U 饱和截断带来的偏差）
    cv::Mat mu = b.mean();
    cv::Mat truth;
    clean.convertTo(truth, CV_32F);
    cv::Mat err;
    cv::absdiff(mu, truth, err);
    QVERIFY2(cv::mean(err)[0] < 1.0,
             qPrintable(QStringLiteral("μ 偏差过大：%1").arg(cv::mean(err)[0])));

    // σ：n=200 的估计相对误差 ≈6%，取 1.0（绝对值，灰度单位）作上界
    const cv::Mat sg = b.sigma();
    double sum = 0.0;
    for (int r = 0; r < sg.rows; ++r)
        for (int c = 0; c < sg.cols; ++c)
            sum += std::abs(sg.at<float>(r, c) - 4.0f);
    QVERIFY2(sum / sg.total() < 1.0,
             qPrintable(QStringLiteral("σ 偏差过大：%1").arg(sum / sg.total())));

    // 尺寸不符：拒绝且不改状态（这是"绝不 resize"的底层保证）
    cv::Mat other(64, 64, CV_8UC1, cv::Scalar(70));
    QVERIFY(!b.addSample(other));
    QCOMPARE(b.sampleCount(), n);
    QCOMPARE(b.size(), clean.size());
}

void AnomalyDetectTest::testHeatScaling()
{
    const double k = 5.0;
    cv::Mat score(4, 1, CV_32FC1);
    score.at<float>(0, 0) = 0.0f;
    score.at<float>(1, 0) = float(k);
    score.at<float>(2, 0) = float(2 * k);
    score.at<float>(3, 0) = float(10 * k);
    const cv::Mat heat = AnomalyDetectNode::heatFromScore(score, k);
    QCOMPARE(int(heat.at<uchar>(0, 0)), 0);
    QVERIFY(std::abs(int(heat.at<uchar>(1, 0)) - 128) <= 1);   // 阈值处＝半亮
    QCOMPARE(int(heat.at<uchar>(2, 0)), 255);
    QCOMPARE(int(heat.at<uchar>(3, 0)), 255);                 // 越界饱和
}

void AnomalyDetectTest::testPlantedDefect()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    AnomalyDetectNode node;
    configure(node, dir.filePath(QStringLiteral("base.vab")));
    const cv::Mat clean = makePart();
    teach(node, clean, 40, 4.0);
    QCOMPARE(node.getParam(QStringLiteral("sampleCount")).toInt(), 40);

    // ① 干净图（同样本噪声水平）⇒ 不报警、判 OK
    const NodeRun ok = runOnce(node, addNoise(clean, 4.0));
    QCOMPARE(ok.count, 0);
    QVERIFY2(ok.status, "干净图被判 NG");

    // ② 植入 +60 灰度、12×12 的块 ⇒ 检出且定位正确
    const cv::Rect where(50, 50, 12, 12);
    const NodeRun r = runOnce(node, plantRect(addNoise(clean, 4.0), where, 60));
    QVERIFY2(r.scoreMax > 5.0, qPrintable(QStringLiteral("scoreMax 过低：%1").arg(r.scoreMax)));
    QCOMPARE(r.count, 1);
    QVERIFY2(r.area >= 100.0 && r.area <= 200.0,
             qPrintable(QStringLiteral("异常面积不在合理区间：%1").arg(r.area)));
    QVERIFY(!r.status);   // ngArea 默认 50 ⇒ 144 px² 判 NG
}

void AnomalyDetectTest::testSigmaFloorGuardsFlatRegions()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat flat = cv::Mat(64, 64, CV_8UC1, cv::Scalar(100));

    // 平坦区（σ≈0）是这套判据唯一的除零来源：floor 没钉住时 1 个灰度级就能爆到 k 以上
    AnomalyDetectNode guarded;
    configure(guarded, dir.filePath(QStringLiteral("flat_guard.vab")));
    teach(guarded, flat, 3, 0.0);
    guarded.setParam(QStringLiteral("sigmaFloor"), 2.0);
    guarded.setParam(QStringLiteral("kSigma"), 5.0);
    const NodeRun g = runOnce(guarded, cv::Mat(64, 64, CV_8UC1, cv::Scalar(101)));
    QCOMPARE(g.count, 0);
    QVERIFY2(g.status, "平坦区 1 个灰度级漂移被判异常（σ 下限没起作用）");

    AnomalyDetectNode unguarded;
    configure(unguarded, dir.filePath(QStringLiteral("flat_unguard.vab")));
    teach(unguarded, flat, 3, 0.0);
    unguarded.setParam(QStringLiteral("sigmaFloor"), 0.1);   // 下限放到最小，复现危险
    unguarded.setParam(QStringLiteral("kSigma"), 5.0);
    const NodeRun u = runOnce(unguarded, cv::Mat(64, 64, CV_8UC1, cv::Scalar(101)));
    QVERIFY2(u.count >= 1, "σ 下限失效时应当满图误报（用于证明 floor 是必需项）");
}

void AnomalyDetectTest::testSaveLoadEquivalence()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("eq.vab"));
    const cv::Mat clean = makePart();

    AnomalyDetectNode a;
    configure(a, path);
    teach(a, clean, 30, 4.0);

    const cv::Mat probe = plantRect(addNoise(clean, 4.0), cv::Rect(40, 70, 14, 14), 70);
    const NodeRun inMem = runOnce(a, probe);
    QVERIFY(inMem.count >= 1);

    // 换一个全新实例，只给路径（trainMode 默认 0）⇒ 判定必须与内存态**完全一致**
    AnomalyDetectNode b;
    configure(b, path);
    const NodeRun reloaded = runOnce(b, probe);
    QCOMPARE(reloaded.count, inMem.count);
    QCOMPARE(reloaded.area, inMem.area);
    QVERIFY2(std::abs(reloaded.scoreMax - inMem.scoreMax) < 1e-6,
             "重载后 scoreMax 漂移（基线序列化没保住精度）");
    QVERIFY(reloaded.baselineStatus.isEmpty());

    // 文件确实落盘且不是空壳
    QVERIFY(QFileInfo(path).size() > 0);
}

void AnomalyDetectTest::testSizeMismatchNeverResizes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("size.vab"));
    const cv::Mat clean = makePart(128, 128);

    AnomalyDetectNode n;
    configure(n, path);
    teach(n, clean, 10, 4.0);

    // 换一张更小的图（尺寸与基线不符）：必须显式报错，不得"顺手 resize 一下"
    const cv::Mat smaller = makePart(64, 64);
    const NodeRun r = runOnce(n, smaller);
    QVERIFY2(!r.status, "尺寸不符却继续出了判定结果");
    QVERIFY2(!r.baselineStatus.isEmpty(), "尺寸不符没有给出可见错误信息");

    // 基线本身没被改动（尺寸/样本数不变）
    const NodeRun again = runOnce(n, addNoise(clean, 4.0));
    QCOMPARE(again.count, 0);
    QVERIFY2(again.status, "报错后基线被破坏了");
}

void AnomalyDetectTest::testUnseenAnomalyTypes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    AnomalyDetectNode n;
    configure(n, dir.filePath(QStringLiteral("types.vab")));
    const cv::Mat clean = makePart();
    teach(n, clean, 40, 4.0);

    // 两类模型训练时**从未见过**的异常：局部块 / 整体对比度偏移
    const NodeRun local = runOnce(n, plantRect(addNoise(clean, 4.0), cv::Rect(80, 20, 10, 10), 50));
    QVERIFY2(local.count >= 1, "局部缺陷未检出");
    QVERIFY2(!local.status, "局部缺陷未判 NG");

    cv::Mat dim;
    cv::convertScaleAbs(clean, dim, 0.6);   // 整体压暗 40%
    const NodeRun global = runOnce(n, addNoise(dim, 4.0));
    QVERIFY2(global.count >= 1, "整体对比度偏移未检出");
    QVERIFY2(!global.status, "整体对比度偏移未判 NG");
}

void AnomalyDetectTest::testRoiAndMaskScope()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat clean = makePart();

    // ① ROI 作用域：**教学前就圈好左半**（基线本身只有左半），右边的缺陷不该进判定
    AnomalyDetectNode roiNode;
    configure(roiNode, dir.filePath(QStringLiteral("roi.vab")));
    roiNode.setParam(QStringLiteral("roiCol"), 0);
    roiNode.setParam(QStringLiteral("roiRow"), 0);
    roiNode.setParam(QStringLiteral("roiWidth"), 64);
    roiNode.setParam(QStringLiteral("roiHeight"), 128);
    teach(roiNode, clean, 40, 4.0);
    QCOMPARE(roiNode.getParam(QStringLiteral("anomalyCount")).toInt(), 0);

    const NodeRun outside =
        runOnce(roiNode, plantRect(addNoise(clean, 4.0), cv::Rect(64, 0, 64, 128), 80));
    QCOMPARE(outside.count, 0);
    QVERIFY2(outside.status, "ROI 外的缺陷被算进了判定");
    const NodeRun inside =
        runOnce(roiNode, plantRect(addNoise(clean, 4.0), cv::Rect(20, 20, 12, 12), 80));
    QVERIFY2(inside.count >= 1, "ROI 内缺陷未检出");

    // ② 掩膜作用域：白=保留、黑=忽略，涂掉缺陷位置后不应检出；清掉掩膜要恢复
    AnomalyDetectNode maskNode;
    configure(maskNode, dir.filePath(QStringLiteral("mask.vab")));
    teach(maskNode, clean, 40, 4.0);

    const cv::Mat withDefect = plantRect(addNoise(clean, 4.0), cv::Rect(90, 40, 16, 16), 80);
    QVERIFY2(runOnce(maskNode, withDefect).count >= 1, "基线正常状态下缺陷未检出（前置条件不成立）");

    QImage mask(128, 128, QImage::Format_Grayscale8);
    mask.fill(uchar(255));
    for (int y = 36; y < 60; ++y) {
        uchar *line = mask.scanLine(y);
        for (int x = 86; x < 110; ++x)
            line[x] = uchar(0);
    }
    maskNode.setEditMask(mask);
    QCOMPARE(runOnce(maskNode, withDefect).count, 0);
    maskNode.clearEditMask();
    QVERIFY2(runOnce(maskNode, withDefect).count >= 1, "清掉掩膜后未恢复检出");
}

void AnomalyDetectTest::testAlignSuppressesShiftFalsePositives()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat clean = makePart();
    AnomalyDetectNode n;
    configure(n, dir.filePath(QStringLiteral("align.vab")));
    n.setParam(QStringLiteral("alignMode"), 0);   // 教学期间不需要对齐（首个样本就是参考）
    teach(n, clean, 40, 4.0);

    // 位置抖动 6×(-4) 像素的**正常**图：没有缺陷，只有位移
    const cv::Mat jittered = addNoise(shiftImage(clean, 6, -4), 4.0);

    n.setParam(QStringLiteral("alignMode"), 0);
    const NodeRun unaligned = runOnce(n, jittered);
    n.setParam(QStringLiteral("alignMode"), 1);
    const NodeRun aligned = runOnce(n, jittered);

    // 前置条件：不对齐时确实会炸出大量假异常（否则本用例什么都没测到）
    QVERIFY2(unaligned.area > 500.0,
             qPrintable(QStringLiteral("前置条件不成立：不对齐的假异常面积只有 %1").arg(unaligned.area)));
    QVERIFY2(aligned.alignApplied, "对齐没有生效（闸门把变换拦掉了，用例测不到对齐的价值）");
    QVERIFY2(aligned.area < unaligned.area / 5.0,
             qPrintable(QStringLiteral("对齐后假异常没被显著压掉：%1 vs %2")
                            .arg(aligned.area)
                            .arg(unaligned.area)));
}

void AnomalyDetectTest::testPcaBackendCatchesWhatPixelBaselineMisses()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat clean = makePart();

    // 产线常态：光照/增益逐件漂移（整图乘一个系数），叠加小噪声。
    // 这正是逐像素统计的软肋——亮区的 σ 被漂移撑大，小缺陷淹死在 5σ 门槛下。
    auto sample = [&clean]() {
        const double gain = 0.85 + g_rng.uniform(0.0, 0.30);
        cv::Mat f;
        clean.convertTo(f, CV_32F);
        f *= gain;
        cv::Mat g8;
        f.convertTo(g8, CV_8U);
        return addNoise(g8, 3.0);
    };
    // 缺陷：亮块（灰度≈200）里 +40 灰度、20×20 —— 幅度落在 A 的 5σ 之下、B 的 5σ 之上
    const cv::Mat probe = plantRect(addNoise(clean, 3.0), cv::Rect(35, 35, 20, 20), 40);

    AnomalyDetectNode a;
    configure(a, dir.filePath(QStringLiteral("gain_pixel.vab")));
    a.setParam(QStringLiteral("alignMode"), 0);   // 本用例只比后端，不让对齐参与
    a.setParam(QStringLiteral("trainMode"), 2);
    for (int i = 0; i < 40; ++i)
        runOnce(a, sample());
    a.setParam(QStringLiteral("trainMode"), 0);
    const NodeRun ra = runOnce(a, probe);
    // 前置事实必须钉成"A 跑通了、也确实打了分，只是没检出"——否则 count==0 可能只是
    // 报错早退留下的残留值，互补性结论就无从谈起。
    QVERIFY2(ra.status,
             QStringLiteral("后端 A 推理轮报错，漏检结论无从谈起：%1")
                 .arg(ra.baselineStatus)
                 .toUtf8()
                 .constData());
    QVERIFY2(ra.scoreMax > 0.0, "后端 A 没产出任何分数（count==0 是空模型造成的，不是漏检）");
    QCOMPARE(ra.count, 0);   // A 在这种增益漂移下确实漏检（不是"我们没测到"）

    AnomalyDetectNode b;
    configure(b, dir.filePath(QStringLiteral("gain_pca.vab")));
    b.setParam(QStringLiteral("backend"), 1);
    b.setParam(QStringLiteral("alignMode"), 0);
    b.setParam(QStringLiteral("trainMode"), 2);
    for (int i = 0; i < 40; ++i)
        runOnce(b, sample());   // 第一轮的"样本不足"报错是预期行为（批量拟合至少要 2 个样本）
    b.setParam(QStringLiteral("trainMode"), 0);
    const NodeRun rb = runOnce(b, probe);
    QVERIFY2(rb.count >= 1,
             QStringLiteral("PCA 后端没能检出 A 漏掉的缺陷（scoreMax=%1；模型状态=%2；教学状态=%3）")
                 .arg(rb.scoreMax)
                 .arg(rb.baselineStatus)
                 .arg(rb.trainStatus)
                 .toUtf8()
                 .constData());
    QVERIFY2(!rb.status, "PCA 检出后没有判 NG");

    // 检出的必须是植入的那块缺陷，而不是别处的噪声团（只断言数量会让"检错地方"过关）
    const cv::Rect target(35, 35, 20, 20);
    bool onTarget = false;
    QString boxDump;
    for (int i = 0; i + 3 < rb.boxes.size(); i += 4) {
        const cv::Rect box(int(rb.boxes[i]), int(rb.boxes[i + 1]),
                           int(rb.boxes[i + 2]), int(rb.boxes[i + 3]));
        boxDump += QStringLiteral("[%1 %2 %3 %4] ")
                       .arg(box.x).arg(box.y).arg(box.width).arg(box.height);
        if ((box & target).area() >= 0.6 * target.area())
            onTarget = true;
    }
    QVERIFY2(onTarget,
             QStringLiteral("PCA 检出的框没落在植入缺陷上（检出框：%1）")
                 .arg(boxDump)
                 .toUtf8()
                 .constData());

    // 模型落盘/重载：换一个只读模型文件的实例，判定必须一致
    AnomalyDetectNode reloaded;
    configure(reloaded, dir.filePath(QStringLiteral("gain_pca.vab")));
    reloaded.setParam(QStringLiteral("backend"), 1);
    reloaded.setParam(QStringLiteral("alignMode"), 0);
    const NodeRun rc = runOnce(reloaded, probe);
    QCOMPARE(rc.count, rb.count);
    QVERIFY2(std::abs(rc.area - rb.area) < 1e-9, "PCA 模型重载后判定漂移");

    // 两种模型文件不通用，且要说清楚（现场最容易犯的错）
    AnomalyDetectNode wrongFile;
    configure(wrongFile, dir.filePath(QStringLiteral("gain_pixel.vab")));
    wrongFile.setParam(QStringLiteral("backend"), 1);
    const NodeRun rd = runOnce(wrongFile, probe);
    QVERIFY2(!rd.status, "拿统计基线文件当 PCA 模型用却没报错");
    QVERIFY2(rd.baselineStatus.contains(QStringLiteral("PCA")),
             qPrintable(QStringLiteral("报错没说明是模型类型不匹配：%1").arg(rd.baselineStatus)));
}

QTEST_MAIN(AnomalyDetectTest)

#include "anomaly_detect_test.moc"
