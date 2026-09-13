#include "OpencvRotateNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <QWidget>

OpencvRotateNode::OpencvRotateNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("图像旋转"));
    m_type = IMAGE_PROCESSING;
}

void OpencvRotateNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("旋转图"), PortDataType::Image);
    registerParams({
        makeDoubleParam(QStringLiteral("angle"), 0.0, -360.0, 360.0,
                        QStringLiteral("旋转角度（度，逆时针）")),
        makeIntParam(QStringLiteral("bgValue"), 0, 0, 255,
                     QStringLiteral("背景填充灰度")),
    });
}

void OpencvRotateNode::run(bool /*autoSwitch*/)
{
    try {
        HImage input(m_inputImage);
        if (!input.IsInitialized()) {
            m_outputImage.Clear();
            return;
        }
        cv::Mat src = OpencvUtil::himageToMat(input);
        if (src.empty()) {
            m_params["moduleStatus"] = false;
            return;
        }
        if (src.channels() == 3) cv::cvtColor(src, src, cv::COLOR_BGR2GRAY);

        const double angle = m_params.value(QStringLiteral("angle"), 0.0).toDouble();
        const int bg = m_params.value(QStringLiteral("bgValue"), 0).toInt();

        // 旋转中心 = 图像中心；输出保持原尺寸（与 HALCON RotateImage 不同，不扩边）
        cv::Point2f center(src.cols / 2.0f, src.rows / 2.0f);
        cv::Mat rot = cv::getRotationMatrix2D(center, angle, 1.0);
        cv::Mat out;
        cv::warpAffine(src, out, rot, src.size(), cv::INTER_LINEAR,
                       cv::BORDER_CONSTANT, cv::Scalar(bg));
        m_params["moduleStatus"] = !out.empty();
        m_outputImage = m_inputImage;
        auto outObj = QSharedPointer<DataObject>::create();
        outObj->setHImage(OpencvUtil::matToHimage(out));
        setOutputData(1, outObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvRotateNode error:" << e.what();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvRotateNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvRotateNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}
