#include "OpencvThresholdNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <QWidget>

OpencvThresholdNode::OpencvThresholdNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("二值化"));
    m_type = IMAGE_PROCESSING;
}

void OpencvThresholdNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("二值图"), PortDataType::Image);
    addOutputPort(QStringLiteral("前景像素数"), PortDataType::Number);
    registerParams({
        makeEnumParam(QStringLiteral("mode"), 0, QStringList{"固定阈值", "OTSU"},
                      QStringLiteral("二值化模式")),
        makeIntParam(QStringLiteral("minVal"), 128, 0, 255,
                     QStringLiteral("固定阈值下限（mode=0 时有效）")),
    });
    m_params[QStringLiteral("foregroundPixels")] = 0;
}

void OpencvThresholdNode::run(bool /*autoSwitch*/)
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
        const int mode = m_params.value(QStringLiteral("mode"), 0).toInt();
        const int minVal = m_params.value(QStringLiteral("minVal"), 128).toInt();

        cv::Mat bin;
        if (mode == 1) {
            cv::threshold(mat, bin, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
        } else {
            cv::threshold(mat, bin, minVal, 255, cv::THRESH_BINARY);
        }
        OpencvUtil::applyGrayMask(bin, editMask());

        // 前景像素数（白像素统计）
        const int fg = static_cast<int>(cv::countNonZero(bin));
        m_params[QStringLiteral("foregroundPixels")] = fg;
        m_params["moduleStatus"] = true;

        // 输出：原图 + 二值图 + 前景像素数
        m_outputImage = m_inputImage;
        auto binObj = QSharedPointer<DataObject>::create();
        binObj->setHImage(OpencvUtil::matToHimage(bin));
        setOutputData(1, binObj);
        auto numObj = QSharedPointer<DataObject>::create();
        numObj->setValue(double(fg));
        setOutputData(2, numObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvThresholdNode error:" << e.what();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvThresholdNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvThresholdNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}
