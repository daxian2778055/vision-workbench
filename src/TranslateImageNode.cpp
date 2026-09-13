#include "TranslateImageNode.h"
#include "DataObject.h"
#include "OpencvUtil.h"
#include <opencv2/imgproc.hpp>

using namespace HalconCpp;

TranslateImageNode::TranslateImageNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("图像平移"));
    m_type = IMAGE_PROCESSING;
}

void TranslateImageNode::init()
{
    HalconNode::init();
    registerParams({
        makeDoubleParam(QStringLiteral("row"), 0.0, -100000, 100000,
                        QStringLiteral("行偏移"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("column"), 0.0, -100000, 100000,
                        QStringLiteral("列偏移"), QStringLiteral("px")),
    });
}

void TranslateImageNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        cv::Mat src = OpencvUtil::himageToMat(HImage(m_inputImage));
        if (src.empty()) {
            m_outputImage.Clear();
            return;
        }
        const double row = m_params.value(QStringLiteral("row"), 0.0).toDouble();
        const double col = m_params.value(QStringLiteral("column"), 0.0).toDouble();
        cv::Mat M = (cv::Mat_<double>(2, 3) << 1, 0, col, 0, 1, row);
        cv::Mat dst;
        cv::warpAffine(src, dst, M, src.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT);
        m_outputImage = OpencvUtil::matToHimage(dst);
    } catch (const std::exception &) {
        m_outputImage.Clear();
    }
}
