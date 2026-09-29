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
    // 结果字段（不进参数面板）：判红时输出端口会被 process() 清空，原因只能留在这里
    m_params[QStringLiteral("correctNote")] = QString();
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
        m_params[QStringLiteral("correctNote")] = QString();
        if (!fixtureName.isEmpty()) {
            FlowScene *fs = flowSceneRef();
            const FlowFixture fx = fs ? fs->fixture(fixtureName) : FlowFixture();
            if (fx.hasPose) {
                sx = fx.poseCol;
                sy = fx.poseRow;
                angleDeg = fx.poseAngle;
                scale = fx.poseScale;
            }
            const QVector<double> hom = fx.hasHom ? fx.hom
                                            : CalibrationManager::instance()->homography(fixtureName);
            if (hom.size() != 6) {
                // 手填的 srcX/srcY/angle/scale/offset 与 R-2 那边的默认单位阵不同，它不是恒等；
                // 但同样是「该有而没有时不报」：声明了要用夹具却没有 6 元矩阵，继续跑就是把像素值
                // （或夹具位姿自带的角／缩放）当成已修正的物理坐标吐给下游且判绿。⇒ 判红，绝不回退手填。
                // R-5：`< 6` 只卡下限 ⇒ 同键的 9 元 OpenCV 内参"够 6"就被当前 6 元仿射用
                // （applyHomography 取 hom[0..5]），实测输出 78220.000,23977.620 且判绿。⇒ 要求恰好 6 项。
                // U-28：判据与文案只有一份（CalibrationManager::affineLookupMissReason），别再抄第二份。
                m_params[QStringLiteral("correctNote")] = CalibrationManager::affineLookupMissReason(
                    QStringLiteral("位置修正"), QStringLiteral("srcX/srcY/angle/scale/offset"), fixtureName,
                    fs != nullptr, !fx.name.isEmpty(), fx.hasHom, hom.size());
                m_params["moduleStatus"] = false;
                return;
            }
            const QPointF w = CalibrationManager::applyHomography(hom, sx, sy);
            sx = w.x();
            sy = w.y();
            ox = 0;
            oy = 0;
            angleDeg = 0;
            scale = 1;
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
