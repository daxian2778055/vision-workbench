#include "BarcodeNode.h"
#include "DataObject.h"
#include "AppLog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QSignalBlocker>

using namespace HalconCpp;

BarcodeNode::BarcodeNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("条码识别"));
    m_type = SHAPE_ANALYSIS;
}

void BarcodeNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("识别结果"), PortDataType::String);
    m_params[QStringLiteral("barcodeText")] = QString();
    m_params[QStringLiteral("barcodeType")] = QStringLiteral("auto");
}

void BarcodeNode::run(bool /*autoSwitch*/)
{
    try {
        HImage input(m_inputImage);
        if (!input.IsInitialized()) {
            m_outputImage.Clear();
            return;
        }

        HImage gray;
        HTuple ch;
        CountChannels(input, &ch);
        if (ch.I() > 1) {
            HObject g;
            Rgb1ToGray(input, &g);
            gray = HImage(g);
        } else {
            gray = input;
        }

        try {
            // Create barcode model
            HTuple barcodeHandle;
            CreateBarCodeModel(HTuple(), HTuple(), &barcodeHandle);

            // Find and decode barcode: FindBarCode(Image, SymbolRegions, BarCodeHandle, CodeType, DecodedDataStrings)
            HObject symbolRegions;
            HTuple decodedStrings;
            FindBarCode(gray, &symbolRegions, barcodeHandle,
                       HTuple(m_params.value(QStringLiteral("barcodeType")).toString().toLocal8Bit().constData()),
                       &decodedStrings);

            if (decodedStrings.Length() > 0) {
                m_params[QStringLiteral("barcodeText")] = QString::fromLocal8Bit(decodedStrings[0].S().TextA());
            } else {
                m_params[QStringLiteral("barcodeText")] = QString();
            }

            // 输出识别结果到 String 端口
            auto strObj = QSharedPointer<DataObject>::create();
            strObj->setType(DataObject::DataType::String);
            strObj->setData(m_params[QStringLiteral("barcodeText")].toString());
            setOutputData(1, strObj);

            ClearBarCodeModel(barcodeHandle);
        } catch (const HException &e) {
            VFP_DEBUG << "Barcode processing error:" << e.ErrorMessage().TextA();
            m_params[QStringLiteral("barcodeText")] = QString();
            setOutputData(1, QSharedPointer<DataObject>());
        }

        m_outputImage = gray;

        HalconCpp::HImage output(m_outputImage);
        if (output.IsInitialized()) {
            auto outObj = QSharedPointer<DataObject>::create();
            outObj->setHImage(output);
            setOutputData(0, outObj);
        }
        m_params["moduleStatus"] = true;
    } catch (const HException &e) {
        VFP_DEBUG << "BarcodeNode error:" << e.ErrorMessage().TextA();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *BarcodeNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>条码识别</b>")));

    auto *resultLabel = new QLabel();
    resultLabel->setObjectName(QStringLiteral("barcodeResult"));
    QString text = m_params.value(QStringLiteral("barcodeText")).toString();
    resultLabel->setText(QStringLiteral("识别结果: %1")
        .arg(text.isEmpty() ? QStringLiteral("(未识别到)") : text));
    layout->addWidget(resultLabel);

    layout->addStretch();
    return panel;
}

void BarcodeNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    if (auto *lbl = panel->findChild<QLabel *>(QStringLiteral("barcodeResult"))) {
        QString text = m_params.value(QStringLiteral("barcodeText")).toString();
        lbl->setText(QStringLiteral("识别结果: %1")
            .arg(text.isEmpty() ? QStringLiteral("(未识别到)") : text));
    }
}
