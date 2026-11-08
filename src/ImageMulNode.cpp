#include "ImageMulNode.h"
#include "DataObject.h"
#include "OpencvUtil.h"

using namespace HalconCpp;

ImageMulNode::ImageMulNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("图像乘法"));
    m_type = IMAGE_PROCESSING;
}

void ImageMulNode::init()
{
    HalconNode::init();
    // B2：图像2 不画输入点，改为逻辑槽（订阅绑定注入）
    addInputSlot(QStringLiteral("图像2"), PortDataType::Image);
    registerParams({
        makeDoubleParam(QStringLiteral("value"), 1.0, 0.0, 255.0,
                        QStringLiteral("乘数（未连接图像2时使用）")),
    });
}

void ImageMulNode::run(bool)
{
    // 纯 OpenCV 实现（2026-11-08）：逐像素乘法，cv::multiply 对整数类型同样是
    // 饱和运算（saturate_cast），与 HALCON MultImage 的类型饱和语义一致。
    if (!m_inputImage.IsInitialized()) {
        m_outputImage.Clear();
        return;
    }
    const cv::Mat src = OpencvUtil::himageToMat(m_inputImage);
    if (src.empty()) {
        m_outputImage.Clear();
        return;
    }
    const double v = m_params.value(QStringLiteral("value"), 1.0).toDouble();
    const bool hasSecond = m_inputData.contains(1) && m_inputData[1]
                           && m_inputData[1]->getHImage().IsInitialized();
    cv::Mat dst;
    if (hasSecond) {
        cv::Mat other = OpencvUtil::himageToMat(m_inputData[1]->getHImage());
        if (other.empty() || other.size() != src.size()) {
            m_outputImage.Clear();
            return;
        }
        if (other.type() != src.type())
            other.convertTo(other, src.type());
        cv::multiply(src, other, dst);   // 饱和
    } else {
        cv::multiply(src, cv::Scalar::all(v), dst);
    }
    m_outputImage = OpencvUtil::matToHimage(dst);
}
