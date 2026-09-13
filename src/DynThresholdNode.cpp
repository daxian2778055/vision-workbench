#include "DynThresholdNode.h"
#include "DataObject.h"
#include "Port.h"

using namespace HalconCpp;

DynThresholdNode::DynThresholdNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("动态阈值"));
    m_type = IMAGE_PROCESSING;
}

void DynThresholdNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("区域"), PortDataType::Region);
    registerParams({
        makeDoubleParam(QStringLiteral("offset"), 10.0, 0.0, 255.0,
                        QStringLiteral("灰度偏移")),
        makeIntParam(QStringLiteral("maskWidth"), 15, 1, 200,
                     QStringLiteral("均值窗口宽"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("maskHeight"), 15, 1, 200,
                     QStringLiteral("均值窗口高"), QStringLiteral("px")),
        makeEnumParam(QStringLiteral("lightDark"), 1,
                      {QStringLiteral("暗"), QStringLiteral("亮"), QStringLiteral("相等"), QStringLiteral("不等")},
                      QStringLiteral("提取方向")),
    });
}

void DynThresholdNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        const double offset = m_params.value(QStringLiteral("offset"), 10.0).toDouble();
        const int mw = m_params.value(QStringLiteral("maskWidth"), 15).toInt();
        const int mh = m_params.value(QStringLiteral("maskHeight"), 15).toInt();
        const QStringList dirs = {QStringLiteral("dark"), QStringLiteral("light"),
                                  QStringLiteral("equal"), QStringLiteral("not_equal")};
        const int idx = qBound(0, m_params.value(QStringLiteral("lightDark"), 1).toInt(), 3);

        HObject mean;
        MeanImage(HImage(m_inputImage), &mean, mw, mh);
        HRegion region;
        DynThreshold(HImage(m_inputImage), HImage(mean), &region, offset,
                     HTuple(dirs[idx].toStdString().c_str()));

        auto regionObj = QSharedPointer<DataObject>::create();
        regionObj->setHRegion(region);
        setOutputData(1, regionObj);

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
