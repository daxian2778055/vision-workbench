#include "OpencvEdgeNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <QWidget>

OpencvEdgeNode::OpencvEdgeNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("边缘检测"));
    m_type = SHAPE_ANALYSIS;
}

void OpencvEdgeNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("边缘图"), PortDataType::Image);
    addOutputPort(QStringLiteral("边缘像素数"), PortDataType::Number);
    registerParams({
        makeIntParam(QStringLiteral("lowThreshold"), 50, 0, 1000,
                     QStringLiteral("Canny 低阈值")),
        makeIntParam(QStringLiteral("highThreshold"), 150, 0, 1000,
                     QStringLiteral("Canny 高阈值")),
        makeIntParam(QStringLiteral("gaussKernel"), 3, 1, 31,
                     QStringLiteral("高斯预滤波核（1=不滤波）")),
    });
    m_params[QStringLiteral("edgePixels")] = 0;
}

void OpencvEdgeNode::run(bool /*autoSwitch*/)
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
        const int low = m_params.value(QStringLiteral("lowThreshold"), 50).toInt();
        const int high = m_params.value(QStringLiteral("highThreshold"), 150).toInt();
        const int gk = m_params.value(QStringLiteral("gaussKernel"), 3).toInt();

        cv::Mat smoothed = mat;
        if (gk > 1) {
            const int k = gk | 1;
            cv::GaussianBlur(mat, smoothed, cv::Size(k, k), 0);
        }
        cv::Mat edges;
        cv::Canny(smoothed, edges, low, high);

        const int edgeCount = static_cast<int>(cv::countNonZero(edges));
        m_params[QStringLiteral("edgePixels")] = edgeCount;
        m_params["moduleStatus"] = true;

        m_outputImage = m_inputImage;
        auto edgeObj = QSharedPointer<DataObject>::create();
        edgeObj->setHImage(OpencvUtil::matToHimage(edges));
        setOutputData(1, edgeObj);
        auto numObj = QSharedPointer<DataObject>::create();
        numObj->setValue(double(edgeCount));
        setOutputData(2, numObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvEdgeNode error:" << e.what();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvEdgeNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvEdgeNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}
