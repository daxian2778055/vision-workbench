// C2 标定链路节点级用例（推进计划 §3.2 的"各环节用例"那一半）
//
// 覆盖 §3.2 表里被判定"有实现、无用例"的环节，全部走 execute()（= HalconNode::process()），
// 因此受 P0-1 契约约束：每轮预置 moduleStatus=true，失败必须由节点自己写 false。
//   ① 标定板角点检出 + ② 内参求解：OpencvCalibNode 逐帧累积 → calibrateCamera，
//      合成帧按**已知内参 K + 位姿**投影生成 ⇒ fx/fy/cx/cy 有真值可对照（不再只判"像个相机"）
//   ③ 畸变校正（内参消费端）：合成帧按**已知 K + 已知畸变系数 D** 投影（=畸变帧），
//      真值 = 同一批 3D 点按 D 为空投影；节点输出的实测角点须回到真值 ⇒ 钉住 R-4 的断链修复
//   ④ N 点：已知仿射生成点对 → 报回来的 6 元矩阵与真值对照
//   ⑤ 手眼：已知刚体（旋转+平移）对照；另一条钉住"2D 刚体无缩放"的口径（结论 C）
//   ⑥ 换算消费：CalibrationManager 的 set/get/toJson/fromJson/applyHomography + CoordinateTransformNode 真读矩阵
//   ⑦ 失败可见性：点对不足／仿射估计退化 ⇒ 必须判红（结论 D 修复的回归锁）
//
// 单例处置：CalibrationManager 是进程级单例，initTestCase 快照、cleanupTestCase 用快照还原，
// 否则本套件写进去的 "cam_params"/测试用名会污染同进程后续用例。
#include <QtTest/QtTest>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

#include "OpencvCalibNode.h"
#include "OpencvUndistortNode.h"
#include "NPointCalibNode.h"
#include "HandEyeCalibNode.h"
#include "CoordinateTransformNode.h"
#include "FlowScene.h"
#include "CalibrationManager.h"
#include "DataObject.h"
#include "OpencvUtil.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>
#include <HalconCpp.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

class CalibChainTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void nPointRecoversKnownAffine();      // ④
    void nPointInsufficientPairsJudgeRed(); // ⑦
    void nPointDegeneratePairsJudgeRed();   // ⑦
    void handEyeRecoversKnownRigid();       // ⑤
    void handEyeIsRigidNotScaled();         // ⑤ 口径（结论 C）
    void handEyeInsufficientPairsJudgeRed();// ⑦
    void calibNodeRecoversKnownIntrinsics();// ①②
    void calibNodeJudgeRedWithoutBoard();   // ① 失败面
    void undistortRecoversKnownDistortion();     // ③ 正向（手填内参）
    void undistortConsumesStoredCamParams();     // ③ 读侧：单例 9 元组
    void undistortChainFromCalibration();        // ①②→③ 端到端
    void undistortZeroDistortionKeepsImage();    // ③ D=0 ⇒ 不重采样
    void undistortJudgeRedWithoutCamParams();    // ③ 判红：键不存在
    void undistortJudgeRedOnShortTuple();        // ③ 判红：项数不足 9
    void undistortJudgeRedOnNonFiniteTuple();    // ③ 判红：含非有限值
    void undistortNeverFallsBackToManual();      // ③ 判红：键缺失不得回退手填（R-2 形态）
    void undistortJudgeRedOnInvalidManualFx();   // ③ 判红：手填 fx/fy 非正
    void calibrationManagerRoundTripAndApply(); // ⑥
    void coordinateTransformConsumesStoredMatrix(); // ⑥
    void coordinateTransformPrefersSceneFixture();  // ⑥ R-2 正向：场景夹具优先于同名单例键
    void coordinateTransformManualMatrixStaysGreen();// ⑥ R-2 反向闸：手填通路不得被一起判红
    void coordinateTransformJudgeRedWithoutMatrix(); // ⑥ R-2 判红：声明夹具却取不到矩阵
    void coordinateTransformJudgeRedOnShortMatrix(); // ⑥ R-2 判红：项数不足且原因带实际项数
    void coordinateTransformJudgeRedWhenFixtureHasNoMatrix(); // ⑥ R-2 判红：夹具在场景里但没有矩阵

private:
    QJsonObject m_managerSnapshot;
};

namespace {

const int kImgW = 640;
const int kImgH = 480;

/// 逐元素按容差比较两个同长向量；不符时回报 "第 i 项 a vs b"
QString vecDeviation(const QVector<double> &got, const QVector<double> &want, double tol)
{
    if (got.size() != want.size())
        return QStringLiteral("长度 %1 vs %2").arg(got.size()).arg(want.size());
    for (int i = 0; i < got.size(); ++i)
        if (std::abs(got[i] - want[i]) > tol)
            return QStringLiteral("第 %1 项 %2 vs %3")
                       .arg(i).arg(got[i], 0, 'g', 12).arg(want[i], 0, 'g', 12);
    return QString();
}

QString csvOf(const QVector<double> &v)
{
    QStringList parts;
    for (double d : v)
        parts << QString::number(d, 'g', 10);
    return parts.join(QStringLiteral(", "));
}

/// 取一份标定矩阵（端口 1 的 Matrix 载荷）；端口被清空或类型不符时返回空。
QVector<double> matrixFromPort(NodeBase &node, int port)
{
    auto d = node.getOutputData(port);
    if (!d)
        return {};
    return d->getData().value<QVector<double>>();
}

QString noteOf(HalconNode &node)
{
    return node.getParam(QStringLiteral("calibNote")).toString();
}

/// ⑥ R-2：坐标系变换的失败原因栏。判红时 process() 会清空全部输出端口 ⇒ 原因只能留在这里
QString transformNoteOf(NodeBase &node)
{
    return node.getParam(QStringLiteral("transformNote")).toString();
}

/// 结果端口（1）的点载荷；端口为空时 *present=false（判红轮必须是 false）
QPointF resultPointOf(NodeBase &node, bool *present)
{
    auto d = node.getOutputData(1);
    if (present)
        *present = bool(d);
    return d ? d->getPoint() : QPointF();
}

// ---------------- ④ N 点：真值仿射 ----------------
// 世界 = 缩放 s × 旋转 θ × 像素 + 平移：与 estimateAffine2D 的 6 元输出同形（m11 m12 m13 / m21 m22 m23）
struct AffineTruth
{
    double m11, m12, m13, m21, m22, m23;
};

QPointF applyTruth(const AffineTruth &a, double px, double py)
{
    return QPointF(a.m11 * px + a.m12 * py + a.m13, a.m21 * px + a.m22 * py + a.m23);
}

/// 点对文本：每行 "像素X,像素Y 世界X,世界Y"（解析口径见 NPointCalibNode.cpp 的 parsePointPairs）
QString pairsText(const QVector<QPointF> &pixels, const QVector<QPointF> &worlds)
{
    QStringList lines;
    for (int i = 0; i < pixels.size(); ++i)
        lines << QStringLiteral("%1,%2 %3,%4")
                     .arg(pixels[i].x(), 0, 'f', 6).arg(pixels[i].y(), 0, 'f', 6)
                     .arg(worlds[i].x(), 0, 'f', 6).arg(worlds[i].y(), 0, 'f', 6);
    return lines.join(QLatin1Char('\n'));
}

HalconCpp::HImage blankImage(int w = kImgW, int h = kImgH)
{
    cv::Mat img(h, w, CV_8UC1, cv::Scalar(40));
    return OpencvUtil::matToHimage(img);
}

void feedImage(NodeBase &node, const HalconCpp::HImage &himg)
{
    auto d = QSharedPointer<DataObject>::create();
    d->setHImage(himg);
    node.setInputData(0, d);
}

// ---------------- ①② 已知内参的合成棋盘格帧 ----------------
// 相机真值：K 固定、畸变取 0 ⇒ 标定输出的 fx/fy/cx/cy 可与真值逐元素对照，k1..p2 的偏离即"模型
// 用零畸变数据去拟 8 系数"的固有噪声。理想棋盘格画在一张图上（方格 30px），其像素坐标与
// "mm 棋盘坐标"数值相同（squareSize 也设 30），于是平面单应 H = K·[r1 r2 t] 就是这些帧的成像模型。
const double kTrueFx = 520.0;
const double kTrueFy = 518.0;
const double kTrueCx = 320.0;
const double kTrueCy = 240.0;
const int kPatternW = 9;      // 内角点（宽）
const int kPatternH = 6;      // 内角点（高）
const double kSquare = 30.0;  // 方格边长：px 与 mm 同值
const int kIdealMargin = 24;  // 理想图上留给棋盘的外边距（避免贴边检测失败）

/// 理想棋盘格（左上角原点，方格 kSquare px），画在 kIdealMargin 偏移处
cv::Mat makeIdealBoard()
{
    const int cols = kPatternW + 1;
    const int rows = kPatternH + 1;
    cv::Mat board = cv::Mat::zeros(kImgH, kImgW, CV_8UC1);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            if ((r + c) % 2 == 0)
                cv::rectangle(board,
                              cv::Rect(kIdealMargin + c * kSquare, kIdealMargin + r * kSquare,
                                       int(kSquare), int(kSquare)),
                              cv::Scalar(255), cv::FILLED);
    return board;
}

/// 按真值 K + 位姿把理想棋盘格投影成一帧（零畸变针孔成像 ⇒ 平面单应）
cv::Mat renderView(const cv::Mat &ideal, const cv::Vec3d &rvec, double depthMm)
{
    cv::Mat K = (cv::Mat_<double>(3, 3) << kTrueFx, 0, kTrueCx, 0, kTrueFy, kTrueCy, 0, 0, 1);
    cv::Mat R;
    cv::Rodrigues(rvec, R);

    // 棋盘中心（**mm 系**，不含理想图的外边距 —— 边距已由 T 消掉）落在光轴上：t = (0, 0, Z) − R·C，
    // 主点 (cx, cy) 由 K 提供（写成 (cx, cy, Z) 就等于把主点加了两次）
    const cv::Mat C = (cv::Mat_<double>(3, 1)
                       << (kPatternW + 1) * kSquare * 0.5,
                       (kPatternH + 1) * kSquare * 0.5, 0.0);
    const cv::Mat t = (cv::Mat_<double>(3, 1) << 0.0, 0.0, depthMm) - R * C;

    cv::Mat M = cv::Mat::eye(3, 3, CV_64F);
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 2; ++c)
            M.at<double>(r, c) = R.at<double>(r, c);
        M.at<double>(r, 2) = t.at<double>(r);
    }
    cv::Mat T = cv::Mat::eye(3, 3, CV_64F);   // 理想像素 → 棋盘 mm（消掉外边距）
    T.at<double>(0, 2) = -kIdealMargin;
    T.at<double>(1, 2) = -kIdealMargin;
    const cv::Mat H = K * M * T;

    cv::Mat out;
    cv::warpPerspective(ideal, out, H, cv::Size(kImgW, kImgH), cv::INTER_LINEAR,
                        cv::BORDER_CONSTANT, cv::Scalar(0));
    // 纯二值渲染太"干净"，检测器在真实帧上才可靠：轻模糊 + 小幅噪声（与既有合成帧同一配方）
    cv::GaussianBlur(out, out, cv::Size(3, 3), 0);
    cv::Mat noise(kImgH, kImgW, CV_8UC1);
    cv::randu(noise, cv::Scalar(0), cv::Scalar(8));
    out += noise;
    return out;
}

