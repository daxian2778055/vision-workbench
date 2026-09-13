#include "AreaNode.h"
#include "DataObject.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QSignalBlocker>

using namespace HalconCpp;

AreaNode::AreaNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("\u9762\u79EF"));
    m_type = MEASUREMENT;
}

void AreaNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("面积"), PortDataType::Number);
    m_params[QStringLiteral("minGray")] = 128.0;
    m_params[QStringLiteral("maxGray")] = 255.0;
}

void AreaNode::run(bool /*autoSwitch*/)
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
        HObject region, connected, selected;
        Threshold(gray, &region, minG, maxG);
        Connection(region, &connected);
        SelectShape(connected, &selected, "area", "and", HTuple(10.0), HTuple(1e12));
        HTuple nObj, w, h;
        CountObj(selected, &nObj);
        GetImageSize(gray, &w, &h);
        const int nc = static_cast<int>(nObj.D());

        if (nc <= 0) {
            HObject canvas;
            GenImageConst(&canvas, "byte", w, h);
            m_params[QStringLiteral("totalArea")] = 0.0;
            m_outputImage = canvas;
            auto zeroObj = QSharedPointer<DataObject>::create();
            zeroObj->setValue(0.0);
            setOutputData(1, zeroObj);
        } else {
            HObject united, bin;
            Union1(selected, &united);
            HTuple areaSum, rr, cc;
            AreaCenter(united, &areaSum, &rr, &cc);
            m_params[QStringLiteral("totalArea")] = areaSum.D();
            m_params[QStringLiteral("regionCentroidRow")] = rr.D();
            m_params[QStringLiteral("regionCentroidCol")] = cc.D();
            RegionToBin(united, &bin, HTuple(220), HTuple(0), w, h);
            m_outputImage = bin;

            // 输出面积端口
            auto areaObj = QSharedPointer<DataObject>::create();
            areaObj->setValue(areaSum.D());
            setOutputData(1, areaObj);
        }
    } catch (const HException &e) {
        m_outputImage.Clear();
    }
}

QWidget *AreaNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>\u9762\u79EF\u6D4B\u91CF\uFF08\u9608\u503C\u5206\u5272\uFF09</b>")));

    auto *minS = new QSpinBox();
    minS->setObjectName(QStringLiteral("areaMinGray"));
    minS->setRange(0, 255);
    auto *maxS = new QSpinBox();
    maxS->setObjectName(QStringLiteral("areaMaxGray"));
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

    double area = m_params.value(QStringLiteral("totalArea"), 0.0).toDouble();
    auto *resultLabel = new QLabel(QStringLiteral("\u9762\u79EF: %1 \u50CF\u7D20").arg(area, 0, 'f', 1));
    resultLabel->setObjectName(QStringLiteral("areaResult"));
    layout->addWidget(resultLabel);

    layout->addStretch();
    return panel;
}

void AreaNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    if (auto *w = panel->findChild<QSpinBox *>(QStringLiteral("areaMinGray"))) {
        QSignalBlocker b(w);
        w->setValue(m_params.value(QStringLiteral("minGray"), 128).toInt());
    }
    if (auto *w = panel->findChild<QSpinBox *>(QStringLiteral("areaMaxGray"))) {
        QSignalBlocker b(w);
        w->setValue(m_params.value(QStringLiteral("maxGray"), 255).toInt());
    }
    if (auto *lbl = panel->findChild<QLabel *>(QStringLiteral("areaResult"))) {
        double area = m_params.value(QStringLiteral("totalArea"), 0.0).toDouble();
        lbl->setText(QStringLiteral("\u9762\u79EF: %1 \u50CF\u7D20").arg(area, 0, 'f', 1));
    }
}
