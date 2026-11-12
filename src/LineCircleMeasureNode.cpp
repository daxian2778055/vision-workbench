#include "LineCircleMeasureNode.h"
#include "NodeResultFields.h"
#include "DataObject.h"
#include "AppLog.h"
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Line2D
{
    QPointF a;
    QPointF b;
};

struct Circle2D
{
    QPointF center;
    double radius = 0.0;
};

bool lineFromMeasure(const QSharedPointer<DataObject> &data, Line2D *line, QString *reason)
{
    if (!data) {
        *reason = QStringLiteral("直线的订阅输入为空（上游本轮没有输出）");
        return false;
    }
    if (!data->hasMeasureResult()) {
        *reason = QStringLiteral("直线的订阅数据不是直线结果（需要 Measure 类型的 line 输出）");
        return false;
    }
    const MeasureResult measure = data->getMeasureResult();
    if (!measure.valid || measure.type != QStringLiteral("line")) {
        *reason = QStringLiteral("直线的订阅结果不是有效的直线（type=%1）")
                      .arg(measure.type.isEmpty() ? QStringLiteral("空") : measure.type);
        return false;
    }
    line->a = measure.point1;
    line->b = measure.point2;
    if (std::hypot(line->b.x() - line->a.x(), line->b.y() - line->a.y()) < 1e-9) {
        *reason = QStringLiteral("直线的两个端点重合，无法确定方向");
        return false;
    }
    return true;
}

bool circleFromMeasure(const QSharedPointer<DataObject> &data, Circle2D *circle, QString *reason)
{
    if (!data) {
        *reason = QStringLiteral("圆的订阅输入为空（上游本轮没有输出）");
        return false;
    }
    if (!data->hasMeasureResult()) {
        *reason = QStringLiteral("圆的订阅数据不是圆结果（需要 Measure 类型的 circle 输出）");
        return false;
    }
    const MeasureResult measure = data->getMeasureResult();
    if (!measure.valid || measure.type != QStringLiteral("circle")) {
        *reason = QStringLiteral("圆的订阅结果不是有效的圆（type=%1）")
                      .arg(measure.type.isEmpty() ? QStringLiteral("空") : measure.type);
        return false;
    }
    circle->center = measure.point1;
    circle->radius = measure.value;
    if (!(circle->radius > 0.0)) {
        *reason = QStringLiteral("圆的订阅结果半径无效（%1）").arg(circle->radius);
        return false;
    }
    return true;
}

/// 「按点」订阅：取一个 Point（只接受 Point 类型）
bool pointFromData(const QSharedPointer<DataObject> &data, const QString &label, QPointF *point,
                   QString *reason)
{
    if (!data) {
        *reason = QStringLiteral("%1没有收到点数据（上游本轮无输出）").arg(label);
        return false;
    }
    if (data->getType() != DataObject::DataType::Point) {
        *reason = QStringLiteral("%1不是 Point 类型（当前是 %2）")
                      .arg(label, data->getTypeString());
        return false;
    }
    const QPointF candidate = data->getPoint();
    if (!std::isfinite(candidate.x()) || !std::isfinite(candidate.y())) {
        *reason = QStringLiteral("%1的坐标含非有限值").arg(label);
        return false;
    }
    *point = candidate;
    return true;
}

} // namespace

LineCircleMeasureNode::LineCircleMeasureNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("线圆测量"));
    m_type = MEASUREMENT;
}

