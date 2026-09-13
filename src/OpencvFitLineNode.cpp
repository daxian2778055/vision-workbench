#include "OpencvFitLineNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <QWidget>
#include <cmath>

OpencvFitLineNode::OpencvFitLineNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("直线拟合"));
    m_type = SHAPE_ANALYSIS;
}

void OpencvFitLineNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("拟合结果"), PortDataType::Measure);
    registerParams({
        makeIntParam(QStringLiteral("minPoints"), 20, 3, 100000,
                     QStringLiteral("参与拟合的最小点数")),
        makeIntParam(QStringLiteral("maxContours"), 10, 1, 1000,
                     QStringLiteral("参与拟合的最大轮廓数")),
    });
    m_params[QStringLiteral("fitAngle")] = 0.0;
    m_params[QStringLiteral("fitRow")] = 0.0;
    m_params[QStringLiteral("fitCol")] = 0.0;
    m_params[QStringLiteral("fitPointCount")] = 0;
}

void OpencvFitLineNode::run(bool /*autoSwitch*/)
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
        const int minPoints = m_params.value(QStringLiteral("minPoints"), 20).toInt();
        const int maxContours = m_params.value(QStringLiteral("maxContours"), 10).toInt();

        cv::Mat bin;
        cv::threshold(mat, bin, 1, 255, cv::THRESH_BINARY);

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(bin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);

        // 收集轮廓点（按面积从大到小，最多 maxContours 个轮廓）
        std::vector<cv::Point> pts;
        std::sort(contours.begin(), contours.end(),
                  [](const auto &a, const auto &b) { return a.size() > b.size(); });
        for (size_t i = 0; i < contours.size() && i < static_cast<size_t>(maxContours); ++i) {
            pts.insert(pts.end(), contours[i].begin(), contours[i].end());
        }

        const bool valid = pts.size() >= static_cast<size_t>(std::max(2, minPoints));
        if (!valid) {
            setOutputData(1, QSharedPointer<DataObject>());
            m_params[QStringLiteral("fitPointCount")] = 0;
            m_params["moduleStatus"] = false;
            m_outputImage = m_inputImage;
            return;
        }

        cv::Vec4f line;
        cv::fitLine(pts, line, cv::DIST_L2, 0, 0.01, 0.01);
        // line = [vx, vy, x0, y0]（列、行方向）
        const double vx = line[0];
        const double vy = line[1];
        const double x0 = line[2];
        const double y0 = line[3];
        const double angleDeg = std::atan2(vy, vx) * 180.0 / CV_PI;

        m_params[QStringLiteral("fitAngle")] = angleDeg;
        m_params[QStringLiteral("fitRow")] = y0;
        m_params[QStringLiteral("fitCol")] = x0;
        m_params[QStringLiteral("fitPointCount")] = static_cast<int>(pts.size());
        m_params["moduleStatus"] = true;

        // 输出 Measure：直线上的两个端点（过 (x0,y0) 沿方向 (vx,vy) 外推）
        const double half = 200.0;
        MeasureResult res;
        res.type = QStringLiteral("line");
        res.valueName = QStringLiteral("直线角度");
        res.valid = true;
        res.value = angleDeg;
        res.point1 = QPointF(x0 - vx * half, y0 - vy * half);
        res.point2 = QPointF(x0 + vx * half, y0 + vy * half);
        res.extraValues = QVector<double>({vx, vy, x0, y0, double(pts.size())});
        auto resObj = QSharedPointer<DataObject>::create();
        resObj->setMeasureResult(res);
        setOutputData(1, resObj);

        m_outputImage = m_inputImage;
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvFitLineNode error:" << e.what();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvFitLineNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvFitLineNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}
