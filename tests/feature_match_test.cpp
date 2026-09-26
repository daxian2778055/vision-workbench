// 特征匹配节点（阶段 C 的 C1）功能测试
//
// 覆盖：真实用法链路（节点教学写盘 → trainFromImage=false 加载）+ 合成图上的旋转/尺度定位
//       （有已知真值可对照：角度、尺度、贴片中心）+ 三条失败面 + FLANN 不适用时的显式改路记录
//       + 冒烟门禁那块合成件"无特征可教学 ⇒ 判失败"的归类
//       + 显示层 parity（type="feature" 必须画成与模板匹配同款旋转矩形）。
//
// 约定与 opencv_defect_align_test 一致：所有成功用例都走"模板落盘再加载"，
// 因为默认 trainFromImage=true 会用**当前图自己**教学，拿它测什么都测不出来。
#include <QtTest/QtTest>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QFileInfo>

#include "OpencvFeatureMatchNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "ImageDisplayController.h"   // OverlayShape + collectOverlayFromNode
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/features2d.hpp>
#include <HalconCpp.h>
#include <cmath>
#include <vector>

class FeatureMatchTest : public QObject
{
    Q_OBJECT
private slots:
    void smokeGatePartHasNoFeaturesByDesign(); // 冒烟合成件对特征族无特征可教学 ⇒ 必须显式失败
    void testOrbLocatesRotatedPatch();      // ORB：只测旋转（ORB 无尺度不变性，不假装能测尺度）
    void testSiftLocatesRotatedAndScaled(); // SIFT：旋转 + 尺度，且 FLANN 真正被用上
    void testMatchModeNeedsTemplateFile();  // 失败面①：匹配模式没有模板文件
    void testFlatSceneReportsNoFeatures();  // 失败面②：平坦图没有特征 ⇒ 显式失败并给出原因
    void testFlannOnBinaryRecordsFallback();// 失败面③的对照：二进制描述子上 FLANN 不适用 ⇒ 必须留痕
    void testOverlayDrawsFeatureLikeTemplate(); // 显示层：type="feature" 必须走 template 那条旋转矩形分支
};

