#include "SharpenNode.h"
#include "NodeResultFields.h"
#include "DataObject.h"
#include "OpencvUtil.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <QWidget>
#include <QRectF>
#include <algorithm>

SharpenNode::SharpenNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("图像锐化"));
    m_type = IMAGE_PROCESSING;
}

void SharpenNode::init()
{
    HalconNode::init();

    // 对齐 VM 4.4 图像增强「锐化」类型（IMVSImageEnhanceModuAlgorithmTab.xml）。
    ParamSpecList specs;
    auto addSpec = [&](ParamSpec spec, const QString &tip) {
        spec.group = QStringLiteral("运行参数");
        spec.tooltip = tip;
        specs.append(spec);
    };
    addSpec(makeIntParam(QStringLiteral("sharpenStrength"), 0, 0, 1000,
                         QStringLiteral("锐化强度")),
            QStringLiteral("100 表示系数 1.0，0 表示不锐化；越大边缘增强越明显"));
    addSpec(makeIntParam(QStringLiteral("sharpenKernelSize"), 3, 1, 51,
                         QStringLiteral("锐化核大小")),
            QStringLiteral("计算局部均值使用的邻域大小（1~51）"));
    registerParams(specs);
}

void SharpenNode::run(bool /*autoSwitch*/)
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

        const int strength = std::max(0, m_params.value(QStringLiteral("sharpenStrength"), 0).toInt());
        const int kernel = std::max(1, m_params.value(QStringLiteral("sharpenKernelSize"), 3).toInt());

        cv::Mat processed;
        if (strength <= 0 || kernel <= 1) {
            processed = mat.clone();   // VM：强度 0 = 不锐化
        } else {
            // VM：原图与邻域均值的差异按强度放大后加回原图。
            cv::Mat background;
            cv::boxFilter(mat, background, CV_8U, cv::Size(kernel, kernel),
                          cv::Point(-1, -1), true, cv::BORDER_REFLECT);
            cv::Mat diff;
            cv::subtract(mat, background, diff, cv::noArray(), CV_16S);
            cv::Mat scaled;
            diff.convertTo(scaled, CV_16S, strength / 100.0);
            cv::add(mat, scaled, processed, cv::noArray(), CV_8U);
        }

        // ROI + 掩膜：ROI 内增强，ROI 外保留原图。
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
        VFP_DEBUG << "SharpenNode error:" << e.what();
        m_params[QStringLiteral("lastError")] = QString::fromUtf8(e.what());
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params[QStringLiteral("lastError")] = unknownExceptionReason();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

RoiShape SharpenNode::geometryRoi() const
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

void SharpenNode::applyGeometryRoi(const RoiShape &shape)
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
