#include "ResizeNode.h"
#include "NodeResultFields.h"
#include "DataObject.h"
#include "OpencvUtil.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>

ResizeNode::ResizeNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("图像缩放"));
    m_type = IMAGE_PROCESSING;
}

void ResizeNode::init()
{
    HalconNode::init();

    // 对齐 VM 4.4 图像缩放（IMVSImageResizeModuAlgorithmTab.xml）：输出宽高 + 插值类型 + 抗混叠。
    ParamSpecList specs;
    auto addSpec = [&](ParamSpec spec, const QString &tip) {
        spec.group = QStringLiteral("运行参数");
        spec.tooltip = tip;
        specs.append(spec);
    };
    addSpec(makeIntParam(QStringLiteral("widthValue"), 800, 8, 65000,
                         QStringLiteral("输出图像宽度"), QStringLiteral("px")),
            QStringLiteral("缩放后的输出宽度（8~65000）"));
    addSpec(makeIntParam(QStringLiteral("heightValue"), 800, 8, 65000,
                         QStringLiteral("输出图像高度"), QStringLiteral("px")),
            QStringLiteral("缩放后的输出高度（8~65000）"));
    addSpec(makeEnumParam(QStringLiteral("interType"), 1,
                          {QStringLiteral("最近邻"), QStringLiteral("双线性"),
                           QStringLiteral("双三次")},
                          QStringLiteral("插值类型")),
            QStringLiteral("最近邻最快；双线性通用；双三次更平滑"));
    addSpec(makeBoolParam(QStringLiteral("antiAliasing"), false,
                          QStringLiteral("抗混叠")),
            QStringLiteral("缩小图像时启用抗混叠（内部按区域插值近似）"));
    registerParams(specs);
}

void ResizeNode::run(bool /*autoSwitch*/)
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

        const int width = qBound(8, m_params.value(QStringLiteral("widthValue"), 800).toInt(), 65000);
        const int height = qBound(8, m_params.value(QStringLiteral("heightValue"), 800).toInt(), 65000);
        // 注册上限 65000×65000 会申请约 12 GB（3 通道），OpenCV 抛异常前就可能 OOM。
        // 按像素总量封顶：正常参数远达不到该上限。
        constexpr qint64 kMaxOutputPixels = 100000000;   // 1 亿像素 ≈ 300 MB（3 通道）
        const qint64 pixels = static_cast<qint64>(width) * static_cast<qint64>(height);
        if (pixels > kMaxOutputPixels) {
            m_params["moduleStatus"] = false;
            m_params[QStringLiteral("lastError")] =
                QStringLiteral("输出尺寸过大（%1×%2）：像素总量上限 1 亿")
                    .arg(width).arg(height);
            m_outputImage.Clear();
            return;
        }
        const int interIdx = qBound(0, m_params.value(QStringLiteral("interType"), 1).toInt(), 2);
        const bool antiAliasing = m_params.value(QStringLiteral("antiAliasing"), false).toBool();

        int interp = cv::INTER_LINEAR;
        if (interIdx == 0)
            interp = cv::INTER_NEAREST;
        else if (interIdx == 2)
            interp = cv::INTER_CUBIC;
        // 缩小 + 抗混叠：区域插值等效于先平均再采样，避免细线闪烁。
        if (antiAliasing && width < src.cols && height < src.rows)
            interp = cv::INTER_AREA;

        cv::Mat dst;
        cv::resize(src, dst, cv::Size(width, height), 0, 0, interp);
        m_params["moduleStatus"] = !dst.empty();
        m_outputImage = OpencvUtil::matToHimage(dst);
    } catch (const std::exception &e) {
        VFP_DEBUG << "ResizeNode error:" << e.what();
        m_params[QStringLiteral("lastError")] = QString::fromUtf8(e.what());
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params[QStringLiteral("lastError")] = unknownExceptionReason();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}