namespace {

const int kTemplateSize = 128;
const int kCanvasSize = 400;
const int kCanvasBackground = 18;

/// 高纹理"贴片"：特征匹配需要足够多的角点，纯色/低纹理块测不出任何东西（只会测到失败面）
cv::Mat makePatch()
{
    cv::Mat t(kTemplateSize, kTemplateSize, CV_8UC1, cv::Scalar(30));
    cv::RNG rng(20260926);
    for (int i = 0; i < 26; ++i) {
        const int x = rng.uniform(4, kTemplateSize - 28);
        const int y = rng.uniform(4, kTemplateSize - 28);
        const int w = rng.uniform(6, 22);
        const int h = rng.uniform(6, 22);
        const int g = rng.uniform(70, 255);
        if (i % 3 == 0)
            cv::circle(t, cv::Point(x + w / 2, y + h / 2), w / 2, cv::Scalar(g), cv::FILLED);
        else if (i % 3 == 1)
            cv::rectangle(t, cv::Rect(x, y, w, h), cv::Scalar(g), cv::FILLED);
        else
            cv::line(t, cv::Point(x, y), cv::Point(x + w, y + h), cv::Scalar(g), 2);
    }
    cv::rectangle(t, cv::Rect(2, 2, kTemplateSize - 5, kTemplateSize - 5), cv::Scalar(250), 2);
    return t;
}

/// 把 patch 旋转 deg、缩放 scale 后贴到画布上，**贴片中心真正落在 (cx, cy)**。
/// 中心平移由仿射矩阵第 3 列显式给出（getRotationMatrix2D 只保证绕自身中心转，
/// 不保证落点），所以 (cx, cy) 就是节点应当报回来的真值。
cv::Mat makeScene(const cv::Mat &patch, double deg, double scale, double cx, double cy)
{
    const cv::Point2f pc(patch.cols * 0.5f, patch.rows * 0.5f);
    cv::Mat M = cv::getRotationMatrix2D(pc, deg, scale);
    // getRotationMatrix2D 自带一个"保持旋转中心不动"的平移，直接 += 会把它算两遍，
    // 贴片落点就随角度/尺度偏掉 —— 这里覆写第 3 列，让 M·pc 严格等于 (cx, cy)。
    M.at<double>(0, 2) = cx - (M.at<double>(0, 0) * pc.x + M.at<double>(0, 1) * pc.y);
    M.at<double>(1, 2) = cy - (M.at<double>(1, 0) * pc.x + M.at<double>(1, 1) * pc.y);
    cv::Mat canvas;
    cv::warpAffine(patch, canvas, M, cv::Size(kCanvasSize, kCanvasSize),
                   cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(kCanvasBackground));
    return canvas;
}

struct Outcome
{
    bool status = false;
    double row = 0.0, col = 0.0, angle = 0.0, scale = 0.0, score = 0.0;
    int templateKeypoints = 0, sceneKeypoints = 0, candidates = 0, inliers = 0;
    QString note;
    QString trainStatus;
    bool hasMeasure = false;
    bool measureValid = false;
};

Outcome runNode(OpencvFeatureMatchNode &node, const cv::Mat &img)
{
    node.setInputImage(OpencvUtil::matToHimage(img));
    // 与 HalconNode::process() 的"成功默认值"契约对齐：process() 每轮先把 moduleStatus 预置为
    // true，失败必须由 run() 显式写 false。本函数直接调 run() 绕过了 process()，若不预置这一位，
    // "忘了判红"这类坏改在本套件里永远照不到（改坏自证 B3 实测到的正是这条盲区）。
    node.setParam(QStringLiteral("moduleStatus"), true);
    node.run();
    Outcome o;
    o.status = node.getParam(QStringLiteral("moduleStatus")).toBool();
    o.row = node.getParam(QStringLiteral("matchRow")).toDouble();
    o.col = node.getParam(QStringLiteral("matchCol")).toDouble();
    o.angle = node.getParam(QStringLiteral("matchAngle")).toDouble();
    o.scale = node.getParam(QStringLiteral("matchScale")).toDouble();
    o.score = node.getParam(QStringLiteral("matchScore")).toDouble();
    o.templateKeypoints = node.getParam(QStringLiteral("templateKeypoints")).toInt();
    o.sceneKeypoints = node.getParam(QStringLiteral("sceneKeypoints")).toInt();
    o.candidates = node.getParam(QStringLiteral("candidateMatches")).toInt();
    o.inliers = node.getParam(QStringLiteral("inlierCount")).toInt();
    o.note = node.getParam(QStringLiteral("matchNote")).toString();
    o.trainStatus = node.getParam(QStringLiteral("trainStatus")).toString();
    auto data = node.getOutputData(1);
    if (data && data->getType() == DataObject::DataType::Measure) {
        o.hasMeasure = true;
        o.measureValid = data->getMeasureResult().valid;
    }
    return o;
}

/// 教学跑：trainFromImage=true + ROI 覆盖画布中央的贴片 + templatePath 落盘。
/// 返回空串表示"教学跑自己判成功 **且模板真的由节点写进磁盘**"；否则返回原因。
/// 模板不能由测试自己 imwrite：那样节点写盘路径坏掉也照样能测出"定位成功"（读的是测试写的文件）。
QString teach(OpencvFeatureMatchNode &node, const cv::Mat &sceneWithPatchAtCentre,
              const QString &templatePath, int detector)
{
    node.setParam(QStringLiteral("detector"), detector);
    node.setParam(QStringLiteral("matcher"), 0);
    node.setParam(QStringLiteral("trainFromImage"), true);
    node.setParam(QStringLiteral("templatePath"), templatePath);
    node.setParam(QStringLiteral("roiCenterCol"), kCanvasSize / 2.0);
    node.setParam(QStringLiteral("roiCenterRow"), kCanvasSize / 2.0);
    node.setParam(QStringLiteral("roiWidth"), kTemplateSize);
    node.setParam(QStringLiteral("roiHeight"), kTemplateSize);
    node.setParam(QStringLiteral("roiAngle"), 0.0);
    const Outcome warm = runNode(node, sceneWithPatchAtCentre);
    node.setParam(QStringLiteral("trainFromImage"), false);
    if (!QFileInfo::exists(templatePath))
        return QStringLiteral("教学跑没有写出模板文件（trainStatus=%1）").arg(warm.trainStatus);
    if (!warm.status)
        return QStringLiteral("教学跑自身判失败：候选=%1 内点=%2 备注=%3")
            .arg(warm.candidates)
            .arg(warm.inliers)
            .arg(warm.note.isEmpty() ? warm.trainStatus : warm.note);
    return QString();
}

} // namespace

