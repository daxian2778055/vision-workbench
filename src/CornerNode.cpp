#include "CornerNode.h"
#include "DataObject.h"
#include "Port.h"
#include "OpencvUtil.h"
#include <opencv2/imgproc.hpp>

using namespace HalconCpp;

CornerNode::CornerNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("角点检测"));
    m_type = SHAPE_ANALYSIS;
}

void CornerNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("角点"), PortDataType::XLD);
    addOutputPort(QStringLiteral("坐标"), PortDataType::String);
    registerParams({
        makeDoubleParam(QStringLiteral("sigmaGrad"), 1.0, 0.1, 10.0,
                        QStringLiteral("梯度平滑σ")),
        makeDoubleParam(QStringLiteral("sigmaSmooth"), 1.5, 0.1, 10.0,
                        QStringLiteral("平滑σ")),
        makeDoubleParam(QStringLiteral("alpha"), 0.08, 0.0, 1.0,
                        QStringLiteral("Harris系数")),
        makeDoubleParam(QStringLiteral("threshold"), 500.0, 0.0, 100000.0,
                        QStringLiteral("响应阈值")),
    });
}

void CornerNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        cv::Mat src = OpencvUtil::himageToMat(HImage(m_inputImage));
        if (src.empty()) {
            m_outputImage.Clear();
            return;
        }
        cv::Mat gray;
        if (src.channels() == 3)
            cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
        else
            gray = src;

        const double quality = qBound(0.001, m_params.value(QStringLiteral("threshold"), 500.0).toDouble() / 100000.0, 1.0);
        std::vector<cv::Point2f> corners;
        cv::goodFeaturesToTrack(gray, corners, 200, quality, 5.0);

        cv::Mat vis = src.clone();
        if (vis.channels() == 1)
            cv::cvtColor(vis, vis, cv::COLOR_GRAY2BGR);
        QStringList coords;
        for (const auto &pt : corners) {
            cv::drawMarker(vis, cv::Point(int(pt.x), int(pt.y)), cv::Scalar(0, 255, 0),
                           cv::MARKER_CROSS, 10, 1);
            coords << QStringLiteral("(%1,%2)").arg(pt.x, 0, 'f', 1).arg(pt.y, 0, 'f', 1);
        }

        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(QStringLiteral("角点数: %1\n%2").arg(corners.size()).arg(coords.join(QLatin1Char(' '))));
        setOutputData(2, strObj);
        setOutputData(1, QSharedPointer<DataObject>());
        m_outputImage = OpencvUtil::matToHimage(vis);
    } catch (const std::exception &) {
        m_outputImage.Clear();
        setOutputData(1, QSharedPointer<DataObject>());
        setOutputData(2, QSharedPointer<DataObject>());
    }
}
