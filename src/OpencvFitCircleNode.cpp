#include "OpencvFitCircleNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <QWidget>

OpencvFitCircleNode::OpencvFitCircleNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("圆拟合"));
    m_type = SHAPE_ANALYSIS;
}

void OpencvFitCircleNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("拟合结果"), PortDataType::Measure);
    registerParams({
        makeIntParam(QStringLiteral("minPoints"), 10, 5, 100000,
                     QStringLiteral("参与拟合的最小点数")),
        makeIntParam(QStringLiteral("maxContours"), 3, 1, 1000,
                     QStringLiteral("参与拟合的最大轮廓数")),
    });
    m_params[QStringLiteral("fitRow")] = 0.0;
    m_params[QStringLiteral("fitCol")] = 0.0;
    m_params[QStringLiteral("fitRadius")] = 0.0;
    m_params[QStringLiteral("fitPointCount")] = 0;
}

void OpencvFitCircleNode::run(bool /*autoSwitch*/)
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
        const int minPoints = m_params.value(QStringLiteral("minPoints"), 10).toInt();
        const int maxContours = m_params.value(QStringLiteral("maxContours"), 3).toInt();

        cv::Mat bin;
        cv::threshold(mat, bin, 1, 255, cv::THRESH_BINARY);

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(bin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);

        // 按点数从大到小取前 maxContours 个轮廓，拟合最小外接圆
        std::sort(contours.begin(), contours.end(),
                  [](const auto &a, const auto &b) { return a.size() > b.size(); });

        bool fitted = false;
        double bestRadius = 0.0, bestRow = 0.0, bestCol = 0.0;
        int bestPoints = 0;
        for (size_t i = 0; i < contours.size() && i < static_cast<size_t>(maxContours); ++i) {
            if (contours[i].size() < static_cast<size_t>(minPoints)) continue;
            cv::Point2f center;
            float radius = 0.0f;
            cv::minEnclosingCircle(contours[i], center, radius);
            if (radius > 0.0f) {
                fitted = true;
                bestRadius = radius;
                bestCol = center.x;
                bestRow = center.y;
                bestPoints = static_cast<int>(contours[i].size());
                break;
            }
        }

        m_params[QStringLiteral("fitRow")] = bestRow;
        m_params[QStringLiteral("fitCol")] = bestCol;
        m_params[QStringLiteral("fitRadius")] = bestRadius;
        m_params[QStringLiteral("fitPointCount")] = bestPoints;
        m_params["moduleStatus"] = fitted;
        m_outputImage = m_inputImage;

        if (!fitted) {
            setOutputData(1, QSharedPointer<DataObject>());
            return;
        }

        MeasureResult res;
        res.type = QStringLiteral("circle");
        res.valueName = QStringLiteral("圆心/半径");
        res.valid = true;
        res.value = bestRadius;
        res.point1 = QPointF(bestCol, bestRow);
        res.extraValues = QVector<double>({bestRow, bestCol, bestRadius, double(bestPoints)});
        auto resObj = QSharedPointer<DataObject>::create();
        resObj->setMeasureResult(res);
        setOutputData(1, resObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvFitCircleNode error:" << e.what();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvFitCircleNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvFitCircleNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}
