#include "BottomHatNode.h"
#include "DataObject.h"
#include "OpencvUtil.h"

#include <opencv2/imgproc.hpp>

using namespace HalconCpp;

BottomHatNode::BottomHatNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("底帽变换"));
    m_type = IMAGE_PROCESSING;
}

void BottomHatNode::init()
{
    HalconNode::init();
    registerParams({
        makeIntParam(QStringLiteral("maskWidth"), 15, 1, 400,
                     QStringLiteral("结构元宽度"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("maskHeight"), 15, 1, 400,
                     QStringLiteral("结构元高度"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("iterations"), 1, 1, 20,
                     QStringLiteral("迭代次数")),
    });
}

void BottomHatNode::run(bool)
{
    // 纯 OpenCV 实现（2026-11-08）：底帽 = 闭运算结果 - 原图（迭代 N 次）。
    // 与 TopHat 同理，保持原实现的逐次闭运算结构，不用 morphologyEx 的 iterations。
    if (!m_inputImage.IsInitialized()) {
        m_outputImage.Clear();
        return;
    }
    const cv::Mat src = OpencvUtil::himageToMat(m_inputImage);
    if (src.empty()) {
        m_outputImage.Clear();
        return;
    }
    const int mw = qBound(1, m_params.value(QStringLiteral("maskWidth"), 15).toInt(), 400);
    const int mh = qBound(1, m_params.value(QStringLiteral("maskHeight"), 15).toInt(), 400);
    const int iters = qBound(1, m_params.value(QStringLiteral("iterations"), 1).toInt(), 20);
    const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(mw, mh));
    // 注意：cv::morphologyEx 不支持 src 与 dst 同一 Mat（in-place 会退化成原图）。
    cv::Mat closed = src;
    for (int i = 0; i < iters; ++i) {
        cv::Mat tmp;
        cv::morphologyEx(closed, tmp, cv::MORPH_CLOSE, kernel);
        closed = tmp;
    }
    cv::Mat dst;
    cv::subtract(closed, src, dst);   // 饱和：与原 SubImage 语义一致
    m_outputImage = OpencvUtil::matToHimage(dst);
}
