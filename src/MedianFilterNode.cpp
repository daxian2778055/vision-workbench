#include "MedianFilterNode.h"
#include "DataObject.h"
#include "OpencvUtil.h"
#include <opencv2/imgproc.hpp>

using namespace HalconCpp;

MedianFilterNode::MedianFilterNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("中值滤波"));
    m_type = IMAGE_PROCESSING;
}

void MedianFilterNode::init()
{
    HalconNode::init();
    registerParams({
        makeIntParam(QStringLiteral("radius"), 1, 1, 50,
                     QStringLiteral("半径"), QStringLiteral("px")),
        makeEnumParam(QStringLiteral("maskType"), 0,
                      {QStringLiteral("圆形"), QStringLiteral("方形"), QStringLiteral("八边形"), QStringLiteral("菱形")},
                      QStringLiteral("掩码类型")),
    });
}

void MedianFilterNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        cv::Mat src = OpencvUtil::himageToMat(HImage(m_inputImage));
        if (src.empty()) {
            m_outputImage.Clear();
            return;
        }
        const int radius = qBound(1, m_params.value(QStringLiteral("radius"), 1).toInt(), 50);
        int ksize = radius * 2 + 1;
        if (ksize % 2 == 0)
            ++ksize;
        cv::Mat dst;
        cv::medianBlur(src, dst, ksize);
        m_outputImage = OpencvUtil::matToHimage(dst);
    } catch (const std::exception &) {
        m_outputImage.Clear();
    }
}
