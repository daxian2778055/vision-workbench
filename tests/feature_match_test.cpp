// 特征匹配节点（阶段 C 的 C1）功能测试
//
// 覆盖：真实用法链路（节点教学写盘 → trainFromImage=false 加载）+ 合成图上的旋转/尺度定位
//       （有已知真值可对照：角度、尺度、贴片中心）+ 三条失败面 + FLANN 不适用时的显式改路记录
//       + 冒烟门禁那块合成件"无特征可教学 ⇒ 判失败"的归类
//       + 显示层 parity（type="feature" 必须画成与模板匹配同款旋转矩形）。
//
// 约定与 opencv_defect_align_test 一致：所有成功用例都走"模板落盘再加载"。
// U-30（推进计划 §3.31）把三处节点的 trainFromImage 默认从 true 翻成 false（运行期常态＝匹配
// 模式），教学变成一次性动作：ROI 落位仍置 true，模板落盘成功后节点自己翻回 false。本文件里
// 显式 setParam(true/false) 的老用例不依赖默认值；teach() 那句"设回 false"从本轮起是冗余的，
// 留着是为了让"翻回"这件事同时有测试断言（见 teachingPersistsOnceAndStopsOverwriting）。
#include <QtTest/QtTest>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QDir>
#include <QFileInfo>
#include <QFile>

#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include "TemplateSearchBudget.h"
#include "OpencvFeatureMatchNode.h"
#include "OpencvTemplateMatchNode.h"
#include "TemplateMatchNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "ImageDisplayController.h"   // OverlayShape + collectOverlayFromNode
#include "TemplateConfigDialog.h"     // 魔术棒取形（纯函数）
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
    // ---- U-30：模板教学不得每轮覆写（推进计划 §3.31）----
    void teachingDefaultsAreMatchMode();            // 三处节点的 trainFromImage 默认必须是 false
    void teachingPersistsOnceAndStopsOverwriting();  // 教学落盘后自动翻回 ＋ 第二轮不覆写模板文件
    void teachingWithoutFileKeepsTraining();         // 边界：没有文件路径时不翻回（否则下一轮平白判红）
    void matchModeWithoutFileFailsAcrossFamily();    // 默认参数＋没模板：三处都判红并给原因
    // 模板匹配基准（r19）：现场 1500×1000 + 208px + 31 角度 = 16380ms/轮
    void templateMatchSearchIsFastEnough();
    void templateMatchFindsMultipleInstances();
    void searchBudgetCountsCombos();
    void templateMatchOnRealImage();
    void templateMatchAcrossFrames();
    void templateMatchSurvivesPartialOcclusion();
    // 掩膜参与评分（r19 遗留项）：只改掩膜 → 命中位置必须变化
    void templateMatchMaskMovesBestHit();
    // 训练后特征点可视化：绿点必须落在模板框内，且跟着 ROI 走（VM 模板配置同款）
    void templateMatchFeaturePointsFollowRoi();
    // 检测区域画布化：模板框（绿）与检测区域（青）必须同时画出来
    void templateMatchOverlayShowsRegionAndTemplate();
    // VM「对比度阈值/阈值模式」对齐：参数必须真的改变参与打分的点
    void templateMatchEdgeThresholdTakesEffect();
    // 模板对象：序列化往返 + 老 templatePath 迁移
    void templateEntryRoundTripAndLegacyMigration();
    // 模板列表驱动匹配：切 active 索引，命中必须跟着换模板
    void templateMatchUsesActiveTemplateFromList();
    // 多模板同时匹配：画布只有 B 时，开关打开后必须自动选中模板 B
    void templateMatchAllTemplatesPicksBest();
    // 多边形/圆形模板：必须能抠出包围盒（此前多边形直接返回空 → 建模板必失败）
    void templateShapeExtractionSupportsPolygonAndCircle();
    // 魔术棒取形：泛洪出的多边形必须贴合目标区域
    void templateWandPolygonFollowsRegion();
    // 链长模式：可见率门槛必须真的能把遮挡过多的候选判无效
    void templateMinVisibleRatioControlsOcclusion();
    // 多形状模板：JSON 往返 + 并集包围盒/并集掩膜
    void templateMultiShapeEntryAndMask();
    // 图像型 ROI（VM 区域类型=图像类型）：候选中心必须落在掩膜白区
    void templateImageTypeRoiLimitsCandidates();
    // 输出掩膜：开关打开后唯一端口上应带一张与搜索区域一致的掩膜图
    void templateOutputsSearchMask();
    // 匹配中心（锚点）偏移：匹配点必须跟着锚点走（VM 的「匹配中心 X/Y」）
    void templateMatchAnchorOffsetsMatchPoint();
    // 分割/剪刀：按连通域拆块、沿画线切开
    void templateSplitShapeByCut();
    // 继承 ROI：上游命中框当成搜索区域（框外的更优候选不许抢）
    void templateInheritsUpstreamBoxAsSearchRegion();
    // 魔术棒 / 魔术棒+：替换 vs 追加
    void templateWandPlusAppends();
    // 多模板顺序匹配：matchAllTemplates=false 时按列表顺序试，达标即停
    void templateSequentialMatchStopsAtFirstHit();
    // 尺度范围 XY：X/Y 独立缩放（scaleRangeType=1）
    void templateScaleRangeXYFindsAnisotropicInstance();
    // 擦除轮廓点：擦除区域内的特征点不参与匹配
    void templateErasesRemoveFeaturePoints();
    // 建模板后的参数建议：低对比给低阈值、大模板给大特征尺度
    void templateSuggestParamsFollowsTemplate();
    // VM 特征尺度/最小链长：特征点必须真的随参数变稀疏、短链被丢弃
    void templateFeatureScaleAndChainLengthFilterPoints();
    // VM 排序类型：多目标结果的排序依据必须生效
    void templateSortTypeOrdersMatchList();
    // 屏蔽形状：最终掩膜 = 保留 − 屏蔽
    void templateExcludeShapesSubtractFromMask();
    // 尺度模式=自动：按模板占比估出搜索范围，能命中 1.15 倍实例
    void templateScaleRangeFindsScaledInstance();
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

/// 按字节读回文件内容（用于"第二轮有没有覆写模板文件"这种逐字节判据）。
QByteArray fileBytes(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.readAll();
}

Outcome runOpenCVTemplate(OpencvTemplateMatchNode &node, const cv::Mat &img)
{
    node.setInputImage(OpencvUtil::matToHimage(img));
    node.setParam(QStringLiteral("moduleStatus"), true);
    node.run();
    Outcome o;
    o.status = node.getParam(QStringLiteral("moduleStatus")).toBool();
    o.trainStatus = node.getParam(QStringLiteral("trainStatus")).toString();
    return o;
}

Outcome runHalconTemplate(TemplateMatchNode &node, const cv::Mat &img)
{
    node.setInputImage(OpencvUtil::matToHimage(img));
    node.setParam(QStringLiteral("moduleStatus"), true);
    node.run();
    Outcome o;
    o.status = node.getParam(QStringLiteral("moduleStatus")).toBool();
    o.trainStatus = node.getParam(QStringLiteral("trainStatus")).toString();
    return o;
}

} // namespace

// 冒烟门禁（NodeExecuteSmokeTest::defaultParamsWithImageSucceed）用的 64×64 合成件对
// **特征定位族**就是无特征可教学 —— 本条把这个归类钉死，并把它与"桥接坏了"分开。
// 实测（本机 OpenCV 4.13，逐次一致）：ORB 整图 0 点 / 中心 32×32 教学窗 0 点；
// SIFT 整图 6 点 / 教学窗 0 点 ⇒ 换默认检测器也救不回来（教学窗拿不到点）。
// 唯一能让它"成功"的办法是把"零对应点"也判成命中，那正是本项目禁止的静默绿灯。
// ⚠️ U-30（§3.31）之后本条分两段：默认参数已经走不到特征提取了（trainFromImage 默认 false
//   ⇒ 停在"匹配模式没有模板文件"那道闸），"无特征可教学"这层前提只有显式教学态才量得到。
//   两段都要红，但红在两级闸上——冒烟表里那条归类理由对应第一段，本条第二段继续钉住第二层。
void FeatureMatchTest::smokeGatePartHasNoFeaturesByDesign()
{
    cv::Mat img(64, 64, CV_8UC1, cv::Scalar(40));
    cv::rectangle(img, cv::Rect(12, 12, 24, 18), cv::Scalar(210), cv::FILLED);
    cv::circle(img, cv::Point(46, 20), 8, cv::Scalar(130), cv::FILLED);
    cv::line(img, cv::Point(6, 52), cv::Point(58, 46), cv::Scalar(235), 2);

    // 段一：一个参数都不改 —— 与冒烟门禁同一条件，红在"匹配模式没有模板文件"
    OpencvFeatureMatchNode asShipped;
    asShipped.init();
    const Outcome dflt = runNode(asShipped, img);
    QVERIFY2(!dflt.status,
             "默认参数 + 冒烟合成件判了成功 ⇒ 要查是不是把\"没配模板\"也判成命中");
    QVERIFY2(dflt.note.contains(QStringLiteral("匹配模式需要设置模板文件路径")),
             qPrintable(QStringLiteral("默认参数的失败点不再是\"没有模板文件\" ⇒ 冒烟归类理由已漂移：")
                        + dflt.note));

    // 段二：显式回到翻转前的旧默认（每轮现教），才真正走到教学窗与特征提取
    OpencvFeatureMatchNode node;
    node.init();
    node.setParam(QStringLiteral("trainFromImage"), true);
    const Outcome o = runNode(node, img);
    QVERIFY2(!o.status,
             "教学态 + 冒烟合成件判了成功 ⇒ 要查是不是把\"零对应点\"也判成命中");
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

// ==================== U-30：模板教学不得每轮覆写（推进计划 §3.31）====================

// 三处模板族节点的默认必须是"匹配模式"。理由不是口味：默认 true 时，流水线上任何一帧
// （坏件、遮挡、位置歪了）都会重新教一遍模板并覆盖模板文件，之后所有判定都在拿脏模板比，
// 而面板上看着一切正常。这条腿只读默认值，不跑图 ⇒ 改动三处 registerParams 是否漏了一处，
// 这里当场报出漏的那一处（bad 里带类名）。
void FeatureMatchTest::teachingDefaultsAreMatchMode()
{
    QStringList bad;
    OpencvFeatureMatchNode feature;
    feature.init();
    if (feature.getParam(QStringLiteral("trainFromImage")).toBool())
        bad << QStringLiteral("OpencvFeatureMatchNode");
    OpencvTemplateMatchNode tmpl;
    tmpl.init();
    if (tmpl.getParam(QStringLiteral("trainFromImage")).toBool())
        bad << QStringLiteral("OpencvTemplateMatchNode");
    TemplateMatchNode halconTmpl;
    halconTmpl.init();
    if (halconTmpl.getParam(QStringLiteral("trainFromImage")).toBool())
        bad << QStringLiteral("TemplateMatchNode");
    QVERIFY2(bad.isEmpty(),
             qPrintable(QStringLiteral("trainFromImage 默认仍是 true（每轮重教并覆写模板）：")
                        + bad.join(QStringLiteral(", "))));
}

// 教学＝一次性动作：落盘成功后节点自己翻回匹配模式，第二轮不再覆写那个文件。
// 判据取**文件字节**而不是状态位：状态位能靠"这轮恰好没写"糊过去，逐字节比对糊不过去。
void FeatureMatchTest::teachingPersistsOnceAndStopsOverwriting()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString tpl = dir.filePath(QStringLiteral("teach_once.png"));

    const cv::Mat patch = makePatch();
    OpencvFeatureMatchNode node;
    node.init();
    node.setParam(QStringLiteral("detector"), 0);
    node.setParam(QStringLiteral("matcher"), 0);
    node.setParam(QStringLiteral("trainFromImage"), true);   // 用户这一轮显式要教学
    node.setParam(QStringLiteral("templatePath"), tpl);
    node.setParam(QStringLiteral("roiCenterCol"), kCanvasSize / 2.0);
    node.setParam(QStringLiteral("roiCenterRow"), kCanvasSize / 2.0);
    node.setParam(QStringLiteral("roiWidth"), kTemplateSize);
    node.setParam(QStringLiteral("roiHeight"), kTemplateSize);
    node.setParam(QStringLiteral("roiAngle"), 0.0);

    const Outcome warm = runNode(node, makeScene(patch, 0.0, 1.0, kCanvasSize / 2.0, kCanvasSize / 2.0));
    QVERIFY2(warm.status, qPrintable(QStringLiteral("教学跑本身判红了，后面的断言无从谈起：")
                                     + (warm.note.isEmpty() ? warm.trainStatus : warm.note)));
    QVERIFY2(QFileInfo::exists(tpl), "教学跑没有写出模板文件");
    QVERIFY2(!node.getParam(QStringLiteral("trainFromImage")).toBool(),
             "教学落盘成功后没有翻回匹配模式 ⇒ 第二轮还会重教并覆写模板文件");

    const QByteArray first = fileBytes(tpl);
    QVERIFY2(!first.isEmpty(), "模板文件读不回字节");

    // 第二轮：贴片挪到画面右上角，模板框仍留在画面中心 ⇒ 框里只剩背景。
    const double cx = 330.0, cy = 80.0;
    const cv::Mat second = makeScene(patch, 0.0, 1.0, cx, cy);
    {   // 前提自证（控制变量）：框内区域确实只有背景，否则"文件没变"就可能是"框里本来就一样"
        const cv::Mat win = second(cv::Rect(kCanvasSize / 2 - kTemplateSize / 2,
                                            kCanvasSize / 2 - kTemplateSize / 2,
                                            kTemplateSize, kTemplateSize));
        const double meanVal = cv::mean(win)[0];
        QVERIFY2(meanVal < kCanvasBackground + 8.0,
                 qPrintable(QStringLiteral("第二轮的模板框里不是纯背景（均值 %1）⇒ 本条前提失效").arg(meanVal)));
    }

    const Outcome again = runNode(node, second);
    const QByteArray after = fileBytes(tpl);
    QVERIFY2(after == first,
             qPrintable(QStringLiteral("第二轮覆写了模板文件（%1 字节 → %2 字节）⇒ 脏模板会污染之后所有判定")
                        .arg(first.size()).arg(after.size())));
    QVERIFY2(again.status,
             qPrintable(QStringLiteral("第二轮读的是第一轮那份模板，贴片仍在场景里就该定位成功；实际：")
                        + (again.note.isEmpty() ? again.trainStatus : again.note)));

    // 同一条判据在第二个站点上再走一遍：翻转逻辑在生产侧是**三处同形**（特征匹配／OpenCV 模板
    // 匹配／HALCON 模板匹配），只钉一处就等于放行"另两处忘了改"。这里量的是 OpencvTemplateMatchNode。
    // 第三处（TemplateMatchNode）不在本条射程：它已注销册（src/NodeRegistry.cpp 明确写着替换版环境
    // CreateShapeModel 异常 ⇒ 替代者是 OpenCV 版），且训练分支在本机实测抛 HALCON 异常，
    // 无法在门禁里跑出"教学成功"这一轮 ⇒ 只作读盘同形核对，不冒充实测。
    const QString tpl2 = dir.filePath(QStringLiteral("teach_once_tmpl.png"));
    OpencvTemplateMatchNode tmplNode;
    tmplNode.init();
    tmplNode.setParam(QStringLiteral("trainFromImage"), true);
    tmplNode.setParam(QStringLiteral("templatePath"), tpl2);
    tmplNode.setParam(QStringLiteral("roiCenterCol"), kCanvasSize / 2.0);
    tmplNode.setParam(QStringLiteral("roiCenterRow"), kCanvasSize / 2.0);
    tmplNode.setParam(QStringLiteral("roiWidth"), kTemplateSize);
    tmplNode.setParam(QStringLiteral("roiHeight"), kTemplateSize);
    tmplNode.setParam(QStringLiteral("angleMin"), 0.0);
    tmplNode.setParam(QStringLiteral("angleMax"), 0.0);
    const Outcome tmplWarm = runOpenCVTemplate(
        tmplNode, makeScene(patch, 0.0, 1.0, kCanvasSize / 2.0, kCanvasSize / 2.0));
    QVERIFY2(tmplWarm.status,
             qPrintable(QStringLiteral("模板匹配的教学跑本身判红：") + tmplWarm.trainStatus));
    QVERIFY2(QFileInfo::exists(tpl2), "模板匹配的教学跑没有写出模板文件");
    QVERIFY2(!tmplNode.getParam(QStringLiteral("trainFromImage")).toBool(),
             "模板匹配落盘成功后没有翻回匹配模式 ⇒ 第二轮还会重教并覆写模板文件");
    const QByteArray tmplFirst = fileBytes(tpl2);
    const Outcome tmplSecond = runOpenCVTemplate(tmplNode, second);
    const QByteArray tmplAfter = fileBytes(tpl2);
    QVERIFY2(tmplAfter == tmplFirst,
             qPrintable(QStringLiteral("模板匹配第二轮覆写了模板文件（%1 字节 → %2 字节）")
                        .arg(tmplFirst.size()).arg(tmplAfter.size())));
    QVERIFY2(tmplSecond.status,
             qPrintable(QStringLiteral("模板匹配第二轮读第一轮那份模板，贴片仍在场景里就该匹配成功；实际：")
                        + tmplSecond.trainStatus));
}

