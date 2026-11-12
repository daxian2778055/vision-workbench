#include "LineLineMeasureNode.h"
#include "NodeResultFields.h"
#include "DataObject.h"
#include "AppLog.h"
#include <QWidget>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

/// 直线的一端（整图坐标：QPointF.x = 列，y = 行）
struct Line2D
{
    QPointF a;
    QPointF b;
};

double directionLength(const Line2D &line)
{
    return std::hypot(line.b.x() - line.a.x(), line.b.y() - line.a.y());
}

double pointToLineDistance(const QPointF &point, const Line2D &line)
{
    const double dx = line.b.x() - line.a.x();
    const double dy = line.b.y() - line.a.y();
    const double length = std::hypot(dx, dy);
    if (length < 1e-12)
        return std::hypot(point.x() - line.a.x(), point.y() - line.a.y());
    return std::fabs(dx * (point.y() - line.a.y()) - dy * (point.x() - line.a.x())) / length;
}

/// 从订阅数据取一条直线：只接受 `Measure(type="line")`，端点必须相异。
/// B1 聚合输出对象的类型可能是 Image/其它，但测量结果挂在对象上，故用 hasMeasureResult 判定。
bool lineFromMeasure(const QSharedPointer<DataObject> &data, const QString &lineLabel,
                     Line2D *line, QString *reason)
{
    if (!data) {
        *reason = QStringLiteral("%1的订阅输入为空（上游本轮没有输出）").arg(lineLabel);
        return false;
    }
    if (!data->hasMeasureResult()) {
        *reason = QStringLiteral("%1的订阅数据不是直线结果（需要 Measure 类型的 line 输出）")
                      .arg(lineLabel);
        return false;
    }
    const MeasureResult measure = data->getMeasureResult();
    if (!measure.valid || measure.type != QStringLiteral("line")) {
        *reason = QStringLiteral("%1的订阅结果不是有效的直线（type=%2）")
                      .arg(lineLabel, measure.type.isEmpty() ? QStringLiteral("空")
                                                             : measure.type);
        return false;
    }
    Line2D candidate;
    candidate.a = measure.point1;
    candidate.b = measure.point2;
    if (directionLength(candidate) < 1e-9) {
        *reason = QStringLiteral("%1的两个端点重合，无法确定直线方向").arg(lineLabel);
        return false;
    }
    *line = candidate;
    return true;
}

/// 「按点」订阅：从数据对象取一个点（只接受 Point 类型）
bool pointFromData(const QSharedPointer<DataObject> &data, const QString &label, QPointF *point,
                   QString *reason)
{
    if (!data) {
        *reason = QStringLiteral("%1的订阅输入为空（没有收到点）").arg(label);
        return false;
    }
    if (data->getType() != DataObject::DataType::Point) {
        *reason = QStringLiteral("%1的订阅数据不是点（需要 Point 类型）").arg(label);
        return false;
    }
    const QPointF candidate = data->getPoint();
    if (!std::isfinite(candidate.x()) || !std::isfinite(candidate.y())) {
        *reason = QStringLiteral("%1的坐标不是有限数").arg(label);
        return false;
    }
    *point = candidate;
    return true;
}

} // namespace

LineLineMeasureNode::LineLineMeasureNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("线线测量"));
    m_type = MEASUREMENT;
}

