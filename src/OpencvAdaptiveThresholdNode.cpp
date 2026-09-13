#include "OpencvAdaptiveThresholdNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <QWidget>

OpencvAdaptiveThresholdNode::OpencvAdaptiveThresholdNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("自适应阈值"));
    m_type = IMAGE_PROCESSING;
}

void OpencvAdaptiveThresholdNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("二值图"), PortDataType::Image);
    addOutputPort(QStringLiteral("前景像素数"), PortDataType::Number);
    registerParams({
        makeEnumParam(QStringLiteral("direction"), 0, QStringList{"暗变亮", "亮变暗"},
                      QStringLiteral("提取方向（亮变暗=前景为暗区域）")),
        makeIntParam(QStringLiteral("blockSize"), 15, 3, 99,
                     QStringLiteral("邻域块大小（奇数）"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("offset"), 5, -20, 20,
                     QStringLiteral("灰度偏移 C")),
    });
    m_params[QStringLiteral("foregroundPixels")] = 0;
}

void OpencvAdaptiveThresholdNode::run(bool /*autoSwitch*/)
{
    try {
        HImage input(m_inputImage);
        if (!input.IsInitialized()) {
            m_outputImage.Clear();
            return;
        }
        cv::Mat mat = OpencvUtil::himageToMat(input);
        if (mat.empty()) {
            m_params["moduleStatus"] = false;
            return;
        }
        if (mat.channels() == 3) {
            cv::cvtColor(mat, mat, cv::COLOR_BGR2GRAY);
        }

        const int direction = m_params.value(QStringLiteral("direction"), 0).toInt();
        int block = qBound(3, m_params.value(QStringLiteral("blockSize"), 15).toInt() | 1, 99);
        const double offset = m_params.value(QStringLiteral("offset"), 5).toInt();

        cv::Mat bin;
        // 暗变暗（提取暗区域）：THRESH_BINARY_INV；亮变暗：THRESH_BINARY
        const int type = (direction == 0) ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY;
        cv::adaptiveThreshold(mat, bin, 255, cv::ADAPTIVE_THRESH_MEAN_C, type, block, offset);

        const int fg = static_cast<int>(cv::countNonZero(bin));
        m_params[QStringLiteral("foregroundPixels")] = fg;
        m_params["moduleStatus"] = true;

        m_outputImage = m_inputImage;
        auto binObj = QSharedPointer<DataObject>::create();
        binObj->setHImage(OpencvUtil::matToHimage(bin));
        setOutputData(1, binObj);
        auto numObj = QSharedPointer<DataObject>::create();
        numObj->setValue(double(fg));
        setOutputData(2, numObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvAdaptiveThresholdNode error:" << e.what();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvAdaptiveThresholdNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvAdaptiveThresholdNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}