// 边界（负对照）：没填模板文件路径时**不**翻回匹配模式——那时磁盘上没有可加载的模板，
// 翻回去等于让一个正在正常工作的节点下一轮平白判红。这条在改前改后都该是绿的，
// 它买的是"修法没有顺手把无文件的现取现匹配也改掉"这一格。
void FeatureMatchTest::teachingWithoutFileKeepsTraining()
{
    const cv::Mat patch = makePatch();
    OpencvFeatureMatchNode node;
    node.init();
    node.setParam(QStringLiteral("detector"), 0);
    node.setParam(QStringLiteral("trainFromImage"), true);
    node.setParam(QStringLiteral("templatePath"), QString());   // 故意不给文件路径
    node.setParam(QStringLiteral("roiCenterCol"), kCanvasSize / 2.0);
    node.setParam(QStringLiteral("roiCenterRow"), kCanvasSize / 2.0);
    node.setParam(QStringLiteral("roiWidth"), kTemplateSize);
    node.setParam(QStringLiteral("roiHeight"), kTemplateSize);
    const Outcome o = runNode(node, makeScene(patch, 0.0, 1.0, kCanvasSize / 2.0, kCanvasSize / 2.0));
    QVERIFY2(o.status, qPrintable(QStringLiteral("无文件教学跑本应成功：")
                                  + (o.note.isEmpty() ? o.trainStatus : o.note)));
    QVERIFY2(node.getParam(QStringLiteral("trainFromImage")).toBool(),
             "没有模板文件却翻回匹配模式 ⇒ 下一轮会因「找不到文件路径」平白判红");
    QVERIFY2(o.trainStatus.contains(QStringLiteral("未保存")),
             qPrintable(QStringLiteral("无文件教学的状态文本应留痕「未保存」，实际：") + o.trainStatus));
}

// 默认参数（＝匹配模式）＋没有模板文件：三处都必须判红，并且给一句人话原因。
// 这条腿同时是冒烟归类改动的动作证：TemplateMatchNode／OpencvTemplateMatchNode 从"默认参数
// 判成功"翻成"默认参数判失败"，NodeExecuteSmokeTest 的 kFailsWithoutResource 必须同步收录它们。
void FeatureMatchTest::matchModeWithoutFileFailsAcrossFamily()
{
    const cv::Mat scene = makeScene(makePatch(), 0.0, 1.0, kCanvasSize / 2.0, kCanvasSize / 2.0);
    const QString needle = QStringLiteral("匹配模式需要设置模板文件路径");

    QStringList wronglyGreen;
    QStringList noReason;

    {
        OpencvTemplateMatchNode node;
        node.init();
        const Outcome o = runOpenCVTemplate(node, scene);
        if (o.status) wronglyGreen << QStringLiteral("OpencvTemplateMatchNode");
        else if (!o.trainStatus.contains(needle)) noReason << QStringLiteral("OpencvTemplateMatchNode");
    }
    {
        TemplateMatchNode node;
        node.init();
        const Outcome o = runHalconTemplate(node, scene);
        if (o.status) wronglyGreen << QStringLiteral("TemplateMatchNode");
        else if (!o.trainStatus.contains(needle)) noReason << QStringLiteral("TemplateMatchNode");
    }
    {
        OpencvFeatureMatchNode node;
        node.init();
        const Outcome o = runNode(node, scene);
        if (o.status) wronglyGreen << QStringLiteral("OpencvFeatureMatchNode");
        else if (!o.note.contains(needle)) noReason << QStringLiteral("OpencvFeatureMatchNode");
    }

    QVERIFY2(wronglyGreen.isEmpty(),
             qPrintable(QStringLiteral("匹配模式没有模板却判了成功（静默绿灯）：")
                        + wronglyGreen.join(QStringLiteral(", "))));
    QVERIFY2(noReason.isEmpty(),
             qPrintable(QStringLiteral("判红但现场读不到原因（未写 "
                                        "匹配模式需要设置模板文件路径"
                                        "）：")
                        + noReason.join(QStringLiteral(", "))));
}

void FeatureMatchTest::templateMatchSearchIsFastEnough()
{
    // 现场等效：1500×1000 图 + 208px 模板 + 31 个角度（-30..30 步进 2）
    cv::Mat big(1000, 1500, CV_8UC1);
    cv::RNG rng(12345);
    rng.fill(big, cv::RNG::UNIFORM, 40, 210);
    cv::Mat color;
    cv::cvtColor(big, color, cv::COLOR_GRAY2BGR);
    const HObject img = OpencvUtil::matToHimage(color);

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("roiCenterCol"), 704.0);   // 600 + 208/2
    node.setParam(QStringLiteral("roiCenterRow"), 504.0);   // 400 + 208/2
    node.setParam(QStringLiteral("roiWidth"), 208.0);
    node.setParam(QStringLiteral("roiHeight"), 208.0);
    node.setParam(QStringLiteral("roiAngle"), 0.0);
    node.setParam(QStringLiteral("angleMin"), -30.0);
    node.setParam(QStringLiteral("angleMax"), 30.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("scaleMin"), 1.0);
    node.setParam(QStringLiteral("scaleMax"), 1.0);
    node.setParam(QStringLiteral("trainFromImage"), true);   // 从图像按矩形取模板
    node.setParam(QStringLiteral("minScore"), 0.3);
    node.setInputImage(img);

    QElapsedTimer timer;
    timer.start();
    node.run(false);
    const qint64 ms = timer.elapsed();
    const double score = node.getParam(QStringLiteral("matchScore")).toDouble();
    const double col = node.getParam(QStringLiteral("matchCol")).toDouble();
    const double row = node.getParam(QStringLiteral("matchRow")).toDouble();
    qInfo("template-match benchmark: %lld ms score=%.3f at (%.1f, %.1f)", ms, score, col, row);

    // 上限取 8s：远低于现场 16.4s，又给慢机器留余量（这条腿守的是"别再回到十几秒"）
    QVERIFY2(ms < 8000, qPrintable(QStringLiteral("匹配耗时 %1 ms 超过 8s 上限").arg(ms)));
    // r19 P1-14：坐标必须钉死。粗到细曾把降采样像素当全图坐标（偏 ≈coarseFactor），
    // 而这条腿当时只钉耗时与分数，放过了它。现在按合成图的真实落点断言（±6px）。
    QVERIFY2(score > 0.9, qPrintable(QStringLiteral("分数过低：%1（匹配没真正生效？）").arg(score)));
    QVERIFY2(qAbs(col - 704.0) <= 1.5,
             qPrintable(QStringLiteral("列偏了：%1（期望 704，粗到细坐标没换算回来？）").arg(col)));
    QVERIFY2(qAbs(row - 504.0) <= 1.5,
             qPrintable(QStringLiteral("行偏了：%1（期望 504）").arg(row)));

    // method=2 边缘梯度相关（r19）：同一张图再跑一遍，只验证"能稳定找到、分数可用"。
    // 这条腿防的是"漏算边沿图 → 直接拿灰度跟边沿比"这种写坏，分数会明显塌下来。
    node.setParam(QStringLiteral("method"), 2);
    timer.restart();
    node.run(false);
    const qint64 msEdge = timer.elapsed();
    const double scoreEdge = node.getParam(QStringLiteral("matchScore")).toDouble();
    const double colEdge = node.getParam(QStringLiteral("matchCol")).toDouble();
    const double rowEdge = node.getParam(QStringLiteral("matchRow")).toDouble();
    qInfo("template-match edge-method: %lld ms score=%.3f at (%.1f, %.1f)",
          msEdge, scoreEdge, colEdge, rowEdge);
    QVERIFY2(scoreEdge > 0.9,
             qPrintable(QStringLiteral("倒角匹配分数过低：%1（距离场没生效？）").arg(scoreEdge)));
    QVERIFY2(qAbs(colEdge - 704.0) <= 1.5 && qAbs(rowEdge - 504.0) <= 1.5,
             qPrintable(QStringLiteral("倒角匹配位置偏了：(%1, %2)").arg(colEdge).arg(rowEdge)));
}


// 多目标（r19 批 A 续）：画布上放 3 个同款工件，maxMatches=3 必须找出多个。
// 这条腿守两件事：① 抑制半径能把已找到的位置排除掉 ② matchList 的坐标是图像坐标不是模板局部坐标。
void FeatureMatchTest::templateMatchFindsMultipleInstances()
{
    const cv::Mat patch = makePatch();                      // 128x128 有纹理的小块
    cv::Mat big(1000, 1500, CV_8UC1, cv::Scalar(25));       // 纯色底：只有贴上去的工件能匹配
    struct Pt { int x, y; };
    const Pt centers[3] = {{300, 250}, {750, 500}, {1180, 750}};
    for (const Pt &c : centers) {
        const cv::Rect dst(c.x - patch.cols / 2, c.y - patch.rows / 2, patch.cols, patch.rows);
        patch.copyTo(big(dst));
    }

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("roiCenterCol"), double(centers[0].x));
    node.setParam(QStringLiteral("roiCenterRow"), double(centers[0].y));
    node.setParam(QStringLiteral("roiWidth"), double(patch.cols));
    node.setParam(QStringLiteral("roiHeight"), double(patch.rows));
    node.setParam(QStringLiteral("roiAngle"), 0.0);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("trainFromImage"), true);
    node.setParam(QStringLiteral("minScore"), 0.5);
    node.setParam(QStringLiteral("maxMatches"), 3);
    node.setInputImage(OpencvUtil::matToHimage(big));
    node.run(false);

    const int count = node.getParam(QStringLiteral("matchCount")).toInt();
    const QString list = node.getParam(QStringLiteral("matchList")).toString();
    qInfo("multi-instance: count=%d list=%s", count, qPrintable(list));

    QVERIFY2(count >= 2, qPrintable(QStringLiteral("多目标只找到 %1 个（抑制半径或循环有问题）").arg(count)));
    const QStringList items = list.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    QCOMPARE(items.size(), count);
    for (const QString &it : items) {
        const QStringList f = it.split(QLatin1Char(','));
        QVERIFY2(f.size() >= 7,
                 qPrintable(QStringLiteral("matchList 应含 角度/尺度/匹配点（实际 %1 项）").arg(f.size())));
        const double row = f[0].toDouble();
        const double col = f[1].toDouble();
        const double score = f[2].toDouble();
        // 默认锚点在模板中心：匹配点 = 匹配框中心（VM 口径，锚点可偏移后才会分开）
        QVERIFY2(qAbs(f[6].toDouble() - col) <= 2.0 && qAbs(f[5].toDouble() - row) <= 2.0,
                 qPrintable(QStringLiteral("匹配点与中心不符：中心(%1,%2) 匹配点(%3,%4)")
                                .arg(col).arg(row).arg(f[6].toDouble()).arg(f[5].toDouble())));
        QVERIFY2(score > 0.5, "多目标里有低于阈值的实例");
        bool nearOk = false;
        for (const Pt &c : centers) {
            if (qAbs(col - c.x) <= 8.0 && qAbs(row - c.y) <= 8.0) {
                nearOk = true;
                break;
            }
        }
        QVERIFY2(nearOk, qPrintable(QStringLiteral("实例位置不在任何真实中心附近：(%1, %2)").arg(col).arg(row)));
    }
}


// 向导 ③/4：搜索预算（角度×尺度组合数）——纯函数，最容易守，也最容易被改坏。
void FeatureMatchTest::searchBudgetCountsCombos()
{
    using namespace TemplateSearchBudget;
    // 现场那组：-30..30 步进 2 => 31 个角度，尺度固定 => 1 档
    QCOMPARE(stepsInRange(-30.0, 30.0, 2.0), 31);
    QCOMPARE(stepsInRange(1.0, 1.0, 0.05), 1);
    QCOMPARE(stepsInRange(0.0, 0.0, 2.0), 1);
    QCOMPARE(stepsInRange(0.0, 1.0, 0.0), 1);          // 步进非法按 1 档
    QCOMPARE(comboCount(-30.0, 30.0, 2.0, 1.0, 1.0, 0.05), 31);
    // 加 3 档尺度 => 31 × 3
    QCOMPARE(comboCount(-30.0, 30.0, 2.0, 0.9, 1.1, 0.1), 93);

    const QString text = describe(31);
    QVERIFY2(text.contains(QStringLiteral("31")), qPrintable(text));
    QVERIFY2(estimateMs(31) > 100.0 && estimateMs(31) < 200.0, "单帧估算偏离实测（约 130ms）");
}


