#include "ContrastStretchNode.h"
#include "NodeResultFields.h"
#include "DataObject.h"
#include "OpencvUtil.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <QWidget>
#include <QRectF>
#include <algorithm>

ContrastStretchNode::ContrastStretchNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("对比度拉伸"));
    m_type = IMAGE_PROCESSING;
}

void ContrastStretchNode::init()
{
    HalconNode::init();

    // 对齐 VM 4.4 图像增强「对比度」类型（IMVSImageEnhanceModuAlgorithmTab.xml）。
    ParamSpec spec = makeIntParam(QStringLiteral("contrastFactor"), 100, 1, 10000,
                                  QStringLiteral("对比度系数"));
    spec.group = QStringLiteral("运行参数");
    spec.tooltip = QStringLiteral("100 = 不调节；大于 100 增强对比度，小于 100 降低对比度");
    registerParams({spec});
}

void ContrastStretchNode::run(bool /*autoSwitch*/)
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

        const int factor = qBound(1, m_params.value(QStringLiteral("contrastFactor"), 100).toInt(), 10000);
        const double alpha = factor / 100.0;
        const double beta = 128.0 * (1.0 - alpha);   // 以 128 为中心调节对比度
        cv::Mat processed;
        mat.convertTo(processed, CV_8U, alpha, beta);

        // ROI + 掩膜：ROI 内调节，ROI 外保留原图。
        cv::Rect roiRect;
        const RoiShape roi = geometryRoi();
        if (roi.type == RoiType::Rect) {
            const QRectF box = QRectF(roi.p1, roi.p2).normalized();
            if (box.width() > 1.0 && box.height() > 1.0)
                roiRect = cv::Rect(int(box.x()), int(box.y()), int(box.width()), int(box.height()));
        }
        cv::Mat allowed = OpencvUtil::allowedMask(mat.size(), roiRect, editMask());
        OpencvUtil::keepOriginalOutside(processed, mat, allowed);

        m_params["moduleStatus"] = true;
        m_outputImage = OpencvUtil::matToHimage(processed);
    } catch (const std::exception &e) {
        VFP_DEBUG << "ContrastStretchNode error:" << e.what();
        m_params[QStringLiteral("lastError")] = QString::fromUtf8(e.what());
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params[QStringLiteral("lastError")] = unknownExceptionReason();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

RoiShape ContrastStretchNode::geometryRoi() const
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

void ContrastStretchNode::applyGeometryRoi(const RoiShape &shape)
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
