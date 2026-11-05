#include "BlurNode.h"
#include "NodeResultFields.h"
#include "DataObject.h"
#include "OpencvUtil.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <QWidget>
#include <QRectF>
#include <algorithm>

BlurNode::BlurNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("模糊"));
    m_type = IMAGE_PROCESSING;
}

void BlurNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("滤波图"), PortDataType::Image);

    // 对齐 VM 4.4 图像滤波的「高斯」类型（IMVSImageFilterModuAlgorithmTab.xml）。
    ParamSpec spec = makeIntParam(QStringLiteral("gaussKernelSize"), 3, 1, 101,
                                  QStringLiteral("高斯滤波核"));
    spec.group = QStringLiteral("运行参数");
    spec.tooltip = QStringLiteral("高斯滤波核大小（内部按奇数取值），越大越平滑");
    registerParams({spec});
}

void BlurNode::run(bool /*autoSwitch*/)
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

        const int size = std::max(1, m_params.value(QStringLiteral("gaussKernelSize"), 3).toInt());
        const int ksize = (size % 2 == 0) ? std::min(size + 1, 101) : size;
        cv::Mat processed;
        cv::GaussianBlur(mat, processed, cv::Size(ksize, ksize), 0, 0, cv::BORDER_REFLECT);

        // ROI + 掩膜：ROI 内滤波，ROI 外保留原图（VM 滤波没有「ROI外处理」开关）。
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
        VFP_DEBUG << "BlurNode error:" << e.what();
        m_params[QStringLiteral("lastError")] = QString::fromUtf8(e.what());
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params[QStringLiteral("lastError")] = unknownExceptionReason();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *BlurNode::createParamPanel()
{
    return createAutoParamPanel();
}

void BlurNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

RoiShape BlurNode::geometryRoi() const
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

void BlurNode::applyGeometryRoi(const RoiShape &shape)
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
