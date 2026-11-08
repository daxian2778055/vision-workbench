#include "TopHatNode.h"
#include "DataObject.h"
#include "OpencvUtil.h"

#include <opencv2/imgproc.hpp>

using namespace HalconCpp;

TopHatNode::TopHatNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("顶帽变换"));
    m_type = IMAGE_PROCESSING;
}

void TopHatNode::init()
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

void TopHatNode::run(bool)
{
    // 纯 OpenCV 实现（2026-11-08）：顶帽 = 原图 - 开运算结果（迭代 N 次）。
    // 注意保持**原实现的迭代结构**：HALCON 版是 for 循环里反复做 GrayOpeningRect，
    // 而开运算幂等（N 次等价于 1 次），不能改用 cv::morphologyEx 的 iterations
    // 参数——那在 OpenCV 里表示"腐蚀 N 次 + 膨胀 N 次"，会用等效大核，结果不同。
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
    // 注意：cv::morphologyEx 不支持 src 与 dst 同一 Mat（in-place 会退化成原图），
    // 每轮必须用临时 Mat 承结果。
    cv::Mat opened = src;
    for (int i = 0; i < iters; ++i) {
        cv::Mat tmp;
        cv::morphologyEx(opened, tmp, cv::MORPH_OPEN, kernel);
        opened = tmp;
    }
    cv::Mat dst;
    cv::subtract(src, opened, dst);   // 饱和：与原 SubImage 语义一致
    m_outputImage = OpencvUtil::matToHimage(dst);
}
