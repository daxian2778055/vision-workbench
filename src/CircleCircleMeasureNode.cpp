#include "CircleCircleMeasureNode.h"
#include "NodeResultFields.h"
#include "DataObject.h"
#include "AppLog.h"
#include <cmath>

namespace {

constexpr double kTol = 1e-9;

struct Circle2D
{
    QPointF center;
    double radius = 0.0;
};

bool circleFromMeasure(const QSharedPointer<DataObject> &data, const QString &label,
                       Circle2D *circle, QString *reason)
{
    if (!data) {
        *reason = QStringLiteral("%1的订阅输入为空（上游没有输出）").arg(label);
        return false;
    }
    if (!data->hasMeasureResult()) {
        *reason = QStringLiteral("%1的订阅数据不是圆结果（需要 Measure 类型的 circle 输出）").arg(label);
        return false;
    }
    const MeasureResult measure = data->getMeasureResult();
    if (!measure.valid || measure.type != QStringLiteral("circle")) {
        *reason = QStringLiteral("%1的订阅结果不是有效的圆（type=%2）")
                      .arg(label, measure.type.isEmpty() ? QStringLiteral("空") : measure.type);
        return false;
    }
    circle->center = measure.point1;
    circle->radius = measure.value;
    if (!(circle->radius > 0.0)) {
        *reason = QStringLiteral("%1的订阅结果半径无效（%2）").arg(label).arg(circle->radius);
        return false;
    }
    return true;
}

} // namespace

CircleCircleMeasureNode::CircleCircleMeasureNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("圆圆测量"));
    m_type = MEASUREMENT;
}

