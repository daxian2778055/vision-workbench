#include "OpencvDefectNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <QWidget>
#include <QFileInfo>
#include <QDir>
#include <algorithm>
#include <cmath>

namespace {

/// 边缘抑制（P1-1 补完）：**黄金模板**的强边缘 ±guard 像素内不判缺陷。
/// 为什么必要：即使做了对齐，亚像素残差也会让硬边缘两侧差出细线，而"一条细线"的连通域
/// 面积不大，minArea 往往拦不住；现场表现就是"工件轮廓一圈假缺陷"。
/// 用黄金模板取边缘（而不是当前图）——当前图上的真缺陷不应制造免检区。
void applyEdgeGuard(const cv::Mat &goldenGray, cv::Mat &bin, int guard, int sobelThresh)
{
    if (guard <= 0 || bin.empty() || goldenGray.size() != bin.size())
        return;
    // 注意：cv::magnitude 只吃 CV_32F/CV_64F——Sobel 若输出 CV_16S 会直接断言失败
    //（而异常会被节点 run() 的 catch 吞成"moduleStatus=false"，表现成"边缘抑制没效果"，很难查）
    cv::Mat gx, gy, mag;
    cv::Sobel(goldenGray, gx, CV_32F, 1, 0, 3);
    cv::Sobel(goldenGray, gy, CV_32F, 0, 1, 3);
    cv::magnitude(gx, gy, mag);
    cv::Mat edge;
    cv::threshold(mag, edge, std::max(1, sobelThresh), 255, cv::THRESH_BINARY);
    edge.convertTo(edge, CV_8U);
    const int k = 2 * guard + 1;
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(k, k));
    cv::dilate(edge, edge, kernel);
    bin.setTo(0, edge);
}

} // namespace

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
        // ── P1-1 补完：对齐归一化 + 边缘抑制 ──────────────────────────────────────────
        // 默认开对齐：工件在新图里偏 1 像素，差影就会把整圈边缘点亮成"缺陷"，
        // 默认关着等于把最常见的现场问题留给用户自己发现。
        makeEnumParam(QStringLiteral("alignMode"), 1,
                      {QStringLiteral("不对齐"), QStringLiteral("平移对齐"),
                       QStringLiteral("平移+旋转对齐")},
                      QStringLiteral("对齐归一化（比对前把当前图对回黄金模板）")),
        makeIntParam(QStringLiteral("alignMaxShift"), 50, 0, 2000,
                     QStringLiteral("平移对齐最大允许偏移（超出视为对齐失败）"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("alignMinResponse"), 0.05, 0.0, 1.0,
                        QStringLiteral("对齐置信度下限（低于则不做变换，防低纹理乱移）")),
        makeIntParam(QStringLiteral("alignAngleRange"), 5, 0, 45,
                     QStringLiteral("旋转搜索范围 ±（仅“平移+旋转”模式，步长 0.5°）"),
                     QStringLiteral("°")),
        makeIntParam(QStringLiteral("edgeGuard"), 2, 0, 32,
                     QStringLiteral("边缘抑制宽度（黄金模板强边缘两侧不判缺陷，0=关闭）"),
                     QStringLiteral("px")),
        makeIntParam(QStringLiteral("edgeThresh"), 60, 1, 255,
                     QStringLiteral("边缘抑制的 Sobel 幅值阈值")),
    });
    m_params[QStringLiteral("defectCount")] = 0;
    m_params[QStringLiteral("defectArea")] = 0.0;
    m_params[QStringLiteral("trainStatus")] = QString();
    m_params[QStringLiteral("alignApplied")] = false;
    m_params[QStringLiteral("alignDx")] = 0.0;
    m_params[QStringLiteral("alignDy")] = 0.0;
    m_params[QStringLiteral("alignAngle")] = 0.0;
    m_params[QStringLiteral("alignResponse")] = 0.0;
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

        // ── 对齐归一化（P1-1 补完）──────────────────────────────────────────────────
        // 为什么必须做：黄金差影对位置极敏感——工件在新图里偏 1 像素，整圈轮廓就会被判成缺陷，
        // 这是"标准件比对"在现场最常翻车的地方。实现已抽到 OpencvUtil::alignToReference
        // （异常检测 G-P1-2 共用同一套估计与闸门），这里只负责取参数与写回回显值。
        const int alignMode = m_params.value(QStringLiteral("alignMode"), 1).toInt();
        cv::Mat aligned;
        const OpencvUtil::AlignInfo align = OpencvUtil::alignToReference(
            golden, inspect, alignMode,
            m_params.value(QStringLiteral("alignMaxShift"), 50).toInt(),
            m_params.value(QStringLiteral("alignMinResponse"), 0.05).toDouble(),
            m_params.value(QStringLiteral("alignAngleRange"), 5).toInt(), aligned);
        inspect = aligned;
        m_params[QStringLiteral("alignApplied")] = align.applied;
        m_params[QStringLiteral("alignDx")] = align.dx;
        m_params[QStringLiteral("alignDy")] = align.dy;
        m_params[QStringLiteral("alignAngle")] = align.angle;
        m_params[QStringLiteral("alignResponse")] = align.response;

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
        // 边缘抑制：黄金模板强边缘 ±guard 内不判缺陷（对齐后仍有亚像素残差，硬边缘必然差出细线）
        applyEdgeGuard(golden, bin, m_params.value(QStringLiteral("edgeGuard"), 2).toInt(),
                       m_params.value(QStringLiteral("edgeThresh"), 60).toInt());

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