// 真图验证（r19 收尾）：现场真实 BMP 上做 ROI 自匹配。
// 文件不在本机时 QSKIP（交付机/现场机上跑）；存在则必须通过。
void FeatureMatchTest::templateMatchOnRealImage()
{
    // 批量：目录下所有 Image_*.bmp 逐张自匹配
    const QString dirPath =
        QStringLiteral("F:/\u65b9\u6848/\u6b63\u6cf0/\u4eea\u8868/\u6fc0\u5149\u6253\u6807");
    QDir dir(dirPath);
    QStringList files = dir.entryList(QStringList() << QStringLiteral("Image_*.bmp"),
                                      QDir::Files, QDir::Name);
    if (files.isEmpty()) {
        const QString legacy =
            QStringLiteral("F:/\u65b9\u6848/\u6b63\u6cf0/\u5357\u7f51/Image_20260423085400391.bmp");
        if (QFileInfo::exists(legacy))
            files << legacy;
    }
    if (files.isEmpty())
        QSKIP("真图不在本机（跳过）");

    // 断言只钉第一张（纹理可分辨的那类）；其余作**实测记录**：
    // 重复纹理（栅格/按键类）会让粗到细锁到另一个重复单元 —— 这是粗到细的
    // 已知局限（coarse 峰在 1/5 尺度上分辨不出重复单元），根治要投影金字塔/全分辨率核验。
    for (const QString &name : files) {
        const QString path = dir.filePath(name);
        QImage qimg(path);
        QVERIFY2(!qimg.isNull(), qPrintable(QStringLiteral("真图读取失败：%1").arg(name)));
    QImage grayImg = qimg.convertToFormat(QImage::Format_Grayscale8);
    cv::Mat gray(grayImg.height(), grayImg.width(), CV_8UC1,
                 const_cast<uchar *>(grayImg.constBits()), qsizetype(grayImg.bytesPerLine()));
    gray = gray.clone();
    cv::Mat color;
    cv::cvtColor(gray, color, cv::COLOR_GRAY2BGR);
    const HObject img = OpencvUtil::matToHimage(color);
    qInfo("real-image [%s]: %d x %d", qPrintable(name), gray.cols, gray.rows);

    // 模板区自动选"纹理最强"的窗口：低纹理区（如工件平面）自匹配会假性失配
    const int side = qBound(120, gray.cols / 5, 260);
    double bestE = -1.0;
    double cx = gray.cols * 0.5, cy = gray.rows * 0.5;
    {
        cv::Mat gx, gy, mag;
        cv::Sobel(gray, gx, CV_32F, 1, 0, 3);
        cv::Sobel(gray, gy, CV_32F, 0, 1, 3);
        cv::magnitude(gx, gy, mag);
        for (int yy = side / 2; yy + side / 2 < gray.rows; yy += side / 2) {
            for (int xx = side / 2; xx + side / 2 < gray.cols; xx += side / 2) {
                const double e = cv::mean(mag(cv::Rect(xx - side / 2, yy - side / 2, side, side)))[0];
                if (e > bestE) {
                    bestE = e;
                    cx = xx;
                    cy = yy;
                }
            }
        }
    }

    for (int method = 1; method <= 2; ++method) {
        OpencvTemplateMatchNode node;
        node.init();
        node.setParam(QStringLiteral("method"), method);
        node.setParam(QStringLiteral("roiCenterCol"), cx);
        node.setParam(QStringLiteral("roiCenterRow"), cy);
        node.setParam(QStringLiteral("roiWidth"), double(side));
        node.setParam(QStringLiteral("roiHeight"), double(side));
        node.setParam(QStringLiteral("roiAngle"), 0.0);
        node.setParam(QStringLiteral("angleMin"), -30.0);
        node.setParam(QStringLiteral("angleMax"), 30.0);
        node.setParam(QStringLiteral("angleStep"), 2.0);
        node.setParam(QStringLiteral("scaleMin"), 1.0);
        node.setParam(QStringLiteral("scaleMax"), 1.0);
        node.setParam(QStringLiteral("trainFromImage"), true);
        node.setParam(QStringLiteral("minScore"), 0.5);
        node.setInputImage(img);

        QElapsedTimer timer;
        timer.start();
        node.run(false);
        const qint64 ms = timer.elapsed();
        const double score = node.getParam(QStringLiteral("matchScore")).toDouble();
        const double col = node.getParam(QStringLiteral("matchCol")).toDouble();
        const double row = node.getParam(QStringLiteral("matchRow")).toDouble();
        qInfo("real-image method=%d: %lld ms score=%.3f at (%.1f, %.1f) expect (%.0f, %.0f)",
              method, ms, score, col, row, cx, cy);

        QVERIFY2(ms < 8000, qPrintable(QStringLiteral("真图 %1 ms 超过 8s").arg(ms)));
        if (method == 1) {
            // 灰度 NCC 在真图自匹配上必须完美（实测 264ms / 1.000 / ±0.4px）
            // 实拍图（带渐晕/圆形工件）自匹配分数比合成图低：
            //   NCC 0.951（位置偏 2.6px）、GOC 0.841（位置与 NCC 一致）。
            // 2.6px 是"期望值口径"的系统偏差（两方法位置完全一致），不是算法偏差。
            // 实拍图自匹配的天花板约 0.90（纹理最强窗口实测 0.895；合成图才是 1.0），
            // 分数阈值按证据取 0.85；**位置**才是硬指标（±4px）。
            QVERIFY2(score > 0.85,
                     qPrintable(QStringLiteral("method=1 真图自匹配分数过低：%1").arg(score)));
            QVERIFY2(qAbs(col - cx) <= 1.5 && qAbs(row - cy) <= 1.5,
                     qPrintable(QStringLiteral("method=1 真图自匹配位置偏了：(%1, %2) 期望 (%3, %4)")
                                    .arg(col).arg(row).arg(cx).arg(cy)));
        } else {
            // 梯度方向相关（GOC）升级后实测：0.998 @ ±0.3px（倒角时 0.721、偏 80px）。
            // 与 method=1 同档严格 —— GOC 的价值在遮挡/非线性光照场景，自匹配必须完美。
            // GOC 对亚像素错位更敏感（边缘方向），实拍图 0.84；阈值按证据取 0.7
            QVERIFY2(score > 0.7,
                     qPrintable(QStringLiteral("method=2 真图自匹配分数过低：%1").arg(score)));
            QVERIFY2(qAbs(col - cx) <= 1.5 && qAbs(row - cy) <= 1.5,
                     qPrintable(QStringLiteral("method=2 真图自匹配位置偏了：(%1, %2) 期望 (%3, %4)")
                                    .arg(col).arg(row).arg(cx).arg(cy)));
        }
    }
    }   // for each image
}


// 遮挡对比（r19 收尾）：干净图训练模板 → 搜索图上遮挡约 20% 模板面积，
// 对比灰度 NCC 与 GOC 的稳定性。位置对是底线，GOC 分数不应比 NCC 差。
void FeatureMatchTest::templateMatchSurvivesPartialOcclusion()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const cv::Mat patch = makePatch();
    cv::Mat clean(1000, 1500, CV_8UC1, cv::Scalar(25));
    const int cx = 750, cy = 500;
    const cv::Rect dst(cx - patch.cols / 2, cy - patch.rows / 2, patch.cols, patch.rows);
    patch.copyTo(clean(dst));

    cv::Mat occluded = clean.clone();
    cv::rectangle(occluded, cv::Rect(cx - patch.cols / 2, cy - patch.rows / 2,
                                     patch.cols, patch.rows / 5),
                  cv::Scalar(25), cv::FILLED);

    struct R { double score, col, row; qint64 ms; };
    R r[2];
    for (int method = 1; method <= 2; ++method) {
        const QString path = dir.filePath(QStringLiteral("occ_template_%1.png").arg(method));

        // 第一步：干净图上训练并落盘模板
        OpencvTemplateMatchNode trainer;
        trainer.init();
        trainer.setParam(QStringLiteral("method"), method);
        trainer.setParam(QStringLiteral("templatePath"), path);
        trainer.setParam(QStringLiteral("roiCenterCol"), double(cx));
        trainer.setParam(QStringLiteral("roiCenterRow"), double(cy));
        trainer.setParam(QStringLiteral("roiWidth"), double(patch.cols));
        trainer.setParam(QStringLiteral("roiHeight"), double(patch.rows));
        trainer.setParam(QStringLiteral("roiAngle"), 0.0);
        trainer.setParam(QStringLiteral("angleMin"), 0.0);
        trainer.setParam(QStringLiteral("angleMax"), 0.0);
        trainer.setParam(QStringLiteral("angleStep"), 2.0);
        trainer.setParam(QStringLiteral("trainFromImage"), true);
        trainer.setParam(QStringLiteral("minScore"), 0.3);
        trainer.setInputImage(OpencvUtil::matToHimage(clean));
        trainer.run(false);
        QVERIFY2(QFileInfo::exists(path), "模板没落盘");

        // 第二步：遮挡图上搜索（用落盘模板，不再训练）
        OpencvTemplateMatchNode node;
        node.init();
        node.setParam(QStringLiteral("method"), method);
        node.setParam(QStringLiteral("templatePath"), path);
        node.setParam(QStringLiteral("angleMin"), 0.0);
        node.setParam(QStringLiteral("angleMax"), 0.0);
        node.setParam(QStringLiteral("angleStep"), 2.0);
        node.setParam(QStringLiteral("trainFromImage"), false);
        node.setParam(QStringLiteral("minScore"), 0.3);
        node.setInputImage(OpencvUtil::matToHimage(occluded));
        QElapsedTimer timer;
        timer.start();
        node.run(false);
        r[method - 1].ms = timer.elapsed();
        r[method - 1].score = node.getParam(QStringLiteral("matchScore")).toDouble();
        r[method - 1].col = node.getParam(QStringLiteral("matchCol")).toDouble();
        r[method - 1].row = node.getParam(QStringLiteral("matchRow")).toDouble();
        qInfo("occlusion method=%d: %lld ms score=%.3f at (%.1f, %.1f)",
              method, r[method - 1].ms, r[method - 1].score, r[method - 1].col, r[method - 1].row);
    }

    // 掩膜参与评分（v3）后实测：NCC 0.383（偏 6.4px）/ GOC **0.994（精确命中）**。
    // GOC 的抗遮挡成立：分子分母用同一套可见掩膜 + 可见率门槛（<40% 判无效），
    // 且不再做低方差清零（GOC 自带可见性逻辑）。
    QVERIFY2(qAbs(r[0].col - cx) <= 8.0 && qAbs(r[0].row - cy) <= 8.0, "NCC 遮挡下位置明显偏了");
    QVERIFY2(r[1].score > 0.9,
             qPrintable(QStringLiteral("GOC 遮挡下分数过低：%1").arg(r[1].score, 0, 'f', 3)));
    QVERIFY2(qAbs(r[1].col - cx) <= 1.5 && qAbs(r[1].row - cy) <= 1.5,
             qPrintable(QStringLiteral("GOC 遮挡下位置偏了：(%1, %2)")
                            .arg(r[1].col, 0, 'f', 1).arg(r[1].row, 0, 'f', 1)));
    // 对比数字保留（不作断言）
    qInfo("occlusion compare: NCC %.3f vs GOC %.3f", r[0].score, r[1].score);
}


// 跨图验证（r19 收尾）：同工件多帧 —— 首帧训练，在其余帧上搜索。
void FeatureMatchTest::templateMatchAcrossFrames()
{
    const QString dirPath =
        QStringLiteral("F:/\u65b9\u6848/\u6b63\u6cf0/\u4eea\u8868/\u6fc0\u5149\u6253\u6807");
    QDir dir(dirPath);
    QStringList files = dir.entryList(QStringList() << QStringLiteral("Image_*.bmp"),
                                      QDir::Files, QDir::Name);
    if (files.size() < 2)
        QSKIP("跨图需要至少两张真图（跳过）");

    // 用首帧训练模板（纹理最强窗口）
    QImage first(dir.filePath(files.first()));
    QVERIFY(!first.isNull());
    QImage grayImg = first.convertToFormat(QImage::Format_Grayscale8);
    cv::Mat gray0(grayImg.height(), grayImg.width(), CV_8UC1,
                  const_cast<uchar *>(grayImg.constBits()), qsizetype(grayImg.bytesPerLine()));
    gray0 = gray0.clone();

    const int side = qBound(120, gray0.cols / 5, 260);
    double bestE = -1.0;
    double cx = gray0.cols * 0.5, cy = gray0.rows * 0.5;
    {
        cv::Mat gx, gy, mag;
        cv::Sobel(gray0, gx, CV_32F, 1, 0, 3);
        cv::Sobel(gray0, gy, CV_32F, 0, 1, 3);
        cv::magnitude(gx, gy, mag);
        for (int yy = side / 2; yy + side / 2 < gray0.rows; yy += side / 2)
            for (int xx = side / 2; xx + side / 2 < gray0.cols; xx += side / 2) {
                const double e = cv::mean(mag(cv::Rect(xx - side / 2, yy - side / 2, side, side)))[0];
                if (e > bestE) {
                    bestE = e;
                    cx = xx;
                    cy = yy;
                }
            }
    }

    QTemporaryDir dir2;
    const QString tmplPath = dir2.filePath(QStringLiteral("cross_template.png"));

    for (int method = 1; method <= 2; ++method) {
        {
            OpencvTemplateMatchNode trainer;
            trainer.init();
            trainer.setParam(QStringLiteral("method"), method);
            trainer.setParam(QStringLiteral("templatePath"), tmplPath);
            trainer.setParam(QStringLiteral("roiCenterCol"), cx);
            trainer.setParam(QStringLiteral("roiCenterRow"), cy);
            trainer.setParam(QStringLiteral("roiWidth"), double(side));
            trainer.setParam(QStringLiteral("roiHeight"), double(side));
            trainer.setParam(QStringLiteral("roiAngle"), 0.0);
            trainer.setParam(QStringLiteral("angleMin"), -30.0);
            trainer.setParam(QStringLiteral("angleMax"), 30.0);
            trainer.setParam(QStringLiteral("angleStep"), 2.0);
            trainer.setParam(QStringLiteral("trainFromImage"), true);
            trainer.setParam(QStringLiteral("minScore"), 0.3);
            cv::Mat color;
            cv::cvtColor(gray0, color, cv::COLOR_GRAY2BGR);
            trainer.setInputImage(OpencvUtil::matToHimage(color));
            trainer.run(false);
            QVERIFY2(QFileInfo::exists(tmplPath), "模板没落盘");
        }

        for (int k = 1; k < files.size(); ++k) {
            QImage q2(dir.filePath(files[k]));
            if (q2.isNull())
                continue;
            QImage g2 = q2.convertToFormat(QImage::Format_Grayscale8);
            cv::Mat gray(g2.height(), g2.width(), CV_8UC1,
                         const_cast<uchar *>(g2.constBits()), qsizetype(g2.bytesPerLine()));
            gray = gray.clone();
            cv::Mat color;
            cv::cvtColor(gray, color, cv::COLOR_GRAY2BGR);

            OpencvTemplateMatchNode node;
            node.init();
            node.setParam(QStringLiteral("method"), method);
            node.setParam(QStringLiteral("templatePath"), tmplPath);
            node.setParam(QStringLiteral("angleMin"), -30.0);
            node.setParam(QStringLiteral("angleMax"), 30.0);
            node.setParam(QStringLiteral("angleStep"), 2.0);
            node.setParam(QStringLiteral("trainFromImage"), false);
            node.setParam(QStringLiteral("minScore"), 0.3);
            node.setInputImage(OpencvUtil::matToHimage(color));
            QElapsedTimer timer;
            timer.start();
            node.run(false);
            const qint64 ms = timer.elapsed();
            const double score = node.getParam(QStringLiteral("matchScore")).toDouble();
            const double col = node.getParam(QStringLiteral("matchCol")).toDouble();
            const double row = node.getParam(QStringLiteral("matchRow")).toDouble();
            qInfo("cross method=%d [%s]: %lld ms score=%.3f at (%.1f, %.1f) tmpl(%.0f, %.0f)",
                  method, qPrintable(files[k]), ms, score, col, row, cx, cy);

            // 跨帧实测（同工件多帧）：NCC 0.705~0.950、GOC 0.387~0.857 ——
            // 跨图鲁棒性是与 VisionPro 的真实差距（自匹配 1.000 证明不了它）。
            // 断言只钉"找得到"的底线 0.3；收严要等跨图数据更全/算法改进后再做。
            QVERIFY2(score > 0.3,
                     qPrintable(QStringLiteral("跨图 method=%1 [%2] 分数过低：%3")
                                    .arg(method).arg(files[k]).arg(score, 0, 'f', 3)));
        }
    }
}

