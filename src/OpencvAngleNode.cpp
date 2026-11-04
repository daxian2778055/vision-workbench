#include "OpencvAngleNode.h"
#include "DataObject.h"
#include <algorithm>
#include <cmath>
#include <QWidget>

namespace {
constexpr double kPi = 3.14159265358979323846;

/// 图像坐标下的二维叉积：a.col*b.row − a.row*b.col
double cross2D(double ac, double ar, double bc, double br)
{
    return ac * br - ar * bc;
}

/// 弧度 → 度
double toDeg(double rad)
{
    return rad * 180.0 / kPi;
}
}   // namespace

OpencvAngleNode::OpencvAngleNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("角度测量"));
    m_type = MEASUREMENT;
}

void OpencvAngleNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("夹角"), PortDataType::Measure);
    addOutputPort(QStringLiteral("夹角数值"), PortDataType::Number);
    registerParams({
        makeDoubleParam(QStringLiteral("r1a"), 100.0, 0.0, 100000.0, QStringLiteral("线段1起点行")),
        makeDoubleParam(QStringLiteral("c1a"), 100.0, 0.0, 100000.0, QStringLiteral("线段1起点列")),
        makeDoubleParam(QStringLiteral("r1b"), 200.0, 0.0, 100000.0, QStringLiteral("线段1终点行")),
        makeDoubleParam(QStringLiteral("c1b"), 100.0, 0.0, 100000.0, QStringLiteral("线段1终点列")),
        makeDoubleParam(QStringLiteral("r2a"), 100.0, 0.0, 100000.0, QStringLiteral("线段2起点行")),
        makeDoubleParam(QStringLiteral("c2a"), 100.0, 0.0, 100000.0, QStringLiteral("线段2起点列")),
        makeDoubleParam(QStringLiteral("r2b"), 200.0, 0.0, 100000.0, QStringLiteral("线段2终点行")),
        makeDoubleParam(QStringLiteral("c2b"), 200.0, 0.0, 100000.0, QStringLiteral("线段2终点列")),
        makeEnumParam(QStringLiteral("angleRange"), 0,
                      {QStringLiteral("0~180°（无向夹角）"),
                       QStringLiteral("0~360°（有向夹角）")},
                      QStringLiteral("角度范围")),
        makeBoolParam(QStringLiteral("showAngleLines"), true,
                      QStringLiteral("显示两条测量线段")),
    });
    setParamDirect(QStringLiteral("angle"), 0.0);
    setParamDirect(QStringLiteral("signedAngle"), 0.0);
    setParamDirect(QStringLiteral("angle360"), 0.0);
    setParamDirect(QStringLiteral("line1Length"), 0.0);
    setParamDirect(QStringLiteral("line2Length"), 0.0);
    setParamDirect(QStringLiteral("line1AngleDeg"), 0.0);
    setParamDirect(QStringLiteral("line2AngleDeg"), 0.0);
    setParamDirect(QStringLiteral("intersectionRow"), 0.0);
    setParamDirect(QStringLiteral("intersectionCol"), 0.0);
    setParamDirect(QStringLiteral("parallel"), false);
}

