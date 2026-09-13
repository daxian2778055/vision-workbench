#include "DataCode2DNode.h"
#include "DataObject.h"
#include "Port.h"

using namespace HalconCpp;

DataCode2DNode::DataCode2DNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("二维码识别"));
    m_type = SHAPE_ANALYSIS;
}

void DataCode2DNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("识别结果"), PortDataType::String);
    addOutputPort(QStringLiteral("符号轮廓"), PortDataType::XLD);
    registerParams({
        makeEnumParam(QStringLiteral("symbolType"), 1,
                      {QStringLiteral("DataMatrix"), QStringLiteral("QR Code"),
                       QStringLiteral("PDF417"), QStringLiteral("Aztec Code")},
                      QStringLiteral("码制")),
        makeEnumParam(QStringLiteral("contrast"), 1,
                      {QStringLiteral("低"), QStringLiteral("默认"), QStringLiteral("高")},
                      QStringLiteral("对比度")),
    });
}

void DataCode2DNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        const QStringList types = {QStringLiteral("Data Matrix ECC 200"), QStringLiteral("QR Code"),
                                   QStringLiteral("PDF417"), QStringLiteral("Aztec Code")};
        const int idx = qBound(0, m_params.value(QStringLiteral("symbolType"), 1).toInt(), 3);
        const int cont = qBound(0, m_params.value(QStringLiteral("contrast"), 1).toInt(), 2);
        const QStringList contrasts = {QStringLiteral("low"), QStringLiteral("default"), QStringLiteral("high")};

        HTuple handle;
        CreateDataCode2dModel(HTuple(types[idx].toStdString().c_str()), HTuple(), HTuple(), &handle);
        SetDataCode2dParam(handle, "contrast", contrasts[cont].toStdString().c_str());

        HObject symbolXLDs;
        HTuple resultHandles, decodedStrings;
        FindDataCode2d(HImage(m_inputImage), &symbolXLDs, handle, HTuple(), HTuple(),
                       &resultHandles, &decodedStrings);

        QString result;
        if (decodedStrings.Length() > 0) {
            QStringList lines;
            for (int i = 0; i < decodedStrings.Length(); ++i)
                lines << QString::fromStdString(decodedStrings[i].S().Text());
            result = lines.join(QStringLiteral("\n"));
        } else {
            result = QStringLiteral("未识别到二维码");
        }

        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(result);
        setOutputData(1, strObj);

        auto xldObj = QSharedPointer<DataObject>::create();
        xldObj->setHXLDCont(HXLDCont(symbolXLDs));
        setOutputData(2, xldObj);

        ClearDataCode2dModel(handle);
        m_outputImage = m_inputImage;
    } catch (const HException &) {
        m_outputImage.Clear();
        setOutputData(1, QSharedPointer<DataObject>());
        setOutputData(2, QSharedPointer<DataObject>());
    }
}
