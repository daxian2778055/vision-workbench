#include "HistogramEqualizeNode.h"
#include "DataObject.h"
#include "OpencvUtil.h"
#include <opencv2/imgproc.hpp>

using namespace HalconCpp;

HistogramEqualizeNode::HistogramEqualizeNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("直方图均衡"));
    m_type = IMAGE_PROCESSING;
}

void HistogramEqualizeNode::init()
{
    HalconNode::init();
}

void HistogramEqualizeNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        cv::Mat src = OpencvUtil::himageToMat(HImage(m_inputImage));
        if (src.empty()) {
            m_outputImage.Clear();
            return;
        }
        cv::Mat gray, dst;
        if (src.channels() == 3)
            cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
        else
            gray = src;
        cv::equalizeHist(gray, dst);
        m_outputImage = OpencvUtil::matToHimage(dst);
    } catch (const std::exception &) {
        m_outputImage.Clear();
    }
}