void OpencvAngleNode::run(bool /*autoSwitch*/)
{
    const auto pv = [this](const QString &n, double d) { return m_params.value(n, d).toDouble(); };
    const double r1a = pv(QStringLiteral("r1a"), 100), c1a = pv(QStringLiteral("c1a"), 100);
    const double r1b = pv(QStringLiteral("r1b"), 200), c1b = pv(QStringLiteral("c1b"), 100);
    const double r2a = pv(QStringLiteral("r2a"), 100), c2a = pv(QStringLiteral("c2a"), 100);
    const double r2b = pv(QStringLiteral("r2b"), 200), c2b = pv(QStringLiteral("c2b"), 200);
    const int angleRange = m_params.value(QStringLiteral("angleRange"), 0).toInt();

    // 两条线段的方向向量（行、列）
    const double v1r = r1b - r1a, v1c = c1b - c1a;
    const double v2r = r2b - r2a, v2c = c2b - c2a;
    const double len1 = std::hypot(v1r, v1c);
    const double len2 = std::hypot(v2r, v2c);

    const bool valid = len1 > 1e-9 && len2 > 1e-9;
    double deg = 0.0;        // 无向夹角 [0,180]
    double signedDeg = 0.0;  // 有向夹角 [-180,180]
    double deg360 = 0.0;     // 有向夹角 [0,360)
    double line1Deg = 0.0, line2Deg = 0.0;
    double interRow = 0.0, interCol = 0.0;
    bool parallel = false;

    if (valid) {
        const double dot = v1r * v2r + v1c * v2c;
        double cosA = dot / (len1 * len2);
        cosA = qBound(-1.0, cosA, 1.0);   // 数值安全（不用 std::min/max：Windows.h 的宏会吃掉这两个名字）
        deg = toDeg(std::acos(cosA));                 // 两条（无向）线段夹角 ∈ [0,180]

        // 有向夹角：从线段1 旋到线段2（图像坐标，行向下）
        const double cross = cross2D(v1c, v1r, v2c, v2r);
        signedDeg = toDeg(std::atan2(cross, dot));
        deg360 = std::fmod(signedDeg + 360.0, 360.0);
        if (deg360 < 0.0)
            deg360 += 360.0;

        // 各线段的方向角（相对列轴，图像坐标）
        line1Deg = toDeg(std::atan2(v1r, v1c));
        line2Deg = toDeg(std::atan2(v2r, v2c));

        // 两条无限延长线的交点（平行时无意义）
        const double denom = cross2D(v1c, v1r, v2c, v2r);
        if (std::fabs(denom) > 1e-9) {
            const double dc = c2a - c1a;
            const double dr = r2a - r1a;
            const double t = cross2D(dc, dr, v2c, v2r) / denom;
            interCol = c1a + v1c * t;
            interRow = r1a + v1r * t;
        } else {
            parallel = true;
        }
    }

    m_params[QStringLiteral("angle")] = deg;
    m_params[QStringLiteral("signedAngle")] = signedDeg;
    m_params[QStringLiteral("angle360")] = deg360;
    m_params[QStringLiteral("line1Length")] = len1;
    m_params[QStringLiteral("line2Length")] = len2;
    m_params[QStringLiteral("line1AngleDeg")] = line1Deg;
    m_params[QStringLiteral("line2AngleDeg")] = line2Deg;
    m_params[QStringLiteral("intersectionRow")] = interRow;
    m_params[QStringLiteral("intersectionCol")] = interCol;
    m_params[QStringLiteral("parallel")] = parallel;

    if (valid) {
        m_params[QStringLiteral("moduleStatus")] = true;
        // 本节点经 HalconNode::init() 对外承诺了 0 号"输出图像"端口，成功就必须吐图，
        // 否则下游"显示/写文件"会报"缺输入图像"而掩盖真因（测量值在端口1/2）。透传原图。
        m_outputImage = m_inputImage;
    } else {
        // 线段退化要说明原因（2026-10-26 复核）：只判红，面板上看不到为什么
        m_params[QStringLiteral("lastError")] =
            QStringLiteral("线段长度为零（A/B 端点重合），无法计算夹角");
        m_params[QStringLiteral("moduleStatus")] = false;
    }

    // 端口1：测量结果（extraValues 带两条线段端点，供画布同时画出两条线）
    MeasureResult res;
    res.type = QStringLiteral("angle");
    res.valueName = QStringLiteral("夹角");
    res.valid = valid;
    // 主值按用户选择的角度范围给出（结果表/下游 Measure 读取以它为准）
    res.value = (angleRange == 1) ? deg360 : deg;
    res.point1 = QPointF(c1a, r1a);
    res.point2 = QPointF(c1b, r1b);
    if (valid) {
        res.extraValues = QVector<double>{c1a, r1a, c1b, r1b,
                                          c2a, r2a, c2b, r2b};
        if (!parallel)
            res.extraValues << interCol << interRow;   // [8],[9] = 两线交点（供画布标注）
    }
    auto resObj = QSharedPointer<DataObject>::create();
    resObj->setMeasureResult(res);
    setOutputData(1, valid ? resObj : QSharedPointer<DataObject>());

    // 端口2：纯数值（无论是否有效都给 0，便于下游公式引用）
    auto numObj = QSharedPointer<DataObject>::create();
    numObj->setValue(res.value);
    setOutputData(2, numObj);
}

QWidget *OpencvAngleNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvAngleNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}
