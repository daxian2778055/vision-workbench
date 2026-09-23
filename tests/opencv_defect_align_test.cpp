// OpenCV 缺陷检测（=标准件/黄金模板比对）功能测试 —— P1-1 补完
//
// 覆盖：基础黄金差影（该算子此前**零测试覆盖**）+ 平移对齐 + 平移/旋转对齐 + 对齐两道闸门
//       + 边缘抑制 + 序列化。
//
// 用法约定：所有用例都走"黄金模板落盘 → trainGolden=false 加载"这条真实用法。
// 注意：默认 trainGolden=true 会用**当前图**重新教学（恒无缺陷），拿它测什么都测不出来。
#include <QtTest/QtTest>
#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QFileInfo>

#include "OpencvDefectNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <HalconCpp.h>

class OpencvDefectAlignTest : public QObject
{
    Q_OBJECT
private slots:
    void testGoldenDiffBasics();               // 零覆盖的基础行为（含教学落盘/加载往返）
    void testAlignTranslational();             // 平移对齐：符号约定 + 假缺陷显著减少
    void testAlignGuards();                    // 闸门：偏移上限（确定性）+ 低置信度下的安全性质
    void testEdgeGuardSuppressesShiftSlivers(); // 边缘抑制：1 像素错位产生的细线假缺陷
    void testAlignRotation();                  // 旋转对齐：角度估计 + 残差下降
    void testSerialization();
};

namespace {

struct RunResult {
    int count = 0;
    double area = 0.0;
    bool status = false;   // moduleStatus（true = OK，false = NG）
};

/// 有足够结构的"工件"图：相位相关需要纹理才能估位移，纯色图估不出任何东西。
cv::Mat makePartImage(int w = 128, int h = 128)
{
    cv::Mat img(h, w, CV_8UC1, cv::Scalar(40));
    cv::rectangle(img, cv::Rect(30, 30, 40, 40), cv::Scalar(200), cv::FILLED);
    cv::circle(img, cv::Point(90, 40), 12, cv::Scalar(120), cv::FILLED);
    cv::line(img, cv::Point(20, 100), cv::Point(108, 92), cv::Scalar(230), 3);
    cv::rectangle(img, cv::Rect(60, 80, 25, 20), cv::Scalar(90), cv::FILLED);
    return img;
}

cv::Mat shiftImage(const cv::Mat &src, double dx, double dy)
{
    const cv::Mat M = (cv::Mat_<double>(2, 3) << 1.0, 0.0, dx, 0.0, 1.0, dy);
    cv::Mat out;
    cv::warpAffine(src, out, M, src.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    return out;
}

cv::Mat rotateImage(const cv::Mat &src, double deg)
{
    const cv::Point2f c(src.cols / 2.0f, src.rows / 2.0f);
    const cv::Mat M = cv::getRotationMatrix2D(c, deg, 1.0);
    cv::Mat out;
    cv::warpAffine(src, out, M, src.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    return out;
}

RunResult runOnce(OpencvDefectNode &node, const cv::Mat &current)
{
    node.setInputImage(OpencvUtil::matToHimage(current));
    node.run();
    RunResult r;
    r.count = node.getParam(QStringLiteral("defectCount")).toInt();
    r.area = node.getParam(QStringLiteral("defectArea")).toDouble();
    r.status = node.getParam(QStringLiteral("moduleStatus")).toBool();
    return r;
}

QString writePng(const QString &path, const cv::Mat &img)
{
    if (cv::imwrite(path.toStdString(), img))
        return path;
    return QString();
}

} // namespace

void OpencvDefectAlignTest::testGoldenDiffBasics()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat golden = makePartImage();
    const QString goldenPath = writePng(dir.filePath(QStringLiteral("golden.png")), golden);
    QVERIFY2(!goldenPath.isEmpty(), "黄金图落盘失败");

    OpencvDefectNode node;
    node.init();
    node.setParam(QStringLiteral("goldenPath"), goldenPath);
    node.setParam(QStringLiteral("trainGolden"), false);
    node.setParam(QStringLiteral("alignMode"), 0);   // 本节只测基础差影
    node.setParam(QStringLiteral("edgeGuard"), 0);

    // ① 与黄金图完全相同 → 无缺陷、不判 NG
    RunResult r = runOnce(node, golden);
    QCOMPARE(r.count, 0);
    QCOMPARE(r.area, 0.0);
    QVERIFY2(r.status, "与黄金图相同却判了 NG");

    // ② 当前图多一块 20×20 亮斑（400 px）→ 恰好 1 个缺陷、面积≈400、判 NG（ngArea 默认 50）
    cv::Mat withBlob = golden.clone();
    cv::rectangle(withBlob, cv::Rect(80, 60, 20, 20), cv::Scalar(255), cv::FILLED);
    r = runOnce(node, withBlob);
    QCOMPARE(r.count, 1);
    QVERIFY2(qAbs(r.area - 400.0) < 60.0,
             qPrintable(QStringLiteral("缺陷面积 %1（应≈400）").arg(r.area)));
    QVERIFY2(!r.status, "缺陷面积超 NG 阈值却判 OK");

    // ③ NG 阈值抬高 → OK（语义：总面积 ≥ ngArea 才判 NG）
    node.setParam(QStringLiteral("ngArea"), 10000.0);
    r = runOnce(node, withBlob);
    QVERIFY2(r.status, "ngArea 抬高后仍判 NG");

    // ④ minArea 过滤掉小缺陷
    node.setParam(QStringLiteral("ngArea"), 50.0);
    node.setParam(QStringLiteral("minArea"), 1000.0);
    r = runOnce(node, withBlob);
    QCOMPARE(r.count, 0);
    node.setParam(QStringLiteral("minArea"), 20.0);

    // ⑤ 教学往返：trainGolden=true 把当前图落盘为黄金图，再加载回来比对 → 无缺陷
    const QString taught = dir.filePath(QStringLiteral("taught.png"));
    node.setParam(QStringLiteral("goldenPath"), taught);
    node.setParam(QStringLiteral("trainGolden"), true);
    runOnce(node, golden);
    QVERIFY2(!node.getParam(QStringLiteral("trainStatus")).toString().isEmpty(),
             "教学后 trainStatus 未写入");
    QVERIFY2(QFileInfo::exists(taught), "教学后黄金图未落盘");

    node.setParam(QStringLiteral("trainGolden"), false);
    r = runOnce(node, golden);
    QCOMPARE(r.count, 0);
    QCOMPARE(r.area, 0.0);
}

void OpencvDefectAlignTest::testAlignTranslational()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat golden = makePartImage();
    const QString goldenPath = writePng(dir.filePath(QStringLiteral("golden.png")), golden);
    QVERIFY2(!goldenPath.isEmpty(), "黄金图落盘失败");