void LineCircleMeasureNode::init()
{
    HalconNode::init();
    addInputSlot(QStringLiteral("直线"), PortDataType::Any);
    addInputSlot(QStringLiteral("圆"), PortDataType::Any);
    addInputSlot(QStringLiteral("直线起点"), PortDataType::Any);
    addInputSlot(QStringLiteral("直线终点"), PortDataType::Any);
    addOutputPort(QStringLiteral("测量结果"), PortDataType::Measure);
    registerParams({
        makeEnumParam(QStringLiteral("sourceMode"), 1,
                      {QStringLiteral("订阅"), QStringLiteral("绘制")},
                      QStringLiteral("来源选择")),
        // 官方「线输入」：按线（订阅直线结果）/ 按点（订阅起点+终点）
        makeEnumParam(QStringLiteral("lineInputMode"), 0,
                      {QStringLiteral("按线"), QStringLiteral("按点")},
                      QStringLiteral("线输入")),
        makeDoubleParam(QStringLiteral("lineCol1"), 100.0, 0.0, 100000.0,
                        QStringLiteral("直线起点X"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("lineRow1"), 100.0, 0.0, 100000.0,
                        QStringLiteral("直线起点Y"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("lineCol2"), 300.0, 0.0, 100000.0,
                        QStringLiteral("直线终点X"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("lineRow2"), 100.0, 0.0, 100000.0,
                        QStringLiteral("直线终点Y"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("circleCol"), 200.0, 0.0, 100000.0,
                        QStringLiteral("圆心X"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("circleRow"), 180.0, 0.0, 100000.0,
                        QStringLiteral("圆心Y"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("drawCircleRadius"), 60.0, 0.0, 100000.0,
                        QStringLiteral("圆半径"), QStringLiteral("px")),
    });
    setParamTooltip(QStringLiteral("sourceMode"),
                    QStringLiteral("订阅：直线槽读一条 Measure(line)、圆槽读一条 Measure(circle)；"
                                   "绘制：使用下面的直线端点与圆心/半径参数"));
    setParamTooltip(QStringLiteral("lineInputMode"),
                    QStringLiteral("订阅方式：按线读一条直线结果；按点分别读直线起点与终点"));
    m_params[QStringLiteral("lineStartX")] = 0.0;
    m_params[QStringLiteral("lineStartY")] = 0.0;
    m_params[QStringLiteral("lineEndX")] = 0.0;
    m_params[QStringLiteral("lineEndY")] = 0.0;
    m_params[QStringLiteral("lineAngle")] = 0.0;
    m_params[QStringLiteral("circleCenterX")] = 0.0;
    m_params[QStringLiteral("circleCenterY")] = 0.0;
    m_params[QStringLiteral("circleRadius")] = 0.0;
    m_params[QStringLiteral("circleInnerRadius")] = 0.0;
    m_params[QStringLiteral("circleStartAngle")] = 0.0;
    m_params[QStringLiteral("circleAngleRange")] = 0.0;
    m_params[QStringLiteral("footX")] = 0.0;
    m_params[QStringLiteral("footY")] = 0.0;
    m_params[QStringLiteral("distance")] = 0.0;
    m_params[QStringLiteral("intersectCount")] = 0;
    m_params[QStringLiteral("intersect1X")] = 0.0;
    m_params[QStringLiteral("intersect1Y")] = 0.0;
    m_params[QStringLiteral("intersect2X")] = 0.0;
    m_params[QStringLiteral("intersect2Y")] = 0.0;
}

bool LineCircleMeasureNode::process()
{
    m_params[QStringLiteral("lineStartX")] = 0.0;
    m_params[QStringLiteral("lineStartY")] = 0.0;
    m_params[QStringLiteral("lineEndX")] = 0.0;
    m_params[QStringLiteral("lineEndY")] = 0.0;
    m_params[QStringLiteral("lineAngle")] = 0.0;
    m_params[QStringLiteral("circleCenterX")] = 0.0;
    m_params[QStringLiteral("circleCenterY")] = 0.0;
    m_params[QStringLiteral("circleRadius")] = 0.0;
    m_params[QStringLiteral("circleInnerRadius")] = 0.0;
    m_params[QStringLiteral("circleStartAngle")] = 0.0;
    m_params[QStringLiteral("circleAngleRange")] = 0.0;
    m_params[QStringLiteral("footX")] = 0.0;
    m_params[QStringLiteral("footY")] = 0.0;
    m_params[QStringLiteral("distance")] = 0.0;
    m_params[QStringLiteral("intersectCount")] = 0;
    m_params[QStringLiteral("intersect1X")] = 0.0;
    m_params[QStringLiteral("intersect1Y")] = 0.0;
    m_params[QStringLiteral("intersect2X")] = 0.0;
    m_params[QStringLiteral("intersect2Y")] = 0.0;
    return HalconNode::process();
}

void LineCircleMeasureNode::run(bool /*autoSwitch*/)
{
    try {
        const int sourceMode =
            qBound(0, m_params.value(QStringLiteral("sourceMode"), 1).toInt(), 1);
        const int lineInputMode =
            qBound(0, m_params.value(QStringLiteral("lineInputMode"), 0).toInt(), 1);

        Line2D line;
        Circle2D circle;
        QString reason;
        if (sourceMode == 0 && lineInputMode == 0) {
            // 按线：直线槽读一条 line 结果；点槽不参与计算
            if (hasInputBinding(kLineStartSlot) || hasInputBinding(kLineEndSlot)) {
                m_params[QStringLiteral("lastError")] =
                    QStringLiteral("线输入为按线时，只使用「直线」槽；请清除点槽绑定，"
                                   "或把线输入切换为按点");
                m_params[QStringLiteral("moduleStatus")] = false;
                setOutputData(1, QSharedPointer<DataObject>());
                return;
            }
            const bool lineBound = hasInputBinding(kLineSlot);
            const bool circleBound = hasInputBinding(kCircleSlot);
            if (!lineBound || !circleBound) {
                m_params[QStringLiteral("lastError")] =
                    QStringLiteral("订阅模式需要同时绑定「直线」与「圆」");
                m_params[QStringLiteral("moduleStatus")] = false;
                setOutputData(1, QSharedPointer<DataObject>());
                return;
            }
            if (!lineFromMeasure(getInputData(kLineSlot), &line, &reason)
                || !circleFromMeasure(getInputData(kCircleSlot), &circle, &reason)) {
                m_params[QStringLiteral("lastError")] = reason;
                m_params[QStringLiteral("moduleStatus")] = false;
                setOutputData(1, QSharedPointer<DataObject>());
                return;
            }
        } else if (sourceMode == 0 && lineInputMode == 1) {
            // 按点：起点/终点两个 Point 组一条直线，圆仍走圆槽
            if (hasInputBinding(kLineSlot)) {
                m_params[QStringLiteral("lastError")] =
                    QStringLiteral("线输入为按点时，「直线」槽不参与计算；请清除该绑定，"
                                   "或把线输入切换为按线");
                m_params[QStringLiteral("moduleStatus")] = false;
                setOutputData(1, QSharedPointer<DataObject>());
                return;
            }
            const bool startBound = hasInputBinding(kLineStartSlot);
            const bool endBound = hasInputBinding(kLineEndSlot);
            const bool circleBound = hasInputBinding(kCircleSlot);
            if (!startBound || !endBound || !circleBound) {
                m_params[QStringLiteral("lastError")] =
                    QStringLiteral("线输入为按点时必须同时绑定「直线起点」「直线终点」与「圆」");
                m_params[QStringLiteral("moduleStatus")] = false;
                setOutputData(1, QSharedPointer<DataObject>());
                return;
            }
            QPointF start;
            QPointF end;
            if (!pointFromData(getInputData(kLineStartSlot), QStringLiteral("直线起点"), &start,
                               &reason)
                || !pointFromData(getInputData(kLineEndSlot), QStringLiteral("直线终点"), &end,
                                  &reason)
                || !circleFromMeasure(getInputData(kCircleSlot), &circle, &reason)) {
                m_params[QStringLiteral("lastError")] = reason;
                m_params[QStringLiteral("moduleStatus")] = false;
                setOutputData(1, QSharedPointer<DataObject>());
                return;
            }
            line.a = start;
            line.b = end;
        } else {
            if (!m_inputImage.IsInitialized()) {
                m_params[QStringLiteral("lastError")] =
                    QStringLiteral("绘制模式缺少输入图像；请接入图像，或切换到订阅来源");
                m_params[QStringLiteral("moduleStatus")] = false;
                setOutputData(1, QSharedPointer<DataObject>());
                return;
            }
            auto pv = [this](const QString &name, double fallback) {
                return m_params.value(name, fallback).toDouble();
            };
            line.a = QPointF(pv(QStringLiteral("lineCol1"), 100.0),
                             pv(QStringLiteral("lineRow1"), 100.0));
            line.b = QPointF(pv(QStringLiteral("lineCol2"), 300.0),
                             pv(QStringLiteral("lineRow2"), 100.0));
            circle.center = QPointF(pv(QStringLiteral("circleCol"), 200.0),
                                    pv(QStringLiteral("circleRow"), 180.0));
            circle.radius = pv(QStringLiteral("drawCircleRadius"), 60.0);
        }

        const double dx = line.b.x() - line.a.x();
        const double dy = line.b.y() - line.a.y();
        const double length = std::hypot(dx, dy);
        if (length < 1e-9) {
            m_params[QStringLiteral("lastError")] =
                QStringLiteral("直线的两个端点重合，无法确定方向");
            m_params[QStringLiteral("moduleStatus")] = false;
            setOutputData(1, QSharedPointer<DataObject>());
            return;
        }
        if (!(circle.radius > 0.0)) {
            m_params[QStringLiteral("lastError")] =
                QStringLiteral("圆半径必须大于 0（当前 %1）").arg(circle.radius);
            m_params[QStringLiteral("moduleStatus")] = false;
            setOutputData(1, QSharedPointer<DataObject>());
            return;
        }

        // ① 圆心到直线的垂足（投影到无限长直线）与距离
        const double ux = dx / length;
        const double uy = dy / length;
        const double t = (circle.center.x() - line.a.x()) * ux
                         + (circle.center.y() - line.a.y()) * uy;
        const QPointF foot(line.a.x() + t * ux, line.a.y() + t * uy);
        const double distance = std::hypot(circle.center.x() - foot.x(),
                                           circle.center.y() - foot.y());

        // ② 交点：|p + s·u - c| = r，解 s² - 2·t·s + (|c-p|² - r²) = 0
        const double discriminant = circle.radius * circle.radius - distance * distance;
        int intersectCount = 0;
        QPointF intersect1;
        QPointF intersect2;
        if (discriminant > 1e-12) {
            const double offset = std::sqrt(discriminant);
            intersect1 = QPointF(line.a.x() + (t - offset) * ux,
                                 line.a.y() + (t - offset) * uy);
            intersect2 = QPointF(line.a.x() + (t + offset) * ux,
                                 line.a.y() + (t + offset) * uy);
            intersectCount = 2;
        } else if (std::fabs(discriminant) <= 1e-12) {
            intersect1 = foot;   // 相切
            intersectCount = 1;
        }

        const double lineAngle = std::atan2(dy, dx) * 180.0 / kPi;

        m_params[QStringLiteral("lineStartX")] = line.a.x();
        m_params[QStringLiteral("lineStartY")] = line.a.y();
        m_params[QStringLiteral("lineEndX")] = line.b.x();
        m_params[QStringLiteral("lineEndY")] = line.b.y();
        m_params[QStringLiteral("lineAngle")] = lineAngle;
        m_params[QStringLiteral("circleCenterX")] = circle.center.x();
        m_params[QStringLiteral("circleCenterY")] = circle.center.y();
        m_params[QStringLiteral("circleRadius")] = circle.radius;
        m_params[QStringLiteral("circleInnerRadius")] = 0.0;      // 单圆无内径（官方默认 0）
        m_params[QStringLiteral("circleStartAngle")] = 0.0;       // 官方默认 0°
        m_params[QStringLiteral("circleAngleRange")] = 360.0;     // 官方默认 360°
        m_params[QStringLiteral("footX")] = foot.x();
        m_params[QStringLiteral("footY")] = foot.y();
        m_params[QStringLiteral("distance")] = distance;
        m_params[QStringLiteral("intersectCount")] = intersectCount;
        m_params[QStringLiteral("intersect1X")] = intersect1.x();
        m_params[QStringLiteral("intersect1Y")] = intersect1.y();
        m_params[QStringLiteral("intersect2X")] = intersect2.x();
        m_params[QStringLiteral("intersect2Y")] = intersect2.y();
        m_params[QStringLiteral("moduleStatus")] = true;

        MeasureResult res;
        res.type = QStringLiteral("line_circle");
        res.valueName = QStringLiteral("圆心到直线距离");
        res.valid = true;
        res.value = distance;
        res.point1 = circle.center;
        res.point2 = foot;
        res.extraValues = QVector<double>({circle.center.x(), circle.center.y(), circle.radius,
                                           foot.x(), foot.y(), distance,
                                           intersect1.x(), intersect1.y(),
                                           intersect2.x(), intersect2.y()});
        auto resObj = QSharedPointer<DataObject>::create();
        resObj->setMeasureResult(res);
        setOutputData(1, resObj);

        m_outputImage = m_inputImage;
    } catch (const std::exception &e) {
        m_params[QStringLiteral("lastError")] = QString::fromUtf8(e.what());
        VFP_DEBUG << "LineCircleMeasureNode error:" << e.what();
        m_params[QStringLiteral("moduleStatus")] = false;
    } catch (...) {
        m_params[QStringLiteral("lastError")] = unknownExceptionReason();
        m_params[QStringLiteral("moduleStatus")] = false;
    }
}

QVector<NodeResultField> LineCircleMeasureNode::resultDisplayFields() const
{
    // VM「线圆测量 → 模块结果」：直线端点/角度、圆环五项、交点 1/2、垂足、距离。
    // 官方「角度（最小外接矩形较长边）」与卡尺检测区未实现，不造字段。
    QVector<NodeResultField> fields = HalconNode::resultDisplayFields();
    fields << resultValue(QStringLiteral("lineStartX"), QStringLiteral("直线起点X"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("lineStartY"), QStringLiteral("直线起点Y"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("lineEndX"), QStringLiteral("直线终点X"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("lineEndY"), QStringLiteral("直线终点Y"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("lineAngle"), QStringLiteral("直线角度"),
                          QStringLiteral("°"), 3)
           << resultValue(QStringLiteral("circleCenterX"), QStringLiteral("圆心X"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("circleCenterY"), QStringLiteral("圆心Y"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("circleInnerRadius"), QStringLiteral("圆内径"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("circleRadius"), QStringLiteral("圆半径"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("circleStartAngle"), QStringLiteral("圆起始角度"),
                          QStringLiteral("°"), 2)
           << resultValue(QStringLiteral("circleAngleRange"), QStringLiteral("圆角度范围"),
                          QStringLiteral("°"), 2)
           << resultValue(QStringLiteral("intersect1X"), QStringLiteral("交点1X"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("intersect1Y"), QStringLiteral("交点1Y"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("intersect2X"), QStringLiteral("交点2X"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("intersect2Y"), QStringLiteral("交点2Y"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("footX"), QStringLiteral("垂足点X"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("footY"), QStringLiteral("垂足点Y"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("distance"), QStringLiteral("距离"),
                          QStringLiteral("px"), 3);
    return fields;
}