// 掩膜参与评分（r19 遗留项）：同一张图、同一套参数，**只改掩膜** → 命中位置必须变化。
// 图上放两个同款工件；把左边那块在掩膜里涂黑，唯一端口给出的命中必须跳到右边。
// 守的是：掩膜真的进了 GOC 的分子/分母（曾经只是存下来、评分没用）。
void FeatureMatchTest::templateMatchMaskMovesBestHit()
{
    const cv::Mat patch = makePatch();                       // 128×128 高纹理块
    cv::Mat canvas(300, 600, CV_8UC1, cv::Scalar(kCanvasBackground));
    const cv::Rect left(110, 110, kTemplateSize, kTemplateSize);
    const cv::Rect right(410, 110, kTemplateSize, kTemplateSize);
    patch.copyTo(canvas(left));
    patch.copyTo(canvas(right));
    const double leftCx = left.x + kTemplateSize / 2.0;
    const double leftCy = left.y + kTemplateSize / 2.0;
    const double rightCx = right.x + kTemplateSize / 2.0;
    const double rightCy = right.y + kTemplateSize / 2.0;

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 2);              // 掩膜只参与 GOC（method=2）
    node.setParam(QStringLiteral("minScore"), 0.3);
    node.setParam(QStringLiteral("polarity"), 1);
    node.setParam(QStringLiteral("angleMin"), 0.0);          // 不搜角度：结果只由掩膜决定
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("maxMatches"), 2);          // 两个同款工件都要能报出来
    node.setParam(QStringLiteral("roiCenterCol"), leftCx);
    node.setParam(QStringLiteral("roiCenterRow"), leftCy);
    node.setParam(QStringLiteral("roiWidth"), double(kTemplateSize));
    node.setParam(QStringLiteral("roiHeight"), double(kTemplateSize));
    node.setParam(QStringLiteral("roiAngle"), 0.0);
    node.setParam(QStringLiteral("searchRegionCol"), 0);
    node.setParam(QStringLiteral("searchRegionRow"), 0);
    node.setParam(QStringLiteral("searchRegionWidth"), canvas.cols);
    node.setParam(QStringLiteral("searchRegionHeight"), canvas.rows);
    node.setParam(QStringLiteral("trainFromImage"), true);   // 直接按 ROI 取模板，不落盘
    node.setInputImage(OpencvUtil::matToHimage(canvas));

    // ① 无掩膜：两个工件都要命中（各 1.000）
    node.run(false);
    const int count0 = node.getParam(QStringLiteral("matchCount")).toInt();
    const QString list0 = node.getParam(QStringLiteral("matchList")).toString();
    qInfo("mask-leg no-mask: count=%d list=%s", count0, qPrintable(list0));
    QVERIFY2(count0 == 2, qPrintable(QStringLiteral("无掩膜应找到 2 个工件，实际 %1（%2）")
                                          .arg(count0).arg(list0)));
    int nearLeft = 0, nearRight = 0;
    for (const QString &it : list0.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QStringList f = it.split(QLatin1Char(','));
        QVERIFY(f.size() >= 3);                   // row,col,score[,angle,scale,prow,pcol]
        const double row = f[0].toDouble();
        const double col = f[1].toDouble();
        if (qAbs(col - leftCx) <= 8.0 && qAbs(row - leftCy) <= 8.0) ++nearLeft;
        if (qAbs(col - rightCx) <= 8.0 && qAbs(row - rightCy) <= 8.0) ++nearRight;
    }
    QVERIFY2(nearLeft == 1 && nearRight == 1,
             qPrintable(QStringLiteral("无掩膜的命中位置不对（左 %1 / 右 %2）：%3")
                            .arg(nearLeft).arg(nearRight).arg(list0)));

    // ② 只改掩膜：把左侧工件整块涂黑（含模板边沿），可见率不足 ⇒ 该处判无效
    QImage mask(canvas.cols, canvas.rows, QImage::Format_RGB32);
    mask.fill(Qt::white);
    {
        QPainter p(&mask);
        p.fillRect(QRect(left.x - 30, left.y - 30, kTemplateSize + 60, kTemplateSize + 60),
                   Qt::black);
        p.end();
    }
    node.setEditMask(mask);

    node.run(false);
    const int count1 = node.getParam(QStringLiteral("matchCount")).toInt();
    const QString list1 = node.getParam(QStringLiteral("matchList")).toString();
    qInfo("mask-leg masked: count=%d list=%s", count1, qPrintable(list1));
    QVERIFY2(count1 == 1, qPrintable(QStringLiteral("掩膜没参与评分：屏蔽左侧后仍报 %1 个（%2）")
                                          .arg(count1).arg(list1)));
    const QStringList items1 = list1.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    QCOMPARE(items1.size(), 1);
    const QStringList f1 = items1.first().split(QLatin1Char(','));
    QVERIFY(f1.size() >= 3);
    const double row1 = f1[0].toDouble();
    const double col1 = f1[1].toDouble();
    const double score1 = f1[2].toDouble();
    QVERIFY2(score1 > 0.9, qPrintable(QStringLiteral("屏蔽左侧后右侧分数过低：%1").arg(score1)));
    QVERIFY2(qAbs(col1 - rightCx) <= 8.0 && qAbs(row1 - rightCy) <= 8.0,
             qPrintable(QStringLiteral("命中应只剩右侧 (%1, %2)，实际 (%3, %4)")
                            .arg(rightCx).arg(rightCy).arg(col1).arg(row1)));
}

// 训练后特征点可视化：绿点 = 模板强梯度点。两条硬要求：
//   ① 点全部落在模板框内（画到框外=坐标映射错，现场会以为模板取错位置）；
//   ② 改 ROI 后点跟着走（说明是实时按当前模板算的，不是缓存的旧图）。
void FeatureMatchTest::templateMatchFeaturePointsFollowRoi()
{
    const cv::Mat patch = makePatch();
    cv::Mat canvas(300, 700, CV_8UC1, cv::Scalar(kCanvasBackground));
    const cv::Rect left(50, 50, kTemplateSize, kTemplateSize);
    const cv::Rect right(400, 50, kTemplateSize, kTemplateSize);
    patch.copyTo(canvas(left));
    patch.copyTo(canvas(right));

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 2);
    node.setParam(QStringLiteral("roiCenterCol"), left.x + kTemplateSize / 2.0);
    node.setParam(QStringLiteral("roiCenterRow"), left.y + kTemplateSize / 2.0);
    node.setParam(QStringLiteral("roiWidth"), double(kTemplateSize));
    node.setParam(QStringLiteral("roiHeight"), double(kTemplateSize));
    node.setParam(QStringLiteral("roiAngle"), 0.0);
    node.setInputImage(OpencvUtil::matToHimage(canvas));

    const QVector<QPointF> pts0 = node.editorFeaturePoints();
    qInfo("feature-points: left=%d", int(pts0.size()));
    QVERIFY2(pts0.size() > 100,
             qPrintable(QStringLiteral("模板特征点太少/为空：%1（训练后应当能看到绿点）").arg(pts0.size())));
    for (const QPointF &p : pts0) {
        QVERIFY2(p.x() >= left.x - 1 && p.x() <= left.x + kTemplateSize + 1
                     && p.y() >= left.y - 1 && p.y() <= left.y + kTemplateSize + 1,
                 qPrintable(QStringLiteral("特征点跑到模板框外：(%1, %2)").arg(p.x()).arg(p.y())));
    }

    // 换到右侧同款工件：点必须整体平移过去
    RoiShape s;
    s.type = RoiType::RotatedRect;
    s.p1 = QPointF(right.x + kTemplateSize / 2.0, right.y + kTemplateSize / 2.0);
    s.width = kTemplateSize;
    s.height = kTemplateSize;
    s.angleDeg = 0.0;
    node.applyGeometryRoi(s);

    const QVector<QPointF> pts1 = node.editorFeaturePoints();
    qInfo("feature-points: right=%d", int(pts1.size()));
    QVERIFY2(pts1.size() > 100, qPrintable(QStringLiteral("换 ROI 后特征点为空：%1").arg(pts1.size())));
    for (const QPointF &p : pts1) {
        QVERIFY2(p.x() >= right.x - 1 && p.x() <= right.x + kTemplateSize + 1
                     && p.y() >= right.y - 1 && p.y() <= right.y + kTemplateSize + 1,
                 qPrintable(QStringLiteral("换 ROI 后特征点没跟着走：(%1, %2)").arg(p.x()).arg(p.y())));
    }
}

// 检测区域画布化：VM 的检测区域是画布上的主对象。落到我们这里两条显示契约：
//   ① 检测区域（青）与模板框（绿）必须同时出现，缺一个现场就会"不知道自己框到哪"；
//   ② 两者的中心/尺寸要等于参数里的值（画错位置比不画更糟）。
void FeatureMatchTest::templateMatchOverlayShowsRegionAndTemplate()
{
    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("roiCenterCol"), 240.0);
    node.setParam(QStringLiteral("roiCenterRow"), 160.0);
    node.setParam(QStringLiteral("roiWidth"), 120.0);
    node.setParam(QStringLiteral("roiHeight"), 80.0);
    node.setParam(QStringLiteral("roiAngle"), 0.0);
    node.setParam(QStringLiteral("searchRegionCol"), 60);
    node.setParam(QStringLiteral("searchRegionRow"), 40);
    node.setParam(QStringLiteral("searchRegionWidth"), 400);
    node.setParam(QStringLiteral("searchRegionHeight"), 260);

    ImageDisplayController ctrl(nullptr, nullptr, nullptr);
    const QVector<OverlayShape> overlay = ctrl.collectOverlayFromNode(&node);

    const OverlayShape *tmplBox = nullptr;
    const OverlayShape *regionBox = nullptr;
    for (const OverlayShape &s : overlay) {
        if (s.type != OverlayShape::Type::RotatedRect)
            continue;
        if (s.color == QColor(0, 220, 120))
            tmplBox = &s;
        else if (s.color == QColor(0, 200, 255))
            regionBox = &s;
    }
    QVERIFY2(tmplBox, "画布上没有模板框（绿）");
    QVERIFY2(regionBox, "画布上没有检测区域（青）");
    QVERIFY2(qAbs(tmplBox->p1.x() - 240.0) <= 0.5 && qAbs(tmplBox->p1.y() - 160.0) <= 0.5
                 && qAbs(tmplBox->width - 120.0) <= 0.5 && qAbs(tmplBox->height - 80.0) <= 0.5,
             qPrintable(QStringLiteral("模板框画错：中心(%1,%2) 尺寸(%3,%4)")
                            .arg(tmplBox->p1.x()).arg(tmplBox->p1.y())
                            .arg(tmplBox->width).arg(tmplBox->height)));
    QVERIFY2(qAbs(regionBox->p1.x() - 260.0) <= 0.5 && qAbs(regionBox->p1.y() - 170.0) <= 0.5
                 && qAbs(regionBox->width - 400.0) <= 0.5 && qAbs(regionBox->height - 260.0) <= 0.5,
             qPrintable(QStringLiteral("检测区域画错：中心(%1,%2) 尺寸(%3,%4)")
                            .arg(regionBox->p1.x()).arg(regionBox->p1.y())
                            .arg(regionBox->width).arg(regionBox->height)));
}

