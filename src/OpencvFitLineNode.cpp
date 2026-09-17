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
        // 搜索区域（ROI）：模块编辑器里在图上拖矩形即可写回；宽或高为 0 表示全图（原行为）
        makeIntParam(QStringLiteral("roiRow"), 0, 0, 100000, QStringLiteral("ROI 起始行")),
        makeIntParam(QStringLiteral("roiCol"), 0, 0, 100000, QStringLiteral("ROI 起始列")),
        makeIntParam(QStringLiteral("roiWidth"), 0, 0, 100000,
                     QStringLiteral("ROI 宽度（0=全图）")),
        makeIntParam(QStringLiteral("roiHeight"), 0, 0, 100000,
                     QStringLiteral("ROI 高度（0=全图）")),
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

        // ROI：只在用户拖出的矩形内找轮廓（宽或高为 0 = 全图，与原行为一致）。
        // 直线拟合里只有"拟合点"需要平移回整图坐标（见下面 x0/y0）；角度由方向向量决定，
        // 平移不改变方向，所以 fitAngle 不需要任何换算。
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
        // 加回 ROI 偏移：对外始终是整图坐标。只要这一处平移，下面的 fitRow/fitCol、
        // 两个端点、extraValues 就都自动是全图坐标；角度来自方向向量，不受平移影响。
        const double x0 = line[2] + roiOffsetX;
        const double y0 = line[3] + roiOffsetY;
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

RoiShape OpencvFitLineNode::geometryRoi() const
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

void OpencvFitLineNode::applyGeometryRoi(const RoiShape &shape)
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