std::vector<cv::Mat> makeCalibFrames()
{
    const cv::Mat ideal = makeIdealBoard();
    const cv::Vec3d poses[] = {
        cv::Vec3d(0.0, 0.0, 0.0),
        cv::Vec3d(0.22, 0.18, 0.02),
        cv::Vec3d(-0.20, 0.24, -0.02),
        cv::Vec3d(0.16, -0.26, 0.03),
        cv::Vec3d(-0.26, -0.14, 0.0),
    };
    const double depths[] = {620.0, 580.0, 660.0, 560.0, 700.0};
    std::vector<cv::Mat> frames;
    for (int i = 0; i < 5; ++i)
        frames.push_back(renderView(ideal, poses[i], depths[i]));
    return frames;
}

// ---------------- ③ 畸变校正：已知 K + 已知畸变系数的合成帧 ----------------
// 口径：同一批棋盘格 3D 点，projectPoints(K, D) 落在哪儿 = "相机实拍的样子"（畸变帧）；
// projectPoints(K, 空) 落在哪儿 = "去畸变后应当落在的位置"（**真值**）。两者之差就是
// "校正该做多少功"——断言因此不拿节点自己的输出当真值，也不与节点共用同一条 remap 通路。
const double kDistK1 = -0.45;
const double kDistK2 = 0.12;
const double kDistP1 = 0.0015;
const double kDistP2 = -0.0010;

/// ③ 组的主测位姿（棋盘中心落在光轴上，与 renderView 同一口径）
const cv::Vec3d kDistPose(0.12, 0.18, 0.05);
const double kDistDepth = 420.0;

cv::Mat trueIntrinsics()
{
    return (cv::Mat_<double>(3, 3) << kTrueFx, 0, kTrueCx, 0, kTrueFy, kTrueCy, 0, 0, 1);
}

cv::Mat trueDistortion()
{
    return (cv::Mat_<double>(8, 1) << kDistK1, kDistK2, kDistP1, kDistP2, 0, 0, 0, 0);
}

/// 位姿 → (R, t)：棋盘中心（mm 系，含整块棋盘的一半）落在光轴上
cv::Mat boardTranslation(const cv::Vec3d &rvec, double depthMm, cv::Mat *R)
{
    cv::Rodrigues(rvec, *R);
    const cv::Mat C = (cv::Mat_<double>(3, 1)
                       << (kPatternW + 1) * kSquare * 0.5,
                       (kPatternH + 1) * kSquare * 0.5, 0.0);
    return (cv::Mat_<double>(3, 1) << 0.0, 0.0, depthMm) - (*R) * C;
}

/// 内角点（kPatternW×kPatternH，行优先）的 3D 坐标（mm）：方格 (c,r) 覆盖
/// [c·kSquare,(c+1)·kSquare]，故第 (i,j) 个内角点落在 ((i+1)·kSquare,(j+1)·kSquare)
std::vector<cv::Point3f> innerCornerObjects()
{
    std::vector<cv::Point3f> obj;
    for (int r = 0; r < kPatternH; ++r)
        for (int c = 0; c < kPatternW; ++c)
            obj.emplace_back(float((c + 1) * kSquare), float((r + 1) * kSquare), 0.0f);
    return obj;
}

/// 内角点投影：D 传空矩阵 ⇒ 零畸变，即"去畸变后应落的位置"（真值）
std::vector<cv::Point2f> projectInnerCorners(const cv::Vec3d &rvec, double depthMm,
                                             const cv::Mat &D)
{
    cv::Mat R;
    const cv::Mat t = boardTranslation(rvec, depthMm, &R);
    std::vector<cv::Point2f> img;
    cv::projectPoints(innerCornerObjects(), rvec, t, trueIntrinsics(), D, img);
    return img;
}

/// 按已知 K+D 把棋盘格"拍"成一张畸变帧：4× 超采样画方格（方格轮廓按亚像素落位取整到
/// 1/4 px）再用 INTER_AREA 回采，避免 1px 量化把角点真值本身污染掉。
cv::Mat renderDistortedView(const cv::Vec3d &rvec, double depthMm, const cv::Mat &D, int ss = 4)
{
    cv::Mat R;
    const cv::Mat t = boardTranslation(rvec, depthMm, &R);
    const cv::Mat K = trueIntrinsics();
    const int cols = kPatternW + 1;   // 方格数比内角点多一圈（每个内角点四周要有 4 个方格）
    const int rows = kPatternH + 1;

    cv::Mat big = cv::Mat::zeros(kImgH * ss, kImgW * ss, CV_8UC1);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if ((r + c) % 2) continue;   // 只画白格；黑格与全黑背景同色（与 ①② 的合成帧同配方）
            const float x0 = float(c * kSquare), x1 = float((c + 1) * kSquare);
            const float y0 = float(r * kSquare), y1 = float((r + 1) * kSquare);
            std::vector<cv::Point3f> quad = {cv::Point3f(x0, y0, 0.f), cv::Point3f(x1, y0, 0.f),
                                             cv::Point3f(x1, y1, 0.f), cv::Point3f(x0, y1, 0.f)};
            std::vector<cv::Point2f> pp;
            cv::projectPoints(quad, rvec, t, K, D, pp);
            std::vector<std::vector<cv::Point>> poly(1);
            for (const cv::Point2f &p : pp)
                poly[0].emplace_back(int(std::lround(p.x * ss)), int(std::lround(p.y * ss)));
            cv::fillPoly(big, poly, cv::Scalar(255));
        }
    }

    cv::Mat out;
    cv::resize(big, out, cv::Size(kImgW, kImgH), 0, 0, cv::INTER_AREA);
    cv::GaussianBlur(out, out, cv::Size(3, 3), 0);   // 对称核 ⇒ 不引入角点位移
    cv::Mat noise(kImgH, kImgW, CV_8UC1);
    cv::randu(noise, cv::Scalar(0), cv::Scalar(8));
    out += noise;
    return out;
}

/// 实测一张图上的 kPatternW×kPatternH 内角点（与 OpencvCalibNode 同优先级：SB 优先、经典兜底）
std::vector<cv::Point2f> detectInnerCorners(const cv::Mat &gray)
{
    std::vector<cv::Point2f> cs;
    const bool sb = cv::findChessboardCornersSB(gray, cv::Size(kPatternW, kPatternH), cs,
                                                cv::CALIB_CB_NORMALIZE_IMAGE);
    if (!sb && !cv::findChessboardCorners(gray, cv::Size(kPatternW, kPatternH), cs,
                                          cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE))
        return {};
    cv::cornerSubPix(gray, cs, cv::Size(5, 5), cv::Size(-1, -1),
                     cv::TermCriteria(cv::TermCriteria::EPS | cv::TermCriteria::COUNT, 30, 0.01));
    return cs;
}

/// 两组点集的双向最近邻最大距离。不用逐号配对：检测顺序可能整体翻转（SB/经典两条路的起点不同），
/// 而方格间距 30px 远于此处的偏差量级，最近邻配对无歧义。数量不等时返回 -1（调用方按"检出数不符"处理）。
double setDeviation(const std::vector<cv::Point2f> &a, const std::vector<cv::Point2f> &b)
{
    if (a.empty() || a.size() != b.size())
        return -1.0;
    double worst = 0.0;
    for (size_t i = 0; i < 2; ++i) {
        const std::vector<cv::Point2f> &from = i ? b : a;
        const std::vector<cv::Point2f> &to = i ? a : b;
        for (const cv::Point2f &p : from) {
            double best = 1e18;
            for (const cv::Point2f &q : to)
                best = std::min(best, std::hypot(double(p.x - q.x), double(p.y - q.y)));
            worst = std::max(worst, best);
        }
    }
    return worst;
}

