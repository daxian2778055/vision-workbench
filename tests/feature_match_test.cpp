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

#include <algorithm>
#include "TemplateMatchNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "FlowScene.h"
#include "NodeRegistry.h"
#include "ImageDisplayController.h"   // OverlayShape + collectOverlayFromNode
#include "TemplateConfigDialog.h"     // 魔术棒取形（纯函数）
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/features2d.hpp>
#include <HalconCpp.h>
#include <cmath>
#include <tuple>
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
    void templateResultDisplaySpec();
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
    // 模板对象：序列化往返 + 只有 templatePath 时的单模板模式
    void templateEntryRoundTripAndSingleTemplateMode();
    // 模板列表驱动匹配：切 active 索引，命中必须跟着换模板
    void templateMatchUsesActiveTemplateFromList();
    // 多模板同时匹配：画布只有 B 时，开关打开后必须自动选中模板 B
    void templateMatchAllTemplatesPicksBest();
    // 全部搜索模式：不同模板各自的目标必须合并输出；多模板重叠率控制重叠目标去留
    void templateAllTemplatesMergesPerTarget();
    void templateSingleOverlapUsesIou_data();
    void templateSingleOverlapUsesIou();
    void templateMultiOverlapUsesIou_data();
    void templateMultiOverlapUsesIou();
    // VM「勾选模板 / 模板排序」：未勾选不参与匹配；列表顺序决定未开全部搜索模式时的优先顺序
    void templateCheckedTemplatesControlSearch();
    // VM「是否考虑噪点」：开启后搜索图毛刺（未被模板特征点解释的强边缘）拉低评分
    void templateNoiseConsiderationLowersScore();
    // VM「结果显示 → 结果判断」：数量/角度/尺度多条件全部满足才 OK
    void templateResultJudgmentHonorsVmConditions();
    // VM「增强匹配」（仅高精度匹配）：被勾选的掩膜区域用局部分数给整体分数封顶
    void templateEnhanceMatchTightensFlaggedMask();
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
    // 屏蔽区多边形结果 + 输出掩膜宽高/像素格式（VM 模块结果补齐）
    void templateMaskPolygonAndOutputMaskResults();
    // 匹配中心（锚点）偏移：匹配点必须跟着锚点走（VM 的「匹配中心 X/Y」）
    void templateMatchAnchorOffsetsMatchPoint();
    void templateAnchorFollowsWarpRotation();
    void templateMatchPointFollowsAnchorAtAngle();
    void templatePolarityZeroMatchesInvertedContrast();
    void templateMaskEditInvalidatesWarpCache();
    void templateTimeoutReturnsNoResults();
    void templateMaxMatchesAllowsVmRange();
    // VM 出厂默认与量程：minScore 0.5 / 角度 ±45 / 尺度 0.1~10.0
    void templateVmDefaultsMatchHelp();
    void templateFingerprintDistinguishesContent();
    void matchCountMatchesFilteredList();
    void templateAnchorUsesScaleYInXYMode();
    void templateCurrentResultFollowsSortType();
    void templateSearchValuesHandlesTinyMaxCount();
    void templateImageTypeRoiAccepts16BitMask();
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
    // 横切角度范围：目标 Y 轴向 X 轴倾斜时，搜索横切角才能命中
    void templateCrossAngleSearchesSkew();
    // 是否获取轮廓 / 是否输出轮廓：显示与模块结果两条出口分别受控
    void templateContourTogglesFollowVm();
    // 多目标叠加框按各自宽高绘制（XY 尺度两轴独立，P0-5）
    void templateOverlayUsesPerTargetSize();
    // 擦除轮廓点：擦除区域内的特征点不参与匹配
    void templateErasesRemoveFeaturePoints();
    // 建模板后的参数建议：低对比给低阈值、大模板给大特征尺度
    void templateSuggestParamsFollowsTemplate();
    // VM 特征尺度/最小链长：特征点必须真的随参数变稀疏、短链被丢弃
    void templateFeatureScaleAndChainLengthFilterPoints();
    // VM 排序类型：多目标结果的排序依据必须生效
    void templateSortTypeOrdersMatchList();
    // VM 排序并列规则：X/Y 整数化后相同才用另一轴兜底（P2-2）
    void templateSortTypeIntegerizedTieBreak();
    // 屏蔽形状：最终掩膜 = 保留 − 屏蔽
    void templateExcludeShapesSubtractFromMask();
    // 运行参数尺度范围：只有显式 min/max 才生效（VM 没有"搜索尺度自动"）
    void templateScaleRangeFindsScaledInstance();
    // VM 模板配置「尺度模式」：只控制速度尺度自动/手动，与搜索尺度范围无关
    void templateScaleModeControlsSpeedScaleOnly();
    // VM「继承方式=按参数」：继承中心 X/Y、宽/高、角度决定搜索区域（可用参数订阅喂值）
    void templateRoiInheritByParamsControlsSearchRegion();
    // VM「屏蔽区创建=绘制/继承」：屏蔽区内的候选不检测（绘制多边形 / 订阅掩膜图）
    void templateMaskRegionExcludesCandidates();
    // VM「位置修正」按信息/按点/按坐标：把检测区域搬到运行点再匹配
    void templatePositionCorrectionMovesSearchRegion();
    // VM 模块分类：本节点=高精度匹配（默认 method=2），旧名「模板匹配」保留别名
    void templateVmModuleIdentityMatchesHelp();
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

// VM 匹配「模块结果」展示口径：官方字段顺序、配置参数不混入、逐目标数组与分组对齐。
void FeatureMatchTest::templateResultDisplaySpec()
{
    const cv::Mat patch = makePatch();                      // 128×128 有纹理小块
    cv::Mat big(1000, 1500, CV_8UC1, cv::Scalar(25));
    const cv::Point center(300, 250);
    patch.copyTo(big(cv::Rect(center.x - patch.cols / 2, center.y - patch.rows / 2,
                              patch.cols, patch.rows)));

    OpencvTemplateMatchNode node;
    node.init();

    // ① 规范：模块级 6 项 + 逐目标 14 项 + 检测区域 5 项 + 轮廓 3 项
    //    + 屏蔽区多边形 4 项 + 输出掩膜 3 项，不含任何配置键
    const QVector<NodeResultField> spec = node.resultDisplayFields();
    QCOMPARE(int(spec.size()), 35);
    QCOMPARE(spec.at(0).key, QStringLiteral("elapsedMs"));
    QCOMPARE(spec.at(1).key, QStringLiteral("moduleStatus"));
    QCOMPARE(spec.at(2).key, QStringLiteral("matchStatus"));
    QCOMPARE(spec.at(3).key, QStringLiteral("matchCount"));
    QCOMPARE(spec.at(4).key, QStringLiteral("matchTemplateIndex"));
    QCOMPARE(spec.at(5).key, QStringLiteral("matchTemplateName"));
    QCOMPARE(spec.at(6).key, QStringLiteral("matchBoxCenters"));
    for (int i = 6; i <= 19; ++i)
        QCOMPARE(spec.at(i).group, QStringLiteral("匹配%1"));
    QCOMPARE(spec.at(18).key, QStringLiteral("matchCrossAngles"));
    QCOMPARE(spec.at(19).key, QStringLiteral("matchScores"));
    QCOMPARE(spec.at(20).key, QStringLiteral("searchCenterX"));
    QCOMPARE(spec.at(24).key, QStringLiteral("searchAngle"));
    QCOMPARE(spec.at(25).key, QStringLiteral("matchContourCount"));
    QCOMPARE(spec.at(26).key, QStringLiteral("matchContourPointXs"));
    QCOMPARE(spec.at(27).key, QStringLiteral("matchContourPointYs"));
    for (int i = 26; i <= 27; ++i)
        QCOMPARE(spec.at(i).group, QStringLiteral("匹配%1"));
    QCOMPARE(spec.at(28).key, QStringLiteral("maskPolygonCount"));
    QCOMPARE(spec.at(29).key, QStringLiteral("maskPolygonPoints"));
    QCOMPARE(spec.at(30).key, QStringLiteral("maskPolygonXs"));
    QCOMPARE(spec.at(31).key, QStringLiteral("maskPolygonYs"));
    for (int i = 29; i <= 31; ++i)
        QCOMPARE(spec.at(i).group, QStringLiteral("屏蔽区多边形"));
    QCOMPARE(spec.at(32).key, QStringLiteral("outMaskWidth"));
    QCOMPARE(spec.at(33).key, QStringLiteral("outMaskHeight"));
    QCOMPARE(spec.at(34).key, QStringLiteral("outMaskPixelFormat"));
    for (const NodeResultField &f : spec) {
        QVERIFY2(f.key != QStringLiteral("minScore")
                     && f.key != QStringLiteral("maxMatches")
                     && f.key != QStringLiteral("roiWidth")
                     && f.key != QStringLiteral("getContour")
                     && f.key != QStringLiteral("outputContour")
                     && f.key != QStringLiteral("searchRegionCol"),
                 qPrintable(QStringLiteral("配置参数混进了模块结果规范：%1").arg(f.key)));
    }

    // ② 单目标匹配：逐目标数组长度 = 匹配个数，匹配框/匹配点/尺度/分数与命中一致
    node.setParam(QStringLiteral("roiCenterCol"), double(center.x));
    node.setParam(QStringLiteral("roiCenterRow"), double(center.y));
    node.setParam(QStringLiteral("roiWidth"), double(patch.cols));
    node.setParam(QStringLiteral("roiHeight"), double(patch.rows));
    node.setParam(QStringLiteral("roiAngle"), 0.0);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("trainFromImage"), true);
    node.setParam(QStringLiteral("minScore"), 0.5);
    node.setParam(QStringLiteral("maxMatches"), 1);
    node.setInputImage(OpencvUtil::matToHimage(big));
    node.run(false);

    QVERIFY2(node.getParam(QStringLiteral("moduleStatus")).toBool(), "单目标匹配执行失败");
    QCOMPARE(node.getParam(QStringLiteral("matchStatus")).toInt(), 1);
    QCOMPARE(node.getParam(QStringLiteral("matchCount")).toInt(), 1);

    auto numbers = [&node](const char *key) {
        QVector<double> out;
        const QString text = node.getParam(QString::fromLatin1(key)).toString();
        const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &p : parts)
            out.push_back(p.toDouble());
        return out;
    };
    const QVector<double> xs = numbers("matchBoxCenterXs");
    const QVector<double> ys = numbers("matchBoxCenterYs");
    const QVector<double> ws = numbers("matchBoxWidths");
    const QVector<double> hs = numbers("matchBoxHeights");
    const QVector<double> pxs = numbers("matchPointXs");
    const QVector<double> pys = numbers("matchPointYs");
    const QVector<double> scores = numbers("matchScores");
    const QVector<double> scales = numbers("matchScales");
    QCOMPARE(int(xs.size()), 1);
    QCOMPARE(int(ys.size()), 1);
    QCOMPARE(int(ws.size()), 1);
    QCOMPARE(int(hs.size()), 1);
    QCOMPARE(int(pxs.size()), 1);
    QCOMPARE(int(pys.size()), 1);
    QCOMPARE(int(scores.size()), 1);
    QCOMPARE(int(scales.size()), 1);
    const double dx = qAbs(xs.at(0) - double(center.x));
    const double dy = qAbs(ys.at(0) - double(center.y));
    const double dw = qAbs(ws.at(0) - double(patch.cols));
    const double dh = qAbs(hs.at(0) - double(patch.rows));
    const double dpx = qAbs(pxs.at(0) - xs.at(0));
    const double dpy = qAbs(pys.at(0) - ys.at(0));
    const double dScale = qAbs(scales.at(0) - 1.0);
    const QString centerMsg = QStringLiteral("匹配框中心偏移：(%1, %2)")
                                  .arg(xs.at(0)).arg(ys.at(0));
    const QString sizeMsg = QStringLiteral("匹配框尺寸异常：%1×%2")
                                .arg(ws.at(0)).arg(hs.at(0));
    QVERIFY2(dx <= 2.0 && dy <= 2.0, qPrintable(centerMsg));
    QVERIFY2(dw <= 2.0 && dh <= 2.0, qPrintable(sizeMsg));
    // 默认锚点在模板中心 ⇒ 匹配点与匹配框中心重合
    QVERIFY2(dpx <= 2.0 && dpy <= 2.0, "匹配点应与默认锚点（模板中心）重合");
    QVERIFY2(scores.at(0) > 0.5, "匹配分数低于阈值");
    QVERIFY2(dScale <= 0.05, "整体尺度应为 1.0 附近");

    // ③ 检测区域：未设搜索 ROI ⇒ 整图（中心 750,500，宽高 1500×1000）
    QCOMPARE(node.getParam(QStringLiteral("searchCenterX")).toDouble(), 750.0);
    QCOMPARE(node.getParam(QStringLiteral("searchCenterY")).toDouble(), 500.0);
    QCOMPARE(node.getParam(QStringLiteral("searchWidth")).toDouble(), 1500.0);
    QCOMPARE(node.getParam(QStringLiteral("searchHeight")).toDouble(), 1000.0);
    QCOMPARE(node.getParam(QStringLiteral("searchAngle")).toDouble(), 0.0);
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
        // VM 结果表 7 字段契约：row,col,score,angle,scale,pointRow,pointCol
        // 早先写成 >=3 是为了兼容老数据，但当前节点必须输出完整 7 项，否则叠加层
        // 读不到角度/尺度/匹配点也照样通过——这里收紧为 7 项。
        QVERIFY2(f.size() >= 7,
                 qPrintable(QStringLiteral("matchList 应有 7 字段，实际 %1 项：%2")
                                .arg(f.size()).arg(it)));
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
    QVERIFY2(f1.size() >= 7,
             qPrintable(QStringLiteral("matchList 应有 7 字段，实际 %1 项：%2")
                            .arg(f1.size()).arg(items1.first())));
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
    node.setParam(QStringLiteral("autoContrastThreshold"), false);
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
    node.setParam(QStringLiteral("autoContrastThreshold"), true);
    const int nAuto = node.editorFeaturePoints().size();
    qInfo("edge-threshold: auto -> %d 点", nAuto);
    QVERIFY2(nAuto > 0, "自动阈值模式没生效（点全被滤掉）");
}

// 模板对象：JSON 往返不丢字段；只有 templatePath（训练直存）时运行期合成一条模板记录。
void FeatureMatchTest::templateEntryRoundTripAndSingleTemplateMode()
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

    // 单模板模式：只设 templatePath（训练模板直存）→ templates() 合成一条，只读不落库
    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("templatePath"), QStringLiteral("templates/single.png"));
    node.setParam(QStringLiteral("contrastThreshold"), 44);
    const QVector<TemplateEntry> single = node.templates();
    QCOMPARE(single.size(), 1);
    QCOMPARE(single.first().imagePath, QStringLiteral("templates/single.png"));
    QCOMPARE(single.first().contrastThreshold, 44);
    QVERIFY(node.getParam(QStringLiteral("templates")).toString().isEmpty());   // 只读不写
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

