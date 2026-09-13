#include "ContrastStretchNode.h"
#include "DataObject.h"
#include "OpencvUtil.h"
#include <opencv2/core.hpp>

using namespace HalconCpp;

ContrastStretchNode::ContrastStretchNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("对比度拉伸"));
    m_type = IMAGE_PROCESSING;
}

void ContrastStretchNode::init()
{
    HalconNode::init();
}

void ContrastStretchNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        cv::Mat src = OpencvUtil::himageToMat(HImage(m_inputImage));
        if (src.empty()) {
            m_outputImage.Clear();
            return;
        }
        cv::Mat dst;
        cv::normalize(src, dst, 0, 255, cv::NORM_MINMAX);
        m_outputImage = OpencvUtil::matToHimage(dst);
    } catch (const std::exception &) {
        m_outputImage.Clear();
    }
}
