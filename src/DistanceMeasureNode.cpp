#include "DistanceMeasureNode.h"
#include "DataObject.h"
#include "Port.h"
#include <cmath>

DistanceMeasureNode::DistanceMeasureNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("两点距离"));
    m_type = MEASUREMENT;
}

void DistanceMeasureNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("测量结果"), PortDataType::Measure);
    registerParams({
        makeDoubleParam(QStringLiteral("p1Row"), 100.0, 0.0, 100000.0, QStringLiteral("点1行"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("p1Col"), 100.0, 0.0, 100000.0, QStringLiteral("点1列"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("p2Row"), 200.0, 0.0, 100000.0, QStringLiteral("点2行"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("p2Col"), 200.0, 0.0, 100000.0, QStringLiteral("点2列"), QStringLiteral("px")),
        // VM4.4「点点测量」官方输出角度范围（本机帮助 GUID-352A1EEE…）：
        // 可选 -90°~90° 或 -180°~180°；角度 = 两点连线与水平线夹角。
        makeEnumParam(QStringLiteral("angleRange"), 0,
                      {QStringLiteral("-90\u00B0~90\u00B0"), QStringLiteral("-180\u00B0~180\u00B0")},
                      QStringLiteral("输出角度范围")),
    });
    setParamTooltip(QStringLiteral("angleRange"),
                    QStringLiteral("只影响角度字段：两点连线与水平线夹角，顺时针为正"));
}

void DistanceMeasureNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        auto pv = [this](const QString &n, double d) { return m_params.value(n, d).toDouble(); };
        const double r1 = pv(QStringLiteral("p1Row"), 100);
        const double c1 = pv(QStringLiteral("p1Col"), 100);
        const double r2 = pv(QStringLiteral("p2Row"), 200);
        const double c2 = pv(QStringLiteral("p2Col"), 200);
        const double dist = std::hypot(r2 - r1, c2 - c1);

        // 官方角度：两点连线与水平线的夹角，顺时针为正（图像 y 向下，
        // atan2(dy,dx) 正值即视觉顺时针，与 LineCircleMeasureNode::lineAngle 同口径）。
        // 角度范围 -90~90 / -180~180 由官方「输出角度范围」参数控制。
        double angle = std::atan2(r2 - r1, c2 - c1) * 180.0 / 3.14159265358979323846;
        while (angle <= -180.0) angle += 360.0;
        while (angle > 180.0) angle -= 360.0;
        if (m_params.value(QStringLiteral("angleRange"), 0).toInt() == 0) {
            if (angle > 90.0) angle -= 180.0;
            else if (angle < -90.0) angle += 180.0;
        }
        m_params[QStringLiteral("lineAngle")] = angle;

        MeasureResult res;
        res.type = QStringLiteral("point_point");
        res.valueName = QStringLiteral("距离");
        res.valid = true;
        res.value = dist;
        res.point1 = QPointF(c1, r1);
        res.point2 = QPointF(c2, r2);

        auto resObj = QSharedPointer<DataObject>::create();
        resObj->setMeasureResult(res);
        setOutputData(1, resObj);

        m_outputImage = m_inputImage;
    } catch (const std::exception &) {
        m_outputImage.Clear();
        setOutputData(1, QSharedPointer<DataObject>());
    }
}

QVector<NodeResultField> DistanceMeasureNode::resultDisplayFields() const
{
    QVector<NodeResultField> fields = HalconNode::resultDisplayFields();
    fields << resultValue(QStringLiteral("lineAngle"), QStringLiteral("角度"),
                          QStringLiteral("\u00B0"), 2);
    return fields;
}
