#include "PixelStatsNode.h"
#include "DataObject.h"
#include "Port.h"

using namespace HalconCpp;

PixelStatsNode::PixelStatsNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("灰度统计"));
    m_type = IMAGE_PROCESSING;
}

void PixelStatsNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("统计结果"), PortDataType::String);
}

void PixelStatsNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        HImage img(m_inputImage);

        // 全图统计
        HTuple w, h;
        GetImageSize(img, &w, &h);
        HRegion full;
        GenRectangle1(&full, 0, 0, h.I() - 1, w.I() - 1);

        HTuple minV, maxV, range;
        MinMaxGray(full, img, 0, &minV, &maxV, &range);
        HTuple mean, dev;
        Intensity(full, img, &mean, &dev);

        QString stats = QStringLiteral("尺寸: %1 x %2\n灰度均值: %3\n标准差: %4\n最小灰度: %5\n最大灰度: %6")
            .arg(w.I()).arg(h.I())
            .arg(mean.D(), 0, 'f', 2)
            .arg(dev.D(), 0, 'f', 2)
            .arg(minV.D(), 0, 'f', 2)
            .arg(maxV.D(), 0, 'f', 2);

        auto statsObj = QSharedPointer<DataObject>::create();
        statsObj->setType(DataObject::DataType::String);
        statsObj->setData(stats);
        setOutputData(1, statsObj);

        // 透传图像
        m_outputImage = m_inputImage;
    } catch (const HException &) {
        m_outputImage.Clear();
        setOutputData(1, QSharedPointer<DataObject>());
    }
}
