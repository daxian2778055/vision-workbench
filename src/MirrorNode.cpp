#include "MirrorNode.h"
#include "DataObject.h"
#include "OpencvUtil.h"
#include <opencv2/core.hpp>

using namespace HalconCpp;

MirrorNode::MirrorNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("图像镜像"));
    m_type = IMAGE_PROCESSING;
}

void MirrorNode::init()
{
    HalconNode::init();
    registerParams({
        makeEnumParam(QStringLiteral("mode"), 0,
                      {QStringLiteral("水平翻转"), QStringLiteral("垂直翻转"), QStringLiteral("对角翻转")},
                      QStringLiteral("镜像方式")),
    });
}

void MirrorNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        cv::Mat src = OpencvUtil::himageToMat(HImage(m_inputImage));
        if (src.empty()) {
            m_outputImage.Clear();
            return;
        }
        const int idx = qBound(0, m_params.value(QStringLiteral("mode"), 0).toInt(), 2);
        cv::Mat dst;
        if (idx == 0)
            cv::flip(src, dst, 1);
        else if (idx == 1)
            cv::flip(src, dst, 0);
        else
            cv::flip(src, dst, -1);
        m_outputImage = OpencvUtil::matToHimage(dst);
    } catch (const std::exception &) {
        m_outputImage.Clear();
    }
}
