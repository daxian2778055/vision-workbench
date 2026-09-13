#include "PointCircleDistanceNode.h"
#include "DataObject.h"
#include "Port.h"
#include <cmath>

using namespace HalconCpp;

PointCircleDistanceNode::PointCircleDistanceNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("点圆距离"));
    m_type = MEASUREMENT;
}

void PointCircleDistanceNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("测量结果"), PortDataType::Measure);
    registerParams({
        makeDoubleParam(QStringLiteral("pRow"), 100.0, 0.0, 100000.0, QStringLiteral("点行"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("pCol"), 100.0, 0.0, 100000.0, QStringLiteral("点列"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("cRow"), 150.0, 0.0, 100000.0, QStringLiteral("圆心行"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("cCol"), 150.0, 0.0, 100000.0, QStringLiteral("圆心列"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("radius"), 40.0, 0.0, 100000.0, QStringLiteral("半径"), QStringLiteral("px")),
    });
}

void PointCircleDistanceNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        auto pv = [this](const QString &n, double d) { return m_params.value(n, d).toDouble(); };
        const double pr = pv(QStringLiteral("pRow"), 100), pc = pv(QStringLiteral("pCol"), 100);
        const double cr = pv(QStringLiteral("cRow"), 150), cc = pv(QStringLiteral("cCol"), 150);
        const double rad = pv(QStringLiteral("radius"), 40);

        const double d = std::sqrt((pr - cr) * (pr - cr) + (pc - cc) * (pc - cc));
        const double dist = std::fabs(d - rad);

        MeasureResult res;
        res.type = QStringLiteral("point_circle");
        res.valueName = QStringLiteral("距离");
        res.valid = true;
        res.value = dist;
        res.point1 = QPointF(pc, pr);
        res.point2 = QPointF(cc, cr);

        auto resObj = QSharedPointer<DataObject>::create();
        resObj->setMeasureResult(res);
        setOutputData(1, resObj);

        m_outputImage = m_inputImage;
    } catch (const std::exception &) {
        m_outputImage.Clear();
        setOutputData(1, QSharedPointer<DataObject>());
    }
}
