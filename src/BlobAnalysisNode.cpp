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
    // 搜索区域（ROI）：模块编辑器里拖框即写回；宽或高为 0 = 全图（原行为）
    m_params[QStringLiteral("roiRow")] = 0;
    m_params[QStringLiteral("roiCol")] = 0;
    m_params[QStringLiteral("roiWidth")] = 0;
    m_params[QStringLiteral("roiHeight")] = 0;
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

        // 尺寸必须在 reduce **之前**取：reduce_domain 只改"域"、不动图像矩阵，但输出图像是用
        // w/h 重建的——若用 reduce 之后的尺寸，输出会变成 ROI 大小，下游坐标系整体错位。
        // 这是本文件最容易踩的一处，别"顺手"把这两行挪到下面去。
        HTuple w, h;
        GetImageSize(gray, &w, &h);

        // ROI：只在矩形内分割+连通域分析。用 reduce_domain 而不是裁剪——它保留原图坐标系，
        // 区域与连通域的坐标直接就是整图坐标，不需要任何偏移。
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
        const double minArea = m_params.value(QStringLiteral("minArea"), 100.0).toDouble();
        HObject region, connected, selected;
        Threshold(gray, &region, minG, maxG);
        Connection(region, &connected);
        SelectShape(connected, &selected, "area", "and", HTuple(minArea), HTuple(1e12));
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

RoiShape BlobAnalysisNode::geometryRoi() const
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

void BlobAnalysisNode::applyGeometryRoi(const RoiShape &shape)
{
    // 「清除几何」= 回到全图（对整图做阈值+连通域本身是有意义的）
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
