#include "OpencvMorphNode.h"
#include "NodeResultFields.h"
#include "VmSharedText.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <QWidget>
#include <QRectF>
#include <QElapsedTimer>
#include <algorithm>

OpencvMorphNode::OpencvMorphNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("形态学"));
    m_type = IMAGE_PROCESSING;
}

void OpencvMorphNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("结果图"), PortDataType::Image);

    // 字段名、顺序、默认值对齐 VM 4.4 形态学处理（IMVSImageMorphModuAlgorithmTab.xml）。
    const QString runGroup = QStringLiteral("运行参数");
    ParamSpecList specs;
    auto addSpec = [&](ParamSpec spec, const QString &tip) {
        spec.group = runGroup;
        spec.tooltip = tip;
        specs.append(spec);
    };
    addSpec(makeEnumParam(QStringLiteral("morphType"), 1,
                          {QStringLiteral("膨胀"), QStringLiteral("腐蚀"),
                           QStringLiteral("开"), QStringLiteral("闭")},
                          QStringLiteral("形态学类型")),
            QStringLiteral("膨胀/腐蚀/开/闭，对应 VM 的四种形态学处理"));
    addSpec(makeEnumParam(QStringLiteral("morphShape"), 0,
                          {QStringLiteral("矩形"), QStringLiteral("椭圆"), QStringLiteral("十字")},
                          QStringLiteral("形态学形状")),
            QStringLiteral("结构元的形状：矩形/椭圆/十字"));
    addSpec(makeIntParam(QStringLiteral("iterations"), 1, 0, 10,
                         QStringLiteral("迭代次数")),
            QStringLiteral("对原图重复处理的次数；0 = 不处理"));
    addSpec(makeIntParam(QStringLiteral("kernelWidth"), 3, 1, 51,
                         QStringLiteral("核宽度")),
            QStringLiteral("结构元宽度（像素）"));
    addSpec(makeIntParam(QStringLiteral("kernelHeight"), 3, 1, 51,
                         QStringLiteral("核高度")),
            QStringLiteral("结构元高度（像素）"));
    registerParams(specs);

    // VM「模块结果」展示用（结果字段，不上面板）
    setParamDirect(QStringLiteral("elapsedMs"), 0.0);
    setParamDirect(QStringLiteral("roiCenterX"), 0.0);
    setParamDirect(QStringLiteral("roiCenterY"), 0.0);
    setParamDirect(QStringLiteral("roiWidth"), 0.0);
    setParamDirect(QStringLiteral("roiHeight"), 0.0);
    setParamDirect(QStringLiteral("roiAngle"), 0.0);
}

