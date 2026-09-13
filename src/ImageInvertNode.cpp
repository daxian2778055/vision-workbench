#include "ImageInvertNode.h"
#include "DataObject.h"
#include "OpencvUtil.h"
#include <opencv2/core.hpp>

using namespace HalconCpp;

ImageInvertNode::ImageInvertNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("图像求反"));
    m_type = IMAGE_PROCESSING;
}

void ImageInvertNode::init()
{
    HalconNode::init();
}

void ImageInvertNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        cv::Mat src = OpencvUtil::himageToMat(HImage(m_inputImage));
        if (src.empty()) {
            m_outputImage.Clear();
            return;
        }
        cv::Mat dst;
        cv::bitwise_not(src, dst);
        m_outputImage = OpencvUtil::matToHimage(dst);
    } catch (const std::exception &) {
        m_outputImage.Clear();
    }
}
