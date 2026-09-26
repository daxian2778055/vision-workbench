// C2 标定链路节点级用例（推进计划 §3.2 的"各环节用例"那一半）
//
// 覆盖 §3.2 表里被判定"有实现、无用例"的环节，全部走 execute()（= HalconNode::process()），
// 因此受 P0-1 契约约束：每轮预置 moduleStatus=true，失败必须由节点自己写 false。
//   ① 标定板角点检出 + ② 内参求解：OpencvCalibNode 逐帧累积 → calibrateCamera，
//      合成帧按**已知内参 K + 位姿**投影生成 ⇒ fx/fy/cx/cy 有真值可对照（不再只判"像个相机"）
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
#include "NPointCalibNode.h"
#include "HandEyeCalibNode.h"
#include "CoordinateTransformNode.h"
#include "CalibrationManager.h"
#include "DataObject.h"
#include "OpencvUtil.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>
#include <HalconCpp.h>

#include <cmath>
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
    void calibrationManagerRoundTripAndApply(); // ⑥
    void coordinateTransformConsumesStoredMatrix(); // ⑥

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

} // namespace

void CalibChainTest::initTestCase()
{
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

QTEST_MAIN(CalibChainTest)
#include "calib_chain_test.moc"