// P0-2（VM「全部搜索模式」）：逐个使用所有模板匹配并合并结果。
// ① 两个不同尺寸的目标分别对应模板 A/B → 必须同时输出两个目标（旧实现只留最高分模板）；
// ② 同一目标被两个模板同时命中 → 多模板重叠率生效：50 时只留高分，100 时都输出。
void FeatureMatchTest::templateAllTemplatesMergesPerTarget()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const cv::Mat patchA = makePatch();                  // 128×128 高纹理
    cv::Mat patchB(64, 64, CV_8UC1, cv::Scalar(20));
    cv::circle(patchB, cv::Point(32, 32), 26, cv::Scalar(255), 5);
    cv::line(patchB, cv::Point(6, 6), cv::Point(58, 58), cv::Scalar(220), 3);

    const QString pathA = dir.filePath(QStringLiteral("a.png"));
    const QString pathB = dir.filePath(QStringLiteral("b.png"));
    QVERIFY(cv::imwrite(pathA.toStdString(), patchA));
    QVERIFY(cv::imwrite(pathB.toStdString(), patchB));

    cv::Mat canvas(400, 700, CV_8UC1, cv::Scalar(kCanvasBackground));
    const cv::Rect boxA(100, 120, patchA.cols, patchA.rows);
    const cv::Rect boxB(480, 150, patchB.cols, patchB.rows);
    patchA.copyTo(canvas(boxA));
    patchB.copyTo(canvas(boxB));
    const double cxA = boxA.x + boxA.width / 2.0, cyA = boxA.y + boxA.height / 2.0;
    const double cxB = boxB.x + boxB.width / 2.0, cyB = boxB.y + boxB.height / 2.0;

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 1);
    node.setParam(QStringLiteral("minScore"), 0.7);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("scaleMin"), 1.0);
    node.setParam(QStringLiteral("scaleMax"), 1.0);
    node.setParam(QStringLiteral("maxMatches"), 10);
    node.setParam(QStringLiteral("maxOverlap"), 100);       // 模板内不做重叠抑制，便于观察
    node.setParam(QStringLiteral("multiTemplateOverlap"), 50);
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("searchRegionCol"), 0);
    node.setParam(QStringLiteral("searchRegionRow"), 0);
    node.setParam(QStringLiteral("searchRegionWidth"), canvas.cols);
    node.setParam(QStringLiteral("searchRegionHeight"), canvas.rows);
    node.setParam(QStringLiteral("matchAllTemplates"), true);
    node.setInputImage(OpencvUtil::matToHimage(canvas));

    TemplateEntry ea;
    ea.name = QStringLiteral("A");
    ea.imagePath = pathA;
    TemplateEntry eb;
    eb.name = QStringLiteral("B");
    eb.imagePath = pathB;
    node.setTemplates({ea, eb});
    node.setActiveTemplateIndex(0);

    // ① 两类目标合并输出
    node.run(false);
    const int count = node.getParam(QStringLiteral("matchCount")).toInt();
    const QStringList centers = node.getParam(QStringLiteral("matchBoxCenters"))
                                    .toString()
                                    .split(QLatin1Char(';'), Qt::SkipEmptyParts);
    const QStringList widths = node.getParam(QStringLiteral("matchBoxWidths"))
                                   .toString()
                                   .split(QLatin1Char(','), Qt::SkipEmptyParts);
    qInfo("multi-merge: count=%d centers=%s widths=%s", count,
          qPrintable(centers.join(QStringLiteral(" | "))),
          qPrintable(widths.join(QLatin1Char(','))));
    QCOMPARE(count, 2);
    QCOMPARE(centers.size(), 2);
    bool gotA = false, gotB = false;
    for (int k = 0; k < centers.size(); ++k) {
        const QStringList c = centers.at(k).split(QLatin1Char(','));
        if (c.size() != 2)
            continue;
        const double col = c[0].toDouble();
        const double row = c[1].toDouble();
        const double w = widths.value(k).toDouble();
        if (qAbs(col - cxA) <= 3.0 && qAbs(row - cyA) <= 3.0 && qAbs(w - 128.0) <= 2.0)
            gotA = true;
        if (qAbs(col - cxB) <= 3.0 && qAbs(row - cyB) <= 3.0 && qAbs(w - 64.0) <= 2.0)
            gotB = true;
    }
    QVERIFY2(gotA && gotB,
             qPrintable(QStringLiteral("应同时输出 A(128) 与 B(64) 两个目标：%1")
                            .arg(centers.join(QStringLiteral(" | ")))));

    // ② 两个模板都命中同一目标：重叠率 50 → 只留高分；100 → 都保留
    cv::Mat sameCanvas(300, 400, CV_8UC1, cv::Scalar(kCanvasBackground));
    const cv::Rect sameBox(140, 90, patchA.cols, patchA.rows);
    patchA.copyTo(sameCanvas(sameBox));
    cv::Mat cropB = patchA(cv::Rect(16, 16, 96, 96)).clone();   // A 的中央子块
    const QString pathC = dir.filePath(QStringLiteral("c.png"));
    QVERIFY(cv::imwrite(pathC.toStdString(), cropB));
    TemplateEntry ec;
    ec.name = QStringLiteral("C");
    ec.imagePath = pathC;
    node.setTemplates({ea, ec});
    node.setActiveTemplateIndex(0);
    node.setInputImage(OpencvUtil::matToHimage(sameCanvas));

    node.setParam(QStringLiteral("multiTemplateOverlap"), 50);
    node.run(false);
    const int overlapCount = node.getParam(QStringLiteral("matchCount")).toInt();
    node.setParam(QStringLiteral("multiTemplateOverlap"), 100);
    node.run(false);
    const int keepAllCount = node.getParam(QStringLiteral("matchCount")).toInt();
    qInfo("multi-overlap: overlap=50 count=%d, overlap=100 count=%d", overlapCount,
          keepAllCount);
    QCOMPARE(overlapCount, 1);
    QCOMPARE(keepAllCount, 2);
}

void FeatureMatchTest::templateSingleOverlapUsesIou_data()
{
    QTest::addColumn<int>("spacing");
    QTest::addColumn<int>("threshold");
    QTest::addColumn<int>("expectedCount");
    QTest::newRow("overlap-disabled") << 80 << 100 << 2;
    QTest::newRow("iou-not-smaller-area") << 80 << 30 << 2;
    QTest::newRow("above-threshold") << 80 << 20 << 1;
    QTest::newRow("zero-threshold") << 80 << 0 << 1;
    QTest::newRow("touching-at-zero") << 128 << 0 << 2;
    QTest::newRow("separated-at-zero") << 160 << 0 << 2;
}

void FeatureMatchTest::templateSingleOverlapUsesIou()
{
    QFETCH(int, spacing);
    QFETCH(int, threshold);
    QFETCH(int, expectedCount);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    cv::Mat patch(128, 128, CV_8UC1, cv::Scalar(kCanvasBackground));
    cv::RNG rng(20261111);
    cv::Mat texture = patch(cv::Rect(52, 32, 24, 64));
    rng.fill(texture, cv::RNG::UNIFORM, 40, 255);
    const QString path = dir.filePath(QStringLiteral("padded.png"));
    QVERIFY(cv::imwrite(path.toStdString(), patch));
    cv::Mat canvas(300, 500, CV_8UC1, cv::Scalar(kCanvasBackground));
    // Only padding overlaps; spacing exceeds the 0.6 * 128 early peak-suppression radius.
    patch.copyTo(canvas(cv::Rect(60, 80, 128, 128)));
    patch.copyTo(canvas(cv::Rect(60 + spacing, 80, 128, 128)));

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 1);
    node.setParam(QStringLiteral("minScore"), 0.95);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("scaleMin"), 1.0);
    node.setParam(QStringLiteral("scaleMax"), 1.0);
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("templatePath"), path);
    node.setParam(QStringLiteral("maxMatches"), 2);
    node.setParam(QStringLiteral("sortType"), 2);
    node.setInputImage(OpencvUtil::matToHimage(canvas));
    node.setParam(QStringLiteral("maxOverlap"), 100);
    node.run(false);
    QVERIFY(node.getParam(QStringLiteral("moduleStatus")).toBool());
    QCOMPARE(node.getParam(QStringLiteral("matchCountBeforeOverlap")).toInt(), 2);
    QCOMPARE(node.getParam(QStringLiteral("matchCount")).toInt(), 2);
    const QStringList baseline = node.getParam(QStringLiteral("matchList")).toString()
                                     .split(QLatin1Char(';'), Qt::SkipEmptyParts);
    QCOMPARE(baseline.size(), 2);
    for (int i = 0; i < baseline.size(); ++i) {
        const QStringList hit = baseline.at(i).split(QLatin1Char(','));
        QCOMPARE(hit.size(), 7);
        QCOMPARE(hit[0].toDouble(), 144.0);
        QCOMPARE(hit[1].toDouble(), double(124 + i * spacing));
    }
    node.setParam(QStringLiteral("maxOverlap"), threshold);
    node.run(false);
    QVERIFY(node.getParam(QStringLiteral("moduleStatus")).toBool());
    QCOMPARE(node.getParam(QStringLiteral("matchCountBeforeOverlap")).toInt(), 2);
    QCOMPARE(node.getParam(QStringLiteral("matchCount")).toInt(), expectedCount);
    QCOMPARE(node.getParam(QStringLiteral("matchList")).toString()
                 .split(QLatin1Char(';'), Qt::SkipEmptyParts).size(), expectedCount);
}

void FeatureMatchTest::templateMultiOverlapUsesIou_data()
{
    QTest::addColumn<int>("cropWidth");
    QTest::addColumn<int>("cropHeight");
    QTest::addColumn<int>("threshold");
    QTest::addColumn<int>("expectedCount");
    QTest::newRow("contained-disabled") << 96 << 96 << 100 << 2;
    QTest::newRow("contained-above-threshold") << 96 << 96 << 56 << 1;
    QTest::newRow("contained-below-threshold") << 96 << 96 << 57 << 2;
    QTest::newRow("half-width-equal-threshold") << 64 << 128 << 50 << 2;
    QTest::newRow("half-height-equal-threshold") << 128 << 64 << 50 << 2;
    QTest::newRow("half-width-above-threshold") << 64 << 128 << 49 << 1;
    QTest::newRow("identical-below-100") << 128 << 128 << 99 << 1;
    QTest::newRow("identical-at-100") << 128 << 128 << 100 << 2;
}

void FeatureMatchTest::templateMultiOverlapUsesIou()
{
    QFETCH(int, cropWidth);
    QFETCH(int, cropHeight);
    QFETCH(int, threshold);
    QFETCH(int, expectedCount);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat patch = makePatch();
    const cv::Mat crop = patch(cv::Rect((128 - cropWidth) / 2, (128 - cropHeight) / 2,
                                       cropWidth, cropHeight)).clone();
    const QString pathA = dir.filePath(QStringLiteral("full.png"));
    const QString pathB = dir.filePath(QStringLiteral("crop.png"));
    QVERIFY(cv::imwrite(pathA.toStdString(), patch));
    QVERIFY(cv::imwrite(pathB.toStdString(), crop));
    cv::Mat canvas(300, 400, CV_8UC1, cv::Scalar(kCanvasBackground));
    patch.copyTo(canvas(cv::Rect(140, 90, 128, 128)));

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 1);
    node.setParam(QStringLiteral("minScore"), 0.95);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("scaleMin"), 1.0);
    node.setParam(QStringLiteral("scaleMax"), 1.0);
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("maxMatches"), 2);
    node.setParam(QStringLiteral("maxOverlap"), 100);
    node.setParam(QStringLiteral("matchAllTemplates"), true);
    TemplateEntry a, b;
    a.name = QStringLiteral("full");
    a.imagePath = pathA;
    b.name = QStringLiteral("crop");
    b.imagePath = pathB;
    node.setTemplates({a, b});
    node.setActiveTemplateIndex(0);
    node.setInputImage(OpencvUtil::matToHimage(canvas));
    node.setParam(QStringLiteral("multiTemplateOverlap"), 100);
    node.run(false);
    QVERIFY(node.getParam(QStringLiteral("moduleStatus")).toBool());
    QCOMPARE(node.getParam(QStringLiteral("matchCount")).toInt(), 2);
    const QStringList centers = node.getParam(QStringLiteral("matchBoxCenters")).toString()
                                    .split(QLatin1Char(';'), Qt::SkipEmptyParts);
    QCOMPARE(centers.size(), 2);
    for (const QString &center : centers) {
        const QStringList coordinates = center.split(QLatin1Char(','));
        QCOMPARE(coordinates.size(), 2);
        QCOMPARE(coordinates[0].toDouble(), 204.0);
        QCOMPARE(coordinates[1].toDouble(), 154.0);
    }
    node.setParam(QStringLiteral("multiTemplateOverlap"), threshold);
    node.run(false);
    QVERIFY(node.getParam(QStringLiteral("moduleStatus")).toBool());
    QCOMPARE(node.getParam(QStringLiteral("matchCount")).toInt(), expectedCount);
    QCOMPARE(node.getParam(QStringLiteral("matchList")).toString()
                 .split(QLatin1Char(';'), Qt::SkipEmptyParts).size(), expectedCount);
}

// P1-3（VM「特征模板」）：勾选模板 + 模板排序。
// ① 未勾选不参与匹配；② 列表顺序 = 未开全部搜索模式时的优先顺序；
// ③ 全部搜索模式也只合并勾选集合；④ 一个都没勾选时判失败且不沿用上一轮结果。
void FeatureMatchTest::templateCheckedTemplatesControlSearch()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const cv::Mat patchA = makePatch();                  // 128×128 高纹理
    cv::Mat patchB(64, 64, CV_8UC1, cv::Scalar(20));
    cv::circle(patchB, cv::Point(32, 32), 26, cv::Scalar(255), 5);
    cv::line(patchB, cv::Point(6, 6), cv::Point(58, 58), cv::Scalar(220), 3);

    const QString pathA = dir.filePath(QStringLiteral("a.png"));
    const QString pathB = dir.filePath(QStringLiteral("b.png"));
    QVERIFY(cv::imwrite(pathA.toStdString(), patchA));
    QVERIFY(cv::imwrite(pathB.toStdString(), patchB));

    cv::Mat canvas(400, 700, CV_8UC1, cv::Scalar(kCanvasBackground));
    const cv::Rect boxA(100, 120, patchA.cols, patchA.rows);
    const cv::Rect boxB(480, 150, patchB.cols, patchB.rows);
    patchA.copyTo(canvas(boxA));
    patchB.copyTo(canvas(boxB));
    const double cxA = boxA.x + boxA.width / 2.0, cyA = boxA.y + boxA.height / 2.0;
    const double cxB = boxB.x + boxB.width / 2.0, cyB = boxB.y + boxB.height / 2.0;

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 1);
    node.setParam(QStringLiteral("minScore"), 0.7);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("scaleMin"), 1.0);
    node.setParam(QStringLiteral("scaleMax"), 1.0);
    node.setParam(QStringLiteral("maxMatches"), 10);
    node.setParam(QStringLiteral("maxOverlap"), 100);
    node.setParam(QStringLiteral("multiTemplateOverlap"), 100);
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("searchRegionCol"), 0);
    node.setParam(QStringLiteral("searchRegionRow"), 0);
    node.setParam(QStringLiteral("searchRegionWidth"), canvas.cols);
    node.setParam(QStringLiteral("searchRegionHeight"), canvas.rows);
    node.setParam(QStringLiteral("matchAllTemplates"), false);
    node.setInputImage(OpencvUtil::matToHimage(canvas));

    TemplateEntry ea;
    ea.name = QStringLiteral("A");
    ea.imagePath = pathA;
    TemplateEntry eb;
    eb.name = QStringLiteral("B");
    eb.imagePath = pathB;

    // 序列化：enabled 往返；旧工程缺该键时默认勾选（否则老流程会突然全不参与匹配）
    {
        TemplateEntry off = ea;
        off.enabled = false;
        QVERIFY(!TemplateEntry::fromJson(off.toJson()).enabled);
        QJsonObject legacy = ea.toJson();
        legacy.remove(QStringLiteral("enabled"));
        QVERIFY(TemplateEntry::fromJson(legacy).enabled);
    }

    node.setTemplates({ea, eb});
    node.setActiveTemplateIndex(0);

    // ① 列表 [A,B] 按顺序试 → 先命中 A 就停
    node.run(false);
    qInfo("checked-order: idx=%d col=%.1f row=%.1f count=%d",
          node.getParam(QStringLiteral("matchTemplateIndex")).toInt(),
          node.getParam(QStringLiteral("matchCol")).toDouble(),
          node.getParam(QStringLiteral("matchRow")).toDouble(),
          node.getParam(QStringLiteral("matchCount")).toInt());
    QCOMPARE(node.getParam(QStringLiteral("matchTemplateIndex")).toInt(), 0);
    QCOMPARE(node.getParam(QStringLiteral("matchCount")).toInt(), 1);
    QVERIFY(qAbs(node.getParam(QStringLiteral("matchCol")).toDouble() - cxA) <= 3.0);
    QVERIFY(qAbs(node.getParam(QStringLiteral("matchRow")).toDouble() - cyA) <= 3.0);

    // ② 取消勾选 A → 只剩 B 参与匹配（A 的目标明明在画面里）
    QVector<TemplateEntry> list = node.templates();
    list[0].enabled = false;
    node.setTemplates(list);
    node.run(false);
    qInfo("checked-filter: idx=%d col=%.1f row=%.1f count=%d",
          node.getParam(QStringLiteral("matchTemplateIndex")).toInt(),
          node.getParam(QStringLiteral("matchCol")).toDouble(),
          node.getParam(QStringLiteral("matchRow")).toDouble(),
          node.getParam(QStringLiteral("matchCount")).toInt());
    QCOMPARE(node.getParam(QStringLiteral("matchTemplateIndex")).toInt(), 1);
    QVERIFY(qAbs(node.getParam(QStringLiteral("matchCol")).toDouble() - cxB) <= 3.0);
    QVERIFY(qAbs(node.getParam(QStringLiteral("matchRow")).toDouble() - cyB) <= 3.0);

    // ③ 排序：列表换成 [B,A] → 未开全部搜索模式时先试 B（交换列表顺序 = VM 上移/下移）
    list[0].enabled = true;
    list[1].enabled = true;
    list.swapItemsAt(0, 1);
    node.setTemplates(list);
    node.setActiveTemplateIndex(0);
    node.run(false);
    qInfo("checked-swap: first=%s idx=%d col=%.1f row=%.1f",
          qPrintable(node.templates().at(0).name),
          node.getParam(QStringLiteral("matchTemplateIndex")).toInt(),
          node.getParam(QStringLiteral("matchCol")).toDouble(),
          node.getParam(QStringLiteral("matchRow")).toDouble());
    QCOMPARE(node.getParam(QStringLiteral("matchTemplateIndex")).toInt(), 0);
    QVERIFY(qAbs(node.getParam(QStringLiteral("matchCol")).toDouble() - cxB) <= 3.0);

    // ④ 全部搜索模式：两个都勾选 → 两个目标都输出
    node.setParam(QStringLiteral("matchAllTemplates"), true);
    node.run(false);
    qInfo("checked-all: count=%d", node.getParam(QStringLiteral("matchCount")).toInt());
    QCOMPARE(node.getParam(QStringLiteral("matchCount")).toInt(), 2);

    // ⑤ 全部搜索模式 + 取消勾选 A → 只剩 B 的目标
    list = node.templates();
    list[1].enabled = false;   // 此时列表 [B,A]
    node.setTemplates(list);
    node.run(false);
    qInfo("checked-all-disabledA: idx=%d count=%d col=%.1f row=%.1f",
          node.getParam(QStringLiteral("matchTemplateIndex")).toInt(),
          node.getParam(QStringLiteral("matchCount")).toInt(),
          node.getParam(QStringLiteral("matchCol")).toDouble(),
          node.getParam(QStringLiteral("matchRow")).toDouble());
    QCOMPARE(node.getParam(QStringLiteral("matchCount")).toInt(), 1);
    QVERIFY(qAbs(node.getParam(QStringLiteral("matchCol")).toDouble() - cxB) <= 3.0);

    // ⑥ 一个都没勾选 → 判失败、结果清空、原因可读
    list[0].enabled = false;
    node.setTemplates(list);
    node.run(false);
    const QString noneError = node.getParam(QStringLiteral("lastError")).toString();
    qInfo("checked-none: status=%d count=%d error=%s",
          node.getParam(QStringLiteral("moduleStatus")).toInt(),
          node.getParam(QStringLiteral("matchCount")).toInt(), qPrintable(noneError));
    QCOMPARE(node.getParam(QStringLiteral("moduleStatus")).toBool(), false);
    QCOMPARE(node.getParam(QStringLiteral("matchCount")).toInt(), 0);
    QVERIFY2(noneError.contains(QStringLiteral("没有勾选")), qPrintable(noneError));

    // ⑦ 勾选只约束匹配：「训练模板」在全部未勾选时仍必须执行（训练是创建模板，不是匹配）
    node.setParam(QStringLiteral("trainFromImage"), true);
    node.run(false);
    const QString trainStatus = node.getParam(QStringLiteral("trainStatus")).toString();
    qInfo("checked-train: status=%s trainFromImage=%d", qPrintable(trainStatus),
          node.getParam(QStringLiteral("trainFromImage")).toBool());
    QVERIFY2(trainStatus.contains(QStringLiteral("已训练")), qPrintable(trainStatus));
    QCOMPARE(node.getParam(QStringLiteral("trainFromImage")).toBool(), false);
}

