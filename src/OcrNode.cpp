#include "OcrNode.h"
#include "DataObject.h"
#include "AppLog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>

using namespace HalconCpp;

OcrNode::OcrNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("OCR识别"));
    m_type = SHAPE_ANALYSIS;
}

void OcrNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("识别文本"), PortDataType::String);
    m_params[QStringLiteral("ocrText")] = QString();
    m_params[QStringLiteral("fontName")] = QStringLiteral("Industrial_0-9A-Z_NoRej");
    m_params[QStringLiteral("confidence")] = 0.0;
}

void OcrNode::run(bool /*autoSwitch*/)
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

        // Simple threshold to get regions for OCR
        HObject region;
        Threshold(gray, &region, 0, 128);
        HObject connected;
        Connection(region, &connected);
        HObject selected;
        SelectShape(connected, &selected, "area", "and", HTuple(20), HTuple(10000));

        // Attempt OCR with DoOcrMulti (works with font files)
        try {
            HTuple ocrHandle;
            ReadOcr(HTuple(m_params.value(QStringLiteral("fontName")).toString().toLocal8Bit().constData()),
                    &ocrHandle);

            HTuple classes, confidences;
            DoOcrMulti(selected, gray, ocrHandle, &classes, &confidences);

            ClearOcrClassMlp(ocrHandle);

            QString result;
            double avgConf = 0.0;
            for (int i = 0; i < classes.Length(); i++) {
                result += QString::fromLocal8Bit(classes[i].S().TextA());
                avgConf += confidences[i].D();
            }
            if (classes.Length() > 0) {
                avgConf /= classes.Length();
            }
            m_params[QStringLiteral("ocrText")] = result;
            m_params[QStringLiteral("confidence")] = avgConf;

            // 输出识别文本到 String 端口
            auto strObj = QSharedPointer<DataObject>::create();
            strObj->setType(DataObject::DataType::String);
            strObj->setData(result);
            setOutputData(1, strObj);
        } catch (const HException &e) {
            VFP_DEBUG << "Ocr processing error:" << e.ErrorMessage().TextA();
            m_params[QStringLiteral("ocrText")] = QString();
            m_params[QStringLiteral("confidence")] = 0.0;
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
        VFP_DEBUG << "OcrNode error:" << e.ErrorMessage().TextA();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OcrNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>OCR 识别</b>")));

    layout->addWidget(new QLabel(QStringLiteral("字体:")));
    auto *fontEdit = new QLineEdit();
    fontEdit->setObjectName(QStringLiteral("ocrFont"));
    fontEdit->setText(m_params.value(QStringLiteral("fontName"), QStringLiteral("Industrial_0-9A-Z_NoRej")).toString());
    connect(fontEdit, &QLineEdit::editingFinished, this, [this, fontEdit]() {
        setParam(QStringLiteral("fontName"), fontEdit->text());
    });
    layout->addWidget(fontEdit);

    auto *resultLabel = new QLabel();
    resultLabel->setObjectName(QStringLiteral("ocrResult"));
    QString text = m_params.value(QStringLiteral("ocrText")).toString();
    double conf = m_params.value(QStringLiteral("confidence"), 0.0).toDouble();
    resultLabel->setText(QStringLiteral("识别结果: %1\n置信度: %2%")
        .arg(text.isEmpty() ? QStringLiteral("(无)") : text)
        .arg(conf * 100, 0, 'f', 1));
    layout->addWidget(resultLabel);

    layout->addStretch();
    return panel;
}

void OcrNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    if (auto *le = panel->findChild<QLineEdit *>(QStringLiteral("ocrFont"))) {
        QSignalBlocker b(le);
        le->setText(m_params.value(QStringLiteral("fontName"), QStringLiteral("Industrial_0-9A-Z_NoRej")).toString());
    }
    if (auto *lbl = panel->findChild<QLabel *>(QStringLiteral("ocrResult"))) {
        QString text = m_params.value(QStringLiteral("ocrText")).toString();
        double conf = m_params.value(QStringLiteral("confidence"), 0.0).toDouble();
        lbl->setText(QStringLiteral("识别结果: %1\n置信度: %2%")
            .arg(text.isEmpty() ? QStringLiteral("(无)") : text)
            .arg(conf * 100, 0, 'f', 1));
    }
}
