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
    // 结果字段（不进参数面板）：判红时输出端口会被 process() 清空，原因只能留在这里
    m_params[QStringLiteral("transformNote")] = QString();
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
        m_params[QStringLiteral("transformNote")] = QString();
        if (!fixtureName.isEmpty()) {
            FlowScene *fs = flowSceneRef();
            const FlowFixture fx = fs ? fs->fixture(fixtureName) : FlowFixture();
            const QVector<double> hom =
                fx.hasHom ? fx.hom : CalibrationManager::instance()->homography(fixtureName);
            if (hom.size() != 6) {
                // 手填默认值（m11=m22=1、其余 0）正好是恒等矩阵 ⇒ 继续跑就是"把像素原值
                // 当物理坐标输出且判绿"。声明了要用夹具却没有 6 元矩阵，只能判红，绝不回退手填。
                // R-5：过去只卡 `< 6`，而同一张标定表里还住着 9 元 OpenCV 内参
                // （fx fy cx cy k1 k2 p1 p2 rms）⇒ "够 6"就把 fx/fy/cx/cy/k1/k2 当仿射算，
                // 实测输出 78220.000,23977.620 且判绿。要求**恰好 6 项**才是仿射。
                QString sceneWhy;
                if (!fx.hasHom) {
                    if (!fs)
                        sceneWhy = QStringLiteral("节点未挂到场景");
                    else if (fx.name.isEmpty())
                        sceneWhy = QStringLiteral("场景里没有该夹具");
                    else
                        sceneWhy = QStringLiteral("该夹具没有矩阵（只有位姿）");
                }
                const QString sizeWhy =
                    hom.size() < 6
                        ? QStringLiteral("只有 %1 项").arg(hom.size())
                        : QStringLiteral("有 %1 项（不是 6 元仿射，疑似另一种载荷：9 元内参）").arg(hom.size());
                QString mgrWhy;
                if (fx.hasHom)
                    mgrWhy = QStringLiteral("场景夹具的矩阵%1").arg(sizeWhy);
                else if (CalibrationManager::instance()->hasHomography(fixtureName))
                    mgrWhy = QStringLiteral("标定单例里该键%1").arg(sizeWhy);
                else
                    mgrWhy = QStringLiteral("标定单例里没有这个键");
                const QString why = sceneWhy.isEmpty()
                    ? QStringLiteral("坐标系换算取不到 6 元矩阵：夹具 \"%1\"——%2，不回退手填 M11..M23")
                          .arg(fixtureName, mgrWhy)
                    : QStringLiteral("坐标系换算取不到 6 元矩阵：夹具 \"%1\"——%2；%3，不回退手填 M11..M23")
                          .arg(fixtureName, sceneWhy, mgrWhy);
                m_params[QStringLiteral("transformNote")] = why;
                m_params["moduleStatus"] = false;
                return;
            }
            m11 = hom[0]; m12 = hom[1]; m13 = hom[2];
            m21 = hom[3]; m22 = hom[4]; m23 = hom[5];

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