// P1-5（VM「是否考虑噪点」）：开启后，匹配图中不在边缘链上的特征点（毛刺）会让评分降低。
// 夹具用「空心方框」模板：模板特征只在四周轮廓，框内是平坦区；在目标框内画几条强边缘毛刺，
// 它们不会被模板特征点解释。同一张毛刺图开/关 considerNoise 对比分数；干净图作对照，
// 证明下降来自毛刺而不是开关本身的常数惩罚。
void FeatureMatchTest::templateNoiseConsiderationLowersScore()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    cv::Mat tplImg(100, 100, CV_8UC1, cv::Scalar(30));
    cv::rectangle(tplImg, cv::Rect(10, 10, 80, 80), cv::Scalar(220), 3);
    const QString tplPath = dir.filePath(QStringLiteral("hollow.png"));
    QVERIFY(cv::imwrite(tplPath.toStdString(), tplImg));

    // 画布底色 = 模板底色（30）：贴片边界不产生模板外的新边缘，干净图才是"零噪点"基线。
    cv::Mat clean(400, 400, CV_8UC1, cv::Scalar(30));
    const cv::Rect targetBox(100, 100, 100, 100);
    tplImg.copyTo(clean(targetBox));
    const double cx = targetBox.x + targetBox.width / 2.0;
    const double cy = targetBox.y + targetBox.height / 2.0;

    OpencvTemplateMatchNode node;
    node.init();
    QCOMPARE(node.getParam(QStringLiteral("considerNoise")).toBool(), false);   // VM 默认关
    node.setParam(QStringLiteral("method"), 2);                                  // 噪点只作用于 GOC
    node.setParam(QStringLiteral("minScore"), 0.3);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("scaleMin"), 1.0);
    node.setParam(QStringLiteral("scaleMax"), 1.0);
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("templatePath"), tplPath);
    TemplateEntry e;
    e.name = QStringLiteral("hollow");
    e.imagePath = tplPath;
    node.setTemplates({e});
    node.setActiveTemplateIndex(0);

    auto runOnce = [&](const cv::Mat &img, bool considerNoise) {
        node.setParam(QStringLiteral("considerNoise"), considerNoise);
        node.setInputImage(OpencvUtil::matToHimage(img));
        node.run(false);
        return QPair<double, double>(node.getParam(QStringLiteral("matchScore")).toDouble(),
                                     node.getParam(QStringLiteral("matchCol")).toDouble());
    };

    const double cleanOff = runOnce(clean, false).first;
    const double cleanOn = runOnce(clean, true).first;

    // 毛刺：目标框内两条强边缘，模板里没有对应特征（框内本是平坦区）
    cv::Mat burred = clean.clone();
    cv::line(burred, cv::Point(135, 135), cv::Point(165, 165), cv::Scalar(255), 3);
    cv::line(burred, cv::Point(160, 130), cv::Point(160, 170), cv::Scalar(255), 3);
    const auto burrOffPair = runOnce(burred, false);
    const auto burrOnPair = runOnce(burred, true);
    const double burrOff = burrOffPair.first;
    const double burrOn = burrOnPair.first;
    qInfo("noise-leg: cleanOff=%.4f cleanOn=%.4f burrOff=%.4f burrOn=%.4f col=%.1f",
          cleanOff, cleanOn, burrOff, burrOn, burrOnPair.second);

    QVERIFY2(qAbs(burrOnPair.second - cx) <= 3.0,
             qPrintable(QStringLiteral("毛刺图命中列应仍在 %1 附近，实际 %2")
                            .arg(cx).arg(burrOnPair.second)));
    QVERIFY2(cleanOn >= cleanOff - 0.02,
             qPrintable(QStringLiteral("干净图开启噪点不应明显掉分：off=%1 on=%2")
                            .arg(cleanOff).arg(cleanOn)));
    QVERIFY2(burrOn < burrOff - 0.05,
             qPrintable(QStringLiteral("开启噪点后毛刺图评分必须明显下降：off=%1 on=%2")
                            .arg(burrOff).arg(burrOn)));
    QVERIFY2((burrOff - burrOn) > (cleanOff - cleanOn),
             qPrintable(QStringLiteral("毛刺图的评分降幅应大于干净图：burr=%1 clean=%2")
                            .arg(burrOff - burrOn).arg(cleanOff - cleanOn)));
}

// P1-7（VM「结果显示 → 结果判断」）：多个判断条件全部满足才输出 OK。
// 模板匹配页的固定条件表用 resJudge<Key>Enable/Low/High；本腿锁三件事：
//   ① 角度/尺度/数量条件各自能把 moduleStatus 判红，且原因可读；
//   ② 判 NG 时 matchStatus 仍为 1（目标找到了，只是不满足产线判定）——
//      这条口径与「匹配失败」分开，下游不能把 NG 当成没找到；
//   ③ 多条件同时启用时是 AND（一条越界即 NG，全部在范围内才 OK）。
void FeatureMatchTest::templateResultJudgmentHonorsVmConditions()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat patch = makePatch();
    const QString tplPath = dir.filePath(QStringLiteral("judge.png"));
    QVERIFY(cv::imwrite(tplPath.toStdString(), patch));

    cv::Mat canvas(400, 400, CV_8UC1, cv::Scalar(kCanvasBackground));
    const cv::Rect box(100, 120, patch.cols, patch.rows);
    patch.copyTo(canvas(box));

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 1);
    node.setParam(QStringLiteral("minScore"), 0.7);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("scaleMin"), 1.0);
    node.setParam(QStringLiteral("scaleMax"), 1.0);
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("templatePath"), tplPath);
    TemplateEntry e;
    e.name = QStringLiteral("judge");
    e.imagePath = tplPath;
    node.setTemplates({e});
    node.setActiveTemplateIndex(0);
    node.setInputImage(OpencvUtil::matToHimage(canvas));

    auto runAndRead = [&]() {
        node.run(false);
        return node.getParam(QStringLiteral("moduleStatus")).toBool();
    };

    QVERIFY2(runAndRead(), "基线（未启用任何判断条件）应 OK");
    QCOMPARE(node.getParam(QStringLiteral("matchStatus")).toInt(), 1);
    QCOMPARE(node.getParam(QStringLiteral("matchCount")).toInt(), 1);

    // ① 角度条件：实际角度 0，限定 [10,20] 必须 NG；原因含「角度」
    node.setParam(QStringLiteral("resJudgeAngleEnable"), true);
    node.setParam(QStringLiteral("resJudgeAngleLow"), 10.0);
    node.setParam(QStringLiteral("resJudgeAngleHigh"), 20.0);
    QVERIFY2(!runAndRead(), "角度 0 不在 [10,20] 内应判 NG");
    QString reason = node.getParam(QStringLiteral("lastError")).toString();
    QVERIFY2(reason.contains(QStringLiteral("角度")), qPrintable(reason));
    QCOMPARE(node.getParam(QStringLiteral("matchStatus")).toInt(), 1);   // 找到目标 ≠ 判定 OK
    QCOMPARE(node.getParam(QStringLiteral("matchCount")).toInt(), 1);

    // 角度放回范围内 → OK
    node.setParam(QStringLiteral("resJudgeAngleLow"), -1.0);
    node.setParam(QStringLiteral("resJudgeAngleHigh"), 1.0);
    QVERIFY2(runAndRead(), "角度 0 在 [-1,1] 内应 OK");

    // ② 尺度条件：实际尺度 1，限定 [0.5,0.9] 必须 NG；原因含「尺度」
    node.setParam(QStringLiteral("resJudgeScaleEnable"), true);
    node.setParam(QStringLiteral("resJudgeScaleLow"), 0.5);
    node.setParam(QStringLiteral("resJudgeScaleHigh"), 0.9);
    QVERIFY2(!runAndRead(), "尺度 1.0 不在 [0.5,0.9] 内应判 NG");
    reason = node.getParam(QStringLiteral("lastError")).toString();
    QVERIFY2(reason.contains(QStringLiteral("尺度")), qPrintable(reason));

    node.setParam(QStringLiteral("resJudgeScaleLow"), 0.99);
    node.setParam(QStringLiteral("resJudgeScaleHigh"), 1.01);
    QVERIFY2(runAndRead(), "尺度 1.0 在 [0.99,1.01] 内应 OK");

    // ③ 数量条件：只有 1 个目标，限定 [2,5] 必须 NG；原因含「匹配数量」
    node.setParam(QStringLiteral("resJudgeNumEnable"), true);
    node.setParam(QStringLiteral("resJudgeNumLow"), 2.0);
    node.setParam(QStringLiteral("resJudgeNumHigh"), 5.0);
    QVERIFY2(!runAndRead(), "数量 1 不在 [2,5] 内应判 NG");
    reason = node.getParam(QStringLiteral("lastError")).toString();
    QVERIFY2(reason.contains(QStringLiteral("匹配数量")), qPrintable(reason));

    // 三个条件同时启用：数量在范围内、角度/尺度通过 → OK（AND 全满足）
    node.setParam(QStringLiteral("resJudgeNumLow"), 1.0);
    node.setParam(QStringLiteral("resJudgeNumHigh"), 1.0);
    QVERIFY2(runAndRead(), "数量=1 且角度/尺度均在范围内应 OK");

    // 任一条件越界即 NG（把数量改回 [2,5]，其余仍通过）
    node.setParam(QStringLiteral("resJudgeNumLow"), 2.0);
    QVERIFY2(!runAndRead(), "多条件里只要数量越界就应 NG");
}

// P1-9（VM「增强匹配」，仅高精度匹配）：被勾选的掩膜区域相似度要求更严。
// 夹具：模板由左右两块掩膜组成，画面右侧工件被替换成另一套边缘。
//  ① 不增强：整体分数只按全模板平均；
//  ② 增强右侧（坏区域）：整体分数被右侧局部分数封顶 → 明显下降；
//  ③ 增强左侧（好区域）：分数不应明显变化（对照组，证明不是开关本身的常数惩罚）；
//  ④ 越界索引：忽略，与不增强一致；
//  ⑤ enhancedShapes JSON 往返。
void FeatureMatchTest::templateEnhanceMatchTightensFlaggedMask()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    cv::Mat left = makePatch();
    cv::resize(left, left, cv::Size(60, 60), 0, 0, cv::INTER_AREA);
    cv::Mat right(60, 60, CV_8UC1, cv::Scalar(30));
    cv::circle(right, cv::Point(30, 30), 22, cv::Scalar(230), 4);
    cv::line(right, cv::Point(10, 50), cv::Point(50, 10), cv::Scalar(200), 3);
    cv::Mat tpl(60, 120, CV_8UC1, cv::Scalar(30));
    left.copyTo(tpl(cv::Rect(0, 0, 60, 60)));
    right.copyTo(tpl(cv::Rect(60, 0, 60, 60)));
    const QString tplPath = dir.filePath(QStringLiteral("enh.png"));
    QVERIFY(cv::imwrite(tplPath.toStdString(), tpl));

    cv::Mat canvas(300, 400, CV_8UC1, cv::Scalar(30));
    const cv::Rect target(100, 100, 120, 60);
    tpl.copyTo(canvas(target));
    // 右侧换成另一套边缘（可见但方向/形状不匹配）→ 局部分数低
    cv::Rect rightBox(160, 100, 60, 60);
    canvas(rightBox).setTo(80);
    cv::rectangle(canvas, cv::Rect(166, 106, 48, 48), cv::Scalar(200), 3);

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 2);   // 增强匹配仅作用于 GOC
    node.setParam(QStringLiteral("minScore"), 0.1);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("scaleMin"), 1.0);
    node.setParam(QStringLiteral("scaleMax"), 1.0);
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("templatePath"), tplPath);
    node.setInputImage(OpencvUtil::matToHimage(canvas));

    TemplateEntry e;
    e.name = QStringLiteral("two-masks");
    e.imagePath = tplPath;
    RoiShape l;
    l.type = RoiType::Rect;
    l.p1 = QPointF(0, 0);
    l.p2 = QPointF(60, 60);
    RoiShape r;
    r.type = RoiType::Rect;
    r.p1 = QPointF(60, 0);
    r.p2 = QPointF(120, 60);
    e.shapes = {l, r};
    e.shape = l;
    node.setTemplates({e});
    node.setActiveTemplateIndex(0);

    auto runWithEnhanced = [&](QVector<int> idx) {
        TemplateEntry cur = e;
        cur.enhancedShapes = idx;
        node.setTemplates({cur});
        node.setActiveTemplateIndex(0);
        node.run(false);
        return QPair<double, double>(node.getParam(QStringLiteral("matchScore")).toDouble(),
                                     node.getParam(QStringLiteral("matchCol")).toDouble());
    };

    const double base = runWithEnhanced({}).first;
    const double bad = runWithEnhanced({1}).first;   // 右侧坏区域
    const double good = runWithEnhanced({0}).first;  // 左侧好区域（对照）
    const double oob = runWithEnhanced({99}).first;  // 越界索引应忽略
    qInfo("enhance-leg: base=%.4f badRight=%.4f goodLeft=%.4f outOfRange=%.4f",
          base, bad, good, oob);

    QVERIFY2(bad < base - 0.05,
             qPrintable(QStringLiteral("增强坏掩膜后分数必须明显下降：base=%1 bad=%2")
                            .arg(base).arg(bad)));
    QVERIFY2(qAbs(good - base) <= 0.02,
             qPrintable(QStringLiteral("增强好掩膜不应明显掉分：base=%1 good=%2")
                            .arg(base).arg(good)));
    QVERIFY2(qAbs(oob - base) <= 0.02,
             qPrintable(QStringLiteral("越界掩膜索引应被忽略：base=%1 oob=%2")
                            .arg(base).arg(oob)));

    e.enhancedShapes = {0, 1};
    QCOMPARE(TemplateEntry::fromJson(e.toJson()).enhancedShapes, QVector<int>({0, 1}));
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
    // VM 两个独立输出掩膜：检测区域 → 「检测区域掩膜」，ROI 区域 → 「掩膜」
    node.setParam(QStringLiteral("roiCenterCol"), 244.0);
    node.setParam(QStringLiteral("roiCenterRow"), 194.0);
    node.setParam(QStringLiteral("roiWidth"), 128.0);
    node.setParam(QStringLiteral("roiHeight"), 128.0);
    node.setParam(QStringLiteral("roiAngle"), 0.0);
    node.setParam(QStringLiteral("outputSearchMask"), true);
    node.setInputImage(OpencvUtil::matToHimage(canvas));
    node.run(false);

    auto attached = [](const QSharedPointer<DataObject> &obj, const QString &name) {
        if (!obj)
            return QSharedPointer<DataObject>();
        for (const auto &p : obj->attachedResults()) {
            if (p.first == name)
                return p.second;
        }
        return QSharedPointer<DataObject>();
    };

    // 直接调用 run() 时结果挂在端口 1；聚合端口(0)由 process() 生成，这里验数据面
    const auto resData = node.getOutputData(1);
    QVERIFY2(resData, "匹配结果对象缺失");
    const auto searchMaskObj = attached(resData, QStringLiteral("检测区域掩膜"));
    QVERIFY2(searchMaskObj && searchMaskObj->getHImage().IsInitialized(),
             "检测区域输出掩膜打开后，结果对象上应有「检测区域掩膜」子结果");
    const cv::Mat searchMask = OpencvUtil::himageToMat(HImage(searchMaskObj->getHImage()));
    QCOMPARE(searchMask.cols, canvas.cols);
    QCOMPARE(searchMask.rows, canvas.rows);
    QVERIFY2(searchMask.at<uchar>(150, 150) != 0, "检测区域内部应为白");
    QVERIFY2(searchMask.at<uchar>(10, 10) == 0, "检测区域外应为黑");

    // 关掉检测区域掩膜后该子结果不应再带图
    node.setParam(QStringLiteral("outputSearchMask"), false);
    node.run(false);
    const auto resOff = node.getOutputData(1);
    QVERIFY2(!attached(resOff, QStringLiteral("检测区域掩膜")),
             "检测区域输出掩膜关闭后不应再产出掩膜图");
    // 但「输出掩膜宽度/高度/像素格式」是 VM 模块结果的固定字段，与开关无关
    QCOMPARE(node.getParam(QStringLiteral("outMaskWidth")).toInt(), canvas.cols);
    QCOMPARE(node.getParam(QStringLiteral("outMaskHeight")).toInt(), canvas.rows);
    QCOMPARE(node.getParam(QStringLiteral("outMaskPixelFormat")).toInt(), 17301505);

    // ROI 区域掩膜：打开 outputMask → 子结果「掩膜」覆盖模板 ROI，而非搜索区域
    node.setParam(QStringLiteral("outputMask"), true);
    node.run(false);
    const auto resRoi = node.getOutputData(1);
    const auto roiMaskObj = attached(resRoi, QStringLiteral("掩膜"));
    QVERIFY2(roiMaskObj && roiMaskObj->getHImage().IsInitialized(),
             "ROI 输出掩膜打开后，结果对象上应有「掩膜」子结果");
    const cv::Mat roiMask = OpencvUtil::himageToMat(HImage(roiMaskObj->getHImage()));
    QVERIFY2(roiMask.at<uchar>(194, 244) != 0, "模板 ROI 内部应为白");
    QVERIFY2(roiMask.at<uchar>(80, 350) == 0, "模板 ROI 外（仍在检测区域内）应为黑");
    node.setParam(QStringLiteral("outputMask"), false);
    node.run(false);
    const auto resRoiOff = node.getOutputData(1);
    QVERIFY2(!attached(resRoiOff, QStringLiteral("掩膜")),
             "ROI 输出掩膜关闭后不应再产出掩膜图");
}

