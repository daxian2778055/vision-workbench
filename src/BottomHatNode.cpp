#include "BottomHatNode.h"
#include "DataObject.h"

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
    try {
        if (!m_inputImage.IsInitialized()) return;
        const HTuple mw = m_params.value(QStringLiteral("maskWidth"), 15).toInt();
        const HTuple mh = m_params.value(QStringLiteral("maskHeight"), 15).toInt();
        // 底帽 = 闭运算结果 - 原图（迭代 N 次）
        const int iters = m_params.value(QStringLiteral("iterations"), 1).toInt();
        HObject tmp = m_inputImage;
        HObject closing;
        for (int i = 0; i < iters; ++i) {
            GrayClosingRect(tmp, &closing, mh, mw);
            tmp = closing;
        }
        HObject result;
        SubImage(HImage(closing), HImage(m_inputImage), &result, 1.0, 0.0);
        m_outputImage = result;
    } catch (const HException &) {
        m_outputImage.Clear();
    }
}