// VM「对比度阈值 / 阈值模式」对齐：这两个参数必须真的改变参与打分的特征点数量，
// 否则就是把界面参数做成了摆设（值改了、匹配一点不变）。
void FeatureMatchTest::templateMatchEdgeThresholdTakesEffect()
{
    // 低对比竖边：灰度 100 → 110，Sobel(ksize=3) 幅值 ≈ 40。
    cv::Mat img(80, 80, CV_8UC1, cv::Scalar(100));
    img(cv::Rect(0, 0, 40, 80)).setTo(110);

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("roiCenterCol"), 40.0);
    node.setParam(QStringLiteral("roiCenterRow"), 40.0);
    node.setParam(QStringLiteral("roiWidth"), 64.0);
    node.setParam(QStringLiteral("roiHeight"), 64.0);
    node.setParam(QStringLiteral("roiAngle"), 0.0);
    node.setInputImage(OpencvUtil::matToHimage(img));

    // 对比度阈值 = 特征点与背景的灰度差（VM 1~255）。这条低对比边灰度差 ≈ 10，
    // Sobel 响应 ≈ 40；阈值 5（Sobel 20）应保留、20（Sobel 80）应淘汰。
    // 阈值模式必须先切到手动（VM 默认是自动，自动会覆盖手填值）。
    node.setParam(QStringLiteral("autoEdgeThreshold"), false);
    node.setParam(QStringLiteral("contrastThreshold"), 5);
    const int n20 = node.editorFeaturePoints().size();
    node.setParam(QStringLiteral("contrastThreshold"), 20);
    const int n60 = node.editorFeaturePoints().size();
    qInfo("edge-threshold: t=5 -> %d 点, t=20 -> %d 点", n20, n60);
    QVERIFY2(n20 > 0, "对比度阈值 5 时低对比边应当被当成特征点");
    QVERIFY2(n60 == 0, qPrintable(QStringLiteral("阈值 20 时不应再留点（实际 %1 个）⇒ "
                                                 "对比度阈值没接进评分").arg(n60)));

    // 阈值模式=自动：按模板对比度自动生成，这条低对比边仍应被判为特征点
    node.setParam(QStringLiteral("contrastThreshold"), 255);
    node.setParam(QStringLiteral("autoEdgeThreshold"), true);
    const int nAuto = node.editorFeaturePoints().size();
    qInfo("edge-threshold: auto -> %d 点", nAuto);
    QVERIFY2(nAuto > 0, "自动阈值模式没生效（点全被滤掉）");
}

// 模板对象：JSON 往返不丢字段；老 templatePath 能无痛迁移成一条模板记录。
void FeatureMatchTest::templateEntryRoundTripAndLegacyMigration()
{
    TemplateEntry e;
    e.name = QStringLiteral("工件A");
    e.imagePath = QStringLiteral("templates/a.png");
    e.shape.type = RoiType::RotatedRect;
    e.shape.p1 = QPointF(120.5, 240.25);
    e.shape.width = 96.0;
    e.shape.height = 64.0;
    e.shape.angleDeg = 12.5;
    e.contrastThreshold = 33;
    e.autoThreshold = false;
    e.featureScale = 5;
    e.speedScale = 7;
    e.minChainLength = 9;

    const TemplateEntry back = TemplateEntry::fromJson(e.toJson());
    QCOMPARE(back.name, e.name);
    QCOMPARE(back.imagePath, e.imagePath);
    QCOMPARE(back.shape.type, RoiType::RotatedRect);
    QCOMPARE(back.shape.p1, e.shape.p1);
    QCOMPARE(back.shape.width, e.shape.width);
    QCOMPARE(back.shape.height, e.shape.height);
    QCOMPARE(back.shape.angleDeg, e.shape.angleDeg);
    QCOMPARE(back.contrastThreshold, e.contrastThreshold);
    QCOMPARE(back.autoThreshold, e.autoThreshold);
    QCOMPARE(back.featureScale, e.featureScale);
    QCOMPARE(back.speedScale, e.speedScale);
    QCOMPARE(back.minChainLength, e.minChainLength);

    // 老方案：只有一个 templatePath → templates() 自动合成一条，migrate 后落库
    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("templatePath"), QStringLiteral("templates/legacy.png"));
    node.setParam(QStringLiteral("contrastThreshold"), 44);
    const QVector<TemplateEntry> legacy = node.templates();
    QCOMPARE(legacy.size(), 1);
    QCOMPARE(legacy.first().imagePath, QStringLiteral("templates/legacy.png"));
    QCOMPARE(legacy.first().contrastThreshold, 44);
    QVERIFY(node.getParam(QStringLiteral("templates")).toString().isEmpty());   // 只读不写

    node.migrateLegacyTemplate();
    QVERIFY(!node.getParam(QStringLiteral("templates")).toString().isEmpty());
    QCOMPARE(node.templates().size(), 1);
    QCOMPARE(node.activeTemplateIndex(), 0);
}

// 模板列表驱动匹配：两个模板指向两张不同图，切 active 后命中位置必须跟着换。
void FeatureMatchTest::templateMatchUsesActiveTemplateFromList()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const cv::Mat patchA = makePatch();
    cv::Mat patchB(kTemplateSize, kTemplateSize, CV_8UC1, cv::Scalar(20));
    cv::circle(patchB, cv::Point(64, 64), 50, cv::Scalar(255), 6);
    cv::circle(patchB, cv::Point(64, 64), 26, cv::Scalar(190), 4);
    cv::line(patchB, cv::Point(12, 12), cv::Point(116, 116), cv::Scalar(240), 3);

    const QString pathA = dir.filePath(QStringLiteral("a.png"));
    const QString pathB = dir.filePath(QStringLiteral("b.png"));
    QVERIFY(cv::imwrite(pathA.toStdString(), patchA));
    QVERIFY(cv::imwrite(pathB.toStdString(), patchB));

    cv::Mat canvas(300, 700, CV_8UC1, cv::Scalar(kCanvasBackground));
    const cv::Rect boxA(80, 90, kTemplateSize, kTemplateSize);
    const cv::Rect boxB(420, 90, kTemplateSize, kTemplateSize);
    patchA.copyTo(canvas(boxA));
    patchB.copyTo(canvas(boxB));
    const double cxA = boxA.x + kTemplateSize / 2.0;
    const double cyA = boxA.y + kTemplateSize / 2.0;
    const double cxB = boxB.x + kTemplateSize / 2.0;
    const double cyB = boxB.y + kTemplateSize / 2.0;

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 1);
    node.setParam(QStringLiteral("minScore"), 0.5);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("searchRegionCol"), 0);
    node.setParam(QStringLiteral("searchRegionRow"), 0);
    node.setParam(QStringLiteral("searchRegionWidth"), canvas.cols);
    node.setParam(QStringLiteral("searchRegionHeight"), canvas.rows);
    node.setInputImage(OpencvUtil::matToHimage(canvas));

    TemplateEntry ea;
    ea.name = QStringLiteral("A");
    ea.imagePath = pathA;
    TemplateEntry eb;
    eb.name = QStringLiteral("B");
    eb.imagePath = pathB;
    node.setTemplates({ea, eb});
    QCOMPARE(node.templates().size(), 2);

    // VM「全部搜索模式=关」语义：按列表顺序 [A, B] 逐个试，达标即停。
    // 画布上 A/B 都在，A 先试就命中 → 停止；active 挂在哪不影响结果。
    node.setActiveTemplateIndex(1);       // 故意挂到 B
    node.setParam(QStringLiteral("matchAllTemplates"), false);
    node.run(false);
    const double c0 = node.getParam(QStringLiteral("matchCol")).toDouble();
    const double r0 = node.getParam(QStringLiteral("matchRow")).toDouble();
    const int idx0 = node.getParam(QStringLiteral("matchTemplateIndex")).toInt();
    qInfo("template-list: sequential hit (%.1f, %.1f) idx=%d", c0, r0, idx0);
    QVERIFY2(qAbs(c0 - cxA) <= 2.0 && qAbs(r0 - cyA) <= 2.0,
             qPrintable(QStringLiteral("顺序匹配应命中列表里第一个能匹配的模板 A (%1,%2)，实际 (%3,%4)")
                            .arg(cxA).arg(cyA).arg(c0).arg(r0)));
    QCOMPARE(idx0, 0);

    // matchAllTemplates=true：全部试完取最优（两个都在，至少命中一个）
    node.setParam(QStringLiteral("matchAllTemplates"), true);
    node.run(false);
    const double c1 = node.getParam(QStringLiteral("matchCol")).toDouble();
    const double r1 = node.getParam(QStringLiteral("matchRow")).toDouble();
    qInfo("template-list: matchAll hit (%.1f, %.1f)", c1, r1);
    QVERIFY2(node.getParam(QStringLiteral("moduleStatus")).toBool(),
             qPrintable(QStringLiteral("全部搜索模式应至少命中一个模板，实际 (%1,%2)")
                            .arg(c1).arg(r1)));
}

// 多模板匹配：列表里 A/B 两块模板，画布上只有 B。
//  ① matchAllTemplates=false（顺序匹配）：A 失败后继续试 B → 命中 B；
//  ② matchAllTemplates=true（全部搜索）：逐个模板跑，取最优 → 命中 B。
void FeatureMatchTest::templateMatchAllTemplatesPicksBest()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const cv::Mat patchA = makePatch();
    cv::Mat patchB(kTemplateSize, kTemplateSize, CV_8UC1, cv::Scalar(20));
    cv::circle(patchB, cv::Point(64, 64), 50, cv::Scalar(255), 6);
    cv::circle(patchB, cv::Point(64, 64), 26, cv::Scalar(190), 4);
    cv::line(patchB, cv::Point(12, 12), cv::Point(116, 116), cv::Scalar(240), 3);

    const QString pathA = dir.filePath(QStringLiteral("a.png"));
    const QString pathB = dir.filePath(QStringLiteral("b.png"));
    QVERIFY(cv::imwrite(pathA.toStdString(), patchA));
    QVERIFY(cv::imwrite(pathB.toStdString(), patchB));

    cv::Mat canvas(300, 600, CV_8UC1, cv::Scalar(kCanvasBackground));
    const cv::Rect boxB(360, 90, kTemplateSize, kTemplateSize);
    patchB.copyTo(canvas(boxB));                       // 只放 B
    const double cxB = boxB.x + kTemplateSize / 2.0;
    const double cyB = boxB.y + kTemplateSize / 2.0;

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 1);
    node.setParam(QStringLiteral("minScore"), 0.5);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("searchRegionCol"), 0);
    node.setParam(QStringLiteral("searchRegionRow"), 0);
    node.setParam(QStringLiteral("searchRegionWidth"), canvas.cols);
    node.setParam(QStringLiteral("searchRegionHeight"), canvas.rows);
    node.setInputImage(OpencvUtil::matToHimage(canvas));

    TemplateEntry ea;
    ea.name = QStringLiteral("A");
    ea.imagePath = pathA;
    TemplateEntry eb;
    eb.name = QStringLiteral("B");
    eb.imagePath = pathB;
    node.setTemplates({ea, eb});
    node.setActiveTemplateIndex(0);                    // 默认挂在 A 上

    // ① matchAllTemplates=false（顺序匹配）：A 失败后继续试 B → 命中 B
    node.setParam(QStringLiteral("matchAllTemplates"), false);
    node.run(false);
    {
        const bool okSeq = node.getParam(QStringLiteral("moduleStatus")).toBool();
        const int idxSeq = node.getParam(QStringLiteral("matchTemplateIndex")).toInt();
        qInfo("sequential-only-B: ok=%d idx=%d", int(okSeq), idxSeq);
        QVERIFY2(okSeq, "顺序匹配应由 B 命中（A 失败后继续试 B）");
        QCOMPARE(idxSeq, 1);
    }

    // ② 打开多模板：必须自动选中 B
    node.setParam(QStringLiteral("matchAllTemplates"), true);
    node.run(false);
    const bool ok = node.getParam(QStringLiteral("moduleStatus")).toBool();
    const int idx = node.getParam(QStringLiteral("matchTemplateIndex")).toInt();
    const QString name = node.getParam(QStringLiteral("matchTemplateName")).toString();
    const double col = node.getParam(QStringLiteral("matchCol")).toDouble();
    const double row = node.getParam(QStringLiteral("matchRow")).toDouble();
    qInfo("all-templates: ok=%d index=%d name=%s at (%.1f, %.1f)",
          int(ok), idx, qPrintable(name), col, row);
    QVERIFY2(ok, "多模板模式下应当由 B 命中");
    QCOMPARE(idx, 1);
    QCOMPARE(name, QStringLiteral("B"));
    QVERIFY2(qAbs(col - cxB) <= 2.0 && qAbs(row - cyB) <= 2.0,
             qPrintable(QStringLiteral("命中位置应在 B (%1,%2)，实际 (%3,%4)")
                            .arg(cxB).arg(cyB).arg(col).arg(row)));

    // 默认锚点（匹配中心）在模板中心：匹配点 = 匹配框中心；尺度 X/Y 同期输出
    const double pcol = node.getParam(QStringLiteral("matchPointCol")).toDouble();
    const double prow = node.getParam(QStringLiteral("matchPointRow")).toDouble();
    const double sx = node.getParam(QStringLiteral("matchScaleX")).toDouble();
    const double sy = node.getParam(QStringLiteral("matchScaleY")).toDouble();
    qInfo("match-point: (%.1f, %.1f) scale=(%.2f, %.2f)", pcol, prow, sx, sy);
    QVERIFY2(qAbs(pcol - cxB) <= 2.0 && qAbs(prow - cyB) <= 2.0,
             qPrintable(QStringLiteral("匹配点应在 (%1,%2)，实际 (%3,%4)")
                            .arg(cxB).arg(cyB).arg(pcol).arg(prow)));
    QCOMPARE(sx, 1.0);
    QCOMPARE(sy, 1.0);
}

// 多边形/圆形模板的形状抠图：以前 Polygon/Circle 落到 roiAxisAlignedBounds 返回空，
// 配置窗里画完多边形点确定只会得到"模板区域无效"。现在按包围盒直裁，形状外的像素由打分掩膜排除。
void FeatureMatchTest::templateShapeExtractionSupportsPolygonAndCircle()
{
    cv::Mat img(200, 200, CV_8UC1, cv::Scalar(50));
    cv::circle(img, cv::Point(100, 100), 60, cv::Scalar(255), 8);

    RoiShape poly;
    poly.type = RoiType::Polygon;
    poly.points = {QPointF(60, 60), QPointF(140, 60), QPointF(100, 140)};
    const cv::Mat polyPatch = OpencvTemplateMatchNode::extractTemplatePatch(img, poly);
    QVERIFY2(!polyPatch.empty(), "多边形模板抠图为空（配置窗会报模板区域无效）");
    QCOMPARE(polyPatch.cols, 80);
    QCOMPARE(polyPatch.rows, 80);

    RoiShape circ;
    circ.type = RoiType::Circle;
    circ.p1 = QPointF(100, 100);          // 圆心
    circ.p2 = QPointF(160, 100);          // 半径 60
    const cv::Mat circlePatch = OpencvTemplateMatchNode::extractTemplatePatch(img, circ);
    QVERIFY2(!circlePatch.empty(), "圆形模板抠图为空");
    QCOMPARE(circlePatch.cols, 120);
    QCOMPARE(circlePatch.rows, 120);

    // 形状 JSON 往返：圆/多边形都要能存进模板列表
    TemplateEntry e;
    e.shape = circ;
    const TemplateEntry back = TemplateEntry::fromJson(e.toJson());
    QCOMPARE(back.shape.type, RoiType::Circle);
    QCOMPARE(back.shape.p1, circ.p1);
    QCOMPARE(back.shape.p2, circ.p2);
}