// 冒烟门禁（NodeExecuteSmokeTest::defaultParamsWithImageSucceed）用的 64×64 合成件对
// **特征定位族**就是无特征可教学 —— 本条把这个归类钉死，并把它与"桥接坏了"分开。
// 实测（本机 OpenCV 4.13，逐次一致）：ORB 整图 0 点 / 中心 32×32 教学窗 0 点；
// SIFT 整图 6 点 / 教学窗 0 点 ⇒ 换默认检测器也救不回来（教学窗拿不到点）。
// 唯一能让它"成功"的办法是把"零对应点"也判成命中，那正是本项目禁止的静默绿灯。
void FeatureMatchTest::smokeGatePartHasNoFeaturesByDesign()
{
    cv::Mat img(64, 64, CV_8UC1, cv::Scalar(40));
    cv::rectangle(img, cv::Rect(12, 12, 24, 18), cv::Scalar(210), cv::FILLED);
    cv::circle(img, cv::Point(46, 20), 8, cv::Scalar(130), cv::FILLED);
    cv::line(img, cv::Point(6, 52), cv::Point(58, 46), cv::Scalar(235), 2);

    OpencvFeatureMatchNode node;
    node.init();                       // 一个参数都不改：与冒烟门禁同一条件
    const Outcome o = runNode(node, img);
    QVERIFY2(!o.status,
             "默认参数 + 冒烟合成件判了成功 ⇒ 要查是不是把\"零对应点\"也判成命中");
    QVERIFY2(o.note.contains(QStringLiteral("特征点不足")),
             qPrintable(QStringLiteral("失败原因不是\"无特征可教学\"，归类已失效：") + o.note));
    QVERIFY2(o.candidates == 0 && o.inliers == 0,
             qPrintable(QStringLiteral("无特征却报出对应点：候选=%1 内点=%2").arg(o.candidates).arg(o.inliers)));
    QVERIFY(!(o.hasMeasure && o.measureValid));

    // 桥接自证：himageToMat 坏掉（空/换型）同样会报"特征点不足" ⇒ 两回事必须分开断言
    const cv::Mat back = OpencvUtil::himageToMat(OpencvUtil::matToHimage(img));
    QVERIFY2(back.cols == 64 && back.rows == 64 && back.channels() == 1
             && back.type() == CV_8UC1 && !back.empty(),
             qPrintable(QStringLiteral("HALCON↔OpenCV 往返已不是那张图：%1x%2 ch=%3 type=%4")
                        .arg(back.cols).arg(back.rows).arg(back.channels()).arg(back.type())));

    // 计数只作留档打印（OpenCV 版本升级可能改变它们，故不作断言）
    cv::Ptr<cv::Feature2D> orb = cv::ORB::create(2000);
    cv::Ptr<cv::Feature2D> sift = cv::SIFT::create(2000);
    std::vector<cv::KeyPoint> kpA, kpB, kpC, kpD;
    orb->detect(img, kpA);
    orb->detect(back(cv::Rect(16, 16, 32, 32)), kpB);
    sift->detect(img, kpC);
    sift->detect(back(cv::Rect(16, 16, 32, 32)), kpD);
    qDebug().noquote() << QStringLiteral("FM-SMOKE-PART orb(full)=%1 orb(teachCrop32)=%2 "
                                         "sift(full)=%3 sift(teachCrop32)=%4 note=%5")
                              .arg(kpA.size()).arg(kpB.size()).arg(kpC.size()).arg(kpD.size())
                              .arg(o.note);
}

