#include "OpencvRotateNode.h"
#include "NodeResultFields.h"
#include "OpencvUtil.h"
#include "VmSharedText.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <QWidget>
#include <algorithm>

OpencvRotateNode::OpencvRotateNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("图像旋转"));
    m_type = IMAGE_PROCESSING;
}

void OpencvRotateNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("旋转图"), PortDataType::Image);

    // 对齐 VM 4.4 仿射变换的旋转/填充字段（IMVSAffineTransformModuAlgorithmTab.xml）。
    ParamSpecList specs;
    auto addSpec = [&](ParamSpec spec, const QString &tip) {
        spec.group = QStringLiteral("运行参数");
        spec.tooltip = tip;
        specs.append(spec);
    };
    addSpec(makeDoubleParam(QStringLiteral("rotateAngle"), 0.0, 0.0, 360.0,
                            QStringLiteral("旋转角度"), QStringLiteral("°")),
            QStringLiteral("绕图像中心旋转的角度（0~360，逆时针）"));
    addSpec(makeEnumParam(QStringLiteral("interpolation"), 1,
                          {QStringLiteral("最近邻"), QStringLiteral("双线性")},
                          QStringLiteral("插值方法")),
            QStringLiteral("最近邻/双线性"));
    addSpec(makeEnumParam(QStringLiteral("extensionType"), 0,
                          {QStringLiteral("常量"), QStringLiteral("邻近复制")},
                          QStringLiteral("填充方式")),
            QStringLiteral("旋转后空白区填常量或复制边缘像素"));
    addSpec(makeIntParam(QStringLiteral("extensionValue"), 0, 0, 255,
                         QStringLiteral("填充值")),
            VmSharedText::constantFillGrayHint());
    addSpec(makeBoolParam(QStringLiteral("lockOutputImageSize"), false,
                          QStringLiteral("锁定输出尺寸")),
            QStringLiteral("勾选=保持原尺寸；不勾=扩展画布容纳完整旋转结果"));
    registerParams(specs);
}

void OpencvRotateNode::run(bool /*autoSwitch*/)
{
    try {
        HImage input(m_inputImage);
        if (!input.IsInitialized()) {
            m_outputImage.Clear();
            return;
        }
        cv::Mat src = OpencvUtil::himageToMat(input);
        if (src.empty()) {
            m_params["moduleStatus"] = false;
            m_outputImage.Clear();
            return;
        }
        if (src.channels() == 3)
            cv::cvtColor(src, src, cv::COLOR_BGR2GRAY);

        const double angle = qBound(0.0, m_params.value(QStringLiteral("rotateAngle"), 0.0).toDouble(), 360.0);
        const int interp = qBound(0, m_params.value(QStringLiteral("interpolation"), 1).toInt(), 1);
        const int ext = qBound(0, m_params.value(QStringLiteral("extensionType"), 0).toInt(), 1);
        const int fill = qBound(0, m_params.value(QStringLiteral("extensionValue"), 0).toInt(), 255);
        const bool lockSize = m_params.value(QStringLiteral("lockOutputImageSize"), false).toBool();

        cv::Point2f center(src.cols / 2.0f, src.rows / 2.0f);
        cv::Mat m = cv::getRotationMatrix2D(center, angle, 1.0);
        cv::Mat dst = OpencvUtil::warpAffineVm(src, m, lockSize, interp, ext, fill);

        m_params["moduleStatus"] = !dst.empty();
        m_outputImage = OpencvUtil::matToHimage(dst);
        auto outObj = QSharedPointer<DataObject>::create();
        outObj->setHImage(m_outputImage);
        setOutputData(1, outObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvRotateNode error:" << e.what();
        m_params[QStringLiteral("lastError")] = QString::fromUtf8(e.what());
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params[QStringLiteral("lastError")] = unknownExceptionReason();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvRotateNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvRotateNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}