/// 取节点端口 0 的图像并转成 OpenCV 灰度矩阵（无产出/类型不符时返回空）
cv::Mat grayFromOutputPort(NodeBase &node, int port)
{
    auto d = node.getOutputData(port);
    if (!d)
        return {};
    cv::Mat m = OpencvUtil::himageToMat(d->getHImage());
    if (m.channels() == 3)
        cv::cvtColor(m, m, cv::COLOR_BGR2GRAY);
    return m;
}

/// 畸变校正节点的判红原因（与标定族同一通道：判红时端口被 process() 清空，原因只在参数表里）
QString undistortNote(HalconNode &node)
{
    return node.getParam(QStringLiteral("calibNote")).toString();
}

} // namespace

void CalibChainTest::initTestCase()
{
    // 合成帧的 cv::randu 噪声走 OpenCV 线程级 RNG，默认种子随进程变：改坏取证三轮里同一用例的
    // "校正前偏差"在 8.879~8.914 px 之间漂，② 解出的 fx 漂到小数点后第二位（520.28 / 520.72）。
    // 钉住种子，日志里的实测值与门限余量才是可复现的量。
    cv::setRNGSeed(20260927);
    m_managerSnapshot = CalibrationManager::instance()->toJson();
}

void CalibChainTest::cleanupTestCase()
{
    CalibrationManager::instance()->fromJson(m_managerSnapshot);
    QVERIFY2(CalibrationManager::instance()->names() == m_managerSnapshot.keys(),
             "单例未还原：本套件写进的矩阵泄漏给了同进程的后续用例");
}

// ④：已知仿射（缩放 0.05 mm/px + 旋转 20° + 平移）生成 5 对点 ⇒ 节点报回的 6 元矩阵逐项对上真值，
//    并真的进了 CalibrationManager（下游 ⑥ 的前提）。
void CalibChainTest::nPointRecoversKnownAffine()
{
    const double s = 0.05, deg = 20.0;
    const double th = deg * CV_PI / 180.0, c = std::cos(th), sn = std::sin(th);
    const AffineTruth truth{s * c, -s * sn, 12.5, s * sn, s * c, -7.25};

    const QVector<QPointF> pixels = {QPointF(100, 80), QPointF(320, 90), QPointF(410, 260),
                                     QPointF(150, 300), QPointF(250, 180)};
    QVector<QPointF> worlds;
    for (const QPointF &p : pixels)
        worlds << applyTruth(truth, p.x(), p.y());

    NPointCalibNode node;
    node.init();
    node.setParam(QStringLiteral("pointsText"), pairsText(pixels, worlds));
    node.setParam(QStringLiteral("saveName"), QStringLiteral("c2test_npoint"));
    feedImage(node, blankImage());
    QVERIFY2(node.execute(), qPrintable(QStringLiteral("N 点标定应成功，calibNote=") + noteOf(node)));

    const QVector<double> m = matrixFromPort(node, 1);
    QCOMPARE(m.size(), 6);
    const QVector<double> truthVec{truth.m11, truth.m12, truth.m13, truth.m21, truth.m22, truth.m23};
    // 门限依据本轮实测：节点把点对转成 cv::Point2f（float32）再送 estimateAffine2D，
    // 平移项偏差 ~1.4e-6（像素量级 400 × float eps），线性项 ~5e-9 ⇒ 绝对容差取 1e-4。
    const QString dev = vecDeviation(m, truthVec, 1e-4);
    QVERIFY2(dev.isEmpty(),
             qPrintable(QStringLiteral("N 点矩阵偏离真值：") + dev + QStringLiteral("，报回 [") + csvOf(m) + QLatin1Char(']')));
    QVERIFY(node.getParam(QStringLiteral("moduleStatus")).toBool());
    QVERIFY(noteOf(node).isEmpty());

    const QVector<double> stored = CalibrationManager::instance()->homography(QStringLiteral("c2test_npoint"));
    const QString storedDev = vecDeviation(stored, truthVec, 1e-4);
    QVERIFY2(storedDev.isEmpty(),
             qPrintable(QStringLiteral("单例里存的矩阵偏离真值：") + storedDev));

    CalibrationManager::instance()->remove(QStringLiteral("c2test_npoint"));
}

// ⑦：默认参数下没有任何点对 ⇒ "标定点对数不足"必须判红（结论 D 修复前只写端口不判红）。
void CalibChainTest::nPointInsufficientPairsJudgeRed()
{
    struct Case { const char *text; };
    const Case cases[] = {
        {""},                                   // 一个点都没有
        {"100,80 5,4"},                         // 只有 1 对
        {"100,80 5,4\n320,90 16,4.5"},          // 2 对（门槛是 3 对）
        {"# 注释行不参与计数\n100,80 5,4"},      // 门槛是 3 对，注释行被跳过
    };
    QStringList notRed;
    for (const Case &cs : cases) {
        NPointCalibNode node;
        node.init();
        node.setParam(QStringLiteral("pointsText"), QString::fromLatin1(cs.text));
        node.setParam(QStringLiteral("saveName"), QStringLiteral("c2test_npoint_bad"));
        feedImage(node, blankImage());
        if (node.execute() || node.getParam(QStringLiteral("moduleStatus")).toBool())
            notRed << QString::fromLatin1(cs.text);
        if (!noteOf(node).isEmpty() && CalibrationManager::instance()->hasHomography(QStringLiteral("c2test_npoint_bad")))
            notRed << QStringLiteral("%1（失败却写进了矩阵）").arg(QString::fromLatin1(cs.text));
    }
    QVERIFY2(notRed.isEmpty(),
             qPrintable(QStringLiteral("点对不足却判成功：") + notRed.join(QStringLiteral(" | "))));
}

// ⑦：像素侧点对全部重合 ⇒ 仿射无解（estimateAffine2D 退化）。这条是"改坏就照不到"的那条：
//     世界坐标各不相同、像素坐标完全相同，任何非退化解都拟不上。
void CalibChainTest::nPointDegeneratePairsJudgeRed()
{
    const QVector<QPointF> pixels = {QPointF(200, 200), QPointF(200, 200), QPointF(200, 200),
                                     QPointF(200, 200), QPointF(200, 200)};
    const QVector<QPointF> worlds = {QPointF(1, 7), QPointF(9, 2), QPointF(4, 4),
                                     QPointF(12, 11), QPointF(0, 6)};

    NPointCalibNode node;
    node.init();
    node.setParam(QStringLiteral("pointsText"), pairsText(pixels, worlds));
    node.setParam(QStringLiteral("saveName"), QStringLiteral("c2test_npoint_degen"));
    feedImage(node, blankImage());
    const bool ok = node.execute();
    const QVector<double> m = matrixFromPort(node, 1);

    QStringList reasons;
    if (ok)
        reasons << QStringLiteral("execute()=true（退化输入被判成功）");
    if (node.getParam(QStringLiteral("moduleStatus")).toBool())
        reasons << QStringLiteral("moduleStatus=true");
    for (double v : m)
        if (!std::isfinite(v))
            reasons << QStringLiteral("矩阵含非有限值");
    QVERIFY2(reasons.isEmpty(),
             qPrintable(QStringLiteral("退化点对未被判红：")
                        + reasons.join(QStringLiteral("; "))
                        + QStringLiteral("，calibNote=") + noteOf(node)
                        + QStringLiteral("，矩阵=[") + csvOf(m) + QLatin1Char(']')));

    CalibrationManager::instance()->remove(QStringLiteral("c2test_npoint_degen"));
}

// ⑤：手眼是"像素→机器人"的 2D 刚体拟合（旋转+平移）。用已知刚体造点对 ⇒ 6 元矩阵逐项对上真值。
void CalibChainTest::handEyeRecoversKnownRigid()
{
    const double deg = -35.0, th = deg * CV_PI / 180.0;
    const double c = std::cos(th), sn = std::sin(th), tx = 100.25, ty = -50.5;

    const QVector<QPointF> pixels = {QPointF(120, 90), QPointF(330, 110), QPointF(210, 300),
                                     QPointF(420, 260), QPointF(80, 220)};
    QVector<QPointF> robots;
    for (const QPointF &p : pixels)
        robots << QPointF(c * p.x() - sn * p.y() + tx, sn * p.x() + c * p.y() + ty);

    HandEyeCalibNode node;
    node.init();
    node.setParam(QStringLiteral("pointsText"), pairsText(pixels, robots));
    node.setParam(QStringLiteral("saveName"), QStringLiteral("c2test_handeye"));
    feedImage(node, blankImage());
    QVERIFY2(node.execute(), qPrintable(QStringLiteral("手眼标定应成功，calibNote=") + noteOf(node)));

    const QVector<double> m = matrixFromPort(node, 1);
    QCOMPARE(m.size(), 6);
    const QString dev = vecDeviation(m, {c, -sn, tx, sn, c, ty}, 1e-6);
    QVERIFY2(dev.isEmpty(),
             qPrintable(QStringLiteral("刚体矩阵偏离真值：") + dev + QStringLiteral("，报回 [") + csvOf(m) + QLatin1Char(']')));

    CalibrationManager::instance()->remove(QStringLiteral("c2test_handeye"));
}

