#include "GrayStretchNode.h"
#include "DataObject.h"
#include "OpencvUtil.h"
#include <opencv2/imgproc.hpp>

using namespace HalconCpp;

GrayStretchNode::GrayStretchNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("灰度拉伸"));
    m_type = IMAGE_PROCESSING;
}

void GrayStretchNode::init()
{
    HalconNode::init();
    // 自动按图像实际灰度范围拉伸到 0~255
}

void GrayStretchNode::run(bool)
{
    // OpenCV 实现（HALCON MinMaxGray 在替换版环境数值级验证垃圾输出，改用 OpenCV 灰度拉伸）
    try {
        if (!m_inputImage.IsInitialized()) return;
        cv::Mat img = OpencvUtil::himageToMat(HImage(m_inputImage));
        if (img.empty()) {
            m_params["moduleStatus"] = false;
            return;
        }
        if (img.channels() == 3) cv::cvtColor(img, img, cv::COLOR_BGR2GRAY);

        double mn = 0.0, mx = 0.0;
        cv::minMaxLoc(img, &mn, &mx);
        double range = mx - mn;
        if (range <= 0.0) range = 1.0;
        cv::Mat out;
        cv::convertScaleAbs(img, out, 255.0 / range, -mn * 255.0 / range);
        m_outputImage = OpencvUtil::matToHimage(out);
    } catch (const HException &) {
        m_outputImage.Clear();
    } catch (const std::exception &) {
        m_outputImage.Clear();
        m_params["moduleStatus"] = false;
    }
}
