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
        // 异常/提前返回也要释放模型（2026-10-26 复核 P3）：原实现只在成功路径清，
        // 反复失败会累积 HALCON 句柄。RAII 守卫覆盖所有退出路径（含 throw）。
        struct ModelScopeGuard {
            HTuple h; bool armed = true;
            explicit ModelScopeGuard(const HTuple &v) : h(v) {}
            ~ModelScopeGuard() { if (armed) { try { ClearDataCode2dModel(h); } catch (...) {} } }
        } modelGuard(handle);
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
            // 未识别到内容判红（2026-10-26：与 Ocr/Barcode/ZXing/Tesseract 统一口径）
            m_params[QStringLiteral("lastError")] = result;
            m_params["moduleStatus"] = false;
        }

        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(result);
        setOutputData(1, strObj);

        auto xldObj = QSharedPointer<DataObject>::create();
        xldObj->setHXLDCont(HXLDCont(symbolXLDs));
        setOutputData(2, xldObj);

        /* 释放由 modelGuard 在离开作用域时完成（RAII，异常路径同样覆盖） */
        m_outputImage = m_inputImage;
    } catch (const HException &) {
        m_outputImage.Clear();
        setOutputData(1, QSharedPointer<DataObject>());
        setOutputData(2, QSharedPointer<DataObject>());
    }
}