// ⑤ 口径（§3.2 结论 C）：节点名叫"手眼标定"，实现是**无缩放**的 2D 刚体。
// 拿带 2× 缩放的真值点对喂它：报回的线性部分仍必须是单位范数（刚体），且残差不为零 ——
// 也就是"它学不出缩放"这件事本身要有一条用例钉住，不能只写在文档里。
void CalibChainTest::handEyeIsRigidNotScaled()
{
    const double deg = 15.0, th = deg * CV_PI / 180.0;
    const double c = std::cos(th), sn = std::sin(th), scale = 2.0;

    const QVector<QPointF> pixels = {QPointF(100, 100), QPointF(300, 120), QPointF(200, 320),
                                     QPointF(410, 260)};
    QVector<QPointF> robots;
    for (const QPointF &p : pixels)
        robots << QPointF(scale * (c * p.x() - sn * p.y()), scale * (sn * p.x() + c * p.y()));

    HandEyeCalibNode node;
    node.init();
    node.setParam(QStringLiteral("pointsText"), pairsText(pixels, robots));
    node.setParam(QStringLiteral("saveName"), QStringLiteral("c2test_handeye_scale"));
    feedImage(node, blankImage());
    QVERIFY2(node.execute(), qPrintable(QStringLiteral("刚体拟合本身不该失败，calibNote=") + noteOf(node)));

    const QVector<double> m = matrixFromPort(node, 1);
    QCOMPARE(m.size(), 6);
    const double row1 = std::hypot(m[0], m[3]), row2 = std::hypot(m[1], m[4]);
    QVERIFY2(std::abs(row1 - 1.0) < 1e-6 && std::abs(row2 - 1.0) < 1e-6,
             qPrintable(QStringLiteral("线性部分不再是刚体（范数 %1 / %2）⇒ 本节点已变成带缩放的变换，"
                                       "§3.2 结论 C 的口径需要重写")
                            .arg(row1).arg(row2)));

    double maxResidual = 0.0;
    for (int i = 0; i < pixels.size(); ++i) {
        const QPointF fit(m[0] * pixels[i].x() + m[1] * pixels[i].y() + m[2],
                          m[3] * pixels[i].x() + m[4] * pixels[i].y() + m[5]);
        maxResidual = std::max(maxResidual, std::hypot(fit.x() - robots[i].x(), fit.y() - robots[i].y()));
    }
    QVERIFY2(maxResidual > 1.0,
             qPrintable(QStringLiteral("缩放 2× 的点对被零残差拟合 ⇒ 节点偷偷学会了缩放（%1）").arg(maxResidual)));

    CalibrationManager::instance()->remove(QStringLiteral("c2test_handeye_scale"));
}

// ⑦：手眼门槛是 2 对；0/1 对都必须判红（修复前只写端口）。
void CalibChainTest::handEyeInsufficientPairsJudgeRed()
{
    const char *cases[] = {"", "100,80 5,4", "100,80 5,4\n3,4"};
    QStringList notRed;
    for (const char *text : cases) {
        HandEyeCalibNode node;
        node.init();
        node.setParam(QStringLiteral("pointsText"), QString::fromLatin1(text));
        node.setParam(QStringLiteral("saveName"), QStringLiteral("c2test_handeye_bad"));
        feedImage(node, blankImage());
        const bool ok = node.execute();
        const bool green = ok || node.getParam(QStringLiteral("moduleStatus")).toBool();
        if (green)
            notRed << QStringLiteral("%1（execute=%2）").arg(QString::fromLatin1(text)).arg(ok);
        else if (noteOf(node).isEmpty())
            notRed << QStringLiteral("%1（判红了但 calibNote 为空 ⇒ 原因没留下）").arg(QString::fromLatin1(text));
        if (CalibrationManager::instance()->hasHomography(QStringLiteral("c2test_handeye_bad")))
            notRed << QStringLiteral("%1（失败却写进了矩阵）").arg(QString::fromLatin1(text));
    }
    QVERIFY2(notRed.isEmpty(),
             qPrintable(QStringLiteral("手眼点对不足却判成功：") + notRed.join(QStringLiteral(" | "))));
}

// ①②：按已知 K 投影的 5 帧驱动 OpencvCalibNode（逐帧检测 → 跨轮累积 → 攒够才解算）。
// 断言分三层：过程可见（collectedFrames 逐帧 +1、未到 required 不提前解算）、
// 产出正确（内参端口 9 元 + 进单例 cam_params）、**精度**（fx/fy/cx/cy 对得上真值 K）。
void CalibChainTest::calibNodeRecoversKnownIntrinsics()
{
    const std::vector<cv::Mat> frames = makeCalibFrames();

    OpencvCalibNode node;
    node.init();
    node.setParam(QStringLiteral("patternW"), kPatternW);
    node.setParam(QStringLiteral("patternH"), kPatternH);
    node.setParam(QStringLiteral("squareSize"), kSquare);
    node.setParam(QStringLiteral("requiredFrames"), int(frames.size()));

    QStringList problems;
    for (size_t i = 0; i < frames.size(); ++i) {
        feedImage(node, OpencvUtil::matToHimage(frames[i]));
        const bool ok = node.execute();
        const int collected = node.getParam(QStringLiteral("collectedFrames")).toInt();
        if (collected != int(i) + 1)
            problems << QStringLiteral("第 %1 帧后 collectedFrames=%2").arg(i + 1).arg(collected);
        if (!ok)
            problems << QStringLiteral("第 %1 帧判红（检测或解算失败）").arg(i + 1);
        const bool calibrated = node.getParam(QStringLiteral("calibrated")).toBool();
        if (calibrated != (i + 1 == frames.size()))
            problems << QStringLiteral("第 %1 帧 calibrated=%2（攒够最后一帧才该置位）").arg(i + 1).arg(calibrated);
    }
    QVERIFY2(problems.isEmpty(), qPrintable(QStringLiteral("逐帧累积不符：") + problems.join(QStringLiteral("; "))));

    QVERIFY2(node.getParam(QStringLiteral("calibrated")).toBool(), "最后一帧未标记 calibrated");
    const QVector<double> params = matrixFromPort(node, 2);
    QCOMPARE(params.size(), 9);

    const double fx = params[0], fy = params[1], cx = params[2], cy = params[3];
    const double k1 = params[4], k2 = params[5], p1 = params[6], p2 = params[7], rms = params[8];
    const QString measured = QStringLiteral(
        "fx=%1 fy=%2 cx=%3 cy=%4 k1=%5 k2=%6 p1=%7 p2=%8 rms=%9 px")
        .arg(fx, 0, 'f', 2).arg(fy, 0, 'f', 2).arg(cx, 0, 'f', 1).arg(cy, 0, 'f', 1)
        .arg(k1, 0, 'f', 4).arg(k2, 0, 'f', 4).arg(p1, 0, 'f', 4).arg(p2, 0, 'f', 4)
        .arg(rms, 0, 'f', 4);
    qInfo("%s", qPrintable(QStringLiteral("② 实测内参（真值 fx=%1 fy=%2 cx=%3 cy=%4）：")
                               .arg(kTrueFx).arg(kTrueFy).arg(kTrueCx).arg(kTrueCy)
                               + measured));

    QVERIFY2(std::isfinite(rms) && rms < 0.5,
             qPrintable(QStringLiteral("重投影误差过大：") + measured));
    // 焦距：相对偏差门限（合成帧经 INTER_LINEAR 重采样 + 噪声，角点有亚像素噪声）
    QVERIFY2(std::abs(fx - kTrueFx) / kTrueFx < 0.02 && std::abs(fy - kTrueFy) / kTrueFy < 0.02,
             qPrintable(QStringLiteral("fx/fy 偏离真值超 2%：") + measured));
    // 主点：真值就在图像中心附近，绝对偏差门限（像素）
    QVERIFY2(std::abs(cx - kTrueCx) < 5.0 && std::abs(cy - kTrueCy) < 5.0,
             qPrintable(QStringLiteral("cx/cy 偏离真值超 5px：") + measured));
    // 畸变真值为 0 ⇒ 系数只要求"量级合理"（不拿零当准度门槛，8 系数模型对零畸变数据自由度高）
    QVERIFY2(std::abs(k1) < 0.5 && std::abs(k2) < 2.0 && std::abs(p1) < 0.05 && std::abs(p2) < 0.05,
             qPrintable(QStringLiteral("零畸变数据拟出的畸变量级异常：") + measured));
    QVERIFY(std::abs(node.getParam(QStringLiteral("calibFx")).toDouble() - fx) < 1e-9);
    QVERIFY(std::abs(node.getParam(QStringLiteral("calibFy")).toDouble() - fy) < 1e-9);

    // 状态串可见性：端口 1 必须真把数值填进去（本轮实测到的是它曾写成 printf 的 "%.3f"，
    // QString::arg 认不得 ⇒ 五次替换全落空，操作员只看到占位符）
    auto statusObj = node.getOutputData(1);
    QVERIFY2(statusObj, "标定状态端口无产出");
    const QString status = statusObj->getData().toString();
    QVERIFY2(status.contains(QRegularExpression(QStringLiteral("重投影误差 \\d+\\.\\d{3} px"))),
             qPrintable(QStringLiteral("状态串里没有重投影误差数值：") + status));
    QVERIFY2(!status.contains(QLatin1Char('%')),
             qPrintable(QStringLiteral("状态串里残留未替换的占位符：") + status));

    const QVector<double> stored = CalibrationManager::instance()->homography(QStringLiteral("cam_params"));
    QCOMPARE(stored.size(), 9);
    for (int i = 0; i < 9; ++i)
        QVERIFY(std::abs(stored[i] - params[i]) < 1e-12);

    CalibrationManager::instance()->remove(QStringLiteral("cam_params"));
}

