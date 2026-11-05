#include "GrayStretchNode.h"
#include "NodeResultFields.h"
#include "DataObject.h"
#include "OpencvUtil.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <QWidget>
#include <QRectF>
#include <algorithm>
#include <cmath>

GrayStretchNode::GrayStretchNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("灰度拉伸"));
    m_type = IMAGE_PROCESSING;
}

void GrayStretchNode::init()
{
    HalconNode::init();

    // 对齐 VM 4.4 图像归一化「均值标准差归一化」类型（IMVSImageNormlizeModuAlgorithmTab.xml）。
    ParamSpecList specs;
    auto addSpec = [&](ParamSpec spec, const QString &tip) {
        spec.group = QStringLiteral("运行参数");
        spec.tooltip = tip;
        specs.append(spec);
    };
    addSpec(makeDoubleParam(QStringLiteral("meanVal"), 128.0, 0.0, 255.0,
                            QStringLiteral("均值")),
            QStringLiteral("归一化后的目标均值（VM 默认 128）"));
    addSpec(makeDoubleParam(QStringLiteral("stdVal"), 20.0, 0.0, 255.0,
                            QStringLiteral("标准差")),
            QStringLiteral("归一化后的目标标准差（VM 默认 20）"));
    registerParams(specs);
}

void GrayStretchNode::run(bool /*autoSwitch*/)
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

        const double targetMean = qBound(0.0, m_params.value(QStringLiteral("meanVal"), 128.0).toDouble(), 255.0);
        const double targetStd = qBound(0.0, m_params.value(QStringLiteral("stdVal"), 20.0).toDouble(), 255.0);

        cv::Rect roiRect;
        const RoiShape roi = geometryRoi();
        if (roi.type == RoiType::Rect) {
            const QRectF box = QRectF(roi.p1, roi.p2).normalized();
            if (box.width() > 1.0 && box.height() > 1.0)
                roiRect = cv::Rect(int(box.x()), int(box.y()), int(box.width()), int(box.height()));
        }
        cv::Mat allowed = OpencvUtil::allowedMask(mat.size(), roiRect, editMask());

        // 统计口径按 ROI/掩膜内的有效像素（VM 归一化只统计检测区域）。
        cv::Mat meanMat, stdMat;
        cv::meanStdDev(mat, meanMat, stdMat, allowed);
        const double srcMean = meanMat.empty() ? 0.0 : meanMat.at<double>(0);
        const double srcStd = stdMat.empty() ? 0.0 : stdMat.at<double>(0);

        cv::Mat processed;
        if (srcStd < 1e-6 || targetStd <= 0.0) {
            processed = cv::Mat(mat.size(), CV_8U, cv::Scalar(uchar(qBound(0.0, targetMean, 255.0))));
        } else {
            const double alpha = targetStd / srcStd;
            const double beta = targetMean - srcMean * alpha;
            mat.convertTo(processed, CV_8U, alpha, beta);
        }
        OpencvUtil::keepOriginalOutside(processed, mat, allowed);

        m_params["moduleStatus"] = true;
        m_outputImage = OpencvUtil::matToHimage(processed);
    } catch (const std::exception &e) {
        VFP_DEBUG << "GrayStretchNode error:" << e.what();
        m_params[QStringLiteral("lastError")] = QString::fromUtf8(e.what());
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params[QStringLiteral("lastError")] = unknownExceptionReason();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

RoiShape GrayStretchNode::geometryRoi() const
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

void GrayStretchNode::applyGeometryRoi(const RoiShape &shape)
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
