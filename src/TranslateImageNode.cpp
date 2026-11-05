#include "TranslateImageNode.h"
#include "NodeResultFields.h"
#include "DataObject.h"
#include "OpencvUtil.h"
#include "VmSharedText.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>

TranslateImageNode::TranslateImageNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("图像平移"));
    m_type = IMAGE_PROCESSING;
}

void TranslateImageNode::init()
{
    HalconNode::init();

    // 对齐 VM 4.4 仿射变换「平移」类型（IMVSAffineTransformModuAlgorithmTab.xml）。
    ParamSpecList specs;
    auto addSpec = [&](ParamSpec spec, const QString &tip) {
        spec.group = QStringLiteral("运行参数");
        spec.tooltip = tip;
        specs.append(spec);
    };
    addSpec(makeIntParam(QStringLiteral("moveXValue"), 0, -30000, 30000,
                         QStringLiteral("X方向移动距离"), QStringLiteral("px")),
            QStringLiteral("水平方向平移像素数（VM -30000~30000）"));
    addSpec(makeIntParam(QStringLiteral("moveYValue"), 0, -20000, 20000,
                         QStringLiteral("Y方向移动距离"), QStringLiteral("px")),
            QStringLiteral("垂直方向平移像素数（VM -20000~20000）"));
    addSpec(makeEnumParam(QStringLiteral("interpolation"), 1,
                          {QStringLiteral("最近邻"), QStringLiteral("双线性")},
                          QStringLiteral("插值方法")),
            QStringLiteral("最近邻/双线性"));
    addSpec(makeEnumParam(QStringLiteral("extensionType"), 0,
                          {QStringLiteral("常量"), QStringLiteral("邻近复制")},
                          QStringLiteral("填充方式")),
            QStringLiteral("空白区填常量或复制边缘像素"));
    addSpec(makeIntParam(QStringLiteral("extensionValue"), 0, 0, 255,
                         QStringLiteral("填充值")),
            VmSharedText::constantFillGrayHint());
    addSpec(makeBoolParam(QStringLiteral("lockOutputImageSize"), false,
                          QStringLiteral("锁定输出尺寸")),
            QStringLiteral("勾选=输出保持原尺寸；不勾=扩展画布容纳完整平移结果"));
    registerParams(specs);
}

void TranslateImageNode::run(bool /*autoSwitch*/)
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

        const int dx = qBound(-30000, m_params.value(QStringLiteral("moveXValue"), 0).toInt(), 30000);
        const int dy = qBound(-20000, m_params.value(QStringLiteral("moveYValue"), 0).toInt(), 20000);
        const int interp = qBound(0, m_params.value(QStringLiteral("interpolation"), 1).toInt(), 1);
        const int ext = qBound(0, m_params.value(QStringLiteral("extensionType"), 0).toInt(), 1);
        const int fill = qBound(0, m_params.value(QStringLiteral("extensionValue"), 0).toInt(), 255);
        const bool lockSize = m_params.value(QStringLiteral("lockOutputImageSize"), false).toBool();

        cv::Mat m = (cv::Mat_<double>(2, 3) << 1, 0, double(dx), 0, 1, double(dy));
        cv::Mat dst = OpencvUtil::warpAffineVm(src, m, lockSize, interp, ext, fill);
        m_params["moduleStatus"] = !dst.empty();
        m_outputImage = OpencvUtil::matToHimage(dst);
    } catch (const std::exception &e) {
        VFP_DEBUG << "TranslateImageNode error:" << e.what();
        m_params[QStringLiteral("lastError")] = QString::fromUtf8(e.what());
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params[QStringLiteral("lastError")] = unknownExceptionReason();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}