// ① 失败面：画面里没有棋盘格 ⇒ 节点必须显式判红（不写位的话 process() 的预置 true 就是绿灯）。
void CalibChainTest::calibNodeJudgeRedWithoutBoard()
{
    OpencvCalibNode node;
    node.init();
    node.setParam(QStringLiteral("patternW"), kPatternW);
    node.setParam(QStringLiteral("patternH"), kPatternH);
    node.setParam(QStringLiteral("squareSize"), kSquare);
    node.setParam(QStringLiteral("requiredFrames"), 3);

    cv::Mat plain = cv::Mat::zeros(kImgH, kImgW, CV_8UC1);
    feedImage(node, OpencvUtil::matToHimage(plain));
    const bool ok = node.execute();
    QVERIFY2(!ok && !node.getParam(QStringLiteral("moduleStatus")).toBool(),
             "空白帧（无棋盘格）不得判成功");
    QVERIFY2(node.getParam(QStringLiteral("collectedFrames")).toInt() == 0,
             "没检测到棋盘格却累积了帧数");
    QVERIFY2(!node.getParam(QStringLiteral("calibrated")).toBool(), "无棋盘格却标记已标定");
}

// ⑥ 单例本体：命名矩阵的存取 + 方案 JSON 往返 + 6 元作用于点的换算。
// 这里是"存盘复用"与"下游取矩阵"两条路的交点，修复前整段 0 断言（§3.2 结论 B）。
void CalibChainTest::calibrationManagerRoundTripAndApply()
{
    CalibrationManager *cm = CalibrationManager::instance();
    const QString name = QStringLiteral("c2test_mgr");
    cm->remove(name);
    QVERIFY(!cm->hasHomography(name));

    const QVector<double> hom{0.02, -0.01, 5.5, 0.01, 0.02, -3.25};
    cm->setHomography(name, hom);
    QVERIFY(cm->hasHomography(name));
    QVERIFY(vecDeviation(cm->homography(name), hom, 1e-12).isEmpty());
    QVERIFY(cm->names().contains(name));

    const QVector<double> overrideVec{1.0, 0.0, 2.0, 0.0, 1.0, 3.0};
    cm->setHomography(name, overrideVec);                      // 同名覆盖
    QVERIFY(vecDeviation(cm->homography(name), overrideVec, 1e-12).isEmpty());
    cm->setHomography(name, hom);

    cm->setHomography(QString(), hom);                          // 空名必须被丢弃
    QVERIFY(!cm->hasHomography(QString()));

    const QJsonObject dumped = cm->toJson();
    QVERIFY(dumped.contains(name));
    QCOMPARE(dumped.value(name).toArray().size(), 6);

    cm->remove(name);
    QVERIFY(!cm->hasHomography(name));
    cm->fromJson(dumped);                                       // 从方案 JSON 装回来
    QVERIFY(cm->hasHomography(name));
    QVERIFY(vecDeviation(cm->homography(name), hom, 1e-12).isEmpty());

    // 长度不足 6 的条目不该被装回来（fromJson 的口径）
    QJsonObject bad = dumped;
    bad[QStringLiteral("c2test_short")] = QJsonArray{1.0, 2.0, 3.0};
    cm->fromJson(bad);
    QVERIFY(!cm->hasHomography(QStringLiteral("c2test_short")));

    const QPointF q = CalibrationManager::applyHomography(hom, 120.0, 80.0);
    QVERIFY(std::abs(q.x() - (0.02 * 120 - 0.01 * 80 + 5.5)) < 1e-12);
    QVERIFY(std::abs(q.y() - (0.01 * 120 + 0.02 * 80 - 3.25)) < 1e-12);
    // 矩阵不足 6 元时是恒等透传（下游拿不到矩阵时的兜底口径，必须留痕）
    const QPointF id = CalibrationManager::applyHomography(QVector<double>{1.0, 2.0}, 7.0, 9.0);
    QVERIFY(id == QPointF(7.0, 9.0));

    cm->clear();
    QVERIFY(cm->names().isEmpty());
    cm->fromJson(m_managerSnapshot);
}

// ⑥ 消费端：CoordinateTransformNode 真的从单例取矩阵来换算（不是拿面板上的手填值算一遍）。
void CalibChainTest::coordinateTransformConsumesStoredMatrix()
{
    const QString name = QStringLiteral("c2test_consume");
    const QVector<double> hom{0.05, -0.02, 11.0, 0.02, 0.05, -6.0};
    CalibrationManager::instance()->setHomography(name, hom);

    CoordinateTransformNode node;
    node.init();
    node.setParam(QStringLiteral("fixtureName"), name);
    node.setParam(QStringLiteral("x"), 200.0);
    node.setParam(QStringLiteral("y"), 150.0);
    // 故意把手填矩阵留成单位阵：若节点没去取单例里的矩阵，就会算出 (200,150) 而不是真值
    feedImage(node, blankImage(640, 480));
    QVERIFY2(node.execute(), "坐标系换算节点在无场景下不应失败");

    auto d = node.getOutputData(1);
    QVERIFY2(d, "结果端口无产出");
    const QPointF q = d->getPoint();
    const QPointF truth = CalibrationManager::applyHomography(hom, 200.0, 150.0);
    QVERIFY2(std::abs(q.x() - truth.x()) < 1e-9 && std::abs(q.y() - truth.y()) < 1e-9,
             qPrintable(QStringLiteral("换算结果 %1,%2 未取自单例矩阵（应为 %3,%4）")
                            .arg(q.x()).arg(q.y()).arg(truth.x()).arg(truth.y())));

    CalibrationManager::instance()->remove(name);
}

// ⑥ R-2 正向：fixtureName 同名时**场景夹具优先**于标定单例（读侧优先级，修复不得顺手改成反的）。
// 同名键在单例里故意放一份**不同**的矩阵 ⇒ 只有真取到场景那份，结果才对得上。
void CalibChainTest::coordinateTransformPrefersSceneFixture()
{
    const QString name = QStringLiteral("c2test_scene_priority");
    FlowScene scene;
    FlowFixture fx;
    fx.name = name;
    scene.setFixture(fx);
    const QVector<double> sceneHom{0.1, 0.0, 5.0, 0.0, 0.1, 7.0};

    CoordinateTransformNode node;
    node.setFlowSceneRef(&scene);
    node.init();
    node.setParam(QStringLiteral("fixtureName"), name);
    node.setParam(QStringLiteral("x"), 100.0);
    node.setParam(QStringLiteral("y"), 50.0);
    feedImage(node, blankImage(640, 480));

    // 第 1 轮：场景与单例都给不出矩阵 ⇒ 判红，且原因栏要有内容（给下一轮的"必须清干净"提供对照）
    QVERIFY2(!node.execute(), "夹具无矩阵的成功轮不该判绿");
    QVERIFY2(!transformNoteOf(node).isEmpty(), "判红轮却没留下 transformNote");

    // 第 2 轮：场景补上矩阵，同时把单例同名键写成**另一份**矩阵 ⇒ 只有真取到场景那份，结果才对得上
    CalibrationManager::instance()->setHomography(name, QVector<double>{0.2, 0.0, 105.0, 0.0, 0.2, 107.0});
    scene.setFixtureHomography(name, sceneHom);
    QVERIFY2(node.execute(), qPrintable(QStringLiteral("场景夹具带矩阵时不该判红，transformNote=")
                                        + transformNoteOf(node)));

    bool present = false;
    const QPointF q = resultPointOf(node, &present);
    const QPointF truth = CalibrationManager::applyHomography(sceneHom, 100.0, 50.0);
    QVERIFY2(present, "成功轮结果端口却无产出");
    QVERIFY2(std::abs(q.x() - truth.x()) < 1e-9 && std::abs(q.y() - truth.y()) < 1e-9,
             qPrintable(QStringLiteral("换算结果 %1,%2 未取场景夹具矩阵（应为 %3,%4；取到单例那份会是 125,117）")
                            .arg(q.x()).arg(q.y()).arg(truth.x()).arg(truth.y())));
    QVERIFY2(transformNoteOf(node).isEmpty(),
             qPrintable(QStringLiteral("成功轮没有清掉 transformNote：") + transformNoteOf(node)));

    CalibrationManager::instance()->remove(name);
}

// ⑥ R-2 反向闸：fixtureName 为空＝手填矩阵，是本节点文档化的合法用法
// （参数标签写的是「Fixture 名（空=手填矩阵）」）。补判红时不得把这条通路一起判红，
// 也不得让手填值被默认恒等顶掉——这里用手填的缩放+平移，输出必须与输入不同。
void CalibChainTest::coordinateTransformManualMatrixStaysGreen()
{
    CoordinateTransformNode node;
    node.init();
    node.setParam(QStringLiteral("fixtureName"), QString());
    node.setParam(QStringLiteral("x"), 200.0);
    node.setParam(QStringLiteral("y"), 150.0);
    node.setParam(QStringLiteral("m11"), 0.1);
    node.setParam(QStringLiteral("m13"), 5.0);
    node.setParam(QStringLiteral("m22"), 0.2);
    node.setParam(QStringLiteral("m23"), 7.0);
    feedImage(node, blankImage(640, 480));
    QVERIFY2(node.execute(), qPrintable(QStringLiteral("手填矩阵通路被判红：") + transformNoteOf(node)));

    bool present = false;
    const QPointF q = resultPointOf(node, &present);
    QVERIFY2(present, "成功轮结果端口却无产出");
    QVERIFY2(std::abs(q.x() - 25.0) < 1e-9 && std::abs(q.y() - 37.0) < 1e-9,
             qPrintable(QStringLiteral("手填矩阵没生效（得 %1,%2，应为 25,37），恒等回退也覆盖了手填值")
                            .arg(q.x()).arg(q.y())));
}

