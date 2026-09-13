#include "ThresholdNode.h"
#include "DataObject.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QSignalBlocker>

using namespace HalconCpp;

ThresholdNode::ThresholdNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("\u9608\u503C"));
    m_type = IMAGE_PROCESSING;
}

void ThresholdNode::init()
{
    HalconNode::init();
    m_params[QStringLiteral("minGray")] = 128.0;
    m_params[QStringLiteral("maxGray")] = 255.0;
}

void ThresholdNode::run(bool /*autoSwitch*/)
{
    try {
        HImage gray;
        HTuple ch;
        CountChannels(HImage(m_inputImage), &ch);
        if (ch.I() > 1) {
            HObject g;
            Rgb1ToGray(m_inputImage, &g);
            gray = HImage(g);
        } else {
            gray = HImage(m_inputImage);
        }
        if (!gray.IsInitialized()) return;

        const HTuple minG = m_params.value(QStringLiteral("minGray"), 128.0).toDouble();
        const HTuple maxG = m_params.value(QStringLiteral("maxGray"), 255.0).toDouble();
        HObject region, bin;
        Threshold(gray, &region, minG, maxG);
        HTuple w, h;
        GetImageSize(gray, &w, &h);
        RegionToBin(region, &bin, HTuple(255), HTuple(0), w, h);
        m_outputImage = bin;
    } catch (const HException &e) {
        m_outputImage.Clear();
    }
}

QWidget *ThresholdNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>\u9608\u503C\u5206\u5272</b>")));

    auto *minS = new QSpinBox();
    minS->setObjectName(QStringLiteral("thMinGray"));
    minS->setRange(0, 255);
    auto *maxS = new QSpinBox();
    maxS->setObjectName(QStringLiteral("thMaxGray"));
    maxS->setRange(0, 255);
    {
        QSignalBlocker bm(minS), bM(maxS);
        minS->setValue(m_params.value(QStringLiteral("minGray"), 128).toInt());
        maxS->setValue(m_params.value(QStringLiteral("maxGray"), 255).toInt());
    }
    connect(minS, qOverload<int>(&QSpinBox::valueChanged), this, [this, minS, maxS](int v) {
        setParam(QStringLiteral("minGray"), v);
        if (maxS->value() < v) {
            QSignalBlocker b(maxS);
            maxS->setValue(v);
        }
        setParam(QStringLiteral("maxGray"), maxS->value());
    });
    connect(maxS, qOverload<int>(&QSpinBox::valueChanged), this, [this, minS, maxS](int v) {
        setParam(QStringLiteral("maxGray"), v);
        if (minS->value() > v) {
            QSignalBlocker b(minS);
            minS->setValue(v);
        }
        setParam(QStringLiteral("minGray"), minS->value());
    });

    layout->addWidget(new QLabel(QStringLiteral("\u7070\u5EA6\u4E0B\u9650:")));
    layout->addWidget(minS);
    layout->addWidget(new QLabel(QStringLiteral("\u7070\u5EA6\u4E0A\u9650:")));
    layout->addWidget(maxS);
    layout->addStretch();
    return panel;
}

void ThresholdNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    if (auto *w = panel->findChild<QSpinBox *>(QStringLiteral("thMinGray"))) {
        QSignalBlocker b(w);
        w->setValue(m_params.value(QStringLiteral("minGray"), 128).toInt());
    }
    if (auto *w = panel->findChild<QSpinBox *>(QStringLiteral("thMaxGray"))) {
        QSignalBlocker b(w);
        w->setValue(m_params.value(QStringLiteral("maxGray"), 255).toInt());
    }
}
