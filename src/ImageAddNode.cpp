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
            // 未接第二输入时与**常量图**相加（2026-10-26 复核 P3）：原来写
            // AddImage(I, I, 1.0, v) = 2I+v，与参数标签"加数"不符（整幅亮一倍）。
            // 常量图构造 makeConstantImageLike 早就在本文件里，之前没被用。
            AddImage(HImage(m_inputImage), makeConstantImageLike(HImage(m_inputImage), v),
                     &result, 1.0, 0.0);
        }
        m_outputImage = result;
    } catch (const HException &) {
        m_outputImage.Clear();
    }
}
