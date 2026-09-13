#include "ImageAddNode.h"
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

ImageAddNode::ImageAddNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("图像加法"));
    m_type = IMAGE_PROCESSING;
}

void ImageAddNode::init()
{
    HalconNode::init();
    addInputPort(QStringLiteral("图像2"));
    registerParams({
        makeDoubleParam(QStringLiteral("value"), 0.0, -255.0, 255.0,
                        QStringLiteral("加数（未连接图像2时使用）")),
    });
}

void ImageAddNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        const double v = m_params.value(QStringLiteral("value"), 0.0).toDouble();
        HObject result;
        if (m_inputData.contains(1) && m_inputData[1] && m_inputData[1]->getHImage().IsInitialized()) {
            AddImage(HImage(m_inputImage), m_inputData[1]->getHImage(), &result, 1.0, 0.0);
        } else {
            AddImage(HImage(m_inputImage), HImage(m_inputImage), &result, 1.0, v);
        }
        m_outputImage = result;
    } catch (const HException &) {
        m_outputImage.Clear();
    }
}