    OpencvDefectNode node;
    node.init();
    node.setParam(QStringLiteral("goldenPath"), goldenPath);
    node.setParam(QStringLiteral("trainGolden"), false);
    node.setParam(QStringLiteral("edgeGuard"), 0);   // 先排除边缘抑制，单独看对齐效果

    const cv::Mat shifted = shiftImage(golden, 3.0, -2.0);   // 当前图向右 3、向上 2

    node.setParam(QStringLiteral("alignMode"), 0);
    const RunResult off = runOnce(node, shifted);
    QVERIFY2(off.area > 300.0,
             qPrintable(QStringLiteral("不对齐本应产生大量假缺陷，实测仅 %1").arg(off.area)));

    node.setParam(QStringLiteral("alignMode"), 1);
    const RunResult on = runOnce(node, shifted);
    QVERIFY2(node.getParam(QStringLiteral("alignApplied")).toBool(), "平移对齐未生效");

    // 位移语义 = 当前图相对黄金模板的位移（右/下为正）：本条**钉住符号约定**，勿凭直觉改
    const double dx = node.getParam(QStringLiteral("alignDx")).toDouble();
    const double dy = node.getParam(QStringLiteral("alignDy")).toDouble();
    QVERIFY2(qAbs(dx - 3.0) < 0.6, qPrintable(QStringLiteral("alignDx=%1（应≈+3）").arg(dx)));
    QVERIFY2(qAbs(dy + 2.0) < 0.6, qPrintable(QStringLiteral("alignDy=%1（应≈-2）").arg(dy)));

    QVERIFY2(on.area < off.area * 0.15,
             qPrintable(QStringLiteral("对齐后假缺陷未显著减少：%1 → %2")
                            .arg(off.area).arg(on.area)));
}

