#include "RotateImageNode.h"
#include "DataObject.h"
#include <cmath>

using namespace HalconCpp;

RotateImageNode::RotateImageNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("图像旋转"));
    m_type = IMAGE_PROCESSING;
}

void RotateImageNode::init()
{
    HalconNode::init();
    registerParams({
        makeDoubleParam(QStringLiteral("angle"), 0.0, -360.0, 360.0,
                        QStringLiteral("旋转角度"), QStringLiteral("°")),
        makeEnumParam(QStringLiteral("interpolation"), 3,
                      {QStringLiteral("最近邻"), QStringLiteral("双线性"), QStringLiteral("常数"), QStringLiteral("加权")},
                      QStringLiteral("插值方式")),
    });
}

void RotateImageNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        const double deg = m_params.value(QStringLiteral("angle"), 0.0).toDouble();
        const double rad = deg * 3.14159265358979323846 / 180.0;
        const QStringList interp = {QStringLiteral("nearest"), QStringLiteral("bilinear"),
                                    QStringLiteral("constant"), QStringLiteral("weighted")};
        const int idx = qBound(0, m_params.value(QStringLiteral("interpolation"), 3).toInt(), 3);
        HObject result;
        RotateImage(HImage(m_inputImage), &result, HTuple(rad), HTuple(interp[idx].toStdString().c_str()));
        m_outputImage = result;
    } catch (const HException &) {
        m_outputImage.Clear();
    }
}