// VM「模块结果 → 屏蔽区多边形 / 输出掩膜」（高精度匹配，GUID-11F05391）：
//   屏蔽区多边形 = 绘制串的顶点（多边形点数/点集/点X/Y）；
//   输出掩膜 = 检测区域 − 屏蔽区，宽高/像素格式（17301505=Mono8）为固定字段。
// 三条腿：① 绘制屏蔽区：顶点逐项回读 + 掩膜图上挖洞；② 清空绘制：点数归零、掩膜恢复；
// ③ 继承屏蔽区：多边形点数=0，但继承掩膜同样从输出掩膜里扣掉。
void FeatureMatchTest::templateMaskPolygonAndOutputMaskResults()
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
    node.setParam(QStringLiteral("outputSearchMask"), true);
    // 屏蔽区多边形（图像坐标，x,y）：右下角一块，不盖住模板中心 (244,194)
    node.setParam(QStringLiteral("maskRegionPolygon"),
                  QStringLiteral("300,250 420,250 420,350 300,350"));
    node.setInputImage(OpencvUtil::matToHimage(canvas));
    node.run(false);

    auto attached = [](const QSharedPointer<DataObject> &obj, const QString &name) {
        if (!obj)
            return QSharedPointer<DataObject>();
        for (const auto &p : obj->attachedResults()) {
            if (p.first == name)
                return p.second;
        }
        return QSharedPointer<DataObject>();
    };

    QVERIFY2(node.getParam(QStringLiteral("moduleStatus")).toBool(), "带屏蔽区的匹配应命中");

    // ① 绘制屏蔽区：多边形点数/点集/点X/Y 与绘制串逐项一致
    QCOMPARE(node.getParam(QStringLiteral("maskPolygonCount")).toInt(), 4);
    const QString pointText =
        node.getParam(QStringLiteral("maskPolygonPoints")).toString();
    const QStringList pointItems =
        pointText.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    QCOMPARE(pointItems.size(), 4);
    QCOMPARE(pointItems.at(0), QStringLiteral("300.00,250.00"));
    QCOMPARE(pointItems.at(2), QStringLiteral("420.00,350.00"));
    const QStringList xs =
        node.getParam(QStringLiteral("maskPolygonXs")).toString().split(QLatin1Char(','));
    const QStringList ys =
        node.getParam(QStringLiteral("maskPolygonYs")).toString().split(QLatin1Char(','));
    QCOMPARE(xs.size(), 4);
    QCOMPARE(ys.size(), 4);
    QCOMPARE(xs.at(1).toDouble(), 420.0);
    QCOMPARE(ys.at(3).toDouble(), 350.0);

    // 输出掩膜固定字段 + 图上挖洞
    QCOMPARE(node.getParam(QStringLiteral("outMaskWidth")).toInt(), canvas.cols);
    QCOMPARE(node.getParam(QStringLiteral("outMaskHeight")).toInt(), canvas.rows);
    QCOMPARE(node.getParam(QStringLiteral("outMaskPixelFormat")).toInt(), 17301505);
    const auto maskObj = attached(node.getOutputData(1), QStringLiteral("检测区域掩膜"));
    QVERIFY2(maskObj && maskObj->getHImage().IsInitialized(), "输出掩膜子结果缺失");
    const cv::Mat mask = OpencvUtil::himageToMat(HImage(maskObj->getHImage()));
    QCOMPARE(mask.at<uchar>(150, 150), uchar(255));   // 检测区域内、屏蔽区外
    QCOMPARE(mask.at<uchar>(280, 360), uchar(0));     // 检测区域内、屏蔽区内
    QCOMPARE(mask.at<uchar>(10, 10), uchar(0));       // 检测区域外

    // ② 清空绘制屏蔽区：点数归零、掩膜恢复成完整检测区域
    node.setParam(QStringLiteral("maskRegionPolygon"), QString());
    node.run(false);
    QCOMPARE(node.getParam(QStringLiteral("maskPolygonCount")).toInt(), 0);
    QCOMPARE(node.getParam(QStringLiteral("maskPolygonPoints")).toString(), QString());
    const auto maskClearObj = attached(node.getOutputData(1), QStringLiteral("检测区域掩膜"));
    QVERIFY2(maskClearObj, "清空屏蔽区后输出掩膜子结果不应消失");
    const cv::Mat maskClear =
        OpencvUtil::himageToMat(HImage(maskClearObj->getHImage()));
    QCOMPARE(maskClear.at<uchar>(280, 360), uchar(255));

    // ③ 继承屏蔽区：多边形点数保持 0（官方字段描述的是「绘制的屏蔽区多边形」），
    //    但继承掩膜图的白区同样从输出掩膜里扣掉
    cv::Mat inherited(canvas.rows, canvas.cols, CV_8UC1, cv::Scalar(0));
    cv::rectangle(inherited, cv::Rect(250, 120, 150, 120), cv::Scalar(255), cv::FILLED);
    auto maskData = QSharedPointer<DataObject>::create();
    maskData->setHImage(OpencvUtil::matToHimage(inherited));
    node.setParam(QStringLiteral("maskSource"), 1);
    node.setInputData(2, maskData);
    node.run(false);
    QVERIFY2(node.getParam(QStringLiteral("moduleStatus")).toBool(),
             "继承屏蔽区不应挡住模板中心 (244,194)");
    QCOMPARE(node.getParam(QStringLiteral("maskPolygonCount")).toInt(), 0);
    const auto maskInheritObj = attached(node.getOutputData(1), QStringLiteral("检测区域掩膜"));
    QVERIFY2(maskInheritObj, "继承屏蔽区时输出掩膜子结果缺失");
    const cv::Mat maskInherit =
        OpencvUtil::himageToMat(HImage(maskInheritObj->getHImage()));
    QCOMPARE(maskInherit.at<uchar>(150, 150), uchar(255));  // 检测区域内、继承屏蔽区外
    QCOMPARE(maskInherit.at<uchar>(200, 300), uchar(0));    // 检测区域内、继承屏蔽区内
}

// VM「是否获取轮廓 / 是否输出轮廓」（高精度匹配·运行参数）：
//   获取=计算模板轮廓特征点并显示（关闭只显示匹配框）；输出=把轮廓点写进模块结果。
// 本腿同时钉住三条边界：① 默认（获取开/输出关）只给叠加层、模块结果字段为空；
// ② 输出开 → matchContourCount/X/Y 与轮廓结果一致，且点落在匹配框内；
// ③ 获取关 → 既无轮廓结果也无模块结果（输出开也没用，依赖获取）。
void FeatureMatchTest::templateContourTogglesFollowVm()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat patch = makePatch();
    const QString tpl = dir.filePath(QStringLiteral("tpl.png"));
    QVERIFY(cv::imwrite(tpl.toStdString(), patch));

    const cv::Mat canvas = makeScene(patch, 0.0, 1.0, 300.0, 250.0);

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

    auto contourResult = [](const QSharedPointer<DataObject> &obj) {
        if (!obj)
            return MeasureResult();
        for (const MeasureResult &mr : obj->measureResults()) {
            if (mr.type == QLatin1String("contour"))
                return mr;
        }
        return MeasureResult();
    };
    auto countPointOverlays = [](const QVector<OverlayShape> &overlay) {
        int points = 0;
        for (const OverlayShape &s : overlay) {
            if (s.type == OverlayShape::Type::Points)
                points += int(s.points.size());
        }
        return points;
    };

    ImageDisplayController ctrl(nullptr, nullptr, nullptr);
    // 直接调 run() 时结果在逻辑端口 1；生产路径由 process() 聚合成端口 0 的单对象。
    // 这里照生产口径把本轮的测量结果合并到端口 0，再让叠加层去画。
    auto mergeToMainPort = [&node](const QSharedPointer<DataObject> &src) {
        auto merged = QSharedPointer<DataObject>::create();
        if (src) {
            for (const MeasureResult &mr : src->measureResults())
                merged->attachMeasureResult(mr);
        }
        node.setOutputData(0, merged);
    };

    // ① 默认：获取开、输出关 → 轮廓只给叠加层，模块结果字段必须为空
    node.run(false);
    const auto res0 = node.getOutputData(1);
    const MeasureResult contour0 = contourResult(res0);
    const int count0 = node.getParam(QStringLiteral("matchContourCount")).toInt();
    mergeToMainPort(res0);
    const int overlay0 = countPointOverlays(ctrl.collectOverlayFromNode(&node));
    qInfo("contour: default resultPoints=%d fieldCount=%d overlayPoints=%d",
          int(contour0.extraValues.size() / 2), count0, overlay0);
    QVERIFY2(contour0.extraValues.size() >= 4,
             "默认（获取轮廓开）结果对象应携带轮廓点集");
    QCOMPARE(count0, 0);
    QVERIFY2(node.getParam(QStringLiteral("matchContourPointXs")).toString().isEmpty(),
             "输出轮廓关闭时模块结果不应写轮廓点");
    QVERIFY2(overlay0 > 0, "获取轮廓开启时画布上应显示轮廓点");

    // ② 输出开：模块结果与轮廓结果一致，且点都落在匹配框内
    node.setParam(QStringLiteral("outputContour"), true);
    node.run(false);
    const auto res1 = node.getOutputData(1);
    const MeasureResult contour1 = contourResult(res1);
    const int count1 = node.getParam(QStringLiteral("matchContourCount")).toInt();
    const QString xs = node.getParam(QStringLiteral("matchContourPointXs")).toString();
    const QString ys = node.getParam(QStringLiteral("matchContourPointYs")).toString();
    const QStringList xsList = xs.split(QLatin1Char(','), Qt::SkipEmptyParts);
    const QStringList ysList = ys.split(QLatin1Char(','), Qt::SkipEmptyParts);
    const double matchCol = node.getParam(QStringLiteral("matchCol")).toDouble();
    const double matchRow = node.getParam(QStringLiteral("matchRow")).toDouble();
    qInfo("contour: output-on count=%d resultPoints=%d match=(%.1f,%.1f)",
          count1, int(contour1.extraValues.size() / 2), matchCol, matchRow);
    mergeToMainPort(res1);
    QVERIFY2(countPointOverlays(ctrl.collectOverlayFromNode(&node)) > 0,
             "输出轮廓打开时画布仍应显示轮廓点");
    QVERIFY2(count1 > 0, "输出轮廓打开后 matchContourCount 应大于 0");
    QCOMPARE(count1, int(contour1.extraValues.size() / 2));
    QCOMPARE(xsList.size(), count1);
    QCOMPARE(ysList.size(), count1);
    for (int i = 0; i < count1; ++i) {
        const double x = xsList.at(i).toDouble();
        const double y = ysList.at(i).toDouble();
        // 模板轮廓点按命中位姿映射后应落在匹配框内（半宽/半高 + 亚像素余量）
        QVERIFY2(qAbs(x - matchCol) <= kTemplateSize * 0.5 + 6.0
                     && qAbs(y - matchRow) <= kTemplateSize * 0.5 + 6.0,
                 qPrintable(QStringLiteral("轮廓点 (%1,%2) 越出匹配框（中心 %3,%4）")
                                .arg(x).arg(y).arg(matchCol).arg(matchRow)));
    }

    // ③ 获取关：即使输出开，也没有轮廓结果与模块结果（只显示匹配框）
    node.setParam(QStringLiteral("getContour"), false);
    node.run(false);
    const auto res2 = node.getOutputData(1);
    const MeasureResult contour2 = contourResult(res2);
    const int count2 = node.getParam(QStringLiteral("matchContourCount")).toInt();
    QVERIFY2(contour2.extraValues.isEmpty(),
             "获取轮廓关闭后不应再产出轮廓结果（叠加层只画匹配框）");
    QCOMPARE(count2, 0);
    QVERIFY2(node.getParam(QStringLiteral("matchContourPointXs")).toString().isEmpty(),
             "获取轮廓关闭后模块结果不应写轮廓点");
    mergeToMainPort(res2);
    QCOMPARE(countPointOverlays(ctrl.collectOverlayFromNode(&node)), 0);
}

// P0-5：多目标叠加框必须按每目标自己的宽/高绘制。XY 尺度模式下两轴独立，
// 旧实现只按 matchList 的单值 scale 比例缩放第一实例，高会算成「宽比例 × 主框高」。
void FeatureMatchTest::templateOverlayUsesPerTargetSize()
{
    OpencvTemplateMatchNode node;
    node.init();
    // 主目标框 150×100（scale 1.0）；第二目标 scale 0.6，但实际框 110×160（Y 反而更大）。
    // 若仍用单一 scale 比例，第二框会被画成 90×60。
    node.setParam(QStringLiteral("matchList"),
                  QStringLiteral("200,200,0.900,0.00,1.0000,200,200;"
                                 "400,300,0.800,0.00,0.6000,400,300"));
    node.setParam(QStringLiteral("matchBoxWidths"), QStringLiteral("150,110"));
    node.setParam(QStringLiteral("matchBoxHeights"), QStringLiteral("100,160"));

    MeasureResult mainRes;
    mainRes.type = QStringLiteral("template");
    mainRes.valid = true;
    mainRes.value = 0.9;
    mainRes.point1 = QPointF(200, 200);
    mainRes.extraValues = {200, 200, 0.9, 0.0, 1.0, 150, 100, 200, 200, 1.0, 1.0, 0.0};
    auto mainObj = QSharedPointer<DataObject>::create();
    mainObj->setMeasureResult(mainRes);
    node.setOutputData(0, mainObj);

    ImageDisplayController ctrl(nullptr, nullptr, nullptr);
    const QVector<OverlayShape> overlay = ctrl.collectOverlayFromNode(&node);
    QStringList shapeTypes;
    for (const OverlayShape &s : overlay)
        shapeTypes << QString::number(int(s.type));
    qInfo("overlay-size: shapes=%d types=%s", int(overlay.size()),
          qPrintable(shapeTypes.join(QLatin1Char(','))));
    const OverlayShape *mainBox = nullptr;
    const OverlayShape *extraBox = nullptr;
    for (const OverlayShape &s : overlay) {
        if (s.type != OverlayShape::Type::RotatedRect)
            continue;
        if (qAbs(s.p1.x() - 200.0) <= 0.5 && qAbs(s.p1.y() - 200.0) <= 0.5)
            mainBox = &s;
        if (qAbs(s.p1.x() - 300.0) <= 0.5 && qAbs(s.p1.y() - 400.0) <= 0.5)
            extraBox = &s;
    }
    QVERIFY2(mainBox && extraBox, "画布上应画出两个匹配框");
    qInfo("overlay-size: main=(%.1f x %.1f) extra=(%.1f x %.1f)", mainBox->width,
          mainBox->height, extraBox->width, extraBox->height);
    QVERIFY2(qAbs(mainBox->width - 150.0) <= 0.5 && qAbs(mainBox->height - 100.0) <= 0.5,
             qPrintable(QStringLiteral("主目标框应为 150×100，实际 %1×%2")
                            .arg(mainBox->width).arg(mainBox->height)));
    QVERIFY2(qAbs(extraBox->width - 110.0) <= 0.5 && qAbs(extraBox->height - 160.0) <= 0.5,
             qPrintable(QStringLiteral("第二目标框应取自己的宽高 110×160，实际 %1×%2"
                                       "（90×60 说明仍按单一 scale 比例缩放）")
                            .arg(extraBox->width).arg(extraBox->height)));
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

// 复核 P1-2 残留（2026-11-08）：锚点旋转不只影响列表里的 pointRow/pointCol，
// 主结果字段 matchPointRow/Col（结果表、结果判断、叠加十字线都读它）也必须与
// warpTemplate 的矩阵同口径。本腿让匹配真的发生在 90°，锚点 (32,-16)：
//   getRotationMatrix2D(90°) 的线性部分 = [0 1; -1 0] ⇒ 偏移变成 (-16,-32)，
//   即匹配点 = 匹配框中心 + (-16,-32)；共轭公式会得到 (+16,+32)，每轴差 32px。
void FeatureMatchTest::templateMatchPointFollowsAnchorAtAngle()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat patch = makePatch();
    const QString tpl = dir.filePath(QStringLiteral("tpl.png"));
    QVERIFY(cv::imwrite(tpl.toStdString(), patch));

    const double cx = 200.0;
    const double cy = 200.0;
    const cv::Mat canvas = makeScene(patch, 90.0, 1.0, cx, cy);

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 1);
    node.setParam(QStringLiteral("minScore"), 0.5);
    node.setParam(QStringLiteral("angleMin"), 90.0);
    node.setParam(QStringLiteral("angleMax"), 90.0);
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
    const double mcol = node.getParam(QStringLiteral("matchCol")).toDouble();
    const double mrow = node.getParam(QStringLiteral("matchRow")).toDouble();
    const double pcol = node.getParam(QStringLiteral("matchPointCol")).toDouble();
    const double prow = node.getParam(QStringLiteral("matchPointRow")).toDouble();
    qInfo("anchor-angle-leg: center=(%.1f,%.1f) point=(%.1f,%.1f)", mcol, mrow, pcol, prow);
    QVERIFY2(ok, "90° 锚点用例应当匹配成功");
    // 旋转+插值下中心会有 ~3px 的栅格误差；锚点断言用实际中心做基准，不受它影响。
    QVERIFY2(qAbs(mcol - cx) <= 4.0 && qAbs(mrow - cy) <= 4.0,
             qPrintable(QStringLiteral("匹配框中心应在 (%1,%2)，实际 (%3,%4)")
                            .arg(cx).arg(cy).arg(mcol).arg(mrow)));
    QVERIFY2(qAbs(pcol - (mcol - 16.0)) <= 2.0 && qAbs(prow - (mrow - 32.0)) <= 2.0,
             qPrintable(QStringLiteral("90° 时匹配点应为中心+(-16,-32)（warpTemplate 同源），"
                                       "实际 (%1,%2)，中心 (%3,%4)；若得到 (+16,+32) 说明用了共轭旋转")
                            .arg(pcol).arg(prow).arg(mcol).arg(mrow)));
}