// ⑥ R-2 判红①：声明了夹具、场景与单例都给不出矩阵 ⇒ 不得静默沿用默认值。
// 修复前实测为"绿灯 + 输出等于输入像素原值 (200,150)"：默认 m11=m22=1、m12=m13=m21=m23=0
// 正好是**恒等矩阵**（见 CoordinateTransformNode::init），于是像素坐标被当成物理坐标吐给下游。
void CalibChainTest::coordinateTransformJudgeRedWithoutMatrix()
{
    const QString name = QStringLiteral("c2test_no_such_fixture");
    CalibrationManager::instance()->remove(name);

    CoordinateTransformNode node;
    node.init();
    node.setParam(QStringLiteral("fixtureName"), name);
    node.setParam(QStringLiteral("x"), 200.0);
    node.setParam(QStringLiteral("y"), 150.0);
    // 故意把手填矩阵留成默认单位阵：判红前它会伪装成"算对了"
    feedImage(node, blankImage(640, 480));
    const bool ok = node.execute();
    const QString note = transformNoteOf(node);
    bool present = true;
    const QPointF q = resultPointOf(node, &present);

    QStringList problems;
    if (ok || node.getParam(QStringLiteral("moduleStatus")).toBool())
        problems << QStringLiteral("判绿（execute=%1，输出 %2,%3），静默恒等仍在").arg(ok).arg(q.x()).arg(q.y());
    if (present)
        problems << QStringLiteral("判红了却还有结果端口产出：%1,%2").arg(q.x()).arg(q.y());
    if (!note.contains(name))
        problems << QStringLiteral("原因没点出夹具名：") + note;
    if (!note.contains(QStringLiteral("节点未挂到场景")))
        problems << QStringLiteral("原因没说明场景侧：") + note;
    if (!note.contains(QStringLiteral("标定单例里没有这个键")))
        problems << QStringLiteral("原因没说明单例侧：") + note;
    if (note.contains(QStringLiteral("项")))
        problems << QStringLiteral("键不存在却报了项数，两种原因不可分辨：") + note;
    CalibrationManager::instance()->remove(name);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral(" | "))));
}

// ⑥ R-2 判红②：单例里有这个键但项数不足 6 ⇒ 判红，且原因必须带**实际项数**并与"键不存在"分辨得开
void CalibChainTest::coordinateTransformJudgeRedOnShortMatrix()
{
    const QString name = QStringLiteral("c2test_short_matrix");
    CalibrationManager::instance()->setHomography(name, QVector<double>{1.0, 2.0});

    CoordinateTransformNode node;
    node.init();
    node.setParam(QStringLiteral("fixtureName"), name);
    node.setParam(QStringLiteral("x"), 200.0);
    node.setParam(QStringLiteral("y"), 150.0);
    feedImage(node, blankImage(640, 480));
    const bool ok = node.execute();
    const QString note = transformNoteOf(node);

    QStringList problems;
    if (ok || node.getParam(QStringLiteral("moduleStatus")).toBool())
        problems << QStringLiteral("2 项矩阵被判绿（execute=%1）").arg(ok);
    if (!note.contains(QStringLiteral("2 项")))
        problems << QStringLiteral("原因没带实际项数：") + note;
    if (note.contains(QStringLiteral("没有这个键")))
        problems << QStringLiteral("键确实存在却报「没有这个键」，与另一种原因混了：") + note;
    CalibrationManager::instance()->remove(name);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral(" | "))));
}

// ⑥ R-2 判红③：夹具**在场景里**（只有位姿、没有矩阵）且单例也没有 ⇒ 判红，
// 且原因要说清"夹具找到了、缺的是矩阵"，与"场景里根本没这个夹具"分辨得开。
// 这条同时钉住一个口径变更：以往"取夹具位姿 + 手填矩阵"的混用会静默按恒等跑，现在判红。
void CalibChainTest::coordinateTransformJudgeRedWhenFixtureHasNoMatrix()
{
    const QString name = QStringLiteral("c2test_pose_only_fixture");
    CalibrationManager::instance()->remove(name);

    FlowScene scene;
    FlowFixture fx;
    fx.name = name;
    scene.setFixture(fx);
    scene.setFixturePose(name, 12.0, 34.0, 0.0, 1.0);

    CoordinateTransformNode node;
    node.setFlowSceneRef(&scene);
    node.init();
    node.setParam(QStringLiteral("fixtureName"), name);
    node.setParam(QStringLiteral("x"), 200.0);
    node.setParam(QStringLiteral("y"), 150.0);
    feedImage(node, blankImage(640, 480));
    const bool ok = node.execute();
    const QString note = transformNoteOf(node);

    QStringList problems;
    if (ok || node.getParam(QStringLiteral("moduleStatus")).toBool())
        problems << QStringLiteral("夹具只有位姿没矩阵时被判绿（execute=%1）").arg(ok);
    if (!note.contains(QStringLiteral("该夹具没有矩阵")))
        problems << QStringLiteral("原因没说清「夹具找到了但缺矩阵」：") + note;
    if (note.contains(QStringLiteral("场景里没有该夹具")))
        problems << QStringLiteral("场景里确有该夹具，却报成「没有该夹具」：") + note;
    CalibrationManager::instance()->remove(name);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral(" | "))));
}

// ==================== ③ 畸变校正（内参消费端）====================
// 门限来源：本轮实测值（见推进计划 §3.4 的 ③ 实测表），不是沿用旧口径的估值。
// ③ 组实测底噪（种子已按 initTestCase 钉死，跨进程复跑两遍读数逐位一致）：
// 校正前 10.416 px → 校正后手填通路 0.795 px / 单例通路 0.791 px；端到端 8.876 → 1.270 px。
// 底噪来源是合成帧本身（4× 超采样渲染的量化阶 + INTER_AREA 缩放 + 噪声下的角点检测），不是节点的几何误差。
static const double kUndistortTolPx = 1.0;    // 手填/单例两条正向通路：校正后角点回真值的容差（实测 0.795／0.791）
static const double kChainTolPx = 2.0;        // ①②→③ 端到端：畸变系数是**解出来的**（实测 1.270 px），容差放宽
static const double kMinDistortionPx = 8.0;   // 反恒等闸：合成帧的畸变必须有量级，否则"回真值"可靠什么都不做实现

namespace {

/// 喂给 ③ 组的一条正向通路：手填真值 K + 真值 D
void setManualIntrinsics(OpencvUndistortNode &node)
{
    node.setParam(QStringLiteral("intrinsicSource"), 1);
    node.setParam(QStringLiteral("fx"), kTrueFx);
    node.setParam(QStringLiteral("fy"), kTrueFy);
    node.setParam(QStringLiteral("cx"), kTrueCx);
    node.setParam(QStringLiteral("cy"), kTrueCy);
    node.setParam(QStringLiteral("k1"), kDistK1);
    node.setParam(QStringLiteral("k2"), kDistK2);
    node.setParam(QStringLiteral("p1"), kDistP1);
    node.setParam(QStringLiteral("p2"), kDistP2);
}

/// 实测角点组对真值组的偏差；检出数不符时直接判红（不能拿"检到了另一堆点"当校正成功）
QString cornerDeviation(const cv::Mat &img, const std::vector<cv::Point2f> &truth, double *dev)
{
    const std::vector<cv::Point2f> got = detectInnerCorners(img);
    if (int(got.size()) != kPatternW * kPatternH)
        return QStringLiteral("检出内角点 %1 个，应为 %2 个").arg(got.size()).arg(kPatternW * kPatternH);
    *dev = setDeviation(got, truth);
    if (*dev < 0.0)
        return QStringLiteral("角点集与真值无法配对（dev=%1）").arg(*dev);
    return QString();
}

} // namespace