void LineLineMeasureNode::init()
{
    HalconNode::init();
    // VM「线线测量」基本参数：来源选择 = 订阅 / 绘制；订阅下再选线输入 = 按线 / 按点。
    // 订阅走逻辑槽（不在画布上多长输入点）。
    addInputSlot(QStringLiteral("直线1"), PortDataType::Any);
    addInputSlot(QStringLiteral("直线2"), PortDataType::Any);
    addInputSlot(QStringLiteral("直线1起点"), PortDataType::Any);
    addInputSlot(QStringLiteral("直线1终点"), PortDataType::Any);
    addInputSlot(QStringLiteral("直线2起点"), PortDataType::Any);
    addInputSlot(QStringLiteral("直线2终点"), PortDataType::Any);
    addOutputPort(QStringLiteral("测量结果"), PortDataType::Measure);
    registerParams({
        makeEnumParam(QStringLiteral("sourceMode"), 1,
                      {QStringLiteral("订阅"), QStringLiteral("绘制")},
                      QStringLiteral("来源选择")),
        // 官方「线输入」：按线（订阅两条直线结果）/ 按点（订阅 4 个点）
        makeEnumParam(QStringLiteral("lineInputMode"), 0,
                      {QStringLiteral("按线"), QStringLiteral("按点")},
                      QStringLiteral("线输入")),
        // 绘制模式的两条直线（整图坐标；列 = X，行 = Y）
        makeDoubleParam(QStringLiteral("line1Col1"), 100.0, 0.0, 100000.0,
                        QStringLiteral("直线1起点X"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("line1Row1"), 100.0, 0.0, 100000.0,
                        QStringLiteral("直线1起点Y"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("line1Col2"), 300.0, 0.0, 100000.0,
                        QStringLiteral("直线1终点X"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("line1Row2"), 100.0, 0.0, 100000.0,
                        QStringLiteral("直线1终点Y"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("line2Col1"), 200.0, 0.0, 100000.0,
                        QStringLiteral("直线2起点X"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("line2Row1"), 50.0, 0.0, 100000.0,
                        QStringLiteral("直线2起点Y"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("line2Col2"), 200.0, 0.0, 100000.0,
                        QStringLiteral("直线2终点X"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("line2Row2"), 250.0, 0.0, 100000.0,
                        QStringLiteral("直线2终点Y"), QStringLiteral("px")),
        // 官方「输出角度范围」：带符号角度按 -90..90 或 -180..180 输出
        makeEnumParam(QStringLiteral("angleRange"), 0,
                      {QStringLiteral("-90°~90°"), QStringLiteral("-180°~180°")},
                      QStringLiteral("输出角度范围")),
    });
    setParamTooltip(QStringLiteral("sourceMode"),
                    QStringLiteral("订阅：从「直线1」「直线2」槽各读一条 Measure(line) 结果；"
                                   "绘制：使用下面的 8 个坐标参数"));
    setParamTooltip(QStringLiteral("lineInputMode"),
                    QStringLiteral("订阅方式：按线读两条直线结果；按点分别读直线1/2 的起点与终点"));
    setParamTooltip(QStringLiteral("angleRange"),
                    QStringLiteral("只影响带符号角度：从直线1到直线2方向，顺时针为正、逆时针为负"));
    m_params[QStringLiteral("line1StartX")] = 0.0;
    m_params[QStringLiteral("line1StartY")] = 0.0;
    m_params[QStringLiteral("line1EndX")] = 0.0;
    m_params[QStringLiteral("line1EndY")] = 0.0;
    m_params[QStringLiteral("line2StartX")] = 0.0;
    m_params[QStringLiteral("line2StartY")] = 0.0;
    m_params[QStringLiteral("line2EndX")] = 0.0;
    m_params[QStringLiteral("line2EndY")] = 0.0;
    m_params[QStringLiteral("line1Angle")] = 0.0;
    m_params[QStringLiteral("line2Angle")] = 0.0;
    m_params[QStringLiteral("intersectX")] = 0.0;
    m_params[QStringLiteral("intersectY")] = 0.0;
    m_params[QStringLiteral("acuteAngle")] = 0.0;
    m_params[QStringLiteral("signedAngle")] = 0.0;
    m_params[QStringLiteral("absDistance")] = 0.0;
}

bool LineLineMeasureNode::process()
{
    // 缺图/订阅无效等早退路径不能让上一轮读数留在面板上
    m_params[QStringLiteral("line1StartX")] = 0.0;
    m_params[QStringLiteral("line1StartY")] = 0.0;
    m_params[QStringLiteral("line1EndX")] = 0.0;
    m_params[QStringLiteral("line1EndY")] = 0.0;
    m_params[QStringLiteral("line2StartX")] = 0.0;
    m_params[QStringLiteral("line2StartY")] = 0.0;
    m_params[QStringLiteral("line2EndX")] = 0.0;
    m_params[QStringLiteral("line2EndY")] = 0.0;
    m_params[QStringLiteral("line1Angle")] = 0.0;
    m_params[QStringLiteral("line2Angle")] = 0.0;
    m_params[QStringLiteral("intersectX")] = 0.0;
    m_params[QStringLiteral("intersectY")] = 0.0;
    m_params[QStringLiteral("acuteAngle")] = 0.0;
    m_params[QStringLiteral("signedAngle")] = 0.0;
    m_params[QStringLiteral("absDistance")] = 0.0;
    return HalconNode::process();
}

void LineLineMeasureNode::run(bool /*autoSwitch*/)
{
    try {
        const int sourceMode =
            qBound(0, m_params.value(QStringLiteral("sourceMode"), 1).toInt(), 1);
        const int lineInputMode =
            qBound(0, m_params.value(QStringLiteral("lineInputMode"), 0).toInt(), 1);
        const int angleRange =
            qBound(0, m_params.value(QStringLiteral("angleRange"), 0).toInt(), 1);

        // ① 取两条直线：订阅（按线/按点）或绘制坐标参数
        Line2D line1;
        Line2D line2;
        QString reason;
        if (sourceMode == 0 && lineInputMode == 0) {
            // 按线：只认「直线1」「直线2」两个槽
            if (hasInputBinding(kLine1StartSlot) || hasInputBinding(kLine1EndSlot)
                || hasInputBinding(kLine2StartSlot) || hasInputBinding(kLine2EndSlot)) {
                m_params[QStringLiteral("lastError")] =
                    QStringLiteral("线输入为按线时，只使用「直线1」「直线2」槽；"
                                   "请清除点槽绑定或把线输入切换为按点");
                m_params[QStringLiteral("moduleStatus")] = false;
                setOutputData(1, QSharedPointer<DataObject>());
                return;
            }
            const bool bound1 = hasInputBinding(kLine1Slot);
            const bool bound2 = hasInputBinding(kLine2Slot);
            if (!bound1 || !bound2) {
                m_params[QStringLiteral("lastError")] =
                    QStringLiteral("订阅模式需要同时绑定「直线1」与「直线2」");
                m_params[QStringLiteral("moduleStatus")] = false;
                setOutputData(1, QSharedPointer<DataObject>());
                return;
            }
            if (!lineFromMeasure(getInputData(kLine1Slot), QStringLiteral("直线1"), &line1,
                                 &reason)
                || !lineFromMeasure(getInputData(kLine2Slot), QStringLiteral("直线2"), &line2,
                                    &reason)) {
                m_params[QStringLiteral("lastError")] = reason;
                m_params[QStringLiteral("moduleStatus")] = false;
                setOutputData(1, QSharedPointer<DataObject>());
                return;
            }
        } else if (sourceMode == 0 && lineInputMode == 1) {
            // 按点：4 个点槽各取一个 Point
            if (hasInputBinding(kLine1Slot) || hasInputBinding(kLine2Slot)) {
                m_params[QStringLiteral("lastError")] =
                    QStringLiteral("线输入为按点时，只使用 4 个点槽；"
                                   "请清除直线1/2 的绑定或把线输入切换为按线");
                m_params[QStringLiteral("moduleStatus")] = false;
                setOutputData(1, QSharedPointer<DataObject>());
                return;
            }
            const bool bound1 = hasInputBinding(kLine1StartSlot);
            const bool bound2 = hasInputBinding(kLine1EndSlot);
            const bool bound3 = hasInputBinding(kLine2StartSlot);
            const bool bound4 = hasInputBinding(kLine2EndSlot);
            if (!bound1 || !bound2 || !bound3 || !bound4) {
                m_params[QStringLiteral("lastError")] =
                    QStringLiteral("按点订阅需要同时绑定直线1起点/终点、直线2起点/终点");
                m_params[QStringLiteral("moduleStatus")] = false;
                setOutputData(1, QSharedPointer<DataObject>());
                return;
            }
            if (!pointFromData(getInputData(kLine1StartSlot), QStringLiteral("直线1起点"),
                               &line1.a, &reason)
                || !pointFromData(getInputData(kLine1EndSlot), QStringLiteral("直线1终点"),
                                  &line1.b, &reason)
                || !pointFromData(getInputData(kLine2StartSlot), QStringLiteral("直线2起点"),
                                  &line2.a, &reason)
                || !pointFromData(getInputData(kLine2EndSlot), QStringLiteral("直线2终点"),
                                  &line2.b, &reason)) {
                m_params[QStringLiteral("lastError")] = reason;
                m_params[QStringLiteral("moduleStatus")] = false;
                setOutputData(1, QSharedPointer<DataObject>());
                return;
            }
        } else {
            if (!m_inputImage.IsInitialized()) {
                m_params[QStringLiteral("lastError")] =
                    QStringLiteral("绘制模式需要输入图像；请接入图像，或把来源选择改为订阅");
                m_params[QStringLiteral("moduleStatus")] = false;
                setOutputData(1, QSharedPointer<DataObject>());
                return;
            }
            auto pv = [this](const QString &name, double fallback) {
                return m_params.value(name, fallback).toDouble();
            };
            line1.a = QPointF(pv(QStringLiteral("line1Col1"), 100.0),
                              pv(QStringLiteral("line1Row1"), 100.0));
            line1.b = QPointF(pv(QStringLiteral("line1Col2"), 300.0),
                              pv(QStringLiteral("line1Row2"), 100.0));
            line2.a = QPointF(pv(QStringLiteral("line2Col1"), 200.0),
                              pv(QStringLiteral("line2Row1"), 50.0));
            line2.b = QPointF(pv(QStringLiteral("line2Col2"), 200.0),
                              pv(QStringLiteral("line2Row2"), 250.0));
        }

        if (directionLength(line1) < 1e-9) {
            m_params[QStringLiteral("lastError")] =
                QStringLiteral("直线1的两个端点重合，无法确定直线方向");
            m_params[QStringLiteral("moduleStatus")] = false;
            setOutputData(1, QSharedPointer<DataObject>());
            return;
        }
        if (directionLength(line2) < 1e-9) {
            m_params[QStringLiteral("lastError")] =
                QStringLiteral("直线2的两个端点重合，无法确定直线方向");
            m_params[QStringLiteral("moduleStatus")] = false;
            setOutputData(1, QSharedPointer<DataObject>());
            return;
        }

        // ② 交点：p + t·d1 = q + u·d2，平行（含重合）无唯一交点
        const double d1x = line1.b.x() - line1.a.x();
        const double d1y = line1.b.y() - line1.a.y();
        const double d2x = line2.b.x() - line2.a.x();
        const double d2y = line2.b.y() - line2.a.y();
        const double cross = d1x * d2y - d1y * d2x;
        const double dot = d1x * d2x + d1y * d2y;
        const double len1 = std::hypot(d1x, d1y);
        const double len2 = std::hypot(d2x, d2y);
        if (std::fabs(cross) <= 1e-12 * len1 * len2) {
            m_params[QStringLiteral("lastError")] =
                QStringLiteral("两条直线平行或重合，没有唯一交点");
            m_params[QStringLiteral("moduleStatus")] = false;
            setOutputData(1, QSharedPointer<DataObject>());
            return;
        }
        const double qpx = line2.a.x() - line1.a.x();
        const double qpy = line2.a.y() - line1.a.y();
        const double t = (qpx * d2y - qpy * d2x) / cross;
        const double intersectX = line1.a.x() + t * d1x;
        const double intersectY = line1.a.y() + t * d1y;

        // ③ 夹角（锐角 0..90）与带符号角度（直线1 → 直线2，顺时针为正）。
        // 直线是无向的：锐角用 |dot|，否则钝向会算成 135°。
        const double acuteAngle = std::atan2(std::fabs(cross), std::fabs(dot)) * 180.0 / kPi;
        double signedAngle = std::atan2(cross, dot) * 180.0 / kPi;
        if (angleRange == 0) {
            // -90..90：超界按直线无向性折回
            if (signedAngle > 90.0)
                signedAngle -= 180.0;
            else if (signedAngle <= -90.0)
                signedAngle += 180.0;
        }

        // ④ 绝对距离：4 个端点到对应另一条直线的垂直距离平均值（官方口径）
        const double absDistance = (pointToLineDistance(line1.a, line2)
                                    + pointToLineDistance(line1.b, line2)
                                    + pointToLineDistance(line2.a, line1)
                                    + pointToLineDistance(line2.b, line1))
                                   / 4.0;

        const double line1Angle = std::atan2(d1y, d1x) * 180.0 / kPi;
        const double line2Angle = std::atan2(d2y, d2x) * 180.0 / kPi;

        m_params[QStringLiteral("line1StartX")] = line1.a.x();
        m_params[QStringLiteral("line1StartY")] = line1.a.y();
        m_params[QStringLiteral("line1EndX")] = line1.b.x();
        m_params[QStringLiteral("line1EndY")] = line1.b.y();
        m_params[QStringLiteral("line2StartX")] = line2.a.x();
        m_params[QStringLiteral("line2StartY")] = line2.a.y();
        m_params[QStringLiteral("line2EndX")] = line2.b.x();
        m_params[QStringLiteral("line2EndY")] = line2.b.y();
        m_params[QStringLiteral("line1Angle")] = line1Angle;
        m_params[QStringLiteral("line2Angle")] = line2Angle;
        m_params[QStringLiteral("intersectX")] = intersectX;
        m_params[QStringLiteral("intersectY")] = intersectY;
        m_params[QStringLiteral("acuteAngle")] = acuteAngle;
        m_params[QStringLiteral("signedAngle")] = signedAngle;
        m_params[QStringLiteral("absDistance")] = absDistance;
        m_params[QStringLiteral("moduleStatus")] = true;

        // 输出 Measure：value = 夹角，point1 = 交点；extraValues 带两条直线端点与角度读数
        MeasureResult res;
        res.type = QStringLiteral("line_line");
        res.valueName = QStringLiteral("夹角");
        res.valid = true;
        res.value = acuteAngle;
        res.point1 = QPointF(intersectX, intersectY);
        res.point2 = line1.a;
        res.extraValues = QVector<double>({line1.a.x(), line1.a.y(), line1.b.x(), line1.b.y(),
                                           line2.a.x(), line2.a.y(), line2.b.x(), line2.b.y(),
                                           signedAngle, absDistance});
        auto resObj = QSharedPointer<DataObject>::create();
        resObj->setMeasureResult(res);
        setOutputData(1, resObj);

        m_outputImage = m_inputImage;   // 纯几何节点：有图就原样透传
    } catch (const std::exception &e) {
        m_params[QStringLiteral("lastError")] = QString::fromUtf8(e.what());
        VFP_DEBUG << "LineLineMeasureNode error:" << e.what();
        m_params[QStringLiteral("moduleStatus")] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params[QStringLiteral("lastError")] = unknownExceptionReason();
        m_params[QStringLiteral("moduleStatus")] = false;
        m_outputImage.Clear();
    }
}

QVector<NodeResultField> LineLineMeasureNode::resultDisplayFields() const
{
    // VM「线线测量 → 模块结果」：两条测量直线的端点/角度、交点、夹角（锐角）、绝对距离。
    // 官方另有「角度标识点 1~5」「卡尺检测区」「屏蔽区多边形」——本节点无卡尺链路，不造这些字段。
    QVector<NodeResultField> fields = HalconNode::resultDisplayFields();
    fields << resultValue(QStringLiteral("line1StartX"), QStringLiteral("直线1起点X"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("line1StartY"), QStringLiteral("直线1起点Y"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("line1EndX"), QStringLiteral("直线1终点X"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("line1EndY"), QStringLiteral("直线1终点Y"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("line2StartX"), QStringLiteral("直线2起点X"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("line2StartY"), QStringLiteral("直线2起点Y"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("line2EndX"), QStringLiteral("直线2终点X"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("line2EndY"), QStringLiteral("直线2终点Y"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("line1Angle"), QStringLiteral("直线1角度"),
                          QStringLiteral("°"), 3)
           << resultValue(QStringLiteral("line2Angle"), QStringLiteral("直线2角度"),
                          QStringLiteral("°"), 3)
           << resultValue(QStringLiteral("intersectX"), QStringLiteral("交点X"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("intersectY"), QStringLiteral("交点Y"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("acuteAngle"), QStringLiteral("夹角"),
                          QStringLiteral("°"), 3)
           << resultValue(QStringLiteral("signedAngle"), QStringLiteral("带符号角度"),
                          QStringLiteral("°"), 3)
           << resultValue(QStringLiteral("absDistance"), QStringLiteral("绝对距离"),
                          QStringLiteral("px"), 3);
    return fields;
}