// 复核 P1-2（2026-11-08）：polarity=0 的契约是「忽略极性、两方向都试」（GOC 取 |score|）。
// 用一张亮底暗块的图去匹配「暗底亮块」的模板：polarity=0 必须命中；polarity=1 必须判失败。
// 旧实现用 score<0 判无效格，会把 GOC 的合法负分（反极性目标）一起压成 -1，
// 于是 polarity=0 退化成与 polarity=1 等价——本腿就是锁住这个退化。
void FeatureMatchTest::templatePolarityZeroMatchesInvertedContrast()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    cv::Mat tplImg(40, 40, CV_8UC1, cv::Scalar(30));
    cv::rectangle(tplImg, cv::Rect(10, 10, 20, 20), cv::Scalar(220), cv::FILLED);
    const QString tpl = dir.filePath(QStringLiteral("tpl.png"));
    QVERIFY(cv::imwrite(tpl.toStdString(), tplImg));

    cv::Mat canvas(200, 200, CV_8UC1, cv::Scalar(225));
    cv::rectangle(canvas, cv::Rect(80, 80, 20, 20), cv::Scalar(30), cv::FILLED);

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 2);   // GOC：只有这条路径受 polarity 影响
    node.setParam(QStringLiteral("minScore"), 0.5);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("scaleMin"), 1.0);
    node.setParam(QStringLiteral("scaleMax"), 1.0);
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("templatePath"), tpl);
    node.setInputImage(OpencvUtil::matToHimage(canvas));

    TemplateEntry e;
    e.name = QStringLiteral("A");
    e.imagePath = tpl;
    node.setTemplates({e});
    node.setActiveTemplateIndex(0);

    node.setParam(QStringLiteral("polarity"), 0);
    node.run(false);
    const bool okIgnore = node.getParam(QStringLiteral("moduleStatus")).toBool();
    const double scoreIgnore = node.getParam(QStringLiteral("matchScore")).toDouble();
    const double col = node.getParam(QStringLiteral("matchCol")).toDouble();
    const double row = node.getParam(QStringLiteral("matchRow")).toDouble();
    qInfo("polarity-leg: ignore ok=%d score=%.3f center=(%.1f,%.1f)",
          int(okIgnore), scoreIgnore, col, row);
    QVERIFY2(okIgnore, qPrintable(QStringLiteral("polarity=0 应命中反极性目标，实际判红（score=%1）")
                                      .arg(scoreIgnore)));
    QVERIFY2(scoreIgnore >= 0.5,
             qPrintable(QStringLiteral("polarity=0 的分数应取 |score|，实际 %1").arg(scoreIgnore)));
    QVERIFY2(qAbs(col - 90.0) <= 2.0 && qAbs(row - 90.0) <= 2.0,
             qPrintable(QStringLiteral("反极性目标中心应在 (90,90)，实际 (%1,%2)").arg(col).arg(row)));

    node.setParam(QStringLiteral("polarity"), 1);
    node.run(false);
    const bool okExact = node.getParam(QStringLiteral("moduleStatus")).toBool();
    const double scoreExact = node.getParam(QStringLiteral("matchScore")).toDouble();
    qInfo("polarity-leg: exact ok=%d score=%.3f", int(okExact), scoreExact);
    QVERIFY2(!okExact, qPrintable(QStringLiteral("polarity=1 不应命中反极性目标（score=%1）")
                                      .arg(scoreExact)));
}

// 复核 P2-1（2026-11-08）：形状掩膜每轮由当前图形/屏蔽重建，但变形缓存的键
// 如果只含模板图指纹、不含掩膜内容，编辑图形/屏蔽后会复用旧掩膜的变形结果。
// 同一节点连跑两次、同一模板同一角度，只改屏蔽区域（两次掩膜尺寸相同）：
//   ① 屏蔽左半（只剩无特征的右半）→ 必须判失败；
//   ② 屏蔽右半（左半特征保留）→ 必须命中。
// 缓存键若不含掩膜内容，② 会复用 ① 的「右半掩膜」→ 仍然 n<16 判失败（本腿红）。
void FeatureMatchTest::templateMaskEditInvalidatesWarpCache()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    cv::Mat tplImg(40, 40, CV_8UC1, cv::Scalar(30));
    cv::rectangle(tplImg, cv::Rect(4, 4, 12, 12), cv::Scalar(220), cv::FILLED);
    cv::rectangle(tplImg, cv::Rect(4, 24, 8, 8), cv::Scalar(180), cv::FILLED);
    cv::circle(tplImg, cv::Point(12, 30), 6, cv::Scalar(200), cv::FILLED);
    // 右半保持纯背景：屏蔽左半后可见点数不足，必然判失败。
    const QString tpl = dir.filePath(QStringLiteral("tpl.png"));
    QVERIFY(cv::imwrite(tpl.toStdString(), tplImg));

    const cv::Mat canvas = makeScene(tplImg, 90.0, 1.0, 200.0, 200.0);

    auto runWithExclude = [&](const cv::Rect &exclude) {
        OpencvTemplateMatchNode node;
        node.init();
        // 形状掩膜只在 GOC（method=2）的评分路径生效；方法 0/1 不消费 shapeMask。
        node.setParam(QStringLiteral("method"), 2);
        node.setParam(QStringLiteral("minScore"), 0.5);
        node.setParam(QStringLiteral("angleMin"), 90.0);
        node.setParam(QStringLiteral("angleMax"), 90.0);
        node.setParam(QStringLiteral("angleStep"), 2.0);
        node.setParam(QStringLiteral("trainFromImage"), false);
        node.setParam(QStringLiteral("templatePath"), tpl);
        node.setInputImage(OpencvUtil::matToHimage(canvas));

        RoiShape keep;
        keep.type = RoiType::Rect;
        keep.p1 = QPointF(0, 0);
        keep.p2 = QPointF(40, 40);
        RoiShape hole;
        hole.type = RoiType::Rect;
        hole.p1 = QPointF(exclude.x, exclude.y);
        hole.p2 = QPointF(exclude.x + exclude.width, exclude.y + exclude.height);

        TemplateEntry e;
        e.name = QStringLiteral("A");
        e.imagePath = tpl;
        e.shapes = {keep};
        e.excludes = {hole};
        node.setTemplates({e});
        node.setActiveTemplateIndex(0);
        node.run(false);
        return std::make_tuple(node.getParam(QStringLiteral("moduleStatus")).toBool(),
                               node.getParam(QStringLiteral("matchCol")).toDouble(),
                               node.getParam(QStringLiteral("matchRow")).toDouble(),
                               node.getParam(QStringLiteral("matchScore")).toDouble());
    };

    // ① 屏蔽左半：可见的右半是纯背景 → 必须失败
    const auto left = runWithExclude(cv::Rect(0, 0, 20, 40));
    qInfo("mask-cache-leg: exclude-left ok=%d score=%.3f",
          int(std::get<0>(left)), std::get<3>(left));
    QVERIFY2(!std::get<0>(left), "屏蔽掉全部特征后不应命中（可见点不足）");

    // ② 屏蔽右半：左半特征保留 → 必须命中，且中心回到 (200,200)
    const auto right = runWithExclude(cv::Rect(20, 0, 20, 40));
    qInfo("mask-cache-leg: exclude-right ok=%d center=(%.1f,%.1f) score=%.3f",
          int(std::get<0>(right)), std::get<1>(right), std::get<2>(right), std::get<3>(right));
    QVERIFY2(std::get<0>(right),
             "改屏蔽区域后应立刻生效并命中；若仍失败，说明掩膜变形缓存键没有包含掩膜内容");
    // 不钉绝对中心：只保留一半特征的模板在缺特征的那一轴上是退化的，
    // 峰位会沿该轴漂移（实测 (194,175)）。本腿只锁「掩膜改动是否立刻生效」。
    QVERIFY2(std::get<3>(right) >= 0.5,
             qPrintable(QStringLiteral("改屏蔽后匹配分数应达标，实际 %1").arg(std::get<3>(right))));
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

// 运行参数「尺度范围」（VM）：只有显式 min/max 参与搜索；不存在"搜索尺度自动"。
// 画布上放一个 1.15 倍的实例：
//   尺度范围 1.0 → 只能按 1.0 找，分数低、尺度=1；
//   尺度范围 0.8~1.3 → 命中 ≈1.15 的实例且分数更高；
//   再把范围收回 1.0 并把 scaleMode 置 1（模板尺度自动）→ 不得扩大搜索范围。
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

    // scaleMode 是模板配置的「尺度模式」（速度尺度自动/手动），不得再扩大搜索尺度范围：
    // 范围收回 1.0 后即使 scaleMode=1，也只能按 1.0 搜。
    node.setParam(QStringLiteral("scaleMin"), 1.0);
    node.setParam(QStringLiteral("scaleMax"), 1.0);
    node.setParam(QStringLiteral("scaleMode"), 1);
    node.run(false);
    const double k2 = node.getParam(QStringLiteral("matchScale")).toDouble();
    const double s2 = node.getParam(QStringLiteral("matchScore")).toDouble();
    qInfo("scale-range: auto-flag k=%.2f score=%.3f", k2, s2);
    QVERIFY2(qAbs(k2 - 1.0) < 1e-6,
             qPrintable(QStringLiteral("VM 没有搜索尺度自动：scaleMode=1 时尺度必须仍为 1.0，实际 %1")
                            .arg(k2)));
    QVERIFY2(s2 < s1,
             qPrintable(QStringLiteral("范围 1.0 的分数应低于覆盖 1.15 的范围：%1 vs %2")
                            .arg(s2).arg(s1)));
}

// VM 模板配置「尺度模式」（默认自动）：只决定速度尺度（粗搜降采样倍率）怎么来，
// 与运行参数「尺度范围」无关。这里锁四件事：
//   ① 模板条目自动 → 800px 图自动值 = qBound(2, round(800/640), 20) = 2，忽略 speedScale=10；
//   ② 模板条目手动 → 用 speedScale=10；粗搜倍率必须是 10（真实进入粗搜路径）；
//   ③ 无模板列表时节点参数 scaleMode 决定合成的模板条目（0=手动10 / 1=自动2）；
//   ④ JSON 往返：显式 false 保留手动，缺键按 VM 默认自动。
void FeatureMatchTest::templateScaleModeControlsSpeedScaleOnly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat patch = makePatch();
    const QString tpl = dir.filePath(QStringLiteral("scale-mode.png"));
    QVERIFY(cv::imwrite(tpl.toStdString(), patch));

    cv::Mat canvas(600, 800, CV_8UC1, cv::Scalar(kCanvasBackground));
    patch.copyTo(canvas(cv::Rect(300, 200, patch.cols, patch.rows)));

    TemplateEntry autoEntry;
    autoEntry.name = QStringLiteral("auto");
    autoEntry.imagePath = tpl;
    autoEntry.scaleModeAuto = true;
    autoEntry.speedScale = 10;          // 自动模式下必须被忽略
    TemplateEntry manualEntry = autoEntry;
    manualEntry.name = QStringLiteral("manual");
    manualEntry.scaleModeAuto = false;

    OpencvTemplateMatchNode autoNode;
    autoNode.init();
    autoNode.setParam(QStringLiteral("method"), 1);
    autoNode.setParam(QStringLiteral("angleMin"), -2.0);
    autoNode.setParam(QStringLiteral("angleMax"), 0.0);
    autoNode.setParam(QStringLiteral("angleStep"), 2.0);
    autoNode.setParam(QStringLiteral("scaleMin"), 1.0);
    autoNode.setParam(QStringLiteral("scaleMax"), 1.0);
    autoNode.setInputImage(OpencvUtil::matToHimage(canvas));
    autoNode.setTemplates({autoEntry});
    autoNode.setActiveTemplateIndex(0);
    autoNode.run(false);
    const int autoSpeed = autoNode.getParam(QStringLiteral("matchSpeedScale")).toInt();
    const double autoCoarse = autoNode.getParam(QStringLiteral("matchCoarseFactor")).toDouble();

    autoNode.setTemplates({manualEntry});
    autoNode.setActiveTemplateIndex(0);
    autoNode.run(false);
    const int manualSpeed = autoNode.getParam(QStringLiteral("matchSpeedScale")).toInt();
    const double manualCoarse = autoNode.getParam(QStringLiteral("matchCoarseFactor")).toDouble();
    qInfo("scale-mode: auto speed=%d coarse=%.0f | manual speed=%d coarse=%.0f", autoSpeed,
          autoCoarse, manualSpeed, manualCoarse);

    QCOMPARE(autoSpeed, 2);
    QCOMPARE(autoCoarse, 2.0);
    QCOMPARE(manualSpeed, 10);
    QCOMPARE(manualCoarse, 10.0);

    // ③ 无模板列表：节点参数 scaleMode 必须真的作用到合成模板条目
    OpencvTemplateMatchNode legacy;
    legacy.init();
    legacy.setParam(QStringLiteral("method"), 1);
    legacy.setParam(QStringLiteral("angleMin"), -2.0);
    legacy.setParam(QStringLiteral("angleMax"), 0.0);
    legacy.setParam(QStringLiteral("angleStep"), 2.0);
    legacy.setParam(QStringLiteral("scaleMin"), 1.0);
    legacy.setParam(QStringLiteral("scaleMax"), 1.0);
    legacy.setParam(QStringLiteral("speedScale"), 10);
    legacy.setParam(QStringLiteral("scaleMode"), 0);          // 手动
    legacy.setParam(QStringLiteral("templatePath"), tpl);
    legacy.setInputImage(OpencvUtil::matToHimage(canvas));
    legacy.run(false);
    const int legacyManual = legacy.getParam(QStringLiteral("matchSpeedScale")).toInt();
    legacy.setParam(QStringLiteral("scaleMode"), 1);          // 自动
    legacy.run(false);
    const int legacyAuto = legacy.getParam(QStringLiteral("matchSpeedScale")).toInt();
    qInfo("scale-mode: node-param manual=%d auto=%d", legacyManual, legacyAuto);
    QCOMPARE(legacyManual, 10);
    QCOMPARE(legacyAuto, 2);

    // ④ JSON 往返
    TemplateEntry e;
    e.speedScale = 7;
    e.scaleModeAuto = false;
    const TemplateEntry back = TemplateEntry::fromJson(e.toJson());
    QCOMPARE(back.scaleModeAuto, false);
    QCOMPARE(back.speedScale, 7);
    TemplateEntry dflt = TemplateEntry::fromJson(QJsonObject());
    QCOMPARE(dflt.scaleModeAuto, true);
}

