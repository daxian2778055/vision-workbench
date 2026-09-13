#include "ImageMulNode.h"
#include "DataObject.h"

using namespace HalconCpp;

static HImage makeConstantImageLike(const HImage &proto, double value)
{
    HTuple w, h;
    GetImageSize(proto, &w, &h);
    HImage zeros, res;
    GenImageConst(&zeros, "byte", w, h);
    AddImage(zeros, zeros, &res, 1.0, value);
    return res;
}

ImageMulNode::ImageMulNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("图像乘法"));
    m_type = IMAGE_PROCESSING;
}

void ImageMulNode::init()
{
    HalconNode::init();
    addInputPort(QStringLiteral("图像2"));
    registerParams({
        makeDoubleParam(QStringLiteral("value"), 1.0, 0.0, 255.0,
                        QStringLiteral("乘数（未连接图像2时使用）")),
    });
}

void ImageMulNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        const double v = m_params.value(QStringLiteral("value"), 1.0).toDouble();
        HObject result;
        if (m_inputData.contains(1) && m_inputData[1] && m_inputData[1]->getHImage().IsInitialized()) {
            MultImage(HImage(m_inputImage), m_inputData[1]->getHImage(), &result, 1.0, 0.0);
        } else {
            HImage constImg = makeConstantImageLike(HImage(m_inputImage), v);
            MultImage(HImage(m_inputImage), constImg, &result, 1.0, 0.0);
        }
        m_outputImage = result;
    } catch (const HException &) {
        m_outputImage.Clear();
    }
}