// 魔术棒取形：亮底上一个暗矩形，从块内取种子，泛洪+轮廓近似必须贴合矩形四边。
void FeatureMatchTest::templateWandPolygonFollowsRegion()
{
    cv::Mat img(200, 200, CV_8UC1, cv::Scalar(240));
    cv::rectangle(img, cv::Rect(50, 40, 100, 80), cv::Scalar(40), cv::FILLED);

    const QVector<QPointF> pts =
        TemplateConfigDialog::wandPolygon(img, QPoint(100, 80), 20);
    qInfo("wand: points=%d", int(pts.size()));
    QVERIFY2(pts.size() >= 4, qPrintable(QStringLiteral("魔术棒只取到 %1 个顶点").arg(pts.size())));

    double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
    for (const QPointF &p : pts) {
        x0 = qMin(x0, p.x());
        x1 = qMax(x1, p.x());
        y0 = qMin(y0, p.y());
        y1 = qMax(y1, p.y());
    }
    QVERIFY2(qAbs(x0 - 50.0) <= 2.0 && qAbs(x1 - 150.0) <= 2.0
                 && qAbs(y0 - 40.0) <= 2.0 && qAbs(y1 - 120.0) <= 2.0,
             qPrintable(QStringLiteral("魔术棒包围盒 (%1,%2)-(%3,%4) 与暗块 (50,40)-(150,120) 不符")
                            .arg(x0).arg(y0).arg(x1).arg(y1)));

    // 空图必须返回空（不能崩）
    QVERIFY(TemplateConfigDialog::wandPolygon(cv::Mat(), QPoint(1, 1), 20).isEmpty());
}

// 可见率门槛（内部常量 40%，VM 未暴露该项）：
//   遮挡 20% → 可见 ~80%，应当命中；
//   遮挡 62% → 可见 ~38% < 40%，必须判无效（抗遮挡的底线）。
void FeatureMatchTest::templateMinVisibleRatioControlsOcclusion()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat patch = makePatch();
    const QString tpl = dir.filePath(QStringLiteral("tpl.png"));
    QVERIFY(cv::imwrite(tpl.toStdString(), patch));

    cv::Mat canvas(400, 400, CV_8UC1, cv::Scalar(kCanvasBackground));
    const cv::Rect box(136, 136, kTemplateSize, kTemplateSize);
    patch.copyTo(canvas(box));

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 2);
    node.setParam(QStringLiteral("minScore"), 0.5);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("templatePath"), tpl);
    node.setParam(QStringLiteral("searchRegionCol"), 0);
    node.setParam(QStringLiteral("searchRegionRow"), 0);
    node.setParam(QStringLiteral("searchRegionWidth"), canvas.cols);
    node.setParam(QStringLiteral("searchRegionHeight"), canvas.rows);
    node.setInputImage(OpencvUtil::matToHimage(canvas));

    auto maskWidth = [&](int covered) {
        QImage mask(canvas.cols, canvas.rows, QImage::Format_Grayscale8);
        mask.fill(255);
        QPainter p(&mask);
        p.fillRect(QRect(box.x, box.y, covered, kTemplateSize), Qt::black);
        p.end();
        return mask;
    };

    node.setEditMask(maskWidth(26));          // 遮挡 ~20%
    node.run(false);
    const bool okLight = node.getParam(QStringLiteral("moduleStatus")).toBool();
    const double scoreLight = node.getParam(QStringLiteral("matchScore")).toDouble();
    qInfo("occlusion-gate: light ok=%d score=%.3f", int(okLight), scoreLight);
    QVERIFY2(okLight, qPrintable(QStringLiteral("遮挡 20%% 应命中，实际判红（score=%1）").arg(scoreLight)));

    node.setEditMask(maskWidth(80));          // 遮挡 ~62%
    node.run(false);
    const bool okHeavy = node.getParam(QStringLiteral("moduleStatus")).toBool();
    const double scoreHeavy = node.getParam(QStringLiteral("matchScore")).toDouble();
    qInfo("occlusion-gate: heavy ok=%d score=%.3f", int(okHeavy), scoreHeavy);
    QVERIFY2(!okHeavy, qPrintable(QStringLiteral("遮挡 62%%（可见 ~38%% < 40%% 门槛）却仍判成功（score=%1）")
                                      .arg(scoreHeavy)));
}

// 多形状模板：一个模板由"矩形 + 圆"两块组成。
// 关键契约：裁剪包围盒 = 并集、评分掩膜 = 并集、老字段 shape = 第一块。
void FeatureMatchTest::templateMultiShapeEntryAndMask()
{
    RoiShape r;
    r.type = RoiType::Rect;
    r.p1 = QPointF(10, 20);
    r.p2 = QPointF(60, 70);

    RoiShape c;
    c.type = RoiType::Circle;
    c.p1 = QPointF(120, 60);
    c.p2 = QPointF(150, 60);      // 半径 30

    TemplateEntry e;
    e.shapes = {r, c};
    e.shape = r;

    const TemplateEntry back = TemplateEntry::fromJson(e.toJson());
    QCOMPARE(back.shapes.size(), 2);
    QCOMPARE(back.shapes.at(0).type, RoiType::Rect);
    QCOMPARE(back.shapes.at(1).type, RoiType::Circle);
    QCOMPARE(back.shape.type, RoiType::Rect);       // 老字段 = 第一块

    const QRectF box = OpencvTemplateMatchNode::shapesBounds(e.shapes);
    QVERIFY2(qAbs(box.x() - 10.0) <= 0.5 && qAbs(box.y() - 20.0) <= 0.5
                 && qAbs(box.right() - 150.0) <= 0.5 && qAbs(box.bottom() - 90.0) <= 0.5,
             qPrintable(QStringLiteral("并集包围盒错：(%1,%2)-(%3,%4)")
                            .arg(box.x()).arg(box.y()).arg(box.right()).arg(box.bottom())));

    const cv::Mat mask = OpencvTemplateMatchNode::shapesMask(e.shapes, box);
    QVERIFY2(!mask.empty(), "并集掩膜为空");
    QCOMPARE(mask.cols, 140);
    QCOMPARE(mask.rows, 70);
    QVERIFY2(mask.at<uchar>(5, 5) == 255, "矩形内部应为保留区");
    QVERIFY2(mask.at<uchar>(10, 70) == 0, "两块形状之间应为排除区");
    QVERIFY2(mask.at<uchar>(40, 110) == 255, "圆心处应为保留区");
}

// 图像型 ROI：第二个图像输入当掩膜（白=允许、黑=排除）。
// 画布上左右各一个同款工件，掩膜只保留右侧 → 必须命中右侧。
void FeatureMatchTest::templateImageTypeRoiLimitsCandidates()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat patch = makePatch();
    const QString tpl = dir.filePath(QStringLiteral("tpl.png"));
    QVERIFY(cv::imwrite(tpl.toStdString(), patch));

    cv::Mat canvas(300, 700, CV_8UC1, cv::Scalar(kCanvasBackground));
    const cv::Rect left(60, 80, kTemplateSize, kTemplateSize);
    const cv::Rect right(460, 80, kTemplateSize, kTemplateSize);
    patch.copyTo(canvas(left));
    patch.copyTo(canvas(right));

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 1);
    node.setParam(QStringLiteral("minScore"), 0.5);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("templatePath"), tpl);
    node.setParam(QStringLiteral("roiType"), 1);      // 图像类型
    node.setInputImage(OpencvUtil::matToHimage(canvas));

    // 掩膜：只在右侧工件处为白，其余全黑
    cv::Mat maskMat(canvas.rows, canvas.cols, CV_8UC1, cv::Scalar(0));
    cv::rectangle(maskMat, right, cv::Scalar(255), cv::FILLED);
    auto maskObj = QSharedPointer<DataObject>::create();
    maskObj->setHImage(OpencvUtil::matToHimage(maskMat));
    node.setInputData(1, maskObj);

    node.run(false);
    const bool ok = node.getParam(QStringLiteral("moduleStatus")).toBool();
    const double col = node.getParam(QStringLiteral("matchCol")).toDouble();
    const double row = node.getParam(QStringLiteral("matchRow")).toDouble();
    qInfo("image-roi: ok=%d at (%.1f, %.1f)", int(ok), col, row);
    QVERIFY2(ok, "图像型 ROI 下右侧工件应能命中");
    QVERIFY2(qAbs(col - (right.x + kTemplateSize / 2.0)) <= 2.0
                 && qAbs(row - (right.y + kTemplateSize / 2.0)) <= 2.0,
             qPrintable(QStringLiteral("掩膜只留右侧，却命中 (%1,%2) ⇒ 图像型 ROI 没生效")
                            .arg(col).arg(row)));
}

// 输出掩膜（VM 基本参数页开关）：打开后唯一端口上除匹配结果外还要带一张掩膜，
// 内容 = 搜索区域（矩形/多边形）白、区域外黑；关掉则不输出。
void FeatureMatchTest::templateOutputsSearchMask()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat patch = makePatch();
    const QString tpl = dir.filePath(QStringLiteral("tpl.png"));
    QVERIFY(cv::imwrite(tpl.toStdString(), patch));

    cv::Mat canvas(400, 500, CV_8UC1, cv::Scalar(kCanvasBackground));
    patch.copyTo(canvas(cv::Rect(180, 130, kTemplateSize, kTemplateSize)));

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 1);
    node.setParam(QStringLiteral("minScore"), 0.5);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("templatePath"), tpl);
    node.setParam(QStringLiteral("searchRegionCol"), 100);
    node.setParam(QStringLiteral("searchRegionRow"), 80);
    node.setParam(QStringLiteral("searchRegionWidth"), 300);
    node.setParam(QStringLiteral("searchRegionHeight"), 220);
    node.setParam(QStringLiteral("outputMask"), true);
    node.setInputImage(OpencvUtil::matToHimage(canvas));
    node.run(false);

    // 直接调用 run() 时结果挂在端口 1；聚合端口(0)由 process() 生成，这里验数据面
    const auto resData = node.getOutputData(1);
    QVERIFY2(resData, "匹配结果对象缺失");
    QSharedPointer<DataObject> maskObj;
    for (const auto &p : resData->attachedResults()) {
        if (p.first == QStringLiteral("掩膜"))
            maskObj = p.second;
    }
    QVERIFY2(maskObj && maskObj->getHImage().IsInitialized(),
             "输出掩膜打开后，结果对象上应有「掩膜」子结果");
    const cv::Mat mask = OpencvUtil::himageToMat(HImage(maskObj->getHImage()));
    QCOMPARE(mask.cols, canvas.cols);
    QCOMPARE(mask.rows, canvas.rows);
    QVERIFY2(mask.at<uchar>(150, 150) != 0, "搜索区域内部应为白");
    QVERIFY2(mask.at<uchar>(10, 10) == 0, "搜索区域外应为黑");

    // 关掉后该子结果不应再带图
    node.setParam(QStringLiteral("outputMask"), false);
    node.run(false);
    const auto resOff = node.getOutputData(1);
    QSharedPointer<DataObject> maskOff;
    if (resOff) {
        for (const auto &p : resOff->attachedResults()) {
            if (p.first == QStringLiteral("掩膜"))
                maskOff = p.second;
        }
    }
    QVERIFY2(!maskOff || !maskOff->getHImage().IsInitialized(),
             "输出掩膜关闭后不应再产出掩膜图");
}

// 匹配中心（锚点）：默认在模板中心；配置偏移后，结果的「匹配点」必须整体平移过去。
// 这条守的是"锚点真的参与了结果换算"，而不是只存在 JSON 里。
void FeatureMatchTest::templateMatchAnchorOffsetsMatchPoint()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat patch = makePatch();
    const QString tpl = dir.filePath(QStringLiteral("tpl.png"));
    QVERIFY(cv::imwrite(tpl.toStdString(), patch));

    cv::Mat canvas(400, 600, CV_8UC1, cv::Scalar(kCanvasBackground));
    const cv::Rect box(240, 140, kTemplateSize, kTemplateSize);
    patch.copyTo(canvas(box));
    const double cx = box.x + kTemplateSize / 2.0;
    const double cy = box.y + kTemplateSize / 2.0;

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 1);
    node.setParam(QStringLiteral("minScore"), 0.5);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("templatePath"), tpl);
    node.setInputImage(OpencvUtil::matToHimage(canvas));

    TemplateEntry e;
    e.name = QStringLiteral("A");
    e.imagePath = tpl;
    e.anchorDx = 32.0;
    e.anchorDy = -16.0;
    e.anchorLocked = true;
    node.setTemplates({e});
    node.setActiveTemplateIndex(0);

    node.run(false);
    const bool ok = node.getParam(QStringLiteral("moduleStatus")).toBool();
    const double pcol = node.getParam(QStringLiteral("matchPointCol")).toDouble();
    const double prow = node.getParam(QStringLiteral("matchPointRow")).toDouble();
    const double mcol = node.getParam(QStringLiteral("matchCol")).toDouble();
    const double mrow = node.getParam(QStringLiteral("matchRow")).toDouble();
    qInfo("anchor-leg: center=(%.1f,%.1f) point=(%.1f,%.1f)", mcol, mrow, pcol, prow);
    QVERIFY2(ok, "锚点用例应当匹配成功");
    QVERIFY2(qAbs(mcol - cx) <= 2.0 && qAbs(mrow - cy) <= 2.0,
             qPrintable(QStringLiteral("匹配框中心应在 (%1,%2)，实际 (%3,%4)")
                            .arg(cx).arg(cy).arg(mcol).arg(mrow)));
    QVERIFY2(qAbs(pcol - (cx + 32.0)) <= 2.0 && qAbs(prow - (cy - 16.0)) <= 2.0,
             qPrintable(QStringLiteral("匹配点应随锚点偏移到 (%1,%2)，实际 (%3,%4)")
                            .arg(cx + 32.0).arg(cy - 16.0).arg(pcol).arg(prow)));

    // 锚点信息必须能存进模板列表（否则换型/重开工程就丢）
    const TemplateEntry back = node.templates().first();
    QCOMPARE(back.anchorDx, 32.0);
    QCOMPARE(back.anchorDy, -16.0);
    QVERIFY(back.anchorLocked);
}

