#include "ImageDivNode.h"
#include "DataObject.h"
#include "OpencvUtil.h"
#include <opencv2/core.hpp>
#include <QtGlobal>

using namespace HalconCpp;

ImageDivNode::ImageDivNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("图像除法"));
    m_type = IMAGE_PROCESSING;
}

void ImageDivNode::init()
{
    HalconNode::init();
    addInputPort(QStringLiteral("图像2"));
    registerParams({
        makeDoubleParam(QStringLiteral("value"), 1.0, 0.0001, 255.0,
                        QStringLiteral("除数（未连接图像2时使用）")),
    });
}

void ImageDivNode::run(bool)
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
            cv::Mat mask = (b == 0);
            b.setTo(1, mask);
            cv::divide(a, b, dst);
        } else {
            const double v = m_params.value(QStringLiteral("value"), 1.0).toDouble();
            if (qFuzzyIsNull(v)) {
                m_outputImage.Clear();
                m_params[QStringLiteral("moduleStatus")] = false;
                return;
            }
            cv::divide(a, v, dst);
        }
        m_outputImage = OpencvUtil::matToHimage(dst);
    } catch (const std::exception &) {
        m_outputImage.Clear();
    }
}
