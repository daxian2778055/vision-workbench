#include "OpencvDefectNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <QWidget>
#include <QFileInfo>
#include <QDir>

OpencvDefectNode::OpencvDefectNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("缺陷检测"));
    m_type = SHAPE_ANALYSIS;
}

void OpencvDefectNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("缺陷"), PortDataType::Measure);
    registerParams({
        makeFilePathParam(QStringLiteral("goldenPath"), QString(),
                          QStringLiteral("黄金模板图像（空=从图上 ROI 教学）")),
        makeBoolParam(QStringLiteral("trainGolden"), true,
                      QStringLiteral("从当前图 ROI 更新黄金模板")),
        makeIntParam(QStringLiteral("roiCol"), 0, 0, 100000, QStringLiteral("ROI 列"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("roiRow"), 0, 0, 100000, QStringLiteral("ROI 行"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("roiWidth"), 0, 0, 100000, QStringLiteral("ROI 宽（0=整图）"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("roiHeight"), 0, 0, 100000, QStringLiteral("ROI 高"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("diffThreshold"), 25, 1, 255, QStringLiteral("差影阈值")),
        makeIntParam(QStringLiteral("morphSize"), 3, 1, 31, QStringLiteral("形态学核"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("minArea"), 20.0, 0.0, 1e9, QStringLiteral("最小缺陷面积"), QStringLiteral("px²")),
        makeDoubleParam(QStringLiteral("maxArea"), 1e9, 0.0, 1e12, QStringLiteral("最大缺陷面积"), QStringLiteral("px²")),
        makeDoubleParam(QStringLiteral("ngArea"), 50.0, 0.0, 1e12, QStringLiteral("NG 面积阈值"), QStringLiteral("px²")),
    });
    m_params[QStringLiteral("defectCount")] = 0;
    m_params[QStringLiteral("defectArea")] = 0.0;
    m_params[QStringLiteral("trainStatus")] = QString();
}

void OpencvDefectNode::run(bool)
{
    try {
        HImage input(m_inputImage);
        if (!input.IsInitialized()) {
            m_outputImage.Clear();
            return;
        }
        cv::Mat gray = OpencvUtil::himageToMat(input);
        if (gray.empty()) {
            m_params["moduleStatus"] = false;
            return;
        }
        if (gray.channels() == 3)
            cv::cvtColor(gray, gray, cv::COLOR_BGR2GRAY);

        const int roiW = m_params.value(QStringLiteral("roiWidth"), 0).toInt();
        const int roiH = m_params.value(QStringLiteral("roiHeight"), 0).toInt();
        cv::Rect roi(0, 0, gray.cols, gray.rows);
        if (roiW > 1 && roiH > 1) {
            const int x = qBound(0, m_params.value(QStringLiteral("roiCol")).toInt(), gray.cols - 1);
            const int y = qBound(0, m_params.value(QStringLiteral("roiRow")).toInt(), gray.rows - 1);
            roi = cv::Rect(x, y,
                           qBound(1, roiW, gray.cols - x),
                           qBound(1, roiH, gray.rows - y));
        }
        cv::Mat inspect = gray(roi).clone();

        const QString goldenPath =
            m_params.value(QStringLiteral("goldenPath")).toString().trimmed();
        const bool train = m_params.value(QStringLiteral("trainGolden"), true).toBool();
        cv::Mat golden;
        if (train) {
            golden = inspect.clone();
            if (!goldenPath.isEmpty()) {
                QFileInfo fi(goldenPath);
                if (!fi.dir().exists())
                    QDir().mkpath(fi.absolutePath());
                if (cv::imwrite(goldenPath.toStdString(), golden))
                    m_params[QStringLiteral("trainStatus")] =
                        QStringLiteral("已保存黄金图: %1").arg(goldenPath);
                else
                    m_params[QStringLiteral("trainStatus")] = QStringLiteral("黄金图保存失败");
            } else {
                m_params[QStringLiteral("trainStatus")] = QStringLiteral("已从 ROI 教学（未落盘）");
            }
        } else if (!goldenPath.isEmpty()) {
            golden = cv::imread(goldenPath.toStdString(), cv::IMREAD_GRAYSCALE);
            m_params[QStringLiteral("trainStatus")] = golden.empty()
                ? QStringLiteral("黄金图加载失败") : QStringLiteral("已加载黄金图");
        }
        if (golden.empty()) {
            m_params["moduleStatus"] = false;
            m_outputImage = m_inputImage;
            setOutputData(1, QSharedPointer<DataObject>());
            return;
        }
        if (golden.size() != inspect.size())
            cv::resize(golden, golden, inspect.size(), 0, 0, cv::INTER_LINEAR);

        cv::Mat diff;
        cv::absdiff(inspect, golden, diff);
        cv::Mat bin;
        cv::threshold(diff, bin, m_params.value(QStringLiteral("diffThreshold"), 25).toInt(),
                      255, cv::THRESH_BINARY);
        int k = m_params.value(QStringLiteral("morphSize"), 3).toInt();
        if (k % 2 == 0) ++k;
        if (k >= 3) {
            cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(k, k));
            cv::morphologyEx(bin, bin, cv::MORPH_OPEN, kernel);
        }
        cv::Mat fullBin = cv::Mat::zeros(gray.size(), CV_8UC1);
        bin.copyTo(fullBin(roi));
        OpencvUtil::applyGrayMask(fullBin, editMask());
        bin = fullBin(roi).clone();

        cv::Mat labels, stats, centroids;
        const int n = cv::connectedComponentsWithStats(bin, labels, stats, centroids, 8, CV_32S);
        const double minA = m_params.value(QStringLiteral("minArea"), 20.0).toDouble();
        const double maxA = m_params.value(QStringLiteral("maxArea"), 1e9).toDouble();
        QVector<double> boxes;
        double totalArea = 0.0;
        int count = 0;
        cv::Mat color;
        cv::cvtColor(gray, color, cv::COLOR_GRAY2BGR);
        for (int i = 1; i < n; ++i) {
            const double area = stats.at<int>(i, cv::CC_STAT_AREA);
            if (area < minA || area > maxA)
                continue;
            const int x = stats.at<int>(i, cv::CC_STAT_LEFT) + roi.x;
            const int y = stats.at<int>(i, cv::CC_STAT_TOP) + roi.y;
            const int w = stats.at<int>(i, cv::CC_STAT_WIDTH);
            const int h = stats.at<int>(i, cv::CC_STAT_HEIGHT);
            boxes << x << y << w << h;
            totalArea += area;
            ++count;
            cv::rectangle(color, cv::Rect(x, y, w, h), cv::Scalar(0, 0, 255), 2);
        }

        const bool ng = totalArea >= m_params.value(QStringLiteral("ngArea"), 50.0).toDouble();
        m_params[QStringLiteral("defectCount")] = count;
        m_params[QStringLiteral("defectArea")] = totalArea;
        m_params["moduleStatus"] = !ng;
        m_outputImage = OpencvUtil::matToHimage(color);

        MeasureResult res;
        res.type = QStringLiteral("defect");
        res.valueName = QStringLiteral("缺陷面积");
        res.valid = true;
        res.value = totalArea;
        res.extraValues = boxes;
        auto obj = QSharedPointer<DataObject>::create();
        obj->setMeasureResult(res);
        setOutputData(1, obj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvDefectNode error:" << e.what();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvDefectNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvDefectNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

RoiShape OpencvDefectNode::geometryRoi() const
{
    RoiShape s;
    s.type = RoiType::Rect;
    const double col = m_params.value(QStringLiteral("roiCol")).toDouble();
    const double row = m_params.value(QStringLiteral("roiRow")).toDouble();
    const double w = m_params.value(QStringLiteral("roiWidth")).toDouble();
    const double h = m_params.value(QStringLiteral("roiHeight")).toDouble();
    if (w <= 1 || h <= 1)
        return RoiShape();
    s.p1 = QPointF(col, row);
    s.p2 = QPointF(col + w, row + h);
    return s;
}

void OpencvDefectNode::applyGeometryRoi(const RoiShape &shape)
{
    const QRectF r = roiAxisAlignedBounds(shape);
    if (r.width() < 1 || r.height() < 1)
        return;
    setParam(QStringLiteral("roiCol"), int(r.x() + 0.5));
    setParam(QStringLiteral("roiRow"), int(r.y() + 0.5));
    setParam(QStringLiteral("roiWidth"), qMax(1, int(r.width() + 0.5)));
    setParam(QStringLiteral("roiHeight"), qMax(1, int(r.height() + 0.5)));
    setParam(QStringLiteral("trainGolden"), true);
}
