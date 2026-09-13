#include "CoordinateTransformNode.h"
#include "DataObject.h"
#include "Port.h"
#include "FlowScene.h"
#include "CalibrationManager.h"

CoordinateTransformNode::CoordinateTransformNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("坐标系变换"));
    m_type = MEASUREMENT;
}

void CoordinateTransformNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("结果"), PortDataType::Point);
    addOutputPort(QStringLiteral("坐标"), PortDataType::String);
    registerParams({
        makeStringParam(QStringLiteral("fixtureName"), QString(),
                        QStringLiteral("Fixture 名（空=手填矩阵）")),
        makeBoolParam(QStringLiteral("useFixturePose"), true,
                      QStringLiteral("用 Fixture 匹配位姿作为输入点")),
        makeDoubleParam(QStringLiteral("x"), 0.0, -100000.0, 100000.0, QStringLiteral("X"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("y"), 0.0, -100000.0, 100000.0, QStringLiteral("Y"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("m11"), 1.0, -1000.0, 1000.0, QStringLiteral("M11")),
        makeDoubleParam(QStringLiteral("m12"), 0.0, -1000.0, 1000.0, QStringLiteral("M12")),
        makeDoubleParam(QStringLiteral("m13"), 0.0, -100000.0, 100000.0, QStringLiteral("M13 (Tx)")),
        makeDoubleParam(QStringLiteral("m21"), 0.0, -1000.0, 1000.0, QStringLiteral("M21")),
        makeDoubleParam(QStringLiteral("m22"), 1.0, -1000.0, 1000.0, QStringLiteral("M22")),
        makeDoubleParam(QStringLiteral("m23"), 0.0, -100000.0, 100000.0, QStringLiteral("M23 (Ty)")),
    });
}

void CoordinateTransformNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        auto pv = [this](const QString &n, double d) { return m_params.value(n, d).toDouble(); };
        double x = pv(QStringLiteral("x"), 0);
        double y = pv(QStringLiteral("y"), 0);
        double m11 = pv(QStringLiteral("m11"), 1);
        double m12 = pv(QStringLiteral("m12"), 0);
        double m13 = pv(QStringLiteral("m13"), 0);
        double m21 = pv(QStringLiteral("m21"), 0);
        double m22 = pv(QStringLiteral("m22"), 1);
        double m23 = pv(QStringLiteral("m23"), 0);

        const QString fixtureName =
            m_params.value(QStringLiteral("fixtureName")).toString().trimmed();
        if (!fixtureName.isEmpty()) {
            FlowFixture fx;
            if (FlowScene *fs = flowSceneRef())
                fx = fs->fixture(fixtureName);
            QVector<double> hom = fx.hasHom ? fx.hom
                                            : CalibrationManager::instance()->homography(fixtureName);
            if (hom.size() >= 6) {
                m11 = hom[0]; m12 = hom[1]; m13 = hom[2];
                m21 = hom[3]; m22 = hom[4]; m23 = hom[5];
            }
            if (m_params.value(QStringLiteral("useFixturePose"), true).toBool() && fx.hasPose) {
                x = fx.poseCol;
                y = fx.poseRow;
            }
        }

        const double qx = m11 * x + m12 * y + m13;
        const double qy = m21 * x + m22 * y + m23;

        auto ptObj = QSharedPointer<DataObject>::create();
        ptObj->setPoint(QPointF(qx, qy));
        setOutputData(1, ptObj);

        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(QStringLiteral("变换坐标: (%1, %2)").arg(qx, 0, 'f', 2).arg(qy, 0, 'f', 2));
        setOutputData(2, strObj);

        m_outputImage = m_inputImage;
    } catch (const std::exception &) {
        m_outputImage.Clear();
        setOutputData(1, QSharedPointer<DataObject>());
        setOutputData(2, QSharedPointer<DataObject>());
    }
}