void OpencvDefectAlignTest::testAlignGuards()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat golden = makePartImage();
    const QString goldenPath = writePng(dir.filePath(QStringLiteral("golden.png")), golden);
    QVERIFY2(!goldenPath.isEmpty(), "黄金图落盘失败");

    OpencvDefectNode node;
    node.init();
    node.setParam(QStringLiteral("goldenPath"), goldenPath);
    node.setParam(QStringLiteral("trainGolden"), false);
    node.setParam(QStringLiteral("edgeGuard"), 0);
    node.setParam(QStringLiteral("alignMode"), 1);
    node.setParam(QStringLiteral("alignMaxShift"), 10);

    // ① 偏移超出上限 → 不做对齐（宁可不做，也不要把图移错）。
    //    用 30 px 位移：phaseCorrelate 是循环相关，位移超过边长一半（64 px）会折回成小值，
    //    那种情况拦不住（折回后的对齐通常也"碰巧正确"），故这里取 (上限, 边长/2] 区间内的确定性样本。
    const cv::Mat big = shiftImage(golden, 30.0, 0.0);
    runOnce(node, big);
    QVERIFY2(!node.getParam(QStringLiteral("alignApplied")).toBool(),
             "偏移超上限仍做了对齐（会把图移得更错）");
    QVERIFY2(node.getParam(QStringLiteral("alignResponse")).toDouble() >= 0.0,
             "alignResponse 未记录");

    // ② 低置信度闸门的安全性质：把阈值顶到 0.9 后，无论是否对齐，缺陷面积都不该被"移"坏。
    //    （是否拒绝取决于图像相关的相关峰响应，故这里只钉"不会把结果改坏"这条不变量。）
    const cv::Mat flat(128, 128, CV_8UC1, cv::Scalar(128));
    const QString flatGolden = writePng(dir.filePath(QStringLiteral("flat.png")), flat);
    QVERIFY2(!flatGolden.isEmpty(), "低纹理黄金图落盘失败");
    node.setParam(QStringLiteral("goldenPath"), flatGolden);
    node.setParam(QStringLiteral("alignMinResponse"), 0.9);

    cv::Mat flatBlob = flat.clone();
    cv::rectangle(flatBlob, cv::Rect(40, 40, 20, 20), cv::Scalar(255), cv::FILLED);
    const RunResult rf = runOnce(node, flatBlob);
    QVERIFY2(qAbs(rf.area - 400.0) < 60.0,
             qPrintable(QStringLiteral("低纹理图只应报亮块本身(≈400)，实测 %1").arg(rf.area)));
}

void OpencvDefectAlignTest::testEdgeGuardSuppressesShiftSlivers()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat golden = makePartImage();
    const QString goldenPath = writePng(dir.filePath(QStringLiteral("golden.png")), golden);
    QVERIFY2(!goldenPath.isEmpty(), "黄金图落盘失败");

    OpencvDefectNode node;
    node.init();
    node.setParam(QStringLiteral("goldenPath"), goldenPath);
    node.setParam(QStringLiteral("trainGolden"), false);
    node.setParam(QStringLiteral("alignMode"), 0);   // 故意不依赖对齐，单独看边缘抑制
    // 关掉形态学：默认 3×3 开运算会把 1~2 像素宽的边缘细线整条吃掉，那就变成"测形态学"而不是"测边缘抑制"了
    node.setParam(QStringLiteral("morphSize"), 1);

    // 当前图整体偏 2 像素：硬边缘两侧各差出一条 2 px 宽的细线条（每条约 40~80 px），minArea 拦不住
    const cv::Mat shifted = shiftImage(golden, 2.0, 0.0);

    node.setParam(QStringLiteral("edgeGuard"), 0);
    const RunResult raw = runOnce(node, shifted);
    QVERIFY2(raw.area > 40.0,
             qPrintable(QStringLiteral("2 像素错位本应产生边缘细线，实测仅 %1").arg(raw.area)));

    node.setParam(QStringLiteral("edgeGuard"), 2);
    const RunResult guarded = runOnce(node, shifted);
    QVERIFY2(guarded.area < raw.area * 0.25,
             qPrintable(QStringLiteral("边缘抑制未生效：%1 → %2").arg(raw.area).arg(guarded.area)));
}