void CircleCircleMeasureNode::init()
{
    HalconNode::init();
    addInputSlot(QStringLiteral("圆1"), PortDataType::Any);
    addInputSlot(QStringLiteral("圆2"), PortDataType::Any);
    addOutputPort(QStringLiteral("测量结果"), PortDataType::Measure);
    registerParams({
        makeEnumParam(QStringLiteral("sourceMode"), 1,
                      {QStringLiteral("订阅"), QStringLiteral("绘制")},
                      QStringLiteral("来源选择")),
        makeDoubleParam(QStringLiteral("circle1Col"), 200.0, 0.0, 100000.0,
                        QStringLiteral("圆1圆心X"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("circle1Row"), 150.0, 0.0, 100000.0,
                        QStringLiteral("圆1圆心Y"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("drawCircle1Radius"), 80.0, 0.0, 100000.0,
                        QStringLiteral("圆1半径"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("circle2Col"), 260.0, 0.0, 100000.0,
                        QStringLiteral("圆2圆心X"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("circle2Row"), 150.0, 0.0, 100000.0,
                        QStringLiteral("圆2圆心Y"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("drawCircle2Radius"), 80.0, 0.0, 100000.0,
                        QStringLiteral("圆2半径"), QStringLiteral("px")),
    });
    setParamTooltip(QStringLiteral("sourceMode"),
                    QStringLiteral("订阅：两个圆槽各读一条 Measure(circle)；"
                                   "绘制：使用下面的圆心/半径参数"));
    m_params[QStringLiteral("circle1CenterX")] = 0.0;
    m_params[QStringLiteral("circle1CenterY")] = 0.0;
    m_params[QStringLiteral("circle1InnerRadius")] = 0.0;
    m_params[QStringLiteral("circle1Radius")] = 0.0;
    m_params[QStringLiteral("circle1StartAngle")] = 0.0;
    m_params[QStringLiteral("circle1AngleRange")] = 0.0;
    m_params[QStringLiteral("circle2CenterX")] = 0.0;
    m_params[QStringLiteral("circle2CenterY")] = 0.0;
    m_params[QStringLiteral("circle2InnerRadius")] = 0.0;
    m_params[QStringLiteral("circle2Radius")] = 0.0;
    m_params[QStringLiteral("circle2StartAngle")] = 0.0;
    m_params[QStringLiteral("circle2AngleRange")] = 0.0;
    m_params[QStringLiteral("intersectCount")] = 0;
    m_params[QStringLiteral("intersect1X")] = 0.0;
    m_params[QStringLiteral("intersect1Y")] = 0.0;
    m_params[QStringLiteral("intersect2X")] = 0.0;
    m_params[QStringLiteral("intersect2Y")] = 0.0;
    m_params[QStringLiteral("centerDistance")] = 0.0;
    m_params[QStringLiteral("relation")] = QString();
}

bool CircleCircleMeasureNode::process()
{
    m_params[QStringLiteral("circle1CenterX")] = 0.0;
    m_params[QStringLiteral("circle1CenterY")] = 0.0;
    m_params[QStringLiteral("circle1InnerRadius")] = 0.0;
    m_params[QStringLiteral("circle1Radius")] = 0.0;
    m_params[QStringLiteral("circle1StartAngle")] = 0.0;
    m_params[QStringLiteral("circle1AngleRange")] = 0.0;
    m_params[QStringLiteral("circle2CenterX")] = 0.0;
    m_params[QStringLiteral("circle2CenterY")] = 0.0;
    m_params[QStringLiteral("circle2InnerRadius")] = 0.0;
    m_params[QStringLiteral("circle2Radius")] = 0.0;
    m_params[QStringLiteral("circle2StartAngle")] = 0.0;
    m_params[QStringLiteral("circle2AngleRange")] = 0.0;
    m_params[QStringLiteral("intersectCount")] = 0;
    m_params[QStringLiteral("intersect1X")] = 0.0;
    m_params[QStringLiteral("intersect1Y")] = 0.0;
    m_params[QStringLiteral("intersect2X")] = 0.0;
    m_params[QStringLiteral("intersect2Y")] = 0.0;
    m_params[QStringLiteral("centerDistance")] = 0.0;
    m_params[QStringLiteral("relation")] = QString();
    return HalconNode::process();
}

void CircleCircleMeasureNode::run(bool /*autoSwitch*/)
{
    try {
        const int sourceMode =
            qBound(0, m_params.value(QStringLiteral("sourceMode"), 1).toInt(), 1);

        Circle2D circle1;
        Circle2D circle2;
        QString reason;
        if (sourceMode == 0) {
            const bool bound1 = hasInputBinding(kCircle1Slot);
            const bool bound2 = hasInputBinding(kCircle2Slot);
            if (!bound1 || !bound2) {
                m_params[QStringLiteral("lastError")] =
                    QStringLiteral("订阅模式需要同时绑定「圆1」与「圆2」");
                m_params[QStringLiteral("moduleStatus")] = false;
                setOutputData(1, QSharedPointer<DataObject>());
                return;
            }
            if (!circleFromMeasure(getInputData(kCircle1Slot), QStringLiteral("圆1"), &circle1,
                                   &reason)
                || !circleFromMeasure(getInputData(kCircle2Slot), QStringLiteral("圆2"),
                                      &circle2, &reason)) {
                m_params[QStringLiteral("lastError")] = reason;
                m_params[QStringLiteral("moduleStatus")] = false;
                setOutputData(1, QSharedPointer<DataObject>());
                return;
            }
        } else {
            if (!m_inputImage.IsInitialized()) {
                m_params[QStringLiteral("lastError")] =
                    QStringLiteral("绘制模式需要一张输入图像；请接入图像，或改用订阅来源");
                m_params[QStringLiteral("moduleStatus")] = false;
                setOutputData(1, QSharedPointer<DataObject>());
                return;
            }
            auto pv = [this](const QString &name, double fallback) {
                return m_params.value(name, fallback).toDouble();
            };
            circle1.center = QPointF(pv(QStringLiteral("circle1Col"), 200.0),
                                     pv(QStringLiteral("circle1Row"), 150.0));
            circle1.radius = pv(QStringLiteral("drawCircle1Radius"), 80.0);
            circle2.center = QPointF(pv(QStringLiteral("circle2Col"), 260.0),
                                     pv(QStringLiteral("circle2Row"), 150.0));
            circle2.radius = pv(QStringLiteral("drawCircle2Radius"), 80.0);
        }

        if (!(circle1.radius > 0.0)) {
            m_params[QStringLiteral("lastError")] =
                QStringLiteral("圆1半径必须大于 0（当前 %1）").arg(circle1.radius);
            m_params[QStringLiteral("moduleStatus")] = false;
            setOutputData(1, QSharedPointer<DataObject>());
            return;
        }
        if (!(circle2.radius > 0.0)) {
            m_params[QStringLiteral("lastError")] =
                QStringLiteral("圆2半径必须大于 0（当前 %1）").arg(circle2.radius);
            m_params[QStringLiteral("moduleStatus")] = false;
            setOutputData(1, QSharedPointer<DataObject>());
            return;
        }

        const double dx = circle2.center.x() - circle1.center.x();
        const double dy = circle2.center.y() - circle1.center.y();
        const double centerDistance = std::hypot(dx, dy);
        const double radiusSum = circle1.radius + circle2.radius;
        const double radiusDiff = std::fabs(circle1.radius - circle2.radius);

        // ① 位置关系（官方五档）；圆心重合且半径相同按「内含」处理
        QString relation;
        if (centerDistance <= kTol && radiusDiff <= kTol) {
            relation = QStringLiteral("Inside");
        } else if (centerDistance > radiusSum + kTol) {
            relation = QStringLiteral("Outside");
        } else if (std::fabs(centerDistance - radiusSum) <= kTol) {
            relation = QStringLiteral("Circumscribe");
        } else if (centerDistance < radiusDiff - kTol) {
            relation = QStringLiteral("Inside");
        } else if (std::fabs(centerDistance - radiusDiff) <= kTol) {
            relation = QStringLiteral("Inscribe");
        } else {
            relation = QStringLiteral("Intersect");
        }

        // ② 交点：相交两个、外切/内切一个、内含/外离零个
        int intersectCount = 0;
        QPointF intersect1;
        QPointF intersect2;
        const QPointF unit = centerDistance > kTol
                                 ? QPointF(dx / centerDistance, dy / centerDistance)
                                 : QPointF(1.0, 0.0);
        if (relation == QStringLiteral("Intersect")) {
            const double a = (circle1.radius * circle1.radius
                              - circle2.radius * circle2.radius + centerDistance * centerDistance)
                             / (2.0 * centerDistance);
            const double underRoot = circle1.radius * circle1.radius - a * a;
            const double h = std::sqrt(underRoot > 0.0 ? underRoot : 0.0);
            const QPointF base(circle1.center.x() + a * unit.x(),
                               circle1.center.y() + a * unit.y());
            // 法向 = 单位方向旋转 90°
            intersect1 = QPointF(base.x() - h * unit.y(), base.y() + h * unit.x());
            intersect2 = QPointF(base.x() + h * unit.y(), base.y() - h * unit.x());
            intersectCount = 2;
        } else if (relation == QStringLiteral("Circumscribe")) {
            intersect1 = QPointF(circle1.center.x() + circle1.radius * unit.x(),
                                 circle1.center.y() + circle1.radius * unit.y());
            intersectCount = 1;
        } else if (relation == QStringLiteral("Inscribe")) {
            intersect1 = QPointF(circle1.center.x() + circle1.radius * unit.x(),
                                 circle1.center.y() + circle1.radius * unit.y());
            intersectCount = 1;
        }

        m_params[QStringLiteral("circle1CenterX")] = circle1.center.x();
        m_params[QStringLiteral("circle1CenterY")] = circle1.center.y();
        m_params[QStringLiteral("circle1InnerRadius")] = 0.0;    // 单圆无内径（官方默认 0）
        m_params[QStringLiteral("circle1Radius")] = circle1.radius;
        m_params[QStringLiteral("circle1StartAngle")] = 0.0;     // 官方默认 0°
        m_params[QStringLiteral("circle1AngleRange")] = 360.0;   // 官方默认 360°
        m_params[QStringLiteral("circle2CenterX")] = circle2.center.x();
        m_params[QStringLiteral("circle2CenterY")] = circle2.center.y();
        m_params[QStringLiteral("circle2InnerRadius")] = 0.0;
        m_params[QStringLiteral("circle2Radius")] = circle2.radius;
        m_params[QStringLiteral("circle2StartAngle")] = 0.0;
        m_params[QStringLiteral("circle2AngleRange")] = 360.0;
        m_params[QStringLiteral("intersectCount")] = intersectCount;
        m_params[QStringLiteral("intersect1X")] = intersect1.x();
        m_params[QStringLiteral("intersect1Y")] = intersect1.y();
        m_params[QStringLiteral("intersect2X")] = intersect2.x();
        m_params[QStringLiteral("intersect2Y")] = intersect2.y();
        m_params[QStringLiteral("centerDistance")] = centerDistance;
        m_params[QStringLiteral("relation")] = relation;
        m_params[QStringLiteral("moduleStatus")] = true;

        MeasureResult res;
        res.type = QStringLiteral("circle_circle");
        res.valueName = QStringLiteral("圆心距");
        res.valid = true;
        res.value = centerDistance;
        res.point1 = circle1.center;
        res.point2 = circle2.center;
        res.extraValues = QVector<double>({circle1.center.x(), circle1.center.y(),
                                           circle1.radius, circle2.center.x(),
                                           circle2.center.y(), circle2.radius,
                                           intersect1.x(), intersect1.y(),
                                           intersect2.x(), intersect2.y()});
        auto resObj = QSharedPointer<DataObject>::create();
        resObj->setMeasureResult(res);
        setOutputData(1, resObj);

        m_outputImage = m_inputImage;
    } catch (const std::exception &e) {
        m_params[QStringLiteral("lastError")] = QString::fromUtf8(e.what());
        VFP_DEBUG << "CircleCircleMeasureNode error:" << e.what();
        m_params[QStringLiteral("moduleStatus")] = false;
    } catch (...) {
        m_params[QStringLiteral("lastError")] = unknownExceptionReason();
        m_params[QStringLiteral("moduleStatus")] = false;
    }
}

QVector<NodeResultField> CircleCircleMeasureNode::resultDisplayFields() const
{
    // VM「圆圆测量 → 模块结果」：两个圆环各 6 项、交点 1/2、圆心距、位置关系。
    // 官方「角度（最小外接矩形较长边）」与卡尺检测区未实现，不造字段。
    QVector<NodeResultField> fields = HalconNode::resultDisplayFields();
    fields << resultValue(QStringLiteral("circle1CenterX"), QStringLiteral("圆1圆心X"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("circle1CenterY"), QStringLiteral("圆1圆心Y"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("circle1InnerRadius"), QStringLiteral("圆1内径"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("circle1Radius"), QStringLiteral("圆1半径"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("circle1StartAngle"), QStringLiteral("圆1起始角度"),
                          QStringLiteral("°"), 2)
           << resultValue(QStringLiteral("circle1AngleRange"), QStringLiteral("圆1角度范围"),
                          QStringLiteral("°"), 2)
           << resultValue(QStringLiteral("circle2CenterX"), QStringLiteral("圆2圆心X"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("circle2CenterY"), QStringLiteral("圆2圆心Y"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("circle2InnerRadius"), QStringLiteral("圆2内径"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("circle2Radius"), QStringLiteral("圆2半径"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("circle2StartAngle"), QStringLiteral("圆2起始角度"),
                          QStringLiteral("°"), 2)
           << resultValue(QStringLiteral("circle2AngleRange"), QStringLiteral("圆2角度范围"),
                          QStringLiteral("°"), 2)
           << resultValue(QStringLiteral("intersect1X"), QStringLiteral("交点1X"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("intersect1Y"), QStringLiteral("交点1Y"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("intersect2X"), QStringLiteral("交点2X"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("intersect2Y"), QStringLiteral("交点2Y"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("centerDistance"), QStringLiteral("圆心距"),
                          QStringLiteral("px"), 3)
           << resultValue(QStringLiteral("relation"), QStringLiteral("位置关系"));
    return fields;
}
