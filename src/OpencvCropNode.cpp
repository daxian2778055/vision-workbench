#include "OpencvCropNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <QWidget>

OpencvCropNode::OpencvCropNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("ROI裁剪"));
    m_type = IMAGE_PROCESSING;
}

void OpencvCropNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("裁剪图"), PortDataType::Image);
    registerParams({
        makeIntParam(QStringLiteral("row"), 0, 0, 100000,
                     QStringLiteral("裁剪区域行起点"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("column"), 0, 0, 100000,
                     QStringLiteral("裁剪区域列起点"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("height"), 100, 1, 100000,
                     QStringLiteral("裁剪高度"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("width"), 100, 1, 100000,
                     QStringLiteral("裁剪宽度"), QStringLiteral("px")),
    });
}

void OpencvCropNode::run(bool /*autoSwitch*/)
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

        const int row = m_params.value(QStringLiteral("row"), 0).toInt();
        const int col = m_params.value(QStringLiteral("column"), 0).toInt();
        const int h = m_params.value(QStringLiteral("height"), 100).toInt();
        const int w = m_params.value(QStringLiteral("width"), 100).toInt();

        // 边界钳制：区域不超出图像范围
        const int x = qBound(0, col, mat.cols - 1);
        const int y = qBound(0, row, mat.rows - 1);
        const int cw = qBound(1, w, mat.cols - x);
        const int ch = qBound(1, h, mat.rows - y);

        cv::Mat cropped = mat(cv::Rect(x, y, cw, ch)).clone();
        m_params["moduleStatus"] = !cropped.empty();
        m_outputImage = OpencvUtil::matToHimage(cropped);

        auto outObj = QSharedPointer<DataObject>::create();
        outObj->setHImage(m_outputImage);
        setOutputData(1, outObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvCropNode error:" << e.what();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvCropNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvCropNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

RoiShape OpencvCropNode::geometryRoi() const
{
    RoiShape s;
    s.type = RoiType::Rect;
    const double col = m_params.value(QStringLiteral("column"), 0).toDouble();
    const double row = m_params.value(QStringLiteral("row"), 0).toDouble();
    const double w = m_params.value(QStringLiteral("width"), 100).toDouble();
    const double h = m_params.value(QStringLiteral("height"), 100).toDouble();
    s.p1 = QPointF(col, row);
    s.p2 = QPointF(col + w, row + h);
    return s;
}

void OpencvCropNode::applyGeometryRoi(const RoiShape &shape)
{
    if (shape.type == RoiType::None)
        return;
    QRectF r;
    if (shape.type == RoiType::Rect || shape.type == RoiType::RotatedRect)
        r = roiAxisAlignedBounds(shape);
    else
        return;
    if (r.width() < 1 || r.height() < 1)
        return;
    setParam(QStringLiteral("column"), int(r.x() + 0.5));
    setParam(QStringLiteral("row"), int(r.y() + 0.5));
    setParam(QStringLiteral("width"), qMax(1, int(r.width() + 0.5)));
    setParam(QStringLiteral("height"), qMax(1, int(r.height() + 0.5)));
}