void FeatureMatchTest::testOrbLocatesRotatedPatch()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString tpl = dir.filePath(QStringLiteral("patch_orb.png"));

    const cv::Mat patch = makePatch();
    const cv::Mat taught = makeScene(patch, 0.0, 1.0, kCanvasSize / 2.0, kCanvasSize / 2.0);

    OpencvFeatureMatchNode teachNode;
    teachNode.init();
    const QString teachErr = teach(teachNode, taught, tpl, 0);
    QVERIFY2(teachErr.isEmpty(), qPrintable(QStringLiteral("ORB 教学失败：") + teachErr));

    const double trueAngle = 18.0;      // ORB 只主张旋转不变，不主张尺度不变
    const double cx = 150.0, cy = 260.0;   // 故意偏离画布中心：位置真值不能被"恰好居中"蒙对
    const cv::Mat scene = makeScene(patch, trueAngle, 1.0, cx, cy);

    OpencvFeatureMatchNode node;
    node.init();
    node.setParam(QStringLiteral("detector"), 0);          // ORB
    node.setParam(QStringLiteral("matcher"), 0);           // BF
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("templatePath"), tpl);

    const Outcome o = runNode(node, scene);
    QVERIFY2(o.status, qPrintable(QStringLiteral("ORB 旋转用例判失败：候选=%1 内点=%2 得分=%3 备注=%4")
                                      .arg(o.candidates).arg(o.inliers).arg(o.score).arg(o.note)));
    // 容差取自 3 次实测（逐次完全一致）：角度误差 0.30°、尺度 0.007、中心 0.2px。
    // 收紧到这个量级才咬得住"约定/符号写反"这一类回归——±3° 时符号错 2° 也能过。
    QVERIFY2(std::abs(o.angle - trueAngle) <= 1.0,
             qPrintable(QStringLiteral("角度偏差过大：实测 %1 期望 %2").arg(o.angle).arg(trueAngle)));
    QVERIFY2(std::abs(o.scale - 1.0) <= 0.05,
             qPrintable(QStringLiteral("尺度偏差过大：实测 %1 期望 1.0").arg(o.scale)));
    QVERIFY2(std::abs(o.col - cx) <= 2.0 && std::abs(o.row - cy) <= 2.0,
             qPrintable(QStringLiteral("中心偏差过大：实测 (%1,%2) 期望 (%3,%4)")
                        .arg(o.col).arg(o.row).arg(cx).arg(cy)));
    QVERIFY(o.inliers >= 5);
    QVERIFY(o.hasMeasure && o.measureValid);
    qDebug().noquote() << QStringLiteral("FM-ORB angle=%1 scale=%2 centre=(%3,%4) candidates=%5 inliers=%6")
                              .arg(o.angle, 0, 'f', 2).arg(o.scale, 0, 'f', 3)
                              .arg(o.col, 0, 'f', 1).arg(o.row, 0, 'f', 1)
                              .arg(o.candidates).arg(o.inliers);
}

