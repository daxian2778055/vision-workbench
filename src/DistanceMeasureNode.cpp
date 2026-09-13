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
    });
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
