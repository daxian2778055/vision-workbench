#include "OtsuThresholdNode.h"
#include "DataObject.h"
#include "Port.h"

using namespace HalconCpp;

OtsuThresholdNode::OtsuThresholdNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("Otsu二值化"));
    m_type = IMAGE_PROCESSING;
}

void OtsuThresholdNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("区域"), PortDataType::Region);
    registerParams({
        makeEnumParam(QStringLiteral("lightDark"), 1,
                      {QStringLiteral("暗"), QStringLiteral("亮")},
                      QStringLiteral("提取方向")),
    });
}

void OtsuThresholdNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        const int idx = qBound(0, m_params.value(QStringLiteral("lightDark"), 1).toInt(), 1);
        const char *dir = (idx == 0) ? "dark" : "light";
        HRegion region;
        HTuple usedThreshold;
        BinaryThreshold(HImage(m_inputImage), &region, "max_separability",
                        HTuple(dir), &usedThreshold);

        // 输出区域到端口 1
        auto regionObj = QSharedPointer<DataObject>::create();
        regionObj->setHRegion(region);
        setOutputData(1, regionObj);

        // 输出二值图像到端口 0
        HTuple w, h;
        GetImageSize(HImage(m_inputImage), &w, &h);
        HObject bin;
        RegionToBin(region, &bin, 255, 0, w, h);
        m_outputImage = bin;
    } catch (const HException &) {
        m_outputImage.Clear();
        setOutputData(1, QSharedPointer<DataObject>());
    }
}
