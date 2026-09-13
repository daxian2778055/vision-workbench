#include "CropNode.h"
#include "DataObject.h"

using namespace HalconCpp;

CropNode::CropNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("ROI裁剪"));
    m_type = IMAGE_PROCESSING;
}

void CropNode::init()
{
    HalconNode::init();
    registerParams({
        makeDoubleParam(QStringLiteral("x"), 0.0, 0.0, 100000,
                        QStringLiteral("起点X"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("y"), 0.0, 0.0, 100000,
                        QStringLiteral("起点Y"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("width"), 100.0, 1.0, 100000,
                        QStringLiteral("宽度"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("height"), 100.0, 1.0, 100000,
                        QStringLiteral("高度"), QStringLiteral("px")),
    });
}

void CropNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        const double x = m_params.value(QStringLiteral("x"), 0.0).toDouble();
        const double y = m_params.value(QStringLiteral("y"), 0.0).toDouble();
        const double w = m_params.value(QStringLiteral("width"), 100.0).toDouble();
        const double h = m_params.value(QStringLiteral("height"), 100.0).toDouble();

        // 真正的 ROI 裁剪：输出裁剪后的新尺寸图像（CropPart 按 行/列/宽/高 截取）
        HObject cropped;
        CropPart(HImage(m_inputImage), &cropped, y, x, w, h);
        m_outputImage = cropped;
    } catch (const HException &) {
        m_outputImage.Clear();
    }
}
