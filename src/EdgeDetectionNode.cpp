#include "EdgeDetectionNode.h"
#include "DataObject.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QSignalBlocker>

using namespace HalconCpp;

EdgeDetectionNode::EdgeDetectionNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("\u8FB9\u7F18\u68C0\u6D4B"));
    m_type = SHAPE_ANALYSIS;
}

void EdgeDetectionNode::init()
{
    HalconNode::init();
    m_params[QStringLiteral("filterSize")] = 3;
}

void EdgeDetectionNode::run(bool /*autoSwitch*/)
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

        const int fs = m_params.value(QStringLiteral("filterSize"), 3).toInt();
        int ms = qBound(3, fs | 1, 39);
        HObject edgeAmp;
        SobelAmp(gray, &edgeAmp, "sum_abs", ms);
        m_outputImage = edgeAmp;
    } catch (const HException &e) {
        m_outputImage.Clear();
    }
}

QWidget *EdgeDetectionNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>\u8FB9\u7F18\u68C0\u6D4B Sobel</b>")));

    auto *combo = new QComboBox();
    combo->setObjectName(QStringLiteral("edgeFilter"));
    for (int s = 3; s <= 39; s += 2)
        combo->addItem(QString::number(s), s);

    const int fs = m_params.value(QStringLiteral("filterSize"), 3).toInt();
    const int idx = combo->findData(qBound(3, fs | 1, 39));
    if (idx >= 0) {
        QSignalBlocker br(combo);
        combo->setCurrentIndex(idx);
    }
    connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, combo](int) {
        setParam(QStringLiteral("filterSize"), combo->currentData().toInt());
    });

    layout->addWidget(new QLabel(QStringLiteral("\u6EE4\u6CE2\u5668\u5C3A\u5BF8:")));
    layout->addWidget(combo);
    layout->addStretch();
    return panel;
}

void EdgeDetectionNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    if (auto *cb = panel->findChild<QComboBox *>(QStringLiteral("edgeFilter"))) {
        QSignalBlocker g(cb);
        const int fs = qBound(3, m_params.value(QStringLiteral("filterSize"), 3).toInt() | 1, 39);
        const int idx = cb->findData(fs);
        if (idx >= 0) cb->setCurrentIndex(idx);
    }
}