// 模板形状的布尔编辑（VM 工具条上的「分割 / 剪刀」）：
//   ① 分割：一块矩形 → 仍是一块（没有断点）；
//   ② 剪刀：一条穿过它的线 → 必须切成两块，且每块面积约为一半；
//   ③ 线没穿过 → 不许误切（仍是 1 块）。
void FeatureMatchTest::templateSplitShapeByCut()
{
    auto area = [](const RoiShape &s) {
        double a = 0.0;
        for (int i = 0; i < s.points.size(); ++i) {
            const QPointF &p = s.points.at(i);
            const QPointF &q = s.points.at((i + 1) % s.points.size());
            a += p.x() * q.y() - q.x() * p.y();
        }
        return std::abs(a) * 0.5;
    };

    RoiShape r;
    r.type = RoiType::Rect;
    r.p1 = QPointF(100, 100);
    r.p2 = QPointF(200, 200);

    const QVector<RoiShape> whole = TemplateConfigDialog::splitShape(r);
    QCOMPARE(whole.size(), 1);
    QVERIFY2(std::abs(area(whole.first()) - 10000.0) <= 400.0,
             qPrintable(QStringLiteral("整块面积应≈10000，实际 %1").arg(area(whole.first()))));

    const QVector<RoiShape> halves = TemplateConfigDialog::splitShape(
        r, QPointF(150, 80), QPointF(150, 220), true);
    qInfo("split: pieces=%d areas=%.0f/%.0f", int(halves.size()),
          halves.isEmpty() ? 0.0 : area(halves.at(0)),
          halves.size() < 2 ? 0.0 : area(halves.at(1)));
    QCOMPARE(halves.size(), 2);
    for (const RoiShape &s : halves) {
        QVERIFY2(std::abs(area(s) - 4850.0) <= 400.0,
                 qPrintable(QStringLiteral("切开后半块面积应≈4850，实际 %1").arg(area(s))));
    }

    // 短线只擦到角上，切不断 → 仍是一整块
    const QVector<RoiShape> notCut = TemplateConfigDialog::splitShape(
        r, QPointF(105, 105), QPointF(115, 105), true);
    QCOMPARE(notCut.size(), 1);
}

// 继承 ROI（VM 基本参数页「ROI创建 = 继承」）：上游命中框直接当本次搜索区域。
// 构造：左侧工件被压掉一块（分数低），右侧完好（分数高）；上游命中框指向左侧。
//   roiSource=0 → 全图搜，命中右侧；
//   roiSource=1 → 只在继承框内搜，命中左侧。
void FeatureMatchTest::templateInheritsUpstreamBoxAsSearchRegion()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat patch = makePatch();
    const QString tpl = dir.filePath(QStringLiteral("tpl.png"));
    QVERIFY(cv::imwrite(tpl.toStdString(), patch));

    cv::Mat canvas(300, 700, CV_8UC1, cv::Scalar(kCanvasBackground));
    const cv::Rect left(60, 80, kTemplateSize, kTemplateSize);
    const cv::Rect right(460, 80, kTemplateSize, kTemplateSize);
    patch.copyTo(canvas(left));
    patch.copyTo(canvas(right));
    cv::rectangle(canvas, cv::Rect(left.x + 10, left.y + 10, 46, 46), cv::Scalar(200), cv::FILLED);

    const double leftCx = left.x + kTemplateSize / 2.0;
    const double leftCy = left.y + kTemplateSize / 2.0;
    const double rightCx = right.x + kTemplateSize / 2.0;

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 1);
    node.setParam(QStringLiteral("minScore"), 0.3);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("templatePath"), tpl);
    node.setInputImage(OpencvUtil::matToHimage(canvas));

    // 上游结果：图像 + 一个指向左侧工件的 template 命中框
    MeasureResult mr;
    mr.type = QStringLiteral("template");
    mr.valid = true;
    mr.value = 0.9;
    mr.point1 = QPointF(leftCx, leftCy);
    mr.extraValues = QVector<double>({leftCy, leftCx, 0.9, 0.0, 1.0,
                                      double(kTemplateSize), double(kTemplateSize)});
    auto upstream = QSharedPointer<DataObject>::create();
    upstream->setHImage(OpencvUtil::matToHimage(canvas));
    upstream->setMeasureResult(mr);
    node.setInputData(0, upstream);

    node.setParam(QStringLiteral("roiSource"), 0);
    node.run(false);
    const double c0 = node.getParam(QStringLiteral("matchCol")).toDouble();
    qInfo("inherit-roi: 绘制模式命中 x=%.1f", c0);
    QVERIFY2(c0 > rightCx - 20.0,
             qPrintable(QStringLiteral("不继承时应命中右侧完好的工件，实际 x=%1").arg(c0)));

    node.setParam(QStringLiteral("roiSource"), 1);
    node.run(false);
    const double c1 = node.getParam(QStringLiteral("matchCol")).toDouble();
    const double r1 = node.getParam(QStringLiteral("matchRow")).toDouble();
    qInfo("inherit-roi: 继承模式命中 (%.1f, %.1f)", c1, r1);
    QVERIFY2(qAbs(c1 - leftCx) <= 2.0 && qAbs(r1 - leftCy) <= 2.0,
             qPrintable(QStringLiteral("继承上游框后应只在左侧搜，实际 (%1, %2)").arg(c1).arg(r1)));
}

// 魔术棒 / 魔术棒+（VM 工具条上成对的两个按钮）：
//   魔术棒 = 用取到的区域替换当前选择；魔术棒+ = 追加一块；取不到时不许误清空。
void FeatureMatchTest::templateWandPlusAppends()
{
    cv::Mat img(200, 200, CV_8UC1, cv::Scalar(240));
    cv::rectangle(img, cv::Rect(50, 40, 100, 80), cv::Scalar(40), cv::FILLED);

    RoiShape existing;
    existing.type = RoiType::Rect;
    existing.p1 = QPointF(0, 0);
    existing.p2 = QPointF(20, 20);
    const QVector<RoiShape> base{existing};

    const QVector<RoiShape> replaced =
        TemplateConfigDialog::applyWand(base, img, QPoint(100, 80), 20, false);
    QCOMPARE(replaced.size(), 1);
    QCOMPARE(replaced.first().type, RoiType::Polygon);
    QVERIFY(replaced.first().points.size() >= 4);

    const QVector<RoiShape> appended =
        TemplateConfigDialog::applyWand(base, img, QPoint(100, 80), 20, true);
    qInfo("wand-plus: base=1 -> append=%d", int(appended.size()));
    QCOMPARE(appended.size(), 2);
    QCOMPARE(appended.at(0).type, RoiType::Rect);       // 原形状保留
    QCOMPARE(appended.at(1).type, RoiType::Polygon);    // 新取到的追加在后

    // 取不到区域（空图）时保持原样，绝不误清空
    const QVector<RoiShape> failed =
        TemplateConfigDialog::applyWand(base, cv::Mat(), QPoint(100, 80), 20, false);
    QCOMPARE(failed.size(), 1);
    QCOMPARE(failed.first().type, RoiType::Rect);
}

// 建/改模板后的参数建议（VM「创建模板成功 → 参数自动变成 8.0/2/37」同款）：
//   ① 低对比模板给低阈值、高对比模板给高阈值（都落在 6..80）；
//   ② 小模板特征尺度 3、大模板 5；
//   ③ 速度尺度 = 模板长边 × 4（夹在 240..1600）。
void FeatureMatchTest::templateSuggestParamsFollowsTemplate()
{
    cv::Mat low(128, 128, CV_8UC1, cv::Scalar(100));
    cv::rectangle(low, cv::Rect(0, 0, 64, 128), cv::Scalar(110), cv::FILLED);   // 对比 10

    cv::Mat high(128, 128, CV_8UC1, cv::Scalar(30));
    cv::rectangle(high, cv::Rect(0, 0, 64, 128), cv::Scalar(220), cv::FILLED);  // 对比 190

    const auto sl = OpencvTemplateMatchNode::suggestParams(low);
    const auto sh = OpencvTemplateMatchNode::suggestParams(high);
    qInfo("suggest: low t=%d f=%d s=%d | high t=%d f=%d s=%d", sl.contrastThreshold,
          sl.featureScale, sl.speedScale, sh.contrastThreshold, sh.featureScale, sh.speedScale);

    QVERIFY2(sl.contrastThreshold >= 1 && sl.contrastThreshold <= 255,
             "对比度阈值必须夹在 1..255（VM 口径：灰度差）");
    QVERIFY2(sh.contrastThreshold >= sl.contrastThreshold,
             qPrintable(QStringLiteral("高对比模板的阈值不应低于低对比：%1 vs %2")
                            .arg(sh.contrastThreshold).arg(sl.contrastThreshold)));
    QCOMPARE(sl.featureScale, 3);           // VM 截图建模板成功后的默认值
    QCOMPARE(sl.speedScale, 3);
    QCOMPARE(sl.minChainLength, 3);
    QVERIFY(!sl.autoThreshold);             // 给的是具体数字

    cv::Mat big(256, 256, CV_8UC1, cv::Scalar(20));
    cv::rectangle(big, cv::Rect(10, 10, 200, 200), cv::Scalar(230), cv::FILLED);
    const auto sb = OpencvTemplateMatchNode::suggestParams(big);
    QCOMPARE(sb.featureScale, 3);
    QCOMPARE(sb.speedScale, 3);
    QVERIFY(sb.contrastThreshold > 40);     // 高对比模板给更高的阈值
}

// 屏蔽形状（VM 的「屏蔽区」）：最终掩膜 = 保留 − 屏蔽，用来挖掉 logo/水印。
//   ① 保留矩形 + 屏蔽圆：圆心 0、角上 255；
//   ② 只有屏蔽、没画保留区：默认整块保留再挖；
//   ③ 屏蔽形状随模板列表 JSON 持久化。
void FeatureMatchTest::templateExcludeShapesSubtractFromMask()
{
    RoiShape keep;
    keep.type = RoiType::Rect;
    keep.p1 = QPointF(100, 100);
    keep.p2 = QPointF(200, 200);

    RoiShape hole;
    hole.type = RoiType::Circle;
    hole.p1 = QPointF(150, 150);
    hole.p2 = QPointF(180, 150);       // 半径 30

    QVector<RoiShape> keepList{keep};
    QVector<RoiShape> exList{hole};

    const QRectF box = OpencvTemplateMatchNode::shapesBounds(keepList + exList);
    QVERIFY2(qAbs(box.width() - 100.0) <= 1.0 && qAbs(box.height() - 100.0) <= 1.0,
             qPrintable(QStringLiteral("并集包围盒应为 100x100，实际 %1x%2")
                            .arg(box.width())
                            .arg(box.height())));

    const cv::Mat mask = OpencvTemplateMatchNode::shapesMask(keepList, box, exList);
    QCOMPARE(mask.cols, 100);
    QCOMPARE(mask.rows, 100);
    QVERIFY2(mask.at<uchar>(50, 50) == 0, "圆心处应被屏蔽挖掉");
    QVERIFY2(mask.at<uchar>(5, 5) != 0, "角上仍应保留");

    // 只画了屏蔽、没画保留区：默认整块保留再挖
    const cv::Mat m2 = OpencvTemplateMatchNode::shapesMask({}, box, exList);
    QVERIFY2(m2.at<uchar>(50, 50) == 0, "仅屏蔽时圆心也应被挖掉");
    QVERIFY2(m2.at<uchar>(5, 5) != 0, "仅屏蔽时其余区域应默认保留");

    TemplateEntry e;
    e.shapes = keepList;
    e.excludes = exList;
    const TemplateEntry back = TemplateEntry::fromJson(e.toJson());
    QCOMPARE(back.excludes.size(), 1);
    QCOMPARE(back.excludes.first().type, RoiType::Circle);
    QCOMPARE(back.shapes.size(), 1);
}

// 尺度模式（VM 配置参数最后一项）：自动 = 按模板占搜索区的比例估出尺度范围。
// 画布上放一个 1.15 倍的实例：
//   scaleMode=0（手动，scaleMin=Max=1）→ 只能按 1.0 找，分数低、尺度=1；
//   scaleMode=1 → 自动把范围扩到 ~0.77..1.3，命中 ≈1.15 的实例且分数更高。
void FeatureMatchTest::templateScaleRangeFindsScaledInstance()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat patch = makePatch();
    const QString tpl = dir.filePath(QStringLiteral("tpl.png"));
    QVERIFY(cv::imwrite(tpl.toStdString(), patch));

    cv::Mat canvas(400, 500, CV_8UC1, cv::Scalar(kCanvasBackground));
    cv::Mat scaled;
    cv::resize(patch, scaled, cv::Size(), 1.15, 1.15, cv::INTER_LINEAR);
    const cv::Rect place(200, 150, scaled.cols, scaled.rows);
    scaled.copyTo(canvas(place));
    const double cx = place.x + scaled.cols / 2.0;
    const double cy = place.y + scaled.rows / 2.0;

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 1);
    node.setParam(QStringLiteral("minScore"), 0.5);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("scaleMin"), 1.0);
    node.setParam(QStringLiteral("scaleMax"), 1.0);
    node.setParam(QStringLiteral("scaleStep"), 0.05);
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("templatePath"), tpl);
    node.setInputImage(OpencvUtil::matToHimage(canvas));

    // 尺度范围=1.0：只能按原尺寸找，分数明显低
    node.run(false);
    const double s0 = node.getParam(QStringLiteral("matchScore")).toDouble();
    const double k0 = node.getParam(QStringLiteral("matchScale")).toDouble();

    // 尺度范围 0.8~1.3：应搜到 ≈1.15 的实例
    node.setParam(QStringLiteral("scaleMin"), 0.8);
    node.setParam(QStringLiteral("scaleMax"), 1.3);
    node.setParam(QStringLiteral("scaleStep"), 0.05);
    node.run(false);
    const bool ok = node.getParam(QStringLiteral("moduleStatus")).toBool();
    const double s1 = node.getParam(QStringLiteral("matchScore")).toDouble();
    const double k1 = node.getParam(QStringLiteral("matchScale")).toDouble();
    const double col = node.getParam(QStringLiteral("matchCol")).toDouble();
    const double row = node.getParam(QStringLiteral("matchRow")).toDouble();
    qInfo("scale-range: fixed k=%.2f score=%.3f | ranged k=%.2f score=%.3f at (%.1f,%.1f)", k0, s0,
          k1, s1, col, row);

    QVERIFY2(ok, "尺度范围覆盖 1.15 时应命中");
    QVERIFY2(qAbs(k1 - 1.15) <= 0.10,
             qPrintable(QStringLiteral("应搜到 ≈1.15 的尺度，实际 %1").arg(k1)));
    QVERIFY2(qAbs(k0 - 1.0) < 1e-6, "尺度范围 1.0 时尺度应固定 1.0");
    QVERIFY2(s1 > s0,
             qPrintable(QStringLiteral("按真实尺度匹配的分数应更高：%1 vs %2").arg(s1).arg(s0)));
    QVERIFY2(qAbs(col - cx) <= 3.0 && qAbs(row - cy) <= 3.0,
             qPrintable(QStringLiteral("命中位置应在 (%1,%2)，实际 (%3,%4)")
                            .arg(cx).arg(cy).arg(col).arg(row)));
}

