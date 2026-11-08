#include "ImageAddNode.h"
#include "DataObject.h"
#include "OpencvUtil.h"

using namespace HalconCpp;

ImageAddNode::ImageAddNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("图像加法"));
    m_type = IMAGE_PROCESSING;
}

void ImageAddNode::init()
{
    HalconNode::init();
    // B2：图像2 不再画第二个输入点，改为逻辑槽（VM 基本参数里的图像输入订阅）
    addInputSlot(QStringLiteral("图像2"), PortDataType::Image);
    registerParams({
        makeDoubleParam(QStringLiteral("value"), 0.0, -255.0, 255.0,
                        QStringLiteral("加数（未连接图像2时使用）")),
    });
}

void ImageAddNode::run(bool)
{
    // 纯 OpenCV 实现（2026-11-08）：本节点原先调 HALCON AddImage，但它是纯粹的
    // 逐像素相加，cv::add 对 CV_8U/CV_16U 同样是**饱和**运算，与 HALCON 的类型
    // 饱和语义一致，因此不再需要 HALCON 算法依赖。
    if (!m_inputImage.IsInitialized()) {
        m_outputImage.Clear();
        return;
    }
    const cv::Mat src = OpencvUtil::himageToMat(m_inputImage);
    if (src.empty()) {
        m_outputImage.Clear();
        return;
    }
    const double v = m_params.value(QStringLiteral("value"), 0.0).toDouble();
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
        cv::add(src, other, dst);   // 饱和
    } else {
        // 未接第二输入：与常量相加（保持 2026-10-26 复核 P3 的语义 —— 不是 2I+v）
        cv::add(src, cv::Scalar::all(v), dst);
    }
    m_outputImage = OpencvUtil::matToHimage(dst);
}
