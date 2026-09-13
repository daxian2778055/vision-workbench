#include "PositionCorrectNode.h"
#include "DataObject.h"
#include "Port.h"
#include "FlowScene.h"
#include "CalibrationManager.h"
#include <cmath>

PositionCorrectNode::PositionCorrectNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("位置修正"));
    m_type = MEASUREMENT;
}

void PositionCorrectNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("结果"), PortDataType::Point);
    addOutputPort(QStringLiteral("坐标"), PortDataType::String);
    registerParams({
        makeStringParam(QStringLiteral("fixtureName"), QString(),
                        QStringLiteral("Fixture 名（空=手填参数）")),
        makeDoubleParam(QStringLiteral("srcX"), 100.0, -100000.0, 100000.0, QStringLiteral("源X"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("srcY"), 100.0, -100000.0, 100000.0, QStringLiteral("源Y"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("angle"), 0.0, -360.0, 360.0, QStringLiteral("旋转角"), QStringLiteral("°")),
        makeDoubleParam(QStringLiteral("scale"), 1.0, 0.001, 1000.0, QStringLiteral("缩放")),
        makeDoubleParam(QStringLiteral("offsetX"), 0.0, -100000.0, 100000.0, QStringLiteral("偏移X"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("offsetY"), 0.0, -100000.0, 100000.0, QStringLiteral("偏移Y"), QStringLiteral("px")),
    });
}

void PositionCorrectNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        auto pv = [this](const QString &n, double d) { return m_params.value(n, d).toDouble(); };
        double sx = pv(QStringLiteral("srcX"), 100);
        double sy = pv(QStringLiteral("srcY"), 100);
        double angleDeg = pv(QStringLiteral("angle"), 0);
        double scale = pv(QStringLiteral("scale"), 1);
        double ox = pv(QStringLiteral("offsetX"), 0);
        double oy = pv(QStringLiteral("offsetY"), 0);

        const QString fixtureName =
            m_params.value(QStringLiteral("fixtureName")).toString().trimmed();
        if (!fixtureName.isEmpty()) {
            FlowFixture fx;
            if (FlowScene *fs = flowSceneRef())
                fx = fs->fixture(fixtureName);
            if (fx.hasPose) {
                sx = fx.poseCol;
                sy = fx.poseRow;
                angleDeg = fx.poseAngle;
                scale = fx.poseScale;
            }
            QVector<double> hom = fx.hasHom ? fx.hom
                                            : CalibrationManager::instance()->homography(fixtureName);
            if (hom.size() >= 6) {
                const QPointF w = CalibrationManager::applyHomography(hom, sx, sy);
                sx = w.x();
                sy = w.y();
                ox = 0;
                oy = 0;
                angleDeg = 0;
                scale = 1;
            }
        }
        const double angle = angleDeg * 3.14159265358979323846 / 180.0;

        const double c = std::cos(angle);
        const double s = std::sin(angle);
        const double qx = scale * (c * sx - s * sy) + ox;
        const double qy = scale * (s * sx + c * sy) + oy;

        auto ptObj = QSharedPointer<DataObject>::create();
        ptObj->setPoint(QPointF(qx, qy));
        setOutputData(1, ptObj);

        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(QStringLiteral("修正坐标: (%1, %2)").arg(qx, 0, 'f', 2).arg(qy, 0, 'f', 2));
        setOutputData(2, strObj);

        m_outputImage = m_inputImage;
    } catch (const std::exception &) {
        m_outputImage.Clear();
        setOutputData(1, QSharedPointer<DataObject>());
        setOutputData(2, QSharedPointer<DataObject>());
    }
}
