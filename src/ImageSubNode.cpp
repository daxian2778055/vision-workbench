#include "ImageSubNode.h"
#include "DataObject.h"
#include "OpencvUtil.h"
#include <opencv2/core.hpp>

using namespace HalconCpp;

ImageSubNode::ImageSubNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("图像减法"));
    m_type = IMAGE_PROCESSING;
}

void ImageSubNode::init()
{
    HalconNode::init();
    addInputPort(QStringLiteral("图像2"));
    registerParams({
        makeDoubleParam(QStringLiteral("value"), 0.0, -255.0, 255.0,
                        QStringLiteral("减数（未连接图像2时使用）")),
    });
}

void ImageSubNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        cv::Mat a = OpencvUtil::himageToMat(HImage(m_inputImage));
        if (a.empty()) {
            m_outputImage.Clear();
            return;
        }
        cv::Mat dst;
        if (m_inputData.contains(1) && m_inputData[1] && m_inputData[1]->getHImage().IsInitialized()) {
            cv::Mat b = OpencvUtil::himageToMat(m_inputData[1]->getHImage());
            if (b.empty() || b.size() != a.size()) {
                m_outputImage.Clear();
                return;
            }
            if (b.type() != a.type())
                b.convertTo(b, a.type());
            cv::subtract(a, b, dst);
        } else {
            const double v = m_params.value(QStringLiteral("value"), 0.0).toDouble();
            cv::subtract(a, cv::Scalar::all(v), dst);
        }
        m_outputImage = OpencvUtil::matToHimage(dst);
    } catch (const std::exception &) {
        m_outputImage.Clear();
    }
}
