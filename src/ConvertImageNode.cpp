#include "ConvertImageNode.h"
#include "DataObject.h"
#include "OpencvUtil.h"
#include <opencv2/core.hpp>

using namespace HalconCpp;

ConvertImageNode::ConvertImageNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("类型转换"));
    m_type = IMAGE_PROCESSING;
}

void ConvertImageNode::init()
{
    HalconNode::init();
    registerParams({
        makeEnumParam(QStringLiteral("targetType"), 0,
                      {QStringLiteral("byte"), QStringLiteral("int1"), QStringLiteral("int2"),
                       QStringLiteral("uint2"), QStringLiteral("int4"), QStringLiteral("real")},
                      QStringLiteral("目标类型")),
    });
}

void ConvertImageNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        cv::Mat src = OpencvUtil::himageToMat(HImage(m_inputImage));
        if (src.empty()) {
            m_outputImage.Clear();
            return;
        }
        const int idx = qBound(0, m_params.value(QStringLiteral("targetType"), 0).toInt(), 5);
        int dtype = CV_8U;
        switch (idx) {
        case 1: dtype = CV_8S; break;
        case 2: dtype = CV_16S; break;
        case 3: dtype = CV_16U; break;
        case 4: dtype = CV_32S; break;
        case 5: dtype = CV_32F; break;
        default: dtype = CV_8U; break;
        }
        cv::Mat conv;
        src.convertTo(conv, dtype);
        m_outputImage = OpencvUtil::matToHimage(conv);
    } catch (const std::exception &) {
        m_outputImage.Clear();
    }
}
