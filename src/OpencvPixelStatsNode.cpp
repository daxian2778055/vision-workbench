#include "OpencvPixelStatsNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <QWidget>

OpencvPixelStatsNode::OpencvPixelStatsNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("灰度统计"));
    m_type = IMAGE_PROCESSING;
}

void OpencvPixelStatsNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("最小灰度"), PortDataType::Number);
    addOutputPort(QStringLiteral("最大灰度"), PortDataType::Number);
    addOutputPort(QStringLiteral("平均灰度"), PortDataType::Number);
    registerParams({});
    m_params[QStringLiteral("minGray")] = 0.0;
    m_params[QStringLiteral("maxGray")] = 0.0;
    m_params[QStringLiteral("meanGray")] = 0.0;
    m_params[QStringLiteral("moduleStatus")] = true;
}

void OpencvPixelStatsNode::run(bool /*autoSwitch*/)
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
        if (mat.channels() == 3) cv::cvtColor(mat, mat, cv::COLOR_BGR2GRAY);

        double minV = 0.0, maxV = 0.0;
        cv::minMaxLoc(mat, &minV, &maxV);
        const double meanV = cv::mean(mat)[0];

        m_params[QStringLiteral("minGray")] = minV;
        m_params[QStringLiteral("maxGray")] = maxV;
        m_params[QStringLiteral("meanGray")] = meanV;
        m_params["moduleStatus"] = true;
        m_outputImage = m_inputImage;

        auto minObj = QSharedPointer<DataObject>::create();
        minObj->setValue(minV);
        setOutputData(1, minObj);
        auto maxObj = QSharedPointer<DataObject>::create();
        maxObj->setValue(maxV);
        setOutputData(2, maxObj);
        auto meanObj = QSharedPointer<DataObject>::create();
        meanObj->setValue(meanV);
        setOutputData(3, meanObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvPixelStatsNode error:" << e.what();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvPixelStatsNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvPixelStatsNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}
