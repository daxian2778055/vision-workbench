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
    // 搜索区域（ROI）：模块编辑器里拖框即写回；宽或高为 0 = 全图（原行为）
    m_params[QStringLiteral("roiRow")] = 0;
    m_params[QStringLiteral("roiCol")] = 0;
    m_params[QStringLiteral("roiWidth")] = 0;
    m_params[QStringLiteral("roiHeight")] = 0;
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

        // 尺寸必须在 reduce **之前**取：reduce_domain 只改"域"、不动图像矩阵，但输出图像是用
        // w/h 重建的——若用 reduce 之后的尺寸，输出会变成 ROI 大小，下游坐标系整体错位。
        // 这是本文件最容易踩的一处，别"顺手"把这两行挪到下面去。
        HTuple w, h;
        GetImageSize(gray, &w, &h);

        // ROI：只在矩形内分割。用 reduce_domain 而不是裁剪——它保留原图坐标系，
        // 阈值区域直接就是整图坐标，不需要任何偏移。
        {
            const int roiRow = m_params.value(QStringLiteral("roiRow"), 0).toInt();
            const int roiCol = m_params.value(QStringLiteral("roiCol"), 0).toInt();
            const int roiW = m_params.value(QStringLiteral("roiWidth"), 0).toInt();
            const int roiH = m_params.value(QStringLiteral("roiHeight"), 0).toInt();
            if (roiW > 0 && roiH > 0) {
                const int imgW = w.I();
                const int imgH = h.I();
                const int r1 = qBound(0, roiRow, imgH);
                const int c1 = qBound(0, roiCol, imgW);
                const int r2 = qBound(0, roiRow + roiH, imgH);
                const int c2 = qBound(0, roiCol + roiW, imgW);
                if (r2 > r1 && c2 > c1) {
                    HObject roiRegion, reduced;
                    GenRectangle1(&roiRegion, r1, c1, r2, c2);
                    ReduceDomain(gray, roiRegion, &reduced);
                    gray = HImage(reduced);
                }
            }
        }

        const HTuple minG = m_params.value(QStringLiteral("minGray"), 128.0).toDouble();
        const HTuple maxG = m_params.value(QStringLiteral("maxGray"), 255.0).toDouble();
        HObject region, bin;
        Threshold(gray, &region, minG, maxG);
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

RoiShape ThresholdNode::geometryRoi() const
{
    RoiShape s;
    const int rw = m_params.value(QStringLiteral("roiWidth"), 0).toInt();
    const int rh = m_params.value(QStringLiteral("roiHeight"), 0).toInt();
    if (rw <= 0 || rh <= 0) {
        return s;   // 未设置 ROI：不显示框（type 保持默认 None）
    }
    s.type = RoiType::Rect;
    s.p1 = QPointF(m_params.value(QStringLiteral("roiCol"), 0).toInt(),
                   m_params.value(QStringLiteral("roiRow"), 0).toInt());
    s.p2 = QPointF(s.p1.x() + rw, s.p1.y() + rh);
    return s;
}

void ThresholdNode::applyGeometryRoi(const RoiShape &shape)
{
    // 「清除几何」= 回到全图（对整图分割本身是有意义的，与拟合/边缘点一致）
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