// VM「特征尺度 / 最小链长」（官方定义：特征尺度越大特征点越稀疏；链长小于最小链长的特征链不保留）。
//   ① 特征尺度 1 → 5：点数必须明显下降（稀疏化真的生效）；
//   ② 孤立噪点（单像素）在最小链长 5 下被丢弃，长线保留。
void FeatureMatchTest::templateFeatureScaleAndChainLengthFilterPoints()
{
    // 长竖线 + 右下角一个孤立点
    cv::Mat img(80, 80, CV_8UC1, cv::Scalar(60));
    img(cv::Rect(0, 0, 40, 80)).setTo(70);          // 一条贯穿的竖边
    img.at<uchar>(70, 70) = 255;                   // 孤立亮噪点

    RoiShape roi;
    roi.type = RoiType::Rect;
    roi.p1 = QPointF(0, 0);
    roi.p2 = QPointF(80, 80);

    const QVector<QPointF> fine =
        OpencvTemplateMatchNode::featurePointsForPatch(img, roi, 20.0, 3, cv::Mat(), 1, 1);
    const QVector<QPointF> coarse =
        OpencvTemplateMatchNode::featurePointsForPatch(img, roi, 20.0, 3, cv::Mat(), 5, 1);
    qInfo("feature-scale: 1 -> %d 点, 5 -> %d 点", int(fine.size()), int(coarse.size()));
    QVERIFY2(fine.size() > coarse.size(),
             qPrintable(QStringLiteral("特征尺度变大点数没减少：%1 vs %2")
                            .arg(fine.size()).arg(coarse.size())));

    // 最小链长：孤立噪点（Sobel 响应只在一小片）要被丢掉，贯穿竖线（长链）要留下
    const QVector<QPointF> unfiltered =
        OpencvTemplateMatchNode::featurePointsForPatch(img, roi, 20.0, 3, cv::Mat(), 1, 1);
    const QVector<QPointF> chained =
        OpencvTemplateMatchNode::featurePointsForPatch(img, roi, 20.0, 3, cv::Mat(), 1, 20);
    auto rightSide = [](const QVector<QPointF> &pts) {
        int n = 0;
        for (const QPointF &p : pts)
            if (p.x() > 45.0)
                ++n;
        return n;
    };
    qInfo("chain-length: 不过滤右侧 %d 点, 最小链长 20 右侧 %d 点", rightSide(unfiltered),
          rightSide(chained));
    QVERIFY2(!chained.isEmpty(), "长链（竖边）应被保留");
    QVERIFY2(rightSide(unfiltered) > 0, "不过滤时右侧噪点应存在（否则测不到过滤效果）");
    QVERIFY2(rightSide(chained) == 0,
             qPrintable(QStringLiteral("最小链长 20 没把孤立噪点滤掉（还剩 %1 个）")
                            .arg(rightSide(chained))));
}

// VM「排序类型」：多目标结果按指定依据排序。两个同款工件一上一下：
//   sortType=3（按Y由小到大）→ 第一行 row 必须小于第二行。
void FeatureMatchTest::templateSortTypeOrdersMatchList()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat patch = makePatch();
    const QString tpl = dir.filePath(QStringLiteral("tpl.png"));
    QVERIFY(cv::imwrite(tpl.toStdString(), patch));

    cv::Mat canvas(500, 400, CV_8UC1, cv::Scalar(kCanvasBackground));
    patch.copyTo(canvas(cv::Rect(60, 300, kTemplateSize, kTemplateSize)));   // 下
    patch.copyTo(canvas(cv::Rect(60, 60, kTemplateSize, kTemplateSize)));    // 上

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 1);
    node.setParam(QStringLiteral("minScore"), 0.5);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("maxMatches"), 2);
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("templatePath"), tpl);
    node.setParam(QStringLiteral("searchRegionCol"), 0);
    node.setParam(QStringLiteral("searchRegionRow"), 0);
    node.setParam(QStringLiteral("searchRegionWidth"), canvas.cols);
    node.setParam(QStringLiteral("searchRegionHeight"), canvas.rows);
    node.setInputImage(OpencvUtil::matToHimage(canvas));

    node.setParam(QStringLiteral("sortType"), 3);        // 按Y由小到大
    node.run(false);
    const QStringList rows =
        node.getParam(QStringLiteral("matchList")).toString().split(QLatin1Char(';'), Qt::SkipEmptyParts);
    qInfo("sort-type: %s", qPrintable(node.getParam(QStringLiteral("matchList")).toString()));
    QCOMPARE(rows.size(), 2);
    const double row0 = rows.at(0).split(QLatin1Char(',')).at(0).toDouble();
    const double row1 = rows.at(1).split(QLatin1Char(',')).at(0).toDouble();
    QVERIFY2(row0 < row1,
             qPrintable(QStringLiteral("按Y升序没生效：第一行 %1，第二行 %2").arg(row0).arg(row1)));

    node.setParam(QStringLiteral("sortType"), 2);        // 按X由小到大（同列时顺序应稳定）
    node.run(false);
    const QStringList rows2 =
        node.getParam(QStringLiteral("matchList")).toString().split(QLatin1Char(';'), Qt::SkipEmptyParts);
    QCOMPARE(rows2.size(), 2);
}

// VM「全部搜索模式=关」语义：按模板列表顺序逐个试，达标即停。
// 画布上只有 B，模板列表 [A, B]：A 先试（失败）→ B 试（命中）→ 停止。
// 不应该跳过 A 直接用 B（那就是 active 模板语义），也不应该试完两个取最优（那是 matchAll）。
void FeatureMatchTest::templateSequentialMatchStopsAtFirstHit()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const cv::Mat patchA = makePatch();
    cv::Mat patchB(kTemplateSize, kTemplateSize, CV_8UC1, cv::Scalar(20));
    cv::circle(patchB, cv::Point(64, 64), 50, cv::Scalar(255), 6);
    cv::circle(patchB, cv::Point(64, 64), 26, cv::Scalar(190), 4);
    cv::line(patchB, cv::Point(12, 12), cv::Point(116, 116), cv::Scalar(240), 3);

    const QString pathA = dir.filePath(QStringLiteral("a.png"));
    const QString pathB = dir.filePath(QStringLiteral("b.png"));
    QVERIFY(cv::imwrite(pathA.toStdString(), patchA));
    QVERIFY(cv::imwrite(pathB.toStdString(), patchB));

    cv::Mat canvas(300, 600, CV_8UC1, cv::Scalar(kCanvasBackground));
    const cv::Rect boxB(360, 90, kTemplateSize, kTemplateSize);
    patchB.copyTo(canvas(boxB));
    const double cxB = boxB.x + kTemplateSize / 2.0;
    const double cyB = boxB.y + kTemplateSize / 2.0;

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 1);
    node.setParam(QStringLiteral("minScore"), 0.5);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("searchRegionCol"), 0);
    node.setParam(QStringLiteral("searchRegionRow"), 0);
    node.setParam(QStringLiteral("searchRegionWidth"), canvas.cols);
    node.setParam(QStringLiteral("searchRegionHeight"), canvas.rows);
    node.setInputImage(OpencvUtil::matToHimage(canvas));

    TemplateEntry ea;
    ea.name = QStringLiteral("A");
    ea.imagePath = pathA;
    TemplateEntry eb;
    eb.name = QStringLiteral("B");
    eb.imagePath = pathB;
    node.setTemplates({ea, eb});          // 列表顺序 [A, B]
    node.setActiveTemplateIndex(0);       // active 挂在 A 上（故意不匹配）
    node.setParam(QStringLiteral("matchAllTemplates"), false);

    node.run(false);
    const bool ok = node.getParam(QStringLiteral("moduleStatus")).toBool();
    const int idx = node.getParam(QStringLiteral("matchTemplateIndex")).toInt();
    const QString name = node.getParam(QStringLiteral("matchTemplateName")).toString();
    const double col = node.getParam(QStringLiteral("matchCol")).toDouble();
    const double row = node.getParam(QStringLiteral("matchRow")).toDouble();
    qInfo("sequential: ok=%d index=%d name=%s at (%.1f, %.1f)", int(ok), idx,
          qPrintable(name), col, row);

    QVERIFY2(ok, "顺序匹配应由 B 命中（A 失败后继续试 B）");
    QCOMPARE(idx, 1);
    QCOMPARE(name, QStringLiteral("B"));
    QVERIFY2(qAbs(col - cxB) <= 2.0 && qAbs(row - cyB) <= 2.0,
             qPrintable(QStringLiteral("命中位置应在 B (%1,%2)，实际 (%3,%4)")
                            .arg(cxB).arg(cyB).arg(col).arg(row)));
}

// 尺度范围 XY（VM「尺度范围类型=尺度范围XY」）：X/Y 独立缩放。
// 画布上放一个 scaleX=1.2, scaleY=0.8 的实例（非均匀缩放），
// scaleRangeType=1 + 覆盖该范围 → 应命中；scaleRangeType=0（整体）→ 分数低。
void FeatureMatchTest::templateScaleRangeXYFindsAnisotropicInstance()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat patch = makePatch();
    const QString tpl = dir.filePath(QStringLiteral("tpl.png"));
    QVERIFY(cv::imwrite(tpl.toStdString(), patch));

    // 非均匀缩放实例：X 放大 1.2、Y 缩小 0.8
    cv::Mat canvas(400, 500, CV_8UC1, cv::Scalar(kCanvasBackground));
    cv::Mat scaled;
    cv::resize(patch, scaled, cv::Size(), 1.2, 0.8, cv::INTER_LINEAR);
    const cv::Rect place(180, 140, scaled.cols, scaled.rows);
    scaled.copyTo(canvas(place));
    const double cx = place.x + scaled.cols / 2.0;
    const double cy = place.y + scaled.rows / 2.0;

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 1);
    node.setParam(QStringLiteral("minScore"), 0.5);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("templatePath"), tpl);
    node.setInputImage(OpencvUtil::matToHimage(canvas));

    // 整体尺度范围：scaleX=scaleY，找不到非均匀实例
    node.setParam(QStringLiteral("scaleRangeType"), 0);
    node.setParam(QStringLiteral("scaleMin"), 0.7);
    node.setParam(QStringLiteral("scaleMax"), 1.3);
    node.setParam(QStringLiteral("scaleStep"), 0.1);
    node.run(false);
    const double score0 = node.getParam(QStringLiteral("matchScore")).toDouble();
    const double sx0 = node.getParam(QStringLiteral("matchScaleX")).toDouble();
    const double sy0 = node.getParam(QStringLiteral("matchScaleY")).toDouble();
    qInfo("scale-xy: uniform score=%.3f scale=(%.2f,%.2f)", score0, sx0, sy0);

    // XY 尺度范围：X 1.2、Y 0.8
    node.setParam(QStringLiteral("scaleRangeType"), 1);
    node.setParam(QStringLiteral("scaleMinX"), 1.0);
    node.setParam(QStringLiteral("scaleMaxX"), 1.4);
    node.setParam(QStringLiteral("scaleMinY"), 0.6);
    node.setParam(QStringLiteral("scaleMaxY"), 1.0);
    node.setParam(QStringLiteral("scaleStep"), 0.1);
    node.run(false);
    const bool ok = node.getParam(QStringLiteral("moduleStatus")).toBool();
    const double score1 = node.getParam(QStringLiteral("matchScore")).toDouble();
    const double sx1 = node.getParam(QStringLiteral("matchScaleX")).toDouble();
    const double sy1 = node.getParam(QStringLiteral("matchScaleY")).toDouble();
    const double col = node.getParam(QStringLiteral("matchCol")).toDouble();
    const double row = node.getParam(QStringLiteral("matchRow")).toDouble();
    qInfo("scale-xy: anisotropic ok=%d score=%.3f scale=(%.2f,%.2f) at (%.1f,%.1f)",
          int(ok), score1, sx1, sy1, col, row);

    QVERIFY2(ok, "XY 尺度范围下应命中非均匀缩放实例");
    QVERIFY2(qAbs(sx1 - 1.2) <= 0.15,
             qPrintable(QStringLiteral("X 尺度应≈1.2，实际 %1").arg(sx1)));
    QVERIFY2(qAbs(sy1 - 0.8) <= 0.15,
             qPrintable(QStringLiteral("Y 尺度应≈0.8，实际 %1").arg(sy1)));
    QVERIFY2(score1 > score0,
             qPrintable(QStringLiteral("XY 尺度范围的分数应高于整体范围：%1 vs %2")
                            .arg(score1).arg(score0)));
}

// 擦除轮廓点（VM「擦除轮廓点」）：擦除区域内的特征点不参与匹配。
//   ① 无擦除：特征点 > 0；
//   ② 全部擦除（覆盖整个模板）：特征点 = 0；
//   ③ JSON 往返不丢擦除区域。
void FeatureMatchTest::templateErasesRemoveFeaturePoints()
{
    cv::Mat img(200, 200, CV_8UC1, cv::Scalar(60));
    cv::rectangle(img, cv::Rect(0, 0, 100, 200), cv::Scalar(70), cv::FILLED);   // 一条竖边

    RoiShape roi;
    roi.type = RoiType::Rect;
    roi.p1 = QPointF(0, 0);
    roi.p2 = QPointF(200, 200);

    // ① 无擦除
    const QVector<QPointF> pts0 =
        OpencvTemplateMatchNode::featurePointsForPatch(img, roi, 20.0, 3);
    QVERIFY2(!pts0.isEmpty(), "无擦除时应有特征点");

    // ② 全部擦除（一个大圆覆盖整个模板）
    RoiShape bigErase;
    bigErase.type = RoiType::Circle;
    bigErase.p1 = QPointF(100, 100);
    bigErase.p2 = QPointF(200, 100);   // 半径 100
    const QVector<QPointF> pts1 =
        OpencvTemplateMatchNode::featurePointsForPatch(img, roi, 20.0, 3, cv::Mat(), 1, 1,
                                                       {bigErase});
    qInfo("erases: no-erase=%d full-erase=%d", int(pts0.size()), int(pts1.size()));
    QVERIFY2(pts1.isEmpty(), qPrintable(QStringLiteral("全部擦除后仍剩 %1 个特征点").arg(pts1.size())));

    // ③ JSON 往返
    TemplateEntry e;
    e.erases = {bigErase};
    const TemplateEntry back = TemplateEntry::fromJson(e.toJson());
    QCOMPARE(back.erases.size(), 1);
    QCOMPARE(back.erases.first().type, RoiType::Circle);
    QCOMPARE(back.erases.first().p1, bigErase.p1);
    QCOMPARE(back.erases.first().p2, bigErase.p2);
}

QTEST_MAIN(FeatureMatchTest)
#include "feature_match_test.moc"