// ③ 正向（手填内参）：已知 K+D 渲染的畸变帧 ⇒ 校正后实测角点回到"零畸变投影"的真值。
// 同一条检测通路在校正前后各量一次（只有"输入/输出"这一维不同 ⇒ 控制变量），
// 用它钉住"这不是恒等"：校正前的偏差必须有 kMinDistortionPx 的量级。
void CalibChainTest::undistortRecoversKnownDistortion()
{
    const cv::Mat frame = renderDistortedView(kDistPose, kDistDepth, trueDistortion());
    const std::vector<cv::Point2f> truth = projectInnerCorners(kDistPose, kDistDepth, cv::Mat());

    double inDev = -1.0;
    const QString inProblem = cornerDeviation(frame, truth, &inDev);
    QVERIFY2(inProblem.isEmpty(), qPrintable(QStringLiteral("③ 合成帧自身不合格：") + inProblem));

    OpencvUndistortNode node;
    node.init();
    setManualIntrinsics(node);
    feedImage(node, OpencvUtil::matToHimage(frame));
    QVERIFY2(node.execute(), qPrintable(QStringLiteral("③ 手填真值内参仍判红：") + undistortNote(node)));
    QVERIFY(node.getParam(QStringLiteral("moduleStatus")).toBool());
    QVERIFY2(undistortNote(node).isEmpty(),
             qPrintable(QStringLiteral("成功轮没有清掉 calibNote：") + undistortNote(node)));

    const cv::Mat out = grayFromOutputPort(node, 0);
    QVERIFY2(!out.empty(), "③ 端口 0 无图像产出");
    double outDev = -1.0;
    const QString outProblem = cornerDeviation(out, truth, &outDev);
    QVERIFY2(outProblem.isEmpty(), qPrintable(QStringLiteral("③ 校正后角点检出异常：") + outProblem));

    qInfo("%s", qPrintable(QStringLiteral("③ 手填通路实测：校正前 %1 px / 校正后 %2 px / 门限 %3 px / 反恒等门限 %4 px")
                               .arg(inDev, 0, 'f', 3).arg(outDev, 0, 'f', 3)
                               .arg(kUndistortTolPx, 0, 'f', 2).arg(kMinDistortionPx, 0, 'f', 1)));
    QVERIFY2(outDev < kUndistortTolPx,
             qPrintable(QStringLiteral("校正后角点未回真值：after=%1 px（门限 %2）").arg(outDev).arg(kUndistortTolPx)));
    QVERIFY2(inDev >= kMinDistortionPx,
             qPrintable(QStringLiteral("合成帧畸变量级不足（before=%1 px），反恒等闸失去意义").arg(inDev)));
    QVERIFY2(inDev > 4.0 * kUndistortTolPx,
             qPrintable(QStringLiteral("校正前后偏差没有拉开到 4 倍门限以上：这条绿灯不证明校正做过功")));

    // 状态串必须真带数值（N-1 的教训：占位符没替换照样是绿灯）
    auto st = node.getOutputData(1);
    QVERIFY2(st, "③ 校正结果端口无产出");
    const QString text = st->getData().toString();
    QVERIFY2(text.contains(QRegularExpression(QStringLiteral("fx=\\d+\\.\\d+"))),
             qPrintable(QStringLiteral("状态串里没有 fx 数值：") + text));
    QVERIFY2(text.contains(QStringLiteral("fx=520.00")),
             qPrintable(QStringLiteral("状态串里的 fx 不是所读内参：") + text));
    QVERIFY2(!text.contains(QLatin1Char('%')),
             qPrintable(QStringLiteral("状态串里残留未替换的占位符：") + text));
}

// ③ 读侧：默认参数（来源=标定单例、键=cam_params）必须真把 OpencvCalibNode.cpp 写下的
// 9 元组按 fx fy cx cy k1 k2 p1 p2 的**下标顺序**读走 —— 这条就是结论 A 的"断链"本身。
void CalibChainTest::undistortConsumesStoredCamParams()
{
    CalibrationManager::instance()->remove(QStringLiteral("cam_params"));
    const QVector<double> tuple = {kTrueFx, kTrueFy, kTrueCx, kTrueCy,
                                  kDistK1, kDistK2, kDistP1, kDistP2, 0.11};
    CalibrationManager::instance()->setHomography(QStringLiteral("cam_params"), tuple);

    const cv::Mat frame = renderDistortedView(kDistPose, kDistDepth, trueDistortion());
    const std::vector<cv::Point2f> truth = projectInnerCorners(kDistPose, kDistDepth, cv::Mat());

    OpencvUndistortNode node;
    node.init();   // 全默认：不碰任何内参数字，只靠单例
    feedImage(node, OpencvUtil::matToHimage(frame));
    QVERIFY2(node.execute(), qPrintable(QStringLiteral("③ 单例里有 9 元组仍判红：") + undistortNote(node)));

    double outDev = -1.0;
    const QString problem = cornerDeviation(grayFromOutputPort(node, 0), truth, &outDev);
    QVERIFY2(problem.isEmpty(), qPrintable(QStringLiteral("③ 单例通路角点检出异常：") + problem));
    qInfo("%s", qPrintable(QStringLiteral("③ 单例通路实测：校正后 %1 px（门限 %2 px）")
                               .arg(outDev, 0, 'f', 3).arg(kUndistortTolPx, 0, 'f', 2)));
    QVERIFY2(outDev < kUndistortTolPx,
             qPrintable(QStringLiteral("按下标读走的 9 元组没把角点拉回真值：after=%1 px").arg(outDev)));

    // 状态串回显的 fx/fy/cx/cy 必须等于元组的第 0/1/2/3 项 ⇒ 下标映射不是"凑巧能用"
    auto st = node.getOutputData(1);
    QVERIFY2(st, "③ 校正结果端口无产出");
    const QString text = st->getData().toString();
    QStringList must;
    must << QStringLiteral("fx=520.00") << QStringLiteral("fy=518.00")
         << QStringLiteral("cx=320.0") << QStringLiteral("cy=240.0")
         << QStringLiteral("k1=-0.4500") << QStringLiteral("p2=-0.0010");
    QStringList missing;
    for (const QString &m : must)
        if (!text.contains(m))
            missing << m;
    QVERIFY2(missing.isEmpty(),
             qPrintable(QStringLiteral("状态串缺少元组对应项：") + missing.join(QStringLiteral(", "))
                        + QStringLiteral("；实际：") + text));
    QVERIFY2(text.contains(QStringLiteral("cam_params")),
             qPrintable(QStringLiteral("状态串没交代内参来自哪个标定键：") + text));

    CalibrationManager::instance()->remove(QStringLiteral("cam_params"));
}

// ①②→③ 端到端：在**畸变帧**上跑标定（① 检角点 → ② calibrateCamera 写 cam_params），
// 再用另一帧畸变图让 ③ 以全默认参数消费 —— 写侧与读侧之间没有任何测试脚手架搭桥。
// 口径注：这里比对的是"净收益"（校正后必须比校正前更接近真值），因为 D 是解出来的、
// 不等于真值；真机准度属 AC 项，不在本条口径内。
void CalibChainTest::undistortChainFromCalibration()
{
    CalibrationManager::instance()->remove(QStringLiteral("cam_params"));

    const cv::Vec3d poses[] = {
        cv::Vec3d(0.12, 0.18, 0.05),
        cv::Vec3d(-0.16, 0.20, -0.04),
        cv::Vec3d(0.20, -0.14, 0.03),
        cv::Vec3d(-0.10, -0.22, -0.05),
        cv::Vec3d(0.05, 0.10, 0.0),
    };
    const double depths[] = {400.0, 440.0, 380.0, 460.0, 420.0};
    const int kFrameCount = int(sizeof(poses) / sizeof(poses[0]));

    OpencvCalibNode calib;
    calib.init();
    calib.setParam(QStringLiteral("patternW"), kPatternW);
    calib.setParam(QStringLiteral("patternH"), kPatternH);
    calib.setParam(QStringLiteral("squareSize"), kSquare);
    calib.setParam(QStringLiteral("requiredFrames"), kFrameCount);

    QStringList problems;
    for (int i = 0; i < kFrameCount; ++i) {
        const cv::Mat frame = renderDistortedView(poses[i], depths[i], trueDistortion());
        feedImage(calib, OpencvUtil::matToHimage(frame));
        if (!calib.execute())
            problems << QStringLiteral("第 %1 帧判红（%2）").arg(i + 1)
                            .arg(calib.getOutputData(1) ? calib.getOutputData(1)->getData().toString()
                                                        : QStringLiteral("状态端口已被清空"));
    }
    QVERIFY2(problems.isEmpty(), qPrintable(QStringLiteral("① ② 在畸变帧上跑不通：")
                                            + problems.join(QStringLiteral("; "))));
    QVERIFY2(calib.getParam(QStringLiteral("calibrated")).toBool(), "畸变帧上未解算出标定结果");

    const QVector<double> stored = CalibrationManager::instance()->homography(QStringLiteral("cam_params"));
    QCOMPARE(stored.size(), 9);
    const QString recovered = QStringLiteral(
        "fx=%1 fy=%2 cx=%3 cy=%4 k1=%5 k2=%6 p1=%7 p2=%8 rms=%9 px")
        .arg(stored[0], 0, 'f', 2).arg(stored[1], 0, 'f', 2)
        .arg(stored[2], 0, 'f', 1).arg(stored[3], 0, 'f', 1)
        .arg(stored[4], 0, 'f', 4).arg(stored[5], 0, 'f', 4)
        .arg(stored[6], 0, 'f', 4).arg(stored[7], 0, 'f', 4)
        .arg(stored[8], 0, 'f', 4);
    qInfo("%s", qPrintable(QStringLiteral("①②→③ 解出的 9 元组（真值 fx=%1 fy=%2 cx=%3 cy=%4 k1=%5 k2=%6 p1=%7 p2=%8）：")
                               .arg(kTrueFx).arg(kTrueFy).arg(kTrueCx).arg(kTrueCy)
                               .arg(kDistK1).arg(kDistK2).arg(kDistP1).arg(kDistP2)
                               + recovered));

    // 留出**没有参与标定**的一帧做 ③ 的输入
    const cv::Vec3d holdPose(0.14, -0.19, 0.02);
    const double holdDepth = 430.0;
    const cv::Mat held = renderDistortedView(holdPose, holdDepth, trueDistortion());
    const std::vector<cv::Point2f> truth = projectInnerCorners(holdPose, holdDepth, cv::Mat());

    double inDev = -1.0;
    QString problem = cornerDeviation(held, truth, &inDev);
    QVERIFY2(problem.isEmpty(), qPrintable(QStringLiteral("留出帧检不到角点：") + problem));

    OpencvUndistortNode node;
    node.init();   // 全默认：来源=单例、键=cam_params，值由 ② 刚刚写下
    feedImage(node, OpencvUtil::matToHimage(held));
    QVERIFY2(node.execute(), qPrintable(QStringLiteral("② 写下的 cam_params 被 ③ 判红：")
                                        + undistortNote(node)));

    double outDev = -1.0;
    problem = cornerDeviation(grayFromOutputPort(node, 0), truth, &outDev);
    QVERIFY2(problem.isEmpty(), qPrintable(QStringLiteral("③ 端到端角点检出异常：") + problem));
    qInfo("%s", qPrintable(QStringLiteral("①②→③ 端到端实测：校正前 %1 px / 校正后 %2 px / 门限 %3 px")
                               .arg(inDev, 0, 'f', 3).arg(outDev, 0, 'f', 3)
                               .arg(kChainTolPx, 0, 'f', 2)));

    QVERIFY2(inDev >= kMinDistortionPx,
             qPrintable(QStringLiteral("留出帧畸变量级不足（before=%1 px）").arg(inDev)));
    QVERIFY2(outDev < inDev,
             qPrintable(QStringLiteral("用解出的内参校正后反而更远：after=%1 before=%2").arg(outDev).arg(inDev)));
    QVERIFY2(outDev < kChainTolPx,
             qPrintable(QStringLiteral("端到端校正后未回真值附近：after=%1 px（门限 %2）").arg(outDev).arg(kChainTolPx)));

    // 状态串必须回显 ② 解出的那份内参：stored[0]/stored[4] 既不等于手填真值也不等于参数默认 0，
    // 所以"串写死成常量"和"回显了参数面板"两种形态都会被这两条逮住。
    auto st = node.getOutputData(1);
    QVERIFY2(st, "③ 端到端校正结果端口无产出");
    const QString text = st->getData().toString();
    QVERIFY2(text.contains(QStringLiteral("fx=%1").arg(stored[0], 0, 'f', 2)),
             qPrintable(QStringLiteral("状态串的 fx 不是 ② 解出的值：") + text
                        + QStringLiteral("（期望含 fx=%1）").arg(stored[0], 0, 'f', 2)));
    QVERIFY2(text.contains(QStringLiteral("k1=%1").arg(stored[4], 0, 'f', 4)),
             qPrintable(QStringLiteral("状态串的 k1 不是 ② 解出的值：") + text
                        + QStringLiteral("（期望含 k1=%1）").arg(stored[4], 0, 'f', 4)));

    CalibrationManager::instance()->remove(QStringLiteral("cam_params"));
}

