#include "EdgePointsNode.h"
#include "DataObject.h"
#include "Port.h"

using namespace HalconCpp;

EdgePointsNode::EdgePointsNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("亚像素边缘点"));
    m_type = SHAPE_ANALYSIS;
}

void EdgePointsNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("边缘轮廓"), PortDataType::XLD);
    addOutputPort(QStringLiteral("结果"), PortDataType::String);
    registerParams({
        makeEnumParam(QStringLiteral("filter"), 0,
                      {QStringLiteral("canny"), QStringLiteral("deriche"), QStringLiteral("lanser"), QStringLiteral("shen")},
                      QStringLiteral("滤波器")),
        makeDoubleParam(QStringLiteral("alpha"), 1.0, 0.1, 10.0,
                        QStringLiteral("平滑系数")),
        makeDoubleParam(QStringLiteral("low"), 20.0, 0.0, 255.0,
                        QStringLiteral("低阈值")),
        makeDoubleParam(QStringLiteral("high"), 80.0, 0.0, 255.0,
                        QStringLiteral("高阈值")),
    });
}

void EdgePointsNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        const QStringList filters = {QStringLiteral("canny"), QStringLiteral("deriche"),
                                     QStringLiteral("lanser"), QStringLiteral("shen")};
        const int idx = qBound(0, m_params.value(QStringLiteral("filter"), 0).toInt(), 3);
        const double al = m_params.value(QStringLiteral("alpha"), 1.0).toDouble();
        const double lo = m_params.value(QStringLiteral("low"), 20.0).toDouble();
        const double hi = m_params.value(QStringLiteral("high"), 80.0).toDouble();

        HObject edges;
        EdgesSubPix(HImage(m_inputImage), &edges, HTuple(filters[idx].toStdString().c_str()),
                    al, lo, hi);

        auto xldObj = QSharedPointer<DataObject>::create();
        xldObj->setHXLDCont(HXLDCont(edges));
        setOutputData(1, xldObj);

        // 结果端口：边缘点数
        HTuple n;
        CountObj(edges, &n);
        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(QStringLiteral("边缘轮廓数: %1").arg(n.I()));
        setOutputData(2, strObj);

        // 透传图像
        m_outputImage = m_inputImage;
    } catch (const HException &) {
        m_outputImage.Clear();
        setOutputData(1, QSharedPointer<DataObject>());
        setOutputData(2, QSharedPointer<DataObject>());
    }
}