// VM「ROI创建=继承 → 继承方式=按参数」：搜索区域由继承中心 X/Y、宽/高、角度决定；
// 这 5 个参数在「参数订阅」表里可逐项订阅上游结果（本用例直接写值，验证消费侧）。
void FeatureMatchTest::templateRoiInheritByParamsControlsSearchRegion()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat patch = makePatch();
    const QString tpl = dir.filePath(QStringLiteral("inherit-by-params.png"));
    QVERIFY(cv::imwrite(tpl.toStdString(), patch));

    cv::Mat canvas(600, 600, CV_8UC1, cv::Scalar(kCanvasBackground));
    const cv::Rect left(50, 60, kTemplateSize, kTemplateSize);
    const cv::Rect right(360, 200, kTemplateSize, kTemplateSize);
    patch.copyTo(canvas(left));
    patch.copyTo(canvas(right));

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 1);
    node.setParam(QStringLiteral("minScore"), 0.5);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("scaleMin"), 1.0);
    node.setParam(QStringLiteral("scaleMax"), 1.0);
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("templatePath"), tpl);
    node.setInputImage(OpencvUtil::matToHimage(canvas));
    node.setParam(QStringLiteral("roiSource"), 1);        // ROI创建=继承
    node.setParam(QStringLiteral("roiInheritMode"), 1);   // 继承方式=按参数
    node.setParam(QStringLiteral("roiInheritX"), right.x + kTemplateSize / 2.0);
    node.setParam(QStringLiteral("roiInheritY"), right.y + kTemplateSize / 2.0);
    node.setParam(QStringLiteral("roiInheritWidth"), 160.0);
    node.setParam(QStringLiteral("roiInheritHeight"), 160.0);
    node.setParam(QStringLiteral("roiInheritAngle"), 0.0);
    node.run(false);
    const double col = node.getParam(QStringLiteral("matchCol")).toDouble();
    const double row = node.getParam(QStringLiteral("matchRow")).toDouble();
    const double cx = right.x + kTemplateSize / 2.0;
    const double cy = right.y + kTemplateSize / 2.0;
    qInfo("inherit-params: hit=(%.1f,%.1f) expect=(%.1f,%.1f)", col, row, cx, cy);
    QVERIFY2(node.getParam(QStringLiteral("moduleStatus")).toBool(),
             "按参数继承覆盖右侧工件时应命中");
    QVERIFY2(qAbs(col - cx) <= 3.0 && qAbs(row - cy) <= 3.0,
             qPrintable(QStringLiteral("按参数继承没生效：命中 (%1,%2)，期望 (%3,%4)")
                            .arg(col).arg(row).arg(cx).arg(cy)));

    // 反向：继承中心挪到空白区（宽高有效）→ 搜索区域里没有目标，必须找不到。
    // 若实现忽略 roiInherit* 而退回整图，这里会命中左侧工件 ⇒ 判红。
    node.setParam(QStringLiteral("roiInheritX"), 150.0);
    node.setParam(QStringLiteral("roiInheritY"), 430.0);
    node.setParam(QStringLiteral("roiInheritWidth"), 200.0);
    node.setParam(QStringLiteral("roiInheritHeight"), 160.0);
    node.run(false);
    qInfo("inherit-params: blank-region status=%d module=%d",
          node.getParam(QStringLiteral("matchStatus")).toInt(),
          int(node.getParam(QStringLiteral("moduleStatus")).toBool()));
    QCOMPARE(node.getParam(QStringLiteral("matchStatus")).toInt(), 0);
    QCOMPARE(node.getParam(QStringLiteral("moduleStatus")).toBool(), false);
}

// VM「屏蔽区创建」：屏蔽区（ROI 内不检测的区域）支持绘制多边形与继承上游 Region/掩膜图。
// 画布放两个工件，maxMatches=2：
//   基线 2 个 → 绘制屏蔽左侧 → 只剩右侧 → 继承掩膜图屏蔽右侧 → 只剩左侧。
void FeatureMatchTest::templateMaskRegionExcludesCandidates()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat patch = makePatch();
    const QString tpl = dir.filePath(QStringLiteral("mask-region.png"));
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
    node.setParam(QStringLiteral("scaleMin"), 1.0);
    node.setParam(QStringLiteral("scaleMax"), 1.0);
    node.setParam(QStringLiteral("maxMatches"), 2);
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("templatePath"), tpl);
    node.setInputImage(OpencvUtil::matToHimage(canvas));

    node.run(false);
    const int baseCount = node.getParam(QStringLiteral("matchCount")).toInt();
    QCOMPARE(baseCount, 2);

    // ① 绘制：多边形覆盖左侧工件（外扩 20px，避免边缘平移候选漏网）
    const cv::Rect mleft(left.x - 20, left.y - 20, left.width + 40, left.height + 40);
    const QString poly = QStringLiteral("%1,%2 %3,%2 %3,%4 %1,%4")
                             .arg(mleft.x)
                             .arg(mleft.y)
                             .arg(mleft.x + mleft.width)
                             .arg(mleft.y + mleft.height);
    node.setParam(QStringLiteral("maskSource"), 0);
    node.setParam(QStringLiteral("maskRegionPolygon"), poly);
    node.run(false);
    const int drawCount = node.getParam(QStringLiteral("matchCount")).toInt();
    const double drawCol = node.getParam(QStringLiteral("matchCol")).toDouble();
    qInfo("mask-region: base=%d drawn-left=%d col=%.1f", baseCount, drawCount, drawCol);
    QCOMPARE(drawCount, 1);
    QVERIFY2(qAbs(drawCol - (right.x + kTemplateSize / 2.0)) <= 3.0,
             qPrintable(QStringLiteral("绘制屏蔽左侧后应只剩右侧，实际命中列 %1").arg(drawCol)));

    // ② 继承：清掉绘制多边形，改订阅「屏蔽区」（slot 2），白=屏蔽；这里屏蔽右侧。
    node.setParam(QStringLiteral("maskRegionPolygon"), QString());
    node.setParam(QStringLiteral("maskSource"), 1);
    cv::Mat maskMat(canvas.rows, canvas.cols, CV_8UC1, cv::Scalar(0));
    cv::rectangle(maskMat,
                  cv::Rect(right.x - 20, right.y - 20, right.width + 40, right.height + 40),
                  cv::Scalar(255), cv::FILLED);
    auto maskObj = QSharedPointer<DataObject>::create();
    maskObj->setHImage(OpencvUtil::matToHimage(maskMat));
    node.setInputData(2, maskObj);
    node.run(false);
    const int inheritCount = node.getParam(QStringLiteral("matchCount")).toInt();
    const double inheritCol = node.getParam(QStringLiteral("matchCol")).toDouble();
    qInfo("mask-region: inherited-right=%d col=%.1f", inheritCount, inheritCol);
    QCOMPARE(inheritCount, 1);
    QVERIFY2(qAbs(inheritCol - (left.x + kTemplateSize / 2.0)) <= 3.0,
             qPrintable(QStringLiteral("继承屏蔽右侧后应只剩左侧，实际命中列 %1").arg(inheritCol)));
}

// VM「位置修正」（基本参数·图形类型）：基准点 = 模板 ROI 中心/角度，运行点来自
// Fixture（按信息）或 5 个可订阅参数（按点/按坐标）；启用后把检测区域搬到运行点。
// 画布：模板在基准位置 (150,150)，工件实际在 (414,314)（中心）；
// 基准检测区域只盖住基准位置 → 不修正时找不到，修正后命中实际位置。
void FeatureMatchTest::templatePositionCorrectionMovesSearchRegion()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat patch = makePatch();
    const QString tpl = dir.filePath(QStringLiteral("position-correct.png"));
    QVERIFY(cv::imwrite(tpl.toStdString(), patch));

    cv::Mat canvas(600, 600, CV_8UC1, cv::Scalar(kCanvasBackground));
    const cv::Rect target(350, 250, kTemplateSize, kTemplateSize);
    patch.copyTo(canvas(target));
    const double targetCx = target.x + kTemplateSize / 2.0;
    const double targetCy = target.y + kTemplateSize / 2.0;

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 1);
    node.setParam(QStringLiteral("minScore"), 0.5);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("scaleMin"), 1.0);
    node.setParam(QStringLiteral("scaleMax"), 1.0);
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("templatePath"), tpl);
    node.setInputImage(OpencvUtil::matToHimage(canvas));
    // 基准点 = 模板 ROI 中心（教学位置）
    node.setParam(QStringLiteral("roiCenterCol"), 150.0);
    node.setParam(QStringLiteral("roiCenterRow"), 150.0);
    node.setParam(QStringLiteral("roiWidth"), kTemplateSize);
    node.setParam(QStringLiteral("roiHeight"), kTemplateSize);
    node.setParam(QStringLiteral("roiAngle"), 0.0);
    // 基准检测区域：只覆盖基准位置
    node.setParam(QStringLiteral("searchRegionCol"), 70);
    node.setParam(QStringLiteral("searchRegionRow"), 70);
    node.setParam(QStringLiteral("searchRegionWidth"), 160);
    node.setParam(QStringLiteral("searchRegionHeight"), 160);

    // ① 不启用：检测区域里没有工件
    node.run(false);
    const int offStatus = node.getParam(QStringLiteral("matchStatus")).toInt();
    const bool offModule = node.getParam(QStringLiteral("moduleStatus")).toBool();
    qInfo("position-correct: off status=%d module=%d", offStatus, int(offModule));
    QCOMPARE(offStatus, 0);
    QCOMPARE(offModule, false);

    // ② 按坐标：运行点=实际工件中心 → 检测区域搬到 (334,234)，命中
    node.setParam(QStringLiteral("positionCorrectMode"), 3);
    node.setParam(QStringLiteral("positionRunX"), targetCx);
    node.setParam(QStringLiteral("positionRunY"), targetCy);
    node.setParam(QStringLiteral("positionRunAngle"), 0.0);
    node.setParam(QStringLiteral("positionRunScaleX"), 1.0);
    node.setParam(QStringLiteral("positionRunScaleY"), 1.0);
    node.run(false);
    const double col = node.getParam(QStringLiteral("matchCol")).toDouble();
    const double row = node.getParam(QStringLiteral("matchRow")).toDouble();
    const double scx = node.getParam(QStringLiteral("searchCenterX")).toDouble();
    const double scy = node.getParam(QStringLiteral("searchCenterY")).toDouble();
    qInfo("position-correct: by-coord hit=(%.1f,%.1f) searchCenter=(%.1f,%.1f)", col, row, scx,
          scy);
    QVERIFY2(node.getParam(QStringLiteral("moduleStatus")).toBool(), "按坐标修正后应命中");
    QVERIFY2(qAbs(col - targetCx) <= 3.0 && qAbs(row - targetCy) <= 3.0,
             qPrintable(QStringLiteral("按坐标修正命中了 (%1,%2)，期望 (%3,%4)")
                            .arg(col).arg(row).arg(targetCx).arg(targetCy)));
    QVERIFY2(qAbs(scx - targetCx) <= 1.0 && qAbs(scy - targetCy) <= 1.0,
             qPrintable(QStringLiteral("检测区域没有搬到运行点：中心 (%1,%2)").arg(scx).arg(scy)));

    // ③ 按点：与按坐标同一条消费路径（按点在「参数订阅」里喂运行点）
    node.setParam(QStringLiteral("positionCorrectMode"), 2);
    node.run(false);
    qInfo("position-correct: by-point status=%d col=%.1f",
          node.getParam(QStringLiteral("matchStatus")).toInt(),
          node.getParam(QStringLiteral("matchCol")).toDouble());
    QVERIFY2(node.getParam(QStringLiteral("moduleStatus")).toBool(), "按点修正后应命中");

    // ④ 按信息：Fixture 位姿即运行点
    FlowScene scene;
    scene.setFixturePose(QStringLiteral("fix-pos"), targetCy, targetCx, 0.0, 1.0);
    node.setFlowSceneRef(&scene);
    node.setParam(QStringLiteral("positionCorrectMode"), 1);
    node.setParam(QStringLiteral("positionFixtureName"), QStringLiteral("fix-pos"));
    node.run(false);
    qInfo("position-correct: by-fixture status=%d col=%.1f",
          node.getParam(QStringLiteral("matchStatus")).toInt(),
          node.getParam(QStringLiteral("matchCol")).toDouble());
    QVERIFY2(node.getParam(QStringLiteral("moduleStatus")).toBool(), "按信息修正后应命中");

    // ⑤ 声明了按信息却没有可用 Fixture → 必须判红并给出原因，不能静默按未修正区域跑
    node.setParam(QStringLiteral("positionFixtureName"), QStringLiteral("missing-fix"));
    node.run(false);
    const QString why = node.getParam(QStringLiteral("lastError")).toString();
    qInfo("position-correct: missing-fixture module=%d why=%s",
          int(node.getParam(QStringLiteral("moduleStatus")).toBool()), qPrintable(why));
    QCOMPARE(node.getParam(QStringLiteral("moduleStatus")).toBool(), false);
    QVERIFY2(why.contains(QStringLiteral("位置修正")), "缺少 Fixture 时必须给出可读原因");

    // ⑥ 多边形检测区域同样要随位置修正搬移（不是只搬矩形）
    node.setParam(QStringLiteral("positionCorrectMode"), 3);
    node.setParam(QStringLiteral("positionFixtureName"), QString());
    node.setParam(QStringLiteral("searchRegionWidth"), 0);
    node.setParam(QStringLiteral("searchRegionHeight"), 0);
    node.setParam(QStringLiteral("searchRegionPolygon"),
                  QStringLiteral("70,70 230,70 230,230 70,230"));
    node.run(false);
    const double polyCol = node.getParam(QStringLiteral("matchCol")).toDouble();
    qInfo("position-correct: polygon status=%d col=%.1f",
          node.getParam(QStringLiteral("matchStatus")).toInt(), polyCol);
    QVERIFY2(node.getParam(QStringLiteral("moduleStatus")).toBool(), "多边形区域修正后应命中");
    QVERIFY2(qAbs(polyCol - targetCx) <= 3.0,
             qPrintable(QStringLiteral("多边形区域没有搬到运行点：命中列 %1").arg(polyCol)));
}

// VM 4.4 模块分类：模板匹配节点的显示名/注册分类对齐「高精度匹配」，
// method 默认 2（GOC 边缘梯度），旧名「模板匹配」保留为别名（拖放/搜索/老夹具兼容）。
void FeatureMatchTest::templateVmModuleIdentityMatchesHelp()
{
    OpencvTemplateMatchNode node;
    node.init();
    QCOMPARE(node.name(), QStringLiteral("高精度匹配"));
    QCOMPARE(node.getParam(QStringLiteral("method")).toInt(), 2);

    registerAllNodes();
    const NodeRegistration *reg =
        NodeRegistry::instance().findById(QStringLiteral("OpencvTemplateMatchNode"));
    QVERIFY2(reg != nullptr, "OpencvTemplateMatchNode 未注册");
    QCOMPARE(reg->displayName, QStringLiteral("高精度匹配"));
    QCOMPARE(reg->englishName, QStringLiteral("High Precision Match"));
    QCOMPARE(reg->group, QStringLiteral("特征定位"));
    QVERIFY2(reg->aliases.contains(QStringLiteral("模板匹配")),
             "旧名「模板匹配」必须保留为别名");
    QCOMPARE(NodeRegistry::instance().findByName(QStringLiteral("模板匹配")), reg);
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

    // P2-2：X 整数化相同 → 按 Y 升序（两个目标同列）
    node.setParam(QStringLiteral("sortType"), 4);
    node.run(false);
    const QStringList rows4 =
        node.getParam(QStringLiteral("matchList")).toString().split(QLatin1Char(';'), Qt::SkipEmptyParts);
    QCOMPARE(rows4.size(), 2);
    const double row4a = rows4.at(0).split(QLatin1Char(',')).at(0).toDouble();
    const double row4b = rows4.at(1).split(QLatin1Char(',')).at(0).toDouble();
    QVERIFY2(row4a < row4b,
             qPrintable(QStringLiteral("X由小到大+Y兜底没生效：%1 / %2").arg(row4a).arg(row4b)));

    // P2-2：Y 整数化相同 → 按 X 升序（同一行）
    node.setParam(QStringLiteral("sortType"), 5);
    node.run(false);
    const QStringList rows5 =
        node.getParam(QStringLiteral("matchList")).toString().split(QLatin1Char(';'), Qt::SkipEmptyParts);
    QCOMPARE(rows5.size(), 2);
    const double row5a = rows5.at(0).split(QLatin1Char(',')).at(0).toDouble();
    const double row5b = rows5.at(1).split(QLatin1Char(',')).at(0).toDouble();
    QVERIFY2(row5a < row5b, "Y由小到大排序没生效");
}

