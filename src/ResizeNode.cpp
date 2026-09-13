#include "ResizeNode.h"
#include "DataObject.h"
#include "OpencvUtil.h"
#include <opencv2/imgproc.hpp>

using namespace HalconCpp;

ResizeNode::ResizeNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("图像缩放"));
    m_type = IMAGE_PROCESSING;
}

void ResizeNode::init()
{
    HalconNode::init();
    registerParams({
        makeDoubleParam(QStringLiteral("scaleX"), 1.0, 0.01, 100.0,
                        QStringLiteral("宽度比例")),
        makeDoubleParam(QStringLiteral("scaleY"), 1.0, 0.01, 100.0,
                        QStringLiteral("高度比例")),
        makeEnumParam(QStringLiteral("interpolation"), 1,
                      {QStringLiteral("最近邻"), QStringLiteral("双线性"), QStringLiteral("常数"), QStringLiteral("加权")},
                      QStringLiteral("插值方式")),
    });
}

void ResizeNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        cv::Mat src = OpencvUtil::himageToMat(HImage(m_inputImage));
        if (src.empty()) {
            m_outputImage.Clear();
            return;
        }
        const double sx = qBound(0.01, m_params.value(QStringLiteral("scaleX"), 1.0).toDouble(), 100.0);
        const double sy = qBound(0.01, m_params.value(QStringLiteral("scaleY"), 1.0).toDouble(), 100.0);
        const int idx = qBound(0, m_params.value(QStringLiteral("interpolation"), 1).toInt(), 3);
        int interp = cv::INTER_LINEAR;
        if (idx == 0) interp = cv::INTER_NEAREST;
        else if (idx == 2) interp = cv::INTER_AREA;
        else if (idx == 3) interp = cv::INTER_CUBIC;
        cv::Mat dst;
        cv::resize(src, dst, cv::Size(), sx, sy, interp);
        m_outputImage = OpencvUtil::matToHimage(dst);
    } catch (const std::exception &) {
        m_outputImage.Clear();
    }
}
