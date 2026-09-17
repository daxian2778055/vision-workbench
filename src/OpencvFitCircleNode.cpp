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
        // 搜索区域（ROI）：模块编辑器里在图上拖矩形即可写回；宽或高为 0 表示全图（原行为）
        makeIntParam(QStringLiteral("roiRow"), 0, 0, 100000, QStringLiteral("ROI 起始行")),
        makeIntParam(QStringLiteral("roiCol"), 0, 0, 100000, QStringLiteral("ROI 起始列")),
        makeIntParam(QStringLiteral("roiWidth"), 0, 0, 100000,
                     QStringLiteral("ROI 宽度（0=全图）")),
        makeIntParam(QStringLiteral("roiHeight"), 0, 0, 100000,
                     QStringLiteral("ROI 高度（0=全图）")),
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

        // ROI：只在用户拖出的矩形内找轮廓（宽或高为 0 = 全图，与原行为一致）。
        // 关键是拟合结果要**加回偏移**（见下面 bestCol/bestRow），否则输出会变成 ROI 局部
        // 坐标——对下游标定/定位是错的，而且这种错很难从数值上看出来。
        int roiOffsetX = 0;
        int roiOffsetY = 0;
        {
            const int roiRow = m_params.value(QStringLiteral("roiRow"), 0).toInt();
            const int roiCol = m_params.value(QStringLiteral("roiCol"), 0).toInt();
            const int roiW = m_params.value(QStringLiteral("roiWidth"), 0).toInt();
            const int roiH = m_params.value(QStringLiteral("roiHeight"), 0).toInt();
            if (roiW > 0 && roiH > 0) {
                const cv::Rect box = cv::Rect(roiCol, roiRow, roiW, roiH)
                                     & cv::Rect(0, 0, mat.cols, mat.rows);
                if (box.width > 0 && box.height > 0) {
                    mat = mat(box).clone();   // clone：ROI 视图不连续，threshold/findContours 需要连续内存
                    roiOffsetX = box.x;
                    roiOffsetY = box.y;
                }
            }
        }

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
                bestCol = center.x + roiOffsetX;   // 加回 ROI 偏移：对外始终是整图坐标
                bestRow = center.y + roiOffsetY;
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

RoiShape OpencvFitCircleNode::geometryRoi() const
{
    RoiShape s;
    const int w = m_params.value(QStringLiteral("roiWidth"), 0).toInt();
    const int h = m_params.value(QStringLiteral("roiHeight"), 0).toInt();
    if (w <= 0 || h <= 0) {
        return s;   // 未设置 ROI：不显示框（type 保持默认 None）
    }
    s.type = RoiType::Rect;
    s.p1 = QPointF(m_params.value(QStringLiteral("roiCol"), 0).toInt(),
                   m_params.value(QStringLiteral("roiRow"), 0).toInt());
    s.p2 = QPointF(s.p1.x() + w, s.p1.y() + h);
    return s;
}

void OpencvFitCircleNode::applyGeometryRoi(const RoiShape &shape)
{
    // 「清除几何」传进来的是默认构造的 RoiShape（type=None）→ 回到全图
    if (shape.type == RoiType::None) {
        setParam(QStringLiteral("roiRow"), 0);
        setParam(QStringLiteral("roiCol"), 0);
        setParam(QStringLiteral("roiWidth"), 0);
        setParam(QStringLiteral("roiHeight"), 0);
        return;
    }
    if (shape.type != RoiType::Rect) {
        return;
    }
    const QRectF r = QRectF(shape.p1, shape.p2).normalized();
    setParam(QStringLiteral("roiCol"), qRound(r.left()));
    setParam(QStringLiteral("roiRow"), qRound(r.top()));
    setParam(QStringLiteral("roiWidth"), qRound(r.width()));
    setParam(QStringLiteral("roiHeight"), qRound(r.height()));
}