void OpencvMorphNode::run(bool /*autoSwitch*/)
{
    QElapsedTimer runTimer;   // 官方模块结果「耗时（ms）」
    runTimer.start();
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

        const int type = m_params.value(QStringLiteral("morphType"), 1).toInt();
        const int shape = m_params.value(QStringLiteral("morphShape"), 0).toInt();
        const int iterations = m_params.value(QStringLiteral("iterations"), 1).toInt();
        const int kw = std::max(1, m_params.value(QStringLiteral("kernelWidth"), 3).toInt());
        const int kh = std::max(1, m_params.value(QStringLiteral("kernelHeight"), 3).toInt());

        cv::Mat processed;
        if (iterations <= 0) {
            processed = mat.clone();   // VM 迭代次数 0 = 不处理
        } else {
            int shapeType = cv::MORPH_RECT;
            if (shape == 1)
                shapeType = cv::MORPH_ELLIPSE;
            else if (shape == 2)
                shapeType = cv::MORPH_CROSS;
            const cv::Mat kernel = cv::getStructuringElement(shapeType, cv::Size(kw, kh));
            switch (type) {
            case 0:
                cv::dilate(mat, processed, kernel, cv::Point(-1, -1), iterations);
                break;
            case 2:
                cv::morphologyEx(mat, processed, cv::MORPH_OPEN, kernel, cv::Point(-1, -1), iterations);
                break;
            case 3:
                cv::morphologyEx(mat, processed, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1), iterations);
                break;
            case 1:
            default:
                cv::erode(mat, processed, kernel, cv::Point(-1, -1), iterations);
                break;
            }
        }

        // ROI：矩形几何 ROI + 掩膜。ROI 外保留原图（VM 形态学没有「ROI外处理」开关）。
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
        mat.copyTo(output, outside);   // ROI 外保留原图

        m_params["moduleStatus"] = true;
        // VM「模块结果 → 检测区域」：矩形 ROI 的中心/宽/高/角度（无 ROI 时置 0）。
        if (roi.type == RoiType::Rect) {
            const QRectF box = QRectF(roi.p1, roi.p2).normalized();
            m_params[QStringLiteral("roiCenterX")] = box.x() + box.width() * 0.5;
            m_params[QStringLiteral("roiCenterY")] = box.y() + box.height() * 0.5;
            m_params[QStringLiteral("roiWidth")] = box.width();
            m_params[QStringLiteral("roiHeight")] = box.height();
        } else {
            m_params[QStringLiteral("roiCenterX")] = 0.0;
            m_params[QStringLiteral("roiCenterY")] = 0.0;
            m_params[QStringLiteral("roiWidth")] = 0.0;
            m_params[QStringLiteral("roiHeight")] = 0.0;
        }
        m_params[QStringLiteral("roiAngle")] = 0.0;   // 本节点只支持轴对齐矩形
        m_outputImage = OpencvUtil::matToHimage(output);
        auto outObj = QSharedPointer<DataObject>::create();
        outObj->setHImage(m_outputImage);
        setOutputData(1, outObj);
        m_params[QStringLiteral("elapsedMs")] = static_cast<double>(runTimer.elapsed());
    } catch (const std::exception &e) {
        m_params[QStringLiteral("lastError")] =
            QString::fromUtf8(e.what());   // 原因到面板（2026-10-26）
        VFP_DEBUG << "OpencvMorphNode error:" << e.what();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params[QStringLiteral("lastError")] = unknownExceptionReason();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvMorphNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvMorphNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

QVector<NodeResultField> OpencvMorphNode::resultDisplayFields() const
{
    // 官方「模块结果」字段表（GUID-40515844「模块结果」节）：
    // 耗时 / 模块状态 / 输出图像 / 输出掩膜 / 检测区域。
    // 输出图像与输出掩膜是 image 型，官方说明「模块结果处不显示任何数据」，故不列；
    // 本节点只支持轴对齐矩形 ROI，检测区域按中心/宽/高/角度四项展示。
    // 配置参数一律不进结果表。
    return {
        resultValue(QStringLiteral("elapsedMs"), QStringLiteral("耗时"),
                    QStringLiteral("ms"), 0),
        resultStatus(QStringLiteral("moduleStatus"), QStringLiteral("模块状态")),
        resultValue(QStringLiteral("roiCenterX"), VmSharedText::roiCenterXLabel(),
                    QStringLiteral("px"), 2),
        resultValue(QStringLiteral("roiCenterY"), VmSharedText::roiCenterYLabel(),
                    QStringLiteral("px"), 2),
        resultValue(QStringLiteral("roiWidth"), QStringLiteral("检测区域宽度"),
                    QStringLiteral("px"), 2),
        resultValue(QStringLiteral("roiHeight"), QStringLiteral("检测区域高度"),
                    QStringLiteral("px"), 2),
        resultValue(QStringLiteral("roiAngle"), QStringLiteral("检测区域角度"),
                    QStringLiteral("°"), 3)
    };
}

RoiShape OpencvMorphNode::geometryRoi() const
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

void OpencvMorphNode::applyGeometryRoi(const RoiShape &shape)
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
