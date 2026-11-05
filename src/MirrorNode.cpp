#include "MirrorNode.h"
#include "NodeResultFields.h"
#include "DataObject.h"
#include "OpencvUtil.h"
#include "AppLog.h"
#include <opencv2/core.hpp>
#include <algorithm>

MirrorNode::MirrorNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("图像镜像"));
    m_type = IMAGE_PROCESSING;
}

void MirrorNode::init()
{
    HalconNode::init();

    // 对齐 VM 4.4 仿射变换「镜像」类型（IMVSAffineTransformModuAlgorithmTab.xml）。
    ParamSpec spec = makeEnumParam(QStringLiteral("mirrorOrientation"), 0,
                                   {QStringLiteral("水平"), QStringLiteral("垂直"),
                                    QStringLiteral("水平垂直")},
                                   QStringLiteral("镜像方向"));
    spec.group = QStringLiteral("运行参数");
    spec.tooltip = QStringLiteral("水平=左右翻转；垂直=上下翻转；水平垂直=对角翻转");
    registerParams({spec});
}

void MirrorNode::run(bool /*autoSwitch*/)
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
        const int orientation = qBound(0, m_params.value(QStringLiteral("mirrorOrientation"), 0).toInt(), 2);
        cv::Mat dst;
        if (orientation == 0)
            cv::flip(src, dst, 1);       // 水平
        else if (orientation == 1)
            cv::flip(src, dst, 0);       // 垂直
        else
            cv::flip(src, dst, -1);      // 水平垂直
        m_params["moduleStatus"] = !dst.empty();
        m_outputImage = OpencvUtil::matToHimage(dst);
    } catch (const std::exception &e) {
        VFP_DEBUG << "MirrorNode error:" << e.what();
        m_params[QStringLiteral("lastError")] = QString::fromUtf8(e.what());
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params[QStringLiteral("lastError")] = unknownExceptionReason();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}