void FeatureMatchTest::testSiftLocatesRotatedAndScaled()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString tpl = dir.filePath(QStringLiteral("patch_sift.png"));

    const cv::Mat patch = makePatch();
    const cv::Mat taught = makeScene(patch, 0.0, 1.0, kCanvasSize / 2.0, kCanvasSize / 2.0);

    OpencvFeatureMatchNode teachNode;
    teachNode.init();
    const QString teachErr = teach(teachNode, taught, tpl, 1);
    QVERIFY2(teachErr.isEmpty(), qPrintable(QStringLiteral("SIFT 教学失败：") + teachErr));

    const double trueAngle = 25.0;
    const double trueScale = 1.3;
    const double cx = 250.0, cy = 150.0;   // 同样偏离中心，位置真值独立于角度/尺度真值
    const cv::Mat scene = makeScene(patch, trueAngle, trueScale, cx, cy);

    OpencvFeatureMatchNode node;
    node.init();
    node.setParam(QStringLiteral("detector"), 1);          // SIFT
    node.setParam(QStringLiteral("matcher"), 1);           // FLANN（浮点描述子，应当真用上）
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("templatePath"), tpl);

    const Outcome o = runNode(node, scene);
    QVERIFY2(o.status, qPrintable(QStringLiteral("SIFT 旋转+尺度用例判失败：候选=%1 内点=%2 得分=%3 备注=%4")
                                      .arg(o.candidates).arg(o.inliers).arg(o.score).arg(o.note)));
    // 实测误差：角度 0.03°、尺度 <0.001、中心 0.2px —— 与 ORB 用例同一档容差，两边一起收紧
    QVERIFY2(std::abs(o.angle - trueAngle) <= 1.0,
             qPrintable(QStringLiteral("角度偏差过大：实测 %1 期望 %2").arg(o.angle).arg(trueAngle)));
    QVERIFY2(std::abs(o.scale - trueScale) <= 0.05,
             qPrintable(QStringLiteral("尺度偏差过大：实测 %1 期望 %2").arg(o.scale).arg(trueScale)));
    QVERIFY2(std::abs(o.col - cx) <= 2.0 && std::abs(o.row - cy) <= 2.0,
             qPrintable(QStringLiteral("中心偏差过大：实测 (%1,%2) 期望 (%3,%4)")
                        .arg(o.col).arg(o.row).arg(cx).arg(cy)));
    // FLANN 成功时不得留改路备注：留了就说明本用例其实跑的是 BF
    QVERIFY2(o.note.isEmpty(),
             qPrintable(QStringLiteral("FLANN 未生效：") + o.note));
    QVERIFY(o.hasMeasure && o.measureValid);
    qDebug().noquote() << QStringLiteral("FM-SIFT angle=%1 scale=%2 centre=(%3,%4) candidates=%5 inliers=%6")
                              .arg(o.angle, 0, 'f', 2).arg(o.scale, 0, 'f', 3)
                              .arg(o.col, 0, 'f', 1).arg(o.row, 0, 'f', 1)
                              .arg(o.candidates).arg(o.inliers);
}

void FeatureMatchTest::testMatchModeNeedsTemplateFile()
{
    OpencvFeatureMatchNode node;
    node.init();
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("templatePath"), QString());
    const Outcome o = runNode(node, makeScene(makePatch(), 0.0, 1.0, 200, 200));
    QVERIFY2(!o.status, "匹配模式没有模板文件却判了成功");
    QVERIFY2(!o.note.isEmpty(), "失败没有留下可见原因（S-2 同类形态）");
    QVERIFY(o.hasMeasure == false || o.measureValid == false);
}

void FeatureMatchTest::testFlatSceneReportsNoFeatures()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString tpl = dir.filePath(QStringLiteral("flat.png"));
    cv::Mat flat(kTemplateSize, kTemplateSize, CV_8UC1, cv::Scalar(128));
    QVERIFY2(cv::imwrite(tpl.toStdString(), flat), "模板落盘失败");

    OpencvFeatureMatchNode node;
    node.init();
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("templatePath"), tpl);
    cv::Mat canvas(kCanvasSize, kCanvasSize, CV_8UC1, cv::Scalar(128));
    const Outcome o = runNode(node, canvas);
    QVERIFY2(!o.status, "平坦图无特征却判了成功（静默绿灯）");
    QVERIFY2(!o.note.isEmpty(), qPrintable(QStringLiteral("无特征失败未给出原因：模板特征 %1 场景特征 %2")
                                               .arg(o.templateKeypoints).arg(o.sceneKeypoints)));
}

void FeatureMatchTest::testFlannOnBinaryRecordsFallback()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString tpl = dir.filePath(QStringLiteral("patch_bin.png"));
    const cv::Mat patch = makePatch();
    const cv::Mat taught = makeScene(patch, 0.0, 1.0, kCanvasSize / 2.0, kCanvasSize / 2.0);

    OpencvFeatureMatchNode teachNode;
    teachNode.init();
    const QString teachErr = teach(teachNode, taught, tpl, 0);   // ORB ⇒ 二进制描述子
    QVERIFY2(teachErr.isEmpty(), qPrintable(QStringLiteral("教学失败：") + teachErr));

    OpencvFeatureMatchNode node;
    node.init();
    node.setParam(QStringLiteral("detector"), 0);
    node.setParam(QStringLiteral("matcher"), 1);  // 用户选了 FLANN，但二进制描述子不适用
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("templatePath"), tpl);
    const Outcome o = runNode(node, makeScene(patch, 12.0, 1.0, kCanvasSize / 2.0, kCanvasSize / 2.0));
    QVERIFY2(o.note.contains(QStringLiteral("BF")),
             qPrintable(QStringLiteral("改用 BF 未留痕（静默换路）：实际备注=") + o.note));
    QVERIFY2(o.status, qPrintable(QStringLiteral("改路后本应仍能定位：候选=%1 内点=%2").arg(o.candidates).arg(o.inliers)));
}

