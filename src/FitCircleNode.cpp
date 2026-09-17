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

        // ROI：只在用户拖出的矩形内找区域（宽或高为 0 = 全图，与原行为一致）。
        // 先与图像求交再交给 HALCON，避免越界矩形触发异常；结果坐标加回偏移
        // （见下面 circleRow/circleCol），否则输出会变成 ROI 局部坐标。
        int roiOffsetRow = 0;
        int roiOffsetCol = 0;
        {
            const int roiRow = m_params.value(QStringLiteral("roiRow"), 0).toInt();
            const int roiCol = m_params.value(QStringLiteral("roiCol"), 0).toInt();
            const int roiW = m_params.value(QStringLiteral("roiWidth"), 0).toInt();
            const int roiH = m_params.value(QStringLiteral("roiHeight"), 0).toInt();
            if (roiW > 0 && roiH > 0) {
                const int imgW = gray.Width().I();
                const int imgH = gray.Height().I();
                const int r1 = qBound(0, roiRow, imgH);
                const int c1 = qBound(0, roiCol, imgW);
                const int r2 = qBound(0, roiRow + roiH, imgH);
                const int c2 = qBound(0, roiCol + roiW, imgW);
                if (r2 > r1 && c2 > c1) {
                    HImage cropped;
                    CropRectangle1(gray, &cropped, r1, c1, r2, c2);
                    gray = cropped;
                    roiOffsetRow = r1;
                    roiOffsetCol = c1;
                }
            }
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
                // 加回 ROI 偏移：圆心对外始终是整图坐标；半径不受平移影响，原样带出
                const double fitRow = row[0].D() + roiOffsetRow;
                const double fitCol = col[0].D() + roiOffsetCol;
                const double fitRadius = radius[0].D();

                m_params[QStringLiteral("circleRow")] = fitRow;
                m_params[QStringLiteral("circleCol")] = fitCol;
                m_params[QStringLiteral("circleRadius")] = fitRadius;

                // 输出拟合圆到 Measure 端口
                MeasureResult res;
                res.type = QStringLiteral("circle");
                res.valueName = QStringLiteral("圆心/半径");
                res.valid = true;
                res.value = fitRadius;
                res.point1 = QPointF(fitCol, fitRow);
                res.extraValues = QVector<double>({fitRow, fitCol, fitRadius});
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

RoiShape FitCircleNode::geometryRoi() const
{
    RoiShape s;
    const int w = m_params.value(QStringLiteral("roiWidth"), 0).toInt();
    const int h = m_params.value(QStringLiteral("roiHeight"), 0).toInt();
    if (w <= 0 || h <= 0) {
        return s;   // 未设置 ROI：不显示框（type 保持默认 None）
    }
    s.type = RoiType::Rect;
    s.p1 = QPointF(m_params.value(QStringLiteral("roiCol"), 0).toInt(),
                   m_params.value(QStringLiteral("roiRow"), 0).toInt());
    s.p2 = QPointF(s.p1.x() + w, s.p1.y() + h);
    return s;
}

void FitCircleNode::applyGeometryRoi(const RoiShape &shape)
{
    // 「清除几何」传进来的是默认构造的 RoiShape（type=None）→ 回到全图
    if (shape.type == RoiType::None) {
        setParam(QStringLiteral("roiRow"), 0);
        setParam(QStringLiteral("roiCol"), 0);
        setParam(QStringLiteral("roiWidth"), 0);
        setParam(QStringLiteral("roiHeight"), 0);
        return;
    }
    if (shape.type != RoiType::Rect) {
        return;
    }
    const QRectF r = QRectF(shape.p1, shape.p2).normalized();
    setParam(QStringLiteral("roiCol"), qRound(r.left()));
    setParam(QStringLiteral("roiRow"), qRound(r.top()));
    setParam(QStringLiteral("roiWidth"), qRound(r.width()));
    setParam(QStringLiteral("roiHeight"), qRound(r.height()));
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
