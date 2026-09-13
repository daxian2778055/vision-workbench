#include "PointLineDistanceNode.h"
#include "DataObject.h"
#include "Port.h"
#include <cmath>

PointLineDistanceNode::PointLineDistanceNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("点线距离"));
    m_type = MEASUREMENT;
}

void PointLineDistanceNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("测量结果"), PortDataType::Measure);
    registerParams({
        makeDoubleParam(QStringLiteral("pRow"), 100.0, 0.0, 100000.0, QStringLiteral("点行"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("pCol"), 100.0, 0.0, 100000.0, QStringLiteral("点列"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("lRow1"), 50.0, 0.0, 100000.0, QStringLiteral("线起点行"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("lCol1"), 50.0, 0.0, 100000.0, QStringLiteral("线起点列"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("lRow2"), 150.0, 0.0, 100000.0, QStringLiteral("线终点行"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("lCol2"), 200.0, 0.0, 100000.0, QStringLiteral("线终点列"), QStringLiteral("px")),
    });
}

void PointLineDistanceNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        auto pv = [this](const QString &n, double d) { return m_params.value(n, d).toDouble(); };
        const double pr = pv(QStringLiteral("pRow"), 100);
        const double pc = pv(QStringLiteral("pCol"), 100);
        const double r1 = pv(QStringLiteral("lRow1"), 50);
        const double c1 = pv(QStringLiteral("lCol1"), 50);
        const double r2 = pv(QStringLiteral("lRow2"), 150);
        const double c2 = pv(QStringLiteral("lCol2"), 200);

        const double dx = c2 - c1;
        const double dy = r2 - r1;
        const double len = std::hypot(dx, dy);
        const double dist = (len < 1e-12)
            ? std::hypot(pc - c1, pr - r1)
            : std::fabs(dx * (pr - r1) - dy * (pc - c1)) / len;

        MeasureResult res;
        res.type = QStringLiteral("point_line");
        res.valueName = QStringLiteral("距离");
        res.valid = true;
        res.value = dist;
        res.point1 = QPointF(pc, pr);
        res.point2 = QPointF(c1, r1);

        auto resObj = QSharedPointer<DataObject>::create();
        resObj->setMeasureResult(res);
        setOutputData(1, resObj);

        m_outputImage = m_inputImage;
    } catch (const std::exception &) {
        m_outputImage.Clear();
        setOutputData(1, QSharedPointer<DataObject>());
    }
}