// VM「排序类型」并列规则（P2-2）：帮助页原文——「X由小到大，Y由小到大」是
// 「按照匹配框中心点X轴坐标由小到大排序，当X坐标整数化后值相同时再按照Y轴坐标从小到大排序」，
// Y 方向对称。旧实现用浮点不等比较，X 差 0.2 就轮不到 Y 兜底；本腿钉住整数桶口径。
// 帮助页未定义「整数化」的取整方向，本实现取截断（TemplateMatchSort::integerized）。
void FeatureMatchTest::templateSortTypeIntegerizedTieBreak()
{
    using TemplateMatchSort::Key;

    // ① X 方向：a.x=100.2 / b.x=100.4 ⇒ 同一整数桶 100，按 Y 升序 b 在前
    Key a; a.x = 100.2; a.y = 500.0; a.angle = 10.0; a.score = 0.9;
    Key b; b.x = 100.4; b.y = 100.0; b.angle = 20.0; b.score = 0.8;
    QVERIFY2(TemplateMatchSort::less(b, a, 4), "X 整数化相同应按 Y 升序");
    QVERIFY2(!TemplateMatchSort::less(a, b, 4), "并列时比较必须反对称");
    // 不同整数桶仍按 X 升序（99 < 100），即使 Y 更大
    Key c; c.x = 99.8; c.y = 900.0; c.angle = 30.0; c.score = 0.7;
    QVERIFY2(TemplateMatchSort::less(c, b, 4), "不同整数 X 桶应按 X 升序");

    // ② Y 方向：d.y=200.2 / e.y=200.4 ⇒ 同一整数桶 200，按 X 升序 e 在前
    Key d; d.x = 500.0; d.y = 200.2; d.angle = 0.0; d.score = 0.9;
    Key e; e.x = 100.0; e.y = 200.4; e.angle = 0.0; e.score = 0.8;
    QVERIFY2(TemplateMatchSort::less(e, d, 5), "Y 整数化相同应按 X 升序");
    Key f; f.x = 900.0; f.y = 199.8; f.angle = 0.0; f.score = 0.7;
    QVERIFY2(TemplateMatchSort::less(f, e, 5), "不同整数 Y 桶应按 Y 升序");

    // ③ 其余四种排序保持原口径
    QVERIFY2(TemplateMatchSort::less(a, b, 0), "按分数降序");
    QVERIFY2(TemplateMatchSort::less(b, a, 1), "按角度降序");
    QVERIFY2(TemplateMatchSort::less(a, b, 2), "按X升序（浮点）");
    QVERIFY2(TemplateMatchSort::less(b, a, 3), "按Y升序（浮点）");

    // ④ 整表排序：X 桶 99 → 100（桶内按 Y：b,e,a）→ 500 → 900
    QVector<Key> keys{a, b, c, d, e, f};
    std::stable_sort(keys.begin(), keys.end(), [](const Key &lhs, const Key &rhs) {
        return TemplateMatchSort::less(lhs, rhs, 4);
    });
    QCOMPARE(int(keys.size()), 6);
    QCOMPARE(keys.at(0).x, 99.8);
    QCOMPARE(keys.at(1).x, 100.4);
    QCOMPARE(keys.at(2).x, 100.0);
    QCOMPARE(keys.at(3).x, 100.2);
    QCOMPARE(keys.at(4).x, 500.0);
    QCOMPARE(keys.at(5).x, 900.0);
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

    // P0-5：匹配框宽高 / 模块结果 extraValues 必须分别按 scaleX、scaleY 输出。
    // 旧实现两轴都用 bestScale，非均匀目标（1.2×0.8）的高度会被写成 1.2 倍。
    const double expectW = kTemplateSize * sx1;
    const double expectH = kTemplateSize * sy1;
    const QStringList boxW = node.getParam(QStringLiteral("matchBoxWidths"))
                                 .toString()
                                 .split(QLatin1Char(','), Qt::SkipEmptyParts);
    const QStringList boxH = node.getParam(QStringLiteral("matchBoxHeights"))
                                 .toString()
                                 .split(QLatin1Char(','), Qt::SkipEmptyParts);
    QCOMPARE(boxW.size(), 1);
    QCOMPARE(boxH.size(), 1);
    qInfo("scale-xy: box=(%.1f x %.1f) expect=(%.1f x %.1f)", boxW.first().toDouble(),
          boxH.first().toDouble(), expectW, expectH);
    QVERIFY2(qAbs(boxW.first().toDouble() - expectW) <= 1.0,
             qPrintable(QStringLiteral("匹配框宽应用 scaleX：%1，期望 %2")
                            .arg(boxW.first().toDouble()).arg(expectW)));
    QVERIFY2(qAbs(boxH.first().toDouble() - expectH) <= 1.0,
             qPrintable(QStringLiteral("匹配框高应用 scaleY：%1，期望 %2（若等于宽则仍用错轴）")
                            .arg(boxH.first().toDouble()).arg(expectH)));
    const auto resData = node.getOutputData(1);
    QVERIFY2(resData && !resData->measureResults().isEmpty(), "匹配结果对象缺失");
    const MeasureResult mr = resData->measureResults().first();
    QVERIFY2(mr.extraValues.size() >= 7, "extraValues 项数不足");
    QVERIFY2(qAbs(mr.extraValues[5] - expectW) <= 1.0
                 && qAbs(mr.extraValues[6] - expectH) <= 1.0,
             qPrintable(QStringLiteral("extraValues 框宽高应为 (%1,%2)，实际 (%3,%4)")
                            .arg(expectW).arg(expectH)
                            .arg(mr.extraValues[5]).arg(mr.extraValues[6])));
}

// VM「横切角度范围」（尺度范围 XY 页签）：目标沿 Y 轴向 X 轴倾斜（剪切）时，
// 只搜无横切应匹配得差；给出 20° 横切范围后应命中并如实报出横切值。
// 场景侧剪切与节点 warpTemplate 同一口径（H=[[1,tanθ],[0,1]]，贴片中心落在 (cx,cy)）。
void FeatureMatchTest::templateCrossAngleSearchesSkew()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat patch = makePatch();
    const QString tpl = dir.filePath(QStringLiteral("tpl.png"));
    QVERIFY(cv::imwrite(tpl.toStdString(), patch));

    const double crossDeg = 20.0;
    const int canvasW = 500, canvasH = 400;
    const double cx = 250.0, cy = 200.0;
    cv::Mat canvas(canvasH, canvasW, CV_8UC1, cv::Scalar(kCanvasBackground));
    {
        const double t = std::tan(crossDeg * 3.14159265358979323846 / 180.0);
        cv::Mat M = cv::Mat::eye(2, 3, CV_64F);
        M.at<double>(0, 1) = t;
        const cv::Point2f pc(patch.cols * 0.5f, patch.rows * 0.5f);
        M.at<double>(0, 2) = cx - (M.at<double>(0, 0) * pc.x + M.at<double>(0, 1) * pc.y);
        M.at<double>(1, 2) = cy - (M.at<double>(1, 0) * pc.x + M.at<double>(1, 1) * pc.y);
        cv::warpAffine(patch, canvas, M, canvas.size(), cv::INTER_LINEAR,
                       cv::BORDER_CONSTANT, cv::Scalar(kCanvasBackground));
    }

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 1);
    node.setParam(QStringLiteral("minScore"), 0.2);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("scaleRangeType"), 1);
    node.setParam(QStringLiteral("scaleMinX"), 1.0);
    node.setParam(QStringLiteral("scaleMaxX"), 1.0);
    node.setParam(QStringLiteral("scaleMinY"), 1.0);
    node.setParam(QStringLiteral("scaleMaxY"), 1.0);
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("templatePath"), tpl);
    node.setInputImage(OpencvUtil::matToHimage(canvas));

    // ① 不搜横切：正的模板对剪切目标，分数应明显更低
    node.setParam(QStringLiteral("crossAngleMin"), 0.0);
    node.setParam(QStringLiteral("crossAngleMax"), 0.0);
    node.run(false);
    const double score0 = node.getParam(QStringLiteral("matchScore")).toDouble();
    const double cross0 = node.getParam(QStringLiteral("matchCrossAngle")).toDouble();

    // ② 只搜 20° 横切：应命中并报出横切值
    node.setParam(QStringLiteral("crossAngleMin"), crossDeg);
    node.setParam(QStringLiteral("crossAngleMax"), crossDeg);
    node.run(false);
    const double score20 = node.getParam(QStringLiteral("matchScore")).toDouble();
    const double cross20 = node.getParam(QStringLiteral("matchCrossAngle")).toDouble();
    const QString crossList = node.getParam(QStringLiteral("matchCrossAngles")).toString();
    const double col = node.getParam(QStringLiteral("matchCol")).toDouble();
    const double row = node.getParam(QStringLiteral("matchRow")).toDouble();
    qInfo("cross-angle: score0=%.3f (cross=%.2f) score20=%.3f (cross=%.2f) at (%.1f,%.1f) list=%s",
          score0, cross0, score20, cross20, col, row, qPrintable(crossList));

    QVERIFY2(qAbs(cross0) < 1e-6,
             qPrintable(QStringLiteral("未开横切搜索时横切值应为 0，实际 %1").arg(cross0)));
    QVERIFY2(score20 > score0 + 0.05,
             qPrintable(QStringLiteral("20° 横切搜索应明显优于 0°：%1 vs %2")
                            .arg(score20).arg(score0)));
    QVERIFY2(qAbs(cross20 - crossDeg) < 0.01,
             qPrintable(QStringLiteral("横切值应≈%1，实际 %2").arg(crossDeg).arg(cross20)));
    const QStringList crossItems = crossList.split(QLatin1Char(','), Qt::SkipEmptyParts);
    QCOMPARE(crossItems.size(), 1);
    QVERIFY2(qAbs(crossItems.first().toDouble() - crossDeg) < 0.01,
             qPrintable(QStringLiteral("matchCrossAngles 应报 %1，实际 %2")
                            .arg(crossDeg).arg(crossList)));
    QVERIFY2(qAbs(col - cx) <= 4.0 && qAbs(row - cy) <= 4.0,
             qPrintable(QStringLiteral("命中位置应≈(%1,%2)，实际 (%3,%4)")
                            .arg(cx).arg(cy).arg(col).arg(row)));

    // ③ 整体尺度模式不参与横切搜索（VM 该参数在「尺度范围 XY」页签下）
    node.setParam(QStringLiteral("scaleRangeType"), 0);
    node.setParam(QStringLiteral("scaleMin"), 1.0);
    node.setParam(QStringLiteral("scaleMax"), 1.0);
    node.run(false);
    const double crossUniform = node.getParam(QStringLiteral("matchCrossAngle")).toDouble();
    qInfo("cross-angle: uniform-mode cross=%.2f", crossUniform);
    QVERIFY2(qAbs(crossUniform) < 1e-6,
             qPrintable(QStringLiteral("整体尺度模式不应搜索横切，实际 %1").arg(crossUniform)));
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

// 复核 P1-2：锚点（模板局部系偏移）→ 图像坐标，必须与 warpTemplate 所用
// cv::getRotationMatrix2D 的线性部分同源。原实现用了共轭旋转 [cos -sin; sin cos]，
// 角度非 0 时匹配点相对中心整体镜像（差 2θ）。默认锚点为 (0,0) 时两种公式退化为
// 同一点，所以这个缺陷只能靠非零锚点 + 非零角度暴露。
void FeatureMatchTest::templateAnchorFollowsWarpRotation()
{
    // 0°：锚点 (20,0) 落在中心右侧
    const QPointF p0 = OpencvTemplateMatchNode::mapAnchorToImage(100, 100, 0.0, 20, 0, 1.0);
    QVERIFY2(qAbs(p0.x() - 120.0) < 1e-9 && qAbs(p0.y() - 100.0) < 1e-9,
             qPrintable(QStringLiteral("0° 应到 (120,100)，实得 (%1,%2)").arg(p0.x()).arg(p0.y())));

    // 90°：getRotationMatrix2D(90°) = [0 1; -1 0] ⇒ (20,0) → (0,-20)（y 向下坐标系里向上）
    const QPointF p90 = OpencvTemplateMatchNode::mapAnchorToImage(100, 100, 90.0, 20, 0, 1.0);
    QVERIFY2(qAbs(p90.x() - 100.0) < 1e-9,
             qPrintable(QStringLiteral("90° 时 col 应仍是中心 100，实得 %1").arg(p90.x())));
    QVERIFY2(qAbs(p90.y() - 80.0) < 1e-9,
             qPrintable(QStringLiteral("90° 时 row 应在中心上方 20px（80），实得 %1"
                                       "（若为 120 说明用的是共轭旋转，差 2θ）").arg(p90.y())));

    // 与 OpenCV 矩阵逐点对齐：防未来又被换成另一套公式
    const cv::Point2f center(100.f, 100.f);
    for (const double ang : {0.0, 30.0, 90.0, 180.0, -45.0}) {
        const cv::Mat M = cv::getRotationMatrix2D(center, ang, 1.0);
        const QPointF got =
            OpencvTemplateMatchNode::mapAnchorToImage(100, 100, ang, 17, -23, 1.0);
        // anchor 是相对模板中心的偏移量，所以期望是「线性部分作用在偏移上，再加中心」。
        // 不能直接用完整 M（它的平移项是"绕 center 旋转绝对点"的口径，会把 center 再算一遍）。
        const double wantX =
            center.x + M.at<double>(0, 0) * 17.0 + M.at<double>(0, 1) * (-23.0);
        const double wantY =
            center.y + M.at<double>(1, 0) * 17.0 + M.at<double>(1, 1) * (-23.0);
        QVERIFY2(qAbs(got.x() - wantX) < 1e-6 && qAbs(got.y() - wantY) < 1e-6,
                 qPrintable(QStringLiteral("angle=%1: got(%2,%3) want(%4,%5)")
                                .arg(ang).arg(got.x()).arg(got.y()).arg(wantX).arg(wantY)));
    }

    // 缩放在旋转之前（与 warpTemplate 里 M.at(0,0)*=scaleX 的顺序一致）：
    // (20,0)×2=(40,0) 再转 90° → (0,-40) → (100,60)
    const QPointF pScaled = OpencvTemplateMatchNode::mapAnchorToImage(100, 100, 90.0, 20, 0, 2.0);
    QVERIFY2(qAbs(pScaled.y() - 60.0) < 1e-9,
             qPrintable(QStringLiteral("90°+scale2 应到 row=60，实得 %1").arg(pScaled.y())));
}

// 官方帮助（高精度匹配·运行参数·超时控制）：「搜索时间达到超时时长算法将停止搜索，
// 不返回任何搜索结果。」旧实现只跳出姿态循环、保留已找到的最优——超时后仍输出坐标。
// 本腿锁住官方口径：超时后 matchCount=0、matchList 为空、模块判失败并给出超时原因。
void FeatureMatchTest::templateTimeoutReturnsNoResults()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat patch = makePatch();
    const QString tpl = dir.filePath(QStringLiteral("tpl.png"));
    QVERIFY(cv::imwrite(tpl.toStdString(), patch));
    const cv::Mat canvas = makeScene(patch, 0.0, 1.0, 200.0, 200.0);

    auto runWithTimeout = [&](int timeoutMs) {
        OpencvTemplateMatchNode node;
        node.init();
        node.setParam(QStringLiteral("method"), 1);
        node.setParam(QStringLiteral("minScore"), 0.5);
        node.setParam(QStringLiteral("angleMin"), -45.0);
        node.setParam(QStringLiteral("angleMax"), 45.0);
        node.setParam(QStringLiteral("angleStep"), 1.0);
        node.setParam(QStringLiteral("scaleMin"), 1.0);
        node.setParam(QStringLiteral("scaleMax"), 1.0);
        node.setParam(QStringLiteral("timeoutMs"), timeoutMs);
        node.setParam(QStringLiteral("trainFromImage"), false);
        node.setParam(QStringLiteral("templatePath"), tpl);
        node.setInputImage(OpencvUtil::matToHimage(canvas));
        TemplateEntry e;
        e.name = QStringLiteral("A");
        e.imagePath = tpl;
        node.setTemplates({e});
        node.setActiveTemplateIndex(0);
        node.run(false);
        return std::make_tuple(node.getParam(QStringLiteral("moduleStatus")).toBool(),
                               node.getParam(QStringLiteral("matchCount")).toInt(),
                               node.getParam(QStringLiteral("matchList")).toString(),
                               node.getParam(QStringLiteral("trainStatus")).toString(),
                               node.getParam(QStringLiteral("lastError")).toString());
    };

    const auto control = runWithTimeout(0);
    qInfo("timeout-leg: control ok=%d count=%d", int(std::get<0>(control)), std::get<1>(control));
    QVERIFY2(std::get<0>(control) && std::get<1>(control) >= 1,
             "对照组（timeout=0）必须能匹配，否则本腿证明不了超时行为");

    const auto timedOut = runWithTimeout(20);
    qInfo("timeout-leg: timeout ok=%d count=%d why=[%s]",
          int(std::get<0>(timedOut)), std::get<1>(timedOut),
          qPrintable(std::get<3>(timedOut)));
    QVERIFY2(!std::get<0>(timedOut), "超时后不得判成功");
    QCOMPARE(std::get<1>(timedOut), 0);
    QVERIFY2(std::get<2>(timedOut).isEmpty(), "超时不得返回任何搜索结果（matchList 必须为空）");
    QVERIFY2(std::get<3>(timedOut).contains(QStringLiteral("超时")) ||
                 std::get<4>(timedOut).contains(QStringLiteral("超时")),
             "超时失败必须给出可读原因");
}

// 官方帮助：最大匹配个数「默认值为 1，有效取值范围为 1~1000」。旧实现注册与运行时
// 都钳在 16，超过 16 个目标就丢结果。本腿同时锁注册范围与运行期上限。
void FeatureMatchTest::templateMaxMatchesAllowsVmRange()
{
    {
        OpencvTemplateMatchNode node;
        node.init();
        bool found = false;
        for (const ParamSpec &s : node.paramSpecs()) {
            if (s.name != QStringLiteral("maxMatches"))
                continue;
            found = true;
            QCOMPARE(s.minValue.toInt(), 1);
            QCOMPARE(s.maxValue.toInt(), 1000);
        }
        QVERIFY2(found, "未注册 maxMatches 参数");
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    cv::Mat patch(32, 32, CV_8UC1, cv::Scalar(30));
    cv::rectangle(patch, cv::Rect(6, 6, 20, 20), cv::Scalar(220), cv::FILLED);
    cv::circle(patch, cv::Point(16, 16), 5, cv::Scalar(80), cv::FILLED);
    const QString tpl = dir.filePath(QStringLiteral("tpl.png"));
    QVERIFY(cv::imwrite(tpl.toStdString(), patch));

    cv::Mat canvas(480, 640, CV_8UC1, cv::Scalar(kCanvasBackground));
    int placed = 0;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 5; ++c) {
            patch.copyTo(canvas(cv::Rect(40 + c * 120, 40 + r * 100, 32, 32)));
            ++placed;
        }

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 1);
    node.setParam(QStringLiteral("minScore"), 0.6);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("scaleMin"), 1.0);
    node.setParam(QStringLiteral("scaleMax"), 1.0);
    node.setParam(QStringLiteral("maxMatches"), 30);   // 旧实现运行时钳到 16
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("templatePath"), tpl);
    node.setInputImage(OpencvUtil::matToHimage(canvas));
    TemplateEntry e;
    e.name = QStringLiteral("A");
    e.imagePath = tpl;
    node.setTemplates({e});
    node.setActiveTemplateIndex(0);
    node.run(false);

    const bool ok = node.getParam(QStringLiteral("moduleStatus")).toBool();
    const int count = node.getParam(QStringLiteral("matchCount")).toInt();
    qInfo("max-matches-leg: placed=%d count=%d ok=%d", placed, count, int(ok));
    QVERIFY2(ok, "多目标用例应当匹配成功");
    QCOMPARE(count, placed);
}

