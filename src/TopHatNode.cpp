#include "TopHatNode.h"
#include "DataObject.h"

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
    try {
        if (!m_inputImage.IsInitialized()) return;
        const HTuple mw = m_params.value(QStringLiteral("maskWidth"), 15).toInt();
        const HTuple mh = m_params.value(QStringLiteral("maskHeight"), 15).toInt();
        // 顶帽 = 原图 - 开运算结果（迭代 N 次）
        const int iters = m_params.value(QStringLiteral("iterations"), 1).toInt();
        HObject tmp = m_inputImage;
        HObject opening;
        for (int i = 0; i < iters; ++i) {
            GrayOpeningRect(tmp, &opening, mh, mw);
            tmp = opening;
        }
        HObject result;
        SubImage(HImage(m_inputImage), HImage(opening), &result, 1.0, 0.0);
        m_outputImage = result;
    } catch (const HException &) {
        m_outputImage.Clear();
    }
}