void OpencvDefectAlignTest::testAlignRotation()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat golden = makePartImage();
    const QString goldenPath = writePng(dir.filePath(QStringLiteral("golden.png")), golden);
    QVERIFY2(!goldenPath.isEmpty(), "黄金图落盘失败");

    OpencvDefectNode node;
    node.init();
    node.setParam(QStringLiteral("goldenPath"), goldenPath);
    node.setParam(QStringLiteral("trainGolden"), false);
    node.setParam(QStringLiteral("edgeGuard"), 0);
    node.setParam(QStringLiteral("morphSize"), 1);   // 同前：别让开运算把细线残差吃掉

    const cv::Mat rotated = rotateImage(golden, 2.0);   // 绕中心 +2°

    node.setParam(QStringLiteral("alignMode"), 0);
    const RunResult off = runOnce(node, rotated);
    QVERIFY2(off.area > 300.0,
             qPrintable(QStringLiteral("旋转 2° 不对齐本应大量假缺陷，实测 %1").arg(off.area)));

    node.setParam(QStringLiteral("alignMode"), 2);
    const RunResult on = runOnce(node, rotated);
    QVERIFY2(node.getParam(QStringLiteral("alignApplied")).toBool(), "旋转对齐未生效");
    const double ang = node.getParam(QStringLiteral("alignAngle")).toDouble();
    QVERIFY2(qAbs(qAbs(ang) - 2.0) < 0.7,
             qPrintable(QStringLiteral("估计角度 %1°（应≈±2°）").arg(ang)));

    // 记录一条实测现象：**只对齐、不开边缘抑制时，残差不一定下降**。
    // 原因是对齐要重采样，插值模糊会把硬边缘的差异带从 1 px 摊到 2~3 px，像素级差影反而更"胖"；
    // 所以这里只确认"没炸掉"，真正的残差下降交给下一步（开边缘抑制）——两者是配套的，不是二选一。
    QVERIFY2(on.area < off.area * 1.5,
             qPrintable(QStringLiteral("对齐后残差异常放大：%1 → %2").arg(off.area).arg(on.area)));

    // 对齐 + 边缘抑制（推荐配置）：几何错位已消除、边缘模糊带被抑制 → 残差应显著下降
    node.setParam(QStringLiteral("edgeGuard"), 2);
    const RunResult guarded = runOnce(node, rotated);
    QVERIFY2(guarded.area < off.area * 0.4,
             qPrintable(QStringLiteral("对齐+边缘抑制后残差未显著下降：%1 → %2")
                            .arg(off.area).arg(guarded.area)));
}

void OpencvDefectAlignTest::testSerialization()
{
    OpencvDefectNode n1;
    n1.init();
    n1.setParam(QStringLiteral("goldenPath"), QStringLiteral("/tmp/g.png"));
    n1.setParam(QStringLiteral("diffThreshold"), 33);
    n1.setParam(QStringLiteral("alignMode"), 2);
    n1.setParam(QStringLiteral("alignMaxShift"), 17);
    n1.setParam(QStringLiteral("alignMinResponse"), 0.2);
    n1.setParam(QStringLiteral("edgeGuard"), 5);
    n1.setParam(QStringLiteral("edgeThresh"), 77);
    QJsonObject j = n1.toJson();

    OpencvDefectNode n2;
    n2.init();
    n2.fromJson(j);
    QCOMPARE(n2.getParam(QStringLiteral("goldenPath")).toString(), QStringLiteral("/tmp/g.png"));
    QCOMPARE(n2.getParam(QStringLiteral("diffThreshold")).toInt(), 33);
    QCOMPARE(n2.getParam(QStringLiteral("alignMode")).toInt(), 2);
    QCOMPARE(n2.getParam(QStringLiteral("alignMaxShift")).toInt(), 17);
    QCOMPARE(n2.getParam(QStringLiteral("alignMinResponse")).toDouble(), 0.2);
    QCOMPARE(n2.getParam(QStringLiteral("edgeGuard")).toInt(), 5);
    QCOMPARE(n2.getParam(QStringLiteral("edgeThresh")).toInt(), 77);
}

QTEST_MAIN(OpencvDefectAlignTest)
#include "opencv_defect_align_test.moc"