// ③ 恒等闸：畸变系数全零 ⇒ 映射就是恒等，输出必须与输入**逐像素相同**。
// 钉的是"校正通路不夹带自己的偏置"（例如 newCameraMatrix 写成缩放后的 K、或 map 方向反置——
// 这两种在 D=0 时都会让图偏移/缩放，而在 D≠0 的正向用例里可能被"看起来更直"掩盖）。
void CalibChainTest::undistortZeroDistortionKeepsImage()
{
    const cv::Mat frame = renderDistortedView(kDistPose, kDistDepth, trueDistortion());

    OpencvUndistortNode node;
    node.init();
    node.setParam(QStringLiteral("intrinsicSource"), 1);
    node.setParam(QStringLiteral("fx"), kTrueFx);
    node.setParam(QStringLiteral("fy"), kTrueFy);
    node.setParam(QStringLiteral("cx"), kTrueCx);
    node.setParam(QStringLiteral("cy"), kTrueCy);
    // k1..p2 保持注册的默认值 0 ⇒ 恒等
    feedImage(node, OpencvUtil::matToHimage(frame));
    QVERIFY2(node.execute(), qPrintable(QStringLiteral("③ D=0 判红：") + undistortNote(node)));

    const cv::Mat out = grayFromOutputPort(node, 0);
    QVERIFY2(!out.empty(), "③ 端口 0 无图像产出");
    QCOMPARE(out.size(), frame.size());
    QCOMPARE(out.type(), frame.type());

    cv::Mat diff;
    cv::absdiff(out, frame, diff);
    const int changed = cv::countNonZero(diff);
    double maxAbs = 0.0;
    cv::minMaxLoc(diff, nullptr, &maxAbs);
    qInfo("%s", qPrintable(QStringLiteral("③ D=0 恒等实测：不同像素 %1 个 / 最大灰度差 %2")
                               .arg(changed).arg(maxAbs, 0, 'f', 3)));
    QVERIFY2(changed == 0,
             qPrintable(QStringLiteral("D=0 时输出与输入不逐像素相同（%1 个像素被改动）").arg(changed)));
}

// ③ 判红①：单例里没有这个键 ⇒ 必须显式失败，并把键名写进原因（现场只能靠这一行分辨"没标定"
// 与"标定键名填错"）。
void CalibChainTest::undistortJudgeRedWithoutCamParams()
{
    CalibrationManager::instance()->remove(QStringLiteral("cam_params"));

    OpencvUndistortNode node;
    node.init();
    feedImage(node, blankImage());
    QVERIFY2(!node.execute(), "单例没有 cam_params 仍判绿（③ 静默绿灯回归）");
    QVERIFY(!node.getParam(QStringLiteral("moduleStatus")).toBool());
    const QString note = undistortNote(node);
    QVERIFY2(note.contains(QStringLiteral("cam_params")),
             qPrintable(QStringLiteral("判红原因里没有标定键名：") + note));
    QVERIFY2(!node.getOutputData(0) && !node.getOutputData(1),
             "判红后输出端口没有被清空（P0-1 的另一半）");
}

// ③ 判红②：同一个键里存的是 6 元单应矩阵（CalibrationManager 的另一种载荷）⇒ 项数不足必须判红，
// 不能"取前 4 项当 K、剩下的当 0"。
void CalibChainTest::undistortJudgeRedOnShortTuple()
{
    const QString key = QStringLiteral("cam_params");
    CalibrationManager::instance()->setHomography(key, {0.05, -0.02, 11.0, 0.02, 0.05, -6.0});

    OpencvUndistortNode node;
    node.init();
    feedImage(node, blankImage());
    QVERIFY2(!node.execute(), "6 元单应被当成内参仍判绿");
    const QString note = undistortNote(node);
    QVERIFY2(note.contains(QStringLiteral("6")),
             qPrintable(QStringLiteral("判红原因没有报出实际项数：") + note));

    CalibrationManager::instance()->remove(key);
}

// ③ 判红③：9 元组里第 5 项（k1）是 NaN ⇒ 含非有限值的标定记录不可用，必须判红并指出下标。
void CalibChainTest::undistortJudgeRedOnNonFiniteTuple()
{
    const QString key = QStringLiteral("cam_params");
    CalibrationManager::instance()->setHomography(
        key, {kTrueFx, kTrueFy, kTrueCx, kTrueCy,
              std::numeric_limits<double>::quiet_NaN(), kDistK2, kDistP1, kDistP2, 0.1});

    OpencvUndistortNode node;
    node.init();
    feedImage(node, blankImage());
    QVERIFY2(!node.execute(), "含 NaN 的内参仍判绿");
    const QString note = undistortNote(node);
    QVERIFY2(note.contains(QStringLiteral("第 4 项")),
             qPrintable(QStringLiteral("判红原因没有指出是哪一项非有限：") + note));

    CalibrationManager::instance()->remove(key);
}

// ③ 判红④（R-2 形态）：标定键不存在时**不得**回退到手填参数——手填值此刻完全合法、
// 一路走下来谁也看不出用的是哪份内参。这条把"静默换数据源"钉死。
void CalibChainTest::undistortNeverFallsBackToManual()
{
    CalibrationManager::instance()->remove(QStringLiteral("cam_params"));

    OpencvUndistortNode node;
    node.init();
    node.setParam(QStringLiteral("calibKey"), QStringLiteral("no_such_calib_key"));
    setManualIntrinsics(node);   // 手填一份完全可用的真值内参
    node.setParam(QStringLiteral("intrinsicSource"), 0);   // 但仍声明"从单例读"
    feedImage(node, blankImage());

    QVERIFY2(!node.execute(), "标定键缺失时回退到了手填参数（静默换数据源）");
    const QString note = undistortNote(node);
    QVERIFY2(note.contains(QStringLiteral("no_such_calib_key")),
             qPrintable(QStringLiteral("判红原因没有报出缺失的键名：") + note));
}

// ③ 判红⑤：手填通路里 fx/fy 还是注册的默认值 0 ⇒ 这不是"能用但差一点"的内参，必须判红。
void CalibChainTest::undistortJudgeRedOnInvalidManualFx()
{
    OpencvUndistortNode node;
    node.init();
    node.setParam(QStringLiteral("intrinsicSource"), 1);
    node.setParam(QStringLiteral("cx"), kTrueCx);
    node.setParam(QStringLiteral("cy"), kTrueCy);
    // fx/fy 留默认 0
    feedImage(node, blankImage());
    QVERIFY2(!node.execute(), "手填 fx=fy=0 仍判绿");
    const QString note = undistortNote(node);
    QVERIFY2(note.contains(QStringLiteral("fx")),
             qPrintable(QStringLiteral("判红原因没点出 fx/fy：") + note));
}

QTEST_MAIN(CalibChainTest)
#include "calib_chain_test.moc"