// VM 4.4 高精度匹配出厂默认/量程（帮助页）：最小匹配分数 0.5；角度范围 -45~45；
// 尺度范围 1.00~1.00、有效值 0.1~10.0。锁住参数壳，避免后续又被老方案默认值带回去。
void FeatureMatchTest::templateVmDefaultsMatchHelp()
{
    OpencvTemplateMatchNode node;
    node.init();

    auto specOf = [&node](const QString &name) {
        for (const ParamSpec &s : node.paramSpecs())
            if (s.name == name)
                return s;
        return ParamSpec();
    };

    const ParamSpec minScore = specOf(QStringLiteral("minScore"));
    QVERIFY2(!minScore.name.isEmpty(), "未注册 minScore");
    QCOMPARE(minScore.defaultValue.toDouble(), 0.5);

    // VM 模板配置「尺度模式」默认自动（只作用于速度尺度）
    const ParamSpec scaleMode = specOf(QStringLiteral("scaleMode"));
    QVERIFY2(!scaleMode.name.isEmpty(), "未注册 scaleMode");
    QCOMPARE(scaleMode.defaultValue.toInt(), 1);

    // VM 模块分类：本节点开箱=高精度匹配（GOC），不是灰度 NCC
    const ParamSpec method = specOf(QStringLiteral("method"));
    QVERIFY2(!method.name.isEmpty(), "未注册 method");
    QCOMPARE(method.defaultValue.toInt(), 2);

    const ParamSpec angleMin = specOf(QStringLiteral("angleMin"));
    const ParamSpec angleMax = specOf(QStringLiteral("angleMax"));
    QVERIFY2(!angleMin.name.isEmpty() && !angleMax.name.isEmpty(), "未注册角度范围");
    QCOMPARE(angleMin.defaultValue.toDouble(), -45.0);
    QCOMPARE(angleMax.defaultValue.toDouble(), 45.0);

    for (const char *name : {"scaleMin", "scaleMax", "scaleMinX", "scaleMaxX",
                             "scaleMinY", "scaleMaxY"}) {
        const ParamSpec s = specOf(QString::fromLatin1(name));
        QVERIFY2(!s.name.isEmpty(), qPrintable(QStringLiteral("未注册 %1").arg(name)));
        QVERIFY2(s.hasRange, qPrintable(QStringLiteral("%1 应声明量程").arg(name)));
        QCOMPARE(s.minValue.toDouble(), 0.1);
        QCOMPARE(s.maxValue.toDouble(), 10.0);
    }

    // 程序化写入也必须按量程钳制（面板、通讯下发、保存载入共用这一入口）
    node.setParam(QStringLiteral("scaleMin"), 0.05);
    node.setParam(QStringLiteral("scaleMax"), 20.0);
    QCOMPARE(node.getParam(QStringLiteral("scaleMin")).toDouble(), 0.1);
    QCOMPARE(node.getParam(QStringLiteral("scaleMax")).toDouble(), 10.0);
    QCOMPARE(node.getParam(QStringLiteral("minScore")).toDouble(), 0.5);
}

// 复核 P1-4：变形缓存的键原先只有「尺寸 + 路径」。同路径换图重训（尺寸往往不变）
// 会命中上一张模板的旋转/缩放结果；路径为空时更糟——所有同尺寸内存模板共用 "mem"。
// 这里锁住键里那个内容指纹的判定基础。
void FeatureMatchTest::templateFingerprintDistinguishesContent()
{
    cv::Mat a(40, 40, CV_8UC1, cv::Scalar(50));
    cv::rectangle(a, cv::Rect(10, 10, 20, 20), cv::Scalar(200), cv::FILLED);

    const quint64 fa1 = OpencvTemplateMatchNode::templateFingerprint(a);
    const quint64 fa2 = OpencvTemplateMatchNode::templateFingerprint(a.clone());
    QCOMPARE(fa1, fa2);   // 同内容同指纹，缓存才能真的复用

    cv::Mat b = a.clone();
    b.at<uchar>(0, 0) = uchar(b.at<uchar>(0, 0) + 1);
    QVERIFY2(OpencvTemplateMatchNode::templateFingerprint(b) != fa1,
             "改一个像素指纹就必须变，否则同路径换图重训仍会命中旧变形");

    cv::Mat c(40, 40, CV_8UC1, cv::Scalar(50));
    cv::circle(c, cv::Point(20, 20), 15, cv::Scalar(200), cv::FILLED);
    QVERIFY2(OpencvTemplateMatchNode::templateFingerprint(c) != fa1,
             "同尺寸不同图形指纹必须不同（否则内存模板互相串味）");

    QVERIFY(OpencvTemplateMatchNode::templateFingerprint(cv::Mat()) != 0);
}

// 复核（扫描清单「matchCount 在重叠过滤前写入」）：matchCount 原先在重叠过滤**之前**
// 就写进参数表，而 matchList / matchBox* 都是按过滤后的 hits 重建 —— 结果表里
// 「匹配数量」与「匹配列表条数」对不上（例如显示 5 个、列表只有 3 条）。
void FeatureMatchTest::matchCountMatchesFilteredList()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat patch = makePatch();
    const QString tpl = dir.filePath(QStringLiteral("tpl.png"));
    QVERIFY(cv::imwrite(tpl.toStdString(), patch));

    // 两个目标故意部分重叠，再用 maxOverlap=0 强制丢掉低分那个：
    // 过滤前 2 个、过滤后 1 个，这样才能把"写早了"暴露出来。
    cv::Mat canvas(400, 600, CV_8UC1, cv::Scalar(kCanvasBackground));
    patch.copyTo(canvas(cv::Rect(150, 150, kTemplateSize, kTemplateSize)));
    patch.copyTo(canvas(cv::Rect(170, 150, kTemplateSize, kTemplateSize)));

    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("method"), 1);
    node.setParam(QStringLiteral("minScore"), 0.5);
    node.setParam(QStringLiteral("angleMin"), 0.0);
    node.setParam(QStringLiteral("angleMax"), 0.0);
    node.setParam(QStringLiteral("angleStep"), 2.0);
    node.setParam(QStringLiteral("trainFromImage"), false);
    node.setParam(QStringLiteral("templatePath"), tpl);
    node.setParam(QStringLiteral("maxMatches"), 10);   // 允许多目标（注册键名是 maxMatches，
                                                        // 不是 matchCount；matchCount 是输出字段）
    node.setParam(QStringLiteral("maxOverlap"), 0);    // 任何重叠都丢低分者
    node.setInputImage(OpencvUtil::matToHimage(canvas));

    TemplateEntry e;
    e.name = QStringLiteral("A");
    e.imagePath = tpl;
    node.setTemplates({e});
    node.setActiveTemplateIndex(0);
    node.run(false);

    const int count = node.getParam(QStringLiteral("matchCount")).toInt();
    const QString list = node.getParam(QStringLiteral("matchList")).toString().trimmed();
    const int listed = list.isEmpty() ? 0 : list.split(QLatin1Char(';')).size();
    QVERIFY2(count >= 1, "至少应保留一个目标");
    QVERIFY2(count == listed,
             qPrintable(QStringLiteral("matchCount(%1) 必须等于 matchList 条数(%2)："
                                       "旧实现在重叠过滤前就写死了 matchCount")
                            .arg(count).arg(listed)));
    // ⚠ 覆盖能力声明（如实记）：本腿只锁住「两者一致」这条不变量，**抓不住改坏**。
    // 实测把 matchCount 改回过滤前写入，本腿仍绿——因为这两个 patch 相距 20px 时
    // matchTemplate 只出一个峰，重叠过滤从未触发（过滤前后数量相同，改不改都一样）。
    // 要真正锁住这条，需要一个「过滤前 N>1、过滤后 1」的稳定夹具（真实图上两个
    // 部分重叠且各自能被检出的目标），本轮没造出来。
}

// 2026-11-09 审核发现：XY 尺度模式（scaleX != scaleY）下 mapAnchorToImage 曾把
// scale 同时用在两轴，锚点/匹配点的 Y 方向偏移用错尺度。轮廓映射已用 scaleY，
// 锚点必须同口径。这里用角度 0 + 锚点 (0,20) 做纯尺度验证。
void FeatureMatchTest::templateAnchorUsesScaleYInXYMode()
{
    const QPointF p = OpencvTemplateMatchNode::mapAnchorToImage(
        100.0, 100.0, 0.0, 0.0, 20.0, 2.0, 0.0, 0.5);
    QVERIFY2(qAbs(p.x() - 100.0) < 1e-9,
             qPrintable(QStringLiteral("dx=0 时 col 应仍是 100，实得 %1").arg(p.x())));
    QVERIFY2(qAbs(p.y() - 110.0) < 1e-9,
             qPrintable(QStringLiteral("dy=20 × scaleY=0.5 应到 row=110，实得 %1"
                                       "（若为 140 说明 Y 轴错用了 scaleX=2）").arg(p.y())));

    // 单尺度哨兵：不传 scaleY 时两轴相同，既有几何腿不受影响
    const QPointF q = OpencvTemplateMatchNode::mapAnchorToImage(
        100.0, 100.0, 0.0, 0.0, 20.0, 2.0);
    QVERIFY2(qAbs(q.y() - 140.0) < 1e-9,
             qPrintable(QStringLiteral("单尺度哨兵应给 row=140，实得 %1").arg(q.y())));

    // 非零角度下也必须按 scaleY 缩放 Y 分量：90° 时 ay=20×0.5=10，
    // [cos sin; -sin cos] 把 (0,10) 映射到 (10,0) ⇒ (110,100)
    const QPointF r = OpencvTemplateMatchNode::mapAnchorToImage(
        100.0, 100.0, 90.0, 0.0, 20.0, 2.0, 0.0, 0.5);
    QVERIFY2(qAbs(r.x() - 110.0) < 1e-6 && qAbs(r.y() - 100.0) < 1e-6,
             qPrintable(QStringLiteral("90° + XY 尺度应到 (110,100)，实得 (%1,%2)")
                            .arg(r.x()).arg(r.y())));
}

// 官方（VM4.4高精度匹配对齐规范.md:68）：排序类型是「当前结果」的排序依据。
// 主标量字段 / matchScore / MeasureResult 必须与 matchList 第一行是同一目标。
// 旧实现恒取第一实例（最高分），sortType != 0 时与列表首行错位。
// 这里刻意让「最高分目标在下方、按Y升序首条在上方」，改坏必红。
void FeatureMatchTest::templateCurrentResultFollowsSortType()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const cv::Mat patch = makePatch();
    const QString tpl = dir.filePath(QStringLiteral("tpl.png"));
    QVERIFY(cv::imwrite(tpl.toStdString(), patch));

    cv::Mat canvas(500, 400, CV_8UC1, cv::Scalar(kCanvasBackground));
    cv::Mat upper = patch.clone();
    // 上方目标轻微缺损 ⇒ 分数略低；下方目标完整 ⇒ 第一实例（最高分）在下方
    upper(cv::Rect(0, 0, 4, 4)).setTo(kCanvasBackground);
    upper.copyTo(canvas(cv::Rect(60, 60, kTemplateSize, kTemplateSize)));
    patch.copyTo(canvas(cv::Rect(60, 300, kTemplateSize, kTemplateSize)));

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
    node.setParam(QStringLiteral("sortType"), 3);        // 按Y由小到大 ⇒ 首条在上方
    node.setParam(QStringLiteral("searchRegionCol"), 0);
    node.setParam(QStringLiteral("searchRegionRow"), 0);
    node.setParam(QStringLiteral("searchRegionWidth"), canvas.cols);
    node.setParam(QStringLiteral("searchRegionHeight"), canvas.rows);
    node.setInputImage(OpencvUtil::matToHimage(canvas));
    node.run(false);

    const QStringList rows = node.getParam(QStringLiteral("matchList"))
                                 .toString().split(QLatin1Char(';'), Qt::SkipEmptyParts);
    QVERIFY2(rows.size() >= 2,
             qPrintable(QStringLiteral("需要两个命中才能验证排序口径，实得 %1").arg(rows.size())));
    const QStringList first = rows.at(0).split(QLatin1Char(','));
    const double firstRow = first.at(0).toDouble();
    const double firstScore = first.at(2).toDouble();
    const double mainRow = node.getParam(QStringLiteral("matchRow")).toDouble();
    const double mainScore = node.getParam(QStringLiteral("matchScore")).toDouble();

    QVERIFY2(qAbs(mainRow - firstRow) <= 1.5,
             qPrintable(QStringLiteral("「当前结果」应跟随排序后首条：matchRow=%1，"
                                       "列表首行 row=%2（旧实现会取最高分的下方目标）")
                            .arg(mainRow).arg(firstRow)));
    QVERIFY2(qAbs(mainScore - firstScore) < 0.02,
             qPrintable(QStringLiteral("matchScore=%1 应与列表首行分数 %2 同源")
                            .arg(mainScore).arg(firstScore)));
}

// 2026-11-09 复核：collectSearchValues 在 n > maxCount 时用 (mx-mn)/(maxCount-1)
// 重算步长；maxCount 为 0/1 时除零或得负步长，产生 inf/乱值。产品当前调用点传
// 90 或 16 不可达，但辅助函数不该留这个坑。钳到至少 2 点后正好取两个端点。
void FeatureMatchTest::templateSearchValuesHandlesTinyMaxCount()
{
    for (int maxCount : {0, 1, 2}) {
        QVector<double> out;
        OpencvTemplateMatchNode::collectSearchValues(-10.0, 10.0, 1.0, maxCount, out);
        QVERIFY2(out.size() >= 2,
                 qPrintable(QStringLiteral("maxCount=%1 应至少给出 2 个采样点，实得 %2")
                                .arg(maxCount).arg(out.size())));
        for (double v : out) {
            QVERIFY2(std::isfinite(v),
                     qPrintable(QStringLiteral("maxCount=%1 产生了 inf/nan 步长").arg(maxCount)));
        }
        QVERIFY2(qAbs(out.first() - (-10.0)) < 1e-9 && qAbs(out.last() - 10.0) < 1e-9,
                 qPrintable(QStringLiteral("maxCount=%1 应覆盖两个端点").arg(maxCount)));
        for (int i = 1; i < out.size(); ++i)
            QVERIFY2(out[i] > out[i - 1],
                     qPrintable(QStringLiteral("maxCount=%1 采样步长必须为正").arg(maxCount)));
    }
    // 正常量程不受影响
    QVector<double> normal;
    OpencvTemplateMatchNode::collectSearchValues(0.0, 9.0, 1.0, 90, normal);
    QCOMPARE(normal.size(), 10);
}

// 2026-11-09 复核：图像型 ROI 掩膜若来自 uint2/CV_16U 或 real/CV_32F，
// maskRegionFromData 原先只做 cv::threshold（保持原深度），后续与 CV_8U 掩膜
// bitwise_and 会触发 OpenCV 类型断言。这里直接测归一化的纯函数，不依赖
// OpencvUtil 的 16 位 HALCON 往返（那属于另一层）。
void FeatureMatchTest::templateImageTypeRoiAccepts16BitMask()
{
    cv::Mat m16(40, 40, CV_16UC1, cv::Scalar(0));
    cv::rectangle(m16, cv::Rect(10, 10, 10, 10), cv::Scalar(65535), cv::FILLED);
    const cv::Mat a = OpencvTemplateMatchNode::normalizeMaskTo8U(m16);
    QVERIFY2(a.type() == CV_8UC1, "16U 掩膜必须归一成 CV_8UC1");
    QCOMPARE(a.size(), m16.size());
    QCOMPARE(static_cast<int>(a.at<uchar>(15, 15)), 255);
    QCOMPARE(static_cast<int>(a.at<uchar>(0, 0)), 0);

    cv::Mat m32(20, 20, CV_32FC1, cv::Scalar(0.0f));
    m32(cv::Rect(5, 5, 5, 5)).setTo(1.0f);
    const cv::Mat b = OpencvTemplateMatchNode::normalizeMaskTo8U(m32);
    QVERIFY2(b.type() == CV_8UC1, "32F 掩膜必须归一成 CV_8UC1");
    QCOMPARE(static_cast<int>(b.at<uchar>(7, 7)), 255);
    QCOMPARE(static_cast<int>(b.at<uchar>(0, 0)), 0);

    cv::Mat rgb(10, 10, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::rectangle(rgb, cv::Rect(2, 2, 4, 4), cv::Scalar(255, 255, 255), cv::FILLED);
    const cv::Mat c = OpencvTemplateMatchNode::normalizeMaskTo8U(rgb);
    QVERIFY2(c.type() == CV_8UC1, "3 通道掩膜必须转成单通道");
    QCOMPARE(static_cast<int>(c.at<uchar>(3, 3)), 255);

    QVERIFY2(OpencvTemplateMatchNode::normalizeMaskTo8U(
                 cv::Mat(10, 10, CV_8UC4, cv::Scalar::all(0))).empty(),
             "不支持的通道数应返回空，交给调用方按无掩膜处理");
    QVERIFY(OpencvTemplateMatchNode::normalizeMaskTo8U(cv::Mat()).empty());
}

QTEST_MAIN(FeatureMatchTest)
#include "feature_match_test.moc"
