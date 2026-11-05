#include "MedianFilterNode.h"
#include "NodeResultFields.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <QWidget>
#include <QRectF>
#include <algorithm>

namespace {

/// 矩形窗口中值滤波（OpenCV medianBlur 只支持方形核）。
/// 逐行维护 256 桶直方图，滑动窗口时移除左列/加入右列；边界按复制延拓。
cv::Mat rectangularMedian(const cv::Mat &src, int kw, int kh)
{
    const int rows = src.rows;
    const int cols = src.cols;
    const int halfW = kw / 2;
    const int halfH = kh / 2;
    cv::Mat dst(rows, cols, CV_8U);
    auto at = [&src, rows, cols](int y, int x) -> int {
        y = qBound(0, y, rows - 1);
        x = qBound(0, x, cols - 1);
        return int(src.at<uchar>(y, x));
    };
    const int target = (kw * kh + 1) / 2;

    for (int y = 0; y < rows; ++y) {
        int hist[256] = {0};
        for (int dy = -halfH; dy <= kh - halfH - 1; ++dy)
            for (int dx = -halfW; dx <= kw - halfW - 1; ++dx)
                ++hist[at(y + dy, dx)];
        for (int x = 0; x < cols; ++x) {
            int acc = 0;
            int med = 0;
            for (int v = 0; v < 256; ++v) {
                acc += hist[v];
                if (acc >= target) {
                    med = v;
                    break;
                }
            }
            dst.at<uchar>(y, x) = uchar(med);
            if (x + 1 < cols) {
                const int removeX = x - halfW;
                const int addX = x + kw - halfW;
                for (int dy = -halfH; dy <= kh - halfH - 1; ++dy) {
                    --hist[at(y + dy, removeX)];
                    ++hist[at(y + dy, addX)];
                }
            }
        }
    }
    return dst;
}

}   // namespace

MedianFilterNode::MedianFilterNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("中值滤波"));
    m_type = IMAGE_PROCESSING;
}

void MedianFilterNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("滤波图"), PortDataType::Image);

    // 对齐 VM 4.4 图像滤波的「中值」类型（IMVSImageFilterModuAlgorithmTab.xml）。
    ParamSpecList specs;
    auto addSpec = [&](ParamSpec spec, const QString &tip) {
        spec.group = QStringLiteral("运行参数");
        spec.tooltip = tip;
        specs.append(spec);
    };
    addSpec(makeIntParam(QStringLiteral("kernelWidth"), 3, 1, 101,
                         QStringLiteral("核宽度")),
            QStringLiteral("中值滤波窗口宽度（像素）"));
    addSpec(makeIntParam(QStringLiteral("kernelHeight"), 3, 1, 101,
                         QStringLiteral("核高度")),
            QStringLiteral("中值滤波窗口高度（像素）"));
    registerParams(specs);
}

void MedianFilterNode::run(bool /*autoSwitch*/)
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
        if (mat.channels() == 3)
            cv::cvtColor(mat, mat, cv::COLOR_BGR2GRAY);

        const int kw = std::max(1, m_params.value(QStringLiteral("kernelWidth"), 3).toInt());
        const int kh = std::max(1, m_params.value(QStringLiteral("kernelHeight"), 3).toInt());

        cv::Mat processed;
        if (kw == kh && kw % 2 == 1) {
            cv::medianBlur(mat, processed, kw);   // 方形奇数核：走 OpenCV 快路径
        } else {
            processed = rectangularMedian(mat, kw, kh);
        }

        // ROI + 掩膜：ROI 内滤波，ROI 外保留原图。
        cv::Mat allowed(mat.size(), CV_8U, cv::Scalar(255));
        const RoiShape roi = geometryRoi();
        if (roi.type == RoiType::Rect) {
            const QRectF box = QRectF(roi.p1, roi.p2).normalized();
            if (box.width() > 1.0 && box.height() > 1.0) {
                allowed.setTo(0);
                cv::rectangle(allowed,
                              cv::Rect(int(box.x()), int(box.y()),
                                       int(box.width()), int(box.height())),
                              cv::Scalar(255), cv::FILLED);
            }
        }
        OpencvUtil::applyGrayMask(allowed, editMask());

        cv::Mat outside;
        cv::compare(allowed, 0, outside, cv::CMP_EQ);
        cv::Mat output = processed.clone();
        mat.copyTo(output, outside);

        m_params["moduleStatus"] = true;
        m_outputImage = OpencvUtil::matToHimage(output);
        auto outObj = QSharedPointer<DataObject>::create();
        outObj->setHImage(m_outputImage);
        setOutputData(1, outObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "MedianFilterNode error:" << e.what();
        m_params[QStringLiteral("lastError")] = QString::fromUtf8(e.what());
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params[QStringLiteral("lastError")] = unknownExceptionReason();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

RoiShape MedianFilterNode::geometryRoi() const
{
    const int row = m_params.value(QStringLiteral("roiRectRow"), 0).toInt();
    const int col = m_params.value(QStringLiteral("roiRectCol"), 0).toInt();
    const int width = m_params.value(QStringLiteral("roiRectWidth"), 0).toInt();
    const int height = m_params.value(QStringLiteral("roiRectHeight"), 0).toInt();
    RoiShape shape;
    if (width <= 1 || height <= 1)
        return shape;
    shape.type = RoiType::Rect;
    shape.p1 = QPointF(col, row);
    shape.p2 = QPointF(col + width, row + height);
    return shape;
}

void MedianFilterNode::applyGeometryRoi(const RoiShape &shape)
{
    if (shape.type == RoiType::Rect) {
        const QRectF box = QRectF(shape.p1, shape.p2).normalized();
        setParam(QStringLiteral("roiRectRow"), int(box.y()));
        setParam(QStringLiteral("roiRectCol"), int(box.x()));
        setParam(QStringLiteral("roiRectWidth"), qMax(1, int(box.width())));
        setParam(QStringLiteral("roiRectHeight"), qMax(1, int(box.height())));
    } else if (shape.type == RoiType::None) {
        setParam(QStringLiteral("roiRectRow"), 0);
        setParam(QStringLiteral("roiRectCol"), 0);
        setParam(QStringLiteral("roiRectWidth"), 0);
        setParam(QStringLiteral("roiRectHeight"), 0);
    }
}
