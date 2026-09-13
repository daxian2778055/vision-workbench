#include "FitLineNode.h"
#include "DataObject.h"
#include "AppLog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QSignalBlocker>
#include <cmath>

using namespace HalconCpp;

FitLineNode::FitLineNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("直线拟合"));
    m_type = MEASUREMENT;
}

void FitLineNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("拟合结果"), PortDataType::Measure);
    m_params[QStringLiteral("minGray")] = 128;
    m_params[QStringLiteral("maxGray")] = 255;
    m_params[QStringLiteral("lineRow1")] = 0.0;
    m_params[QStringLiteral("lineCol1")] = 0.0;
    m_params[QStringLiteral("lineRow2")] = 0.0;
    m_params[QStringLiteral("lineCol2")] = 0.0;
}

void FitLineNode::run(bool /*autoSwitch*/)
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

        const int minG = m_params.value(QStringLiteral("minGray"), 128).toInt();
        const int maxG = m_params.value(QStringLiteral("maxGray"), 255).toInt();
        HObject region, connected, selected;
        Threshold(gray, &region, minG, maxG);
        Connection(region, &connected);
        SelectShape(connected, &selected, "area", "and", HTuple(10), HTuple(1e9));

        // Try to fit line to selected regions
        try {
            HObject edges;
            SelectShape(selected, &edges, "width", "and", HTuple(2), HTuple(200));

            // Get contour from region border: GenContourRegionXld(Regions, Contours, Mode)
            HObject border;
            Boundary(edges, &border, "inner");
            HObject contour;
            GenContourRegionXld(border, &contour, "border");

            HTuple rowBegin, colBegin, rowEnd, colEnd;
            HTuple nr, nc, dist;
            FitLineContourXld(contour, "tukey", -1, 0, 5, 2,
                            &rowBegin, &colBegin, &rowEnd, &colEnd,
                            &nr, &nc, &dist);

            if (rowBegin.Length() > 0) {
                m_params[QStringLiteral("lineRow1")] = rowBegin[0].D();
                m_params[QStringLiteral("lineCol1")] = colBegin[0].D();
                m_params[QStringLiteral("lineRow2")] = rowEnd[0].D();
                m_params[QStringLiteral("lineCol2")] = colEnd[0].D();

                // 输出拟合直线到 Measure 端口
                MeasureResult res;
                res.type = QStringLiteral("line");
                res.valueName = QStringLiteral("直线参数");
                res.valid = true;
                res.point1 = QPointF(colBegin[0].D(), rowBegin[0].D());
                res.point2 = QPointF(colEnd[0].D(), rowEnd[0].D());
                double ang = std::atan2(rowEnd[0].D() - rowBegin[0].D(),
                                        colEnd[0].D() - colBegin[0].D());
                res.value = ang * 180.0 / 3.14159265358979323846;
                res.extraValues = QVector<double>({rowBegin[0].D(), colBegin[0].D(),
                                                   rowEnd[0].D(), colEnd[0].D()});
                auto resObj = QSharedPointer<DataObject>::create();
                resObj->setMeasureResult(res);
                setOutputData(1, resObj);

                // 输出干净图像（叠加线在显示阶段由 UI 绘制，避免 ConcatObj→HImage 丢失）
                m_outputImage = gray;
            } else {
                setOutputData(1, QSharedPointer<DataObject>());
                m_outputImage = gray;
            }
        } catch (const HException &) {
            m_outputImage = gray;
        }

        HalconCpp::HImage output(m_outputImage);
        if (output.IsInitialized()) {
            auto outObj = QSharedPointer<DataObject>::create();
            outObj->setHImage(output);
            setOutputData(0, outObj);
        }
        m_params["moduleStatus"] = true;
    } catch (const HException &e) {
        VFP_DEBUG << "FitLineNode error:" << e.ErrorMessage().TextA();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *FitLineNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>直线拟合</b>")));

    auto *minS = new QSpinBox();
    minS->setObjectName(QStringLiteral("flMinGray"));
    minS->setRange(0, 255);
    auto *maxS = new QSpinBox();
    maxS->setObjectName(QStringLiteral("flMaxGray"));
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

    layout->addWidget(new QLabel(QStringLiteral("灰度下限:")));
    layout->addWidget(minS);
    layout->addWidget(new QLabel(QStringLiteral("灰度上限:")));
    layout->addWidget(maxS);

    auto *resultLabel = new QLabel();
    resultLabel->setObjectName(QStringLiteral("flResult"));
    double r1 = m_params.value(QStringLiteral("lineRow1"), 0.0).toDouble();
    double c1 = m_params.value(QStringLiteral("lineCol1"), 0.0).toDouble();
    double r2 = m_params.value(QStringLiteral("lineRow2"), 0.0).toDouble();
    double c2 = m_params.value(QStringLiteral("lineCol2"), 0.0).toDouble();
    resultLabel->setText(QStringLiteral("直线: (%1,%2) -> (%3,%4)")
        .arg(r1, 0, 'f', 1).arg(c1, 0, 'f', 1).arg(r2, 0, 'f', 1).arg(c2, 0, 'f', 1));
    layout->addWidget(resultLabel);

    layout->addStretch();
    return panel;
}

void FitLineNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    if (auto *w = panel->findChild<QSpinBox *>(QStringLiteral("flMinGray"))) {
        QSignalBlocker b(w);
        w->setValue(m_params.value(QStringLiteral("minGray"), 128).toInt());
    }
    if (auto *w = panel->findChild<QSpinBox *>(QStringLiteral("flMaxGray"))) {
        QSignalBlocker b(w);
        w->setValue(m_params.value(QStringLiteral("maxGray"), 255).toInt());
    }
    if (auto *lbl = panel->findChild<QLabel *>(QStringLiteral("flResult"))) {
        double r1 = m_params.value(QStringLiteral("lineRow1"), 0.0).toDouble();
        double c1 = m_params.value(QStringLiteral("lineCol1"), 0.0).toDouble();
        double r2 = m_params.value(QStringLiteral("lineRow2"), 0.0).toDouble();
        double c2 = m_params.value(QStringLiteral("lineCol2"), 0.0).toDouble();
        lbl->setText(QStringLiteral("直线: (%1,%2) -> (%3,%4)")
            .arg(r1, 0, 'f', 1).arg(c1, 0, 'f', 1).arg(r2, 0, 'f', 1).arg(c2, 0, 'f', 1));
    }
}
