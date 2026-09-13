#include "DilateNode.h"
#include "DataObject.h"

using namespace HalconCpp;

DilateNode::DilateNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("膨胀"));
    m_type = IMAGE_PROCESSING;
}

void DilateNode::init()
{
    HalconNode::init();
    registerParams({
        makeIntParam(QStringLiteral("maskWidth"), 5, 1, 200,
                     QStringLiteral("结构元宽度"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("maskHeight"), 5, 1, 200,
                     QStringLiteral("结构元高度"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("iterations"), 1, 1, 20,
                     QStringLiteral("迭代次数")),
    });
}

void DilateNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        const HTuple mw = m_params.value(QStringLiteral("maskWidth"), 5).toInt();
        const HTuple mh = m_params.value(QStringLiteral("maskHeight"), 5).toInt();
        HObject tmp = m_inputImage;
        const int iters = m_params.value(QStringLiteral("iterations"), 1).toInt();
        HObject result;
        for (int i = 0; i < iters; ++i) {
            GrayDilationRect(tmp, &result, mh, mw);
            tmp = result;
        }
        m_outputImage = result;
    } catch (const HException &) {
        m_outputImage.Clear();
    }
}
