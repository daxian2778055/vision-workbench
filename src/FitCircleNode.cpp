#include "FitCircleNode.h"
#include "DataObject.h"
#include "AppLog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QSignalBlocker>

using namespace HalconCpp;

FitCircleNode::FitCircleNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("圆拟合"));
    m_type = MEASUREMENT;
}

void FitCircleNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("拟合结果"), PortDataType::Measure);
    m_params[QStringLiteral("minGray")] = 128;
    m_params[QStringLiteral("maxGray")] = 255;
    m_params[QStringLiteral("circleRow")] = 0.0;
    m_params[QStringLiteral("circleCol")] = 0.0;
    m_params[QStringLiteral("circleRadius")] = 0.0;
}

void FitCircleNode::run(bool /*autoSwitch*/)
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

        try {
            HObject border;
            Boundary(selected, &border, "inner");
            HObject contour;
            GenContourRegionXld(border, &contour, "border");

            HTuple row, col, radius, startPhi, endPhi, pointOrder;
            FitCircleContourXld(contour, "algebraic", -1, 0, 0, 3, 2,
                              &row, &col, &radius, &startPhi, &endPhi, &pointOrder);

            if (radius.Length() > 0 && radius[0].D() > 0) {
                m_params[QStringLiteral("circleRow")] = row[0].D();
                m_params[QStringLiteral("circleCol")] = col[0].D();
                m_params[QStringLiteral("circleRadius")] = radius[0].D();

                // 输出拟合圆到 Measure 端口
                MeasureResult res;
                res.type = QStringLiteral("circle");
                res.valueName = QStringLiteral("圆心/半径");
                res.valid = true;
                res.value = radius[0].D();
                res.point1 = QPointF(col[0].D(), row[0].D());
                res.extraValues = QVector<double>({row[0].D(), col[0].D(), radius[0].D()});
                auto resObj = QSharedPointer<DataObject>::create();
                resObj->setMeasureResult(res);
                setOutputData(1, resObj);

                // 输出干净图像（叠加标记在显示阶段绘制，避免 ConcatObj→HImage 丢失）
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
        VFP_DEBUG << "FitCircleNode error:" << e.ErrorMessage().TextA();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *FitCircleNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>圆拟合</b>")));

    auto *minS = new QSpinBox();
    minS->setObjectName(QStringLiteral("fcMinGray"));
    minS->setRange(0, 255);
    auto *maxS = new QSpinBox();
    maxS->setObjectName(QStringLiteral("fcMaxGray"));
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
    resultLabel->setObjectName(QStringLiteral("fcResult"));
    double r = m_params.value(QStringLiteral("circleRow"), 0.0).toDouble();
    double c = m_params.value(QStringLiteral("circleCol"), 0.0).toDouble();
    double rad = m_params.value(QStringLiteral("circleRadius"), 0.0).toDouble();
    resultLabel->setText(QStringLiteral("圆心: (%1, %2)  半径: %3")
        .arg(r, 0, 'f', 1).arg(c, 0, 'f', 1).arg(rad, 0, 'f', 2));
    layout->addWidget(resultLabel);

    layout->addStretch();
    return panel;
}

void FitCircleNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    if (auto *w = panel->findChild<QSpinBox *>(QStringLiteral("fcMinGray"))) {
        QSignalBlocker b(w);
        w->setValue(m_params.value(QStringLiteral("minGray"), 128).toInt());
    }
    if (auto *w = panel->findChild<QSpinBox *>(QStringLiteral("fcMaxGray"))) {
        QSignalBlocker b(w);
        w->setValue(m_params.value(QStringLiteral("maxGray"), 255).toInt());
    }
    if (auto *lbl = panel->findChild<QLabel *>(QStringLiteral("fcResult"))) {
        double r = m_params.value(QStringLiteral("circleRow"), 0.0).toDouble();
        double c = m_params.value(QStringLiteral("circleCol"), 0.0).toDouble();
        double rad = m_params.value(QStringLiteral("circleRadius"), 0.0).toDouble();
        lbl->setText(QStringLiteral("圆心: (%1, %2)  半径: %3")
            .arg(r, 0, 'f', 1).arg(c, 0, 'f', 1).arg(rad, 0, 'f', 2));
    }
}