// 显示层parity：本节点写出的 Measure 是 type="feature"，而 ImageDisplayController 的旋转矩形
// 分支原先只认 "template"。这条分支在改之前**没有任何用例覆盖**，所以它既可能是"画不出来"
// 也可能被后续改动悄悄退回点状 —— 两头都在这里钉住：图元必须是 RotatedRect，
// 且中心/角度/边长与节点自己报的读数一致。
void FeatureMatchTest::testOverlayDrawsFeatureLikeTemplate()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString tpl = dir.filePath(QStringLiteral("patch_ovl.png"));

    const cv::Mat patch = makePatch();
    const cv::Mat taught = makeScene(patch, 0.0, 1.0, kCanvasSize / 2.0, kCanvasSize / 2.0);
    OpencvFeatureMatchNode teachNode;
    teachNode.init();
    const QString teachErr = teach(teachNode, taught, tpl, 0);
    QVERIFY2(teachErr.isEmpty(), qPrintable(QStringLiteral("教学失败：") + teachErr));

    const double cx = 170.0, cy = 230.0;
    OpencvFeatureMatchNode node;
    node.init();
    node.setParam(QStringLiteral("detector"), 0);
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("templatePath"), tpl);
    const Outcome o = runNode(node, makeScene(patch, 15.0, 1.0, cx, cy));
    QVERIFY2(o.status, qPrintable(QStringLiteral("匹配未成功，叠加无从谈起：") + o.note));

    ImageDisplayController ctrl(nullptr, nullptr, nullptr);
    const QVector<OverlayShape> overlay = ctrl.collectOverlayFromNode(&node);

    int rects = 0;
    int rectIdx = -1;
    for (int i = 0; i < overlay.size(); ++i) {
        if (overlay[i].type == OverlayShape::Type::RotatedRect) {
            ++rects;
            rectIdx = i;
        }
    }
    QVERIFY2(rects == 1,
             qPrintable(QStringLiteral("feature 结果没有画出旋转矩形（画出了 %1 个）⇒ 显示层分支失效").arg(rects)));
    const OverlayShape rect = overlay.at(rectIdx);
    QVERIFY2(std::abs(rect.p1.x() - o.col) <= 2.0 && std::abs(rect.p1.y() - o.row) <= 2.0,
             qPrintable(QStringLiteral("叠加中心与节点读数不符：图元(%1,%2) 读数(%3,%4)")
                        .arg(rect.p1.x()).arg(rect.p1.y()).arg(o.col).arg(o.row)));
    QVERIFY2(std::abs(rect.angleDeg - o.angle) <= 1.0,
             qPrintable(QStringLiteral("叠加角度与节点读数不符：%1 vs %2").arg(rect.angleDeg).arg(o.angle)));
    // 边长按模板尺寸×尺度还原（模板 = 128×128 教学窗）
    QVERIFY2(std::abs(rect.width - kTemplateSize * o.scale) <= 3.0
             && std::abs(rect.height - kTemplateSize * o.scale) <= 3.0,
             qPrintable(QStringLiteral("叠加边长不符：(%1,%2) 期望 %3").arg(rect.width).arg(rect.height)
                        .arg(kTemplateSize * o.scale)));
    QVERIFY2(rect.text.contains(QStringLiteral("score=")),
             qPrintable(QStringLiteral("叠加缺少得分文本：") + rect.text));
    QVERIFY2(std::abs(o.col - cx) <= 2.0 && std::abs(o.row - cy) <= 2.0,
             qPrintable(QStringLiteral("中心偏离真值：(%1,%2) vs (%3,%4)").arg(o.col).arg(o.row).arg(cx).arg(cy)));
}

QTEST_MAIN(FeatureMatchTest)
#include "feature_match_test.moc"
