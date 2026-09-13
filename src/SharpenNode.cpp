#include "SharpenNode.h"
#include "DataObject.h"
#include "OpencvUtil.h"
#include <opencv2/imgproc.hpp>

using namespace HalconCpp;

SharpenNode::SharpenNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("图像锐化"));
    m_type = IMAGE_PROCESSING;
}

void SharpenNode::init()
{
    HalconNode::init();
    registerParams({
        makeDoubleParam(QStringLiteral("weight"), 1.0, 0.0, 10.0,
                        QStringLiteral("锐化强度")),
        makeEnumParam(QStringLiteral("maskSize"), 3,
                      {QStringLiteral("3"), QStringLiteral("5"), QStringLiteral("7"), QStringLiteral("9"), QStringLiteral("11"), QStringLiteral("13")},
                      QStringLiteral("模板尺寸")),
    });
}

void SharpenNode::run(bool)
{
    // OpenCV 实现（HALCON AddImage 在替换版环境数值级验证错误，改用 OpenCV 拉普拉斯锐化）
    try {
        if (!m_inputImage.IsInitialized()) return;
        cv::Mat src = OpencvUtil::himageToMat(HImage(m_inputImage));
        if (src.empty()) {
            m_params["moduleStatus"] = false;
            return;
        }
        if (src.channels() == 3) cv::cvtColor(src, src, cv::COLOR_BGR2GRAY);

        const double w = m_params.value(QStringLiteral("weight"), 1.0).toDouble();
        const int masks[] = {3, 5, 7, 9, 11, 13};
        const int idx = qBound(0, m_params.value(QStringLiteral("maskSize"), 3).toInt(), 5);

        cv::Mat lap;
        cv::Laplacian(src, lap, CV_16S, masks[idx]);
        cv::Mat scaled;
        lap.convertTo(scaled, CV_8U, w, 0.0);
        cv::Mat out;
        cv::add(src, scaled, out);
        m_outputImage = OpencvUtil::matToHimage(out);
    } catch (const HException &) {
        m_outputImage.Clear();
    } catch (const std::exception &) {
        m_outputImage.Clear();
        m_params["moduleStatus"] = false;
    }
}
