#include "BlobAnalysisNode.h"
#include "DataObject.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSignalBlocker>

using namespace HalconCpp;

BlobAnalysisNode::BlobAnalysisNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("Blob\u5206\u6790"));
    m_type = SHAPE_ANALYSIS;
}

void BlobAnalysisNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("区域"), PortDataType::Region);
    addOutputPort(QStringLiteral("区域数"), PortDataType::Number);
    m_params[QStringLiteral("minGray")] = 128.0;
    m_params[QStringLiteral("maxGray")] = 255.0;
    m_params[QStringLiteral("minArea")] = 100.0;
}

void BlobAnalysisNode::run(bool /*autoSwitch*/)
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
        const double minArea = m_params.value(QStringLiteral("minArea"), 100.0).toDouble();
        HObject region, connected, selected;
        Threshold(gray, &region, minG, maxG);
        Connection(region, &connected);
        SelectShape(connected, &selected, "area", "and", HTuple(minArea), HTuple(1e12));
        HTuple w, h;
        GetImageSize(gray, &w, &h);
        HTuple n;
        CountObj(selected, &n);
        const int rc = static_cast<int>(n.D());
        m_params[QStringLiteral("regionCount")] = rc;

        // 输出区域与区域数端口
        auto regionObj = QSharedPointer<DataObject>::create();
        regionObj->setHRegion(HRegion(selected));
        setOutputData(1, regionObj);
        auto numObj = QSharedPointer<DataObject>::create();
        numObj->setValue(double(rc));
        setOutputData(2, numObj);

        if (rc <= 0) {
            HObject canvas;
            GenImageConst(&canvas, "byte", w, h);
            m_outputImage = canvas;
        } else {
            HObject united, bin;
            Union1(selected, &united);
            RegionToBin(united, &bin, HTuple(255), HTuple(0), w, h);
            m_outputImage = bin;
        }
    } catch (const HException &e) {
        m_outputImage.Clear();
    }
}

QWidget *BlobAnalysisNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>Blob \u5206\u6790</b>")));

    auto *minS = new QSpinBox();
    minS->setObjectName(QStringLiteral("blobMinGray"));
    minS->setRange(0, 255);
    auto *maxS = new QSpinBox();
    maxS->setObjectName(QStringLiteral("blobMaxGray"));
    maxS->setRange(0, 255);
    auto *area = new QDoubleSpinBox();
    area->setObjectName(QStringLiteral("blobMinArea"));
    area->setRange(1.0, 1e9);
    area->setDecimals(0);
    area->setSingleStep(10.0);

    {
        QSignalBlocker bm(minS), bM(maxS), ba(area);
        minS->setValue(m_params.value(QStringLiteral("minGray"), 128).toInt());
        maxS->setValue(m_params.value(QStringLiteral("maxGray"), 255).toInt());
        area->setValue(m_params.value(QStringLiteral("minArea"), 100.0).toDouble());
    }

    auto saveAll = [this, minS, maxS, area]() {
        setParam(QStringLiteral("minGray"), minS->value());
        setParam(QStringLiteral("maxGray"), maxS->value());
        setParam(QStringLiteral("minArea"), area->value());
    };
    connect(minS, qOverload<int>(&QSpinBox::valueChanged), this, saveAll);
    connect(maxS, qOverload<int>(&QSpinBox::valueChanged), this, saveAll);
    connect(area, qOverload<double>(&QDoubleSpinBox::valueChanged), this, saveAll);

    layout->addWidget(new QLabel(QStringLiteral("\u7070\u5EA6\u4E0B\u9650:")));
    layout->addWidget(minS);
    layout->addWidget(new QLabel(QStringLiteral("\u7070\u5EA6\u4E0A\u9650:")));
    layout->addWidget(maxS);
    layout->addWidget(new QLabel(QStringLiteral("\u6700\u5C0F\u9762\u79EF(\u50CF\u7D20):")));
    layout->addWidget(area);
    layout->addStretch();
    return panel;
}

void BlobAnalysisNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    if (auto *w = panel->findChild<QSpinBox *>(QStringLiteral("blobMinGray"))) {
        QSignalBlocker b(w);
        w->setValue(m_params.value(QStringLiteral("minGray"), 128).toInt());
    }
    if (auto *w = panel->findChild<QSpinBox *>(QStringLiteral("blobMaxGray"))) {
        QSignalBlocker b(w);
        w->setValue(m_params.value(QStringLiteral("maxGray"), 255).toInt());
    }
    if (auto *w = panel->findChild<QDoubleSpinBox *>(QStringLiteral("blobMinArea"))) {
        QSignalBlocker b(w);
        w->setValue(m_params.value(QStringLiteral("minArea"), 100.0).toDouble());
    }
}
