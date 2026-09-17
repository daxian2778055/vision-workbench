#include "OtsuThresholdNode.h"
#include "DataObject.h"
#include "Port.h"

using namespace HalconCpp;

OtsuThresholdNode::OtsuThresholdNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("Otsu二值化"));
    m_type = IMAGE_PROCESSING;
}

void OtsuThresholdNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("区域"), PortDataType::Region);
    registerParams({
        makeEnumParam(QStringLiteral("lightDark"), 1,
                      {QStringLiteral("暗"), QStringLiteral("亮")},
                      QStringLiteral("提取方向")),
        // 搜索区域（ROI）：模块编辑器里拖框即写回；宽或高为 0 = 全图（原行为）
        makeIntParam(QStringLiteral("roiRow"), 0, 0, 100000, QStringLiteral("ROI 起始行")),
        makeIntParam(QStringLiteral("roiCol"), 0, 0, 100000, QStringLiteral("ROI 起始列")),
        makeIntParam(QStringLiteral("roiWidth"), 0, 0, 100000,
                     QStringLiteral("ROI 宽度（0=全图）")),
        makeIntParam(QStringLiteral("roiHeight"), 0, 0, 100000,
                     QStringLiteral("ROI 高度（0=全图）")),
    });
}

void OtsuThresholdNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        const int idx = qBound(0, m_params.value(QStringLiteral("lightDark"), 1).toInt(), 1);
        const char *dir = (idx == 0) ? "dark" : "light";

        // ROI：只在矩形内做 Otsu。用 reduce_domain 而不是裁剪——它只改"域"、不动图像矩阵，
        // 区域坐标仍是整图坐标；下面的输出图像用的是 **m_inputImage（原图）** 的尺寸，
        // 所以输出仍是整图大小，下游坐标系不变。
        HImage work(m_inputImage);
        {
            const int roiRow = m_params.value(QStringLiteral("roiRow"), 0).toInt();
            const int roiCol = m_params.value(QStringLiteral("roiCol"), 0).toInt();
            const int roiW = m_params.value(QStringLiteral("roiWidth"), 0).toInt();
            const int roiH = m_params.value(QStringLiteral("roiHeight"), 0).toInt();
            if (roiW > 0 && roiH > 0) {
                const int imgW = work.Width().I();
                const int imgH = work.Height().I();
                const int r1 = qBound(0, roiRow, imgH);
                const int c1 = qBound(0, roiCol, imgW);
                const int r2 = qBound(0, roiRow + roiH, imgH);
                const int c2 = qBound(0, roiCol + roiW, imgW);
                if (r2 > r1 && c2 > c1) {
                    HObject roiRegion, reduced;
                    GenRectangle1(&roiRegion, r1, c1, r2, c2);
                    ReduceDomain(work, roiRegion, &reduced);
                    work = HImage(reduced);
                }
            }
        }

        HRegion region;
        HTuple usedThreshold;
        BinaryThreshold(work, &region, "max_separability",
                        HTuple(dir), &usedThreshold);

        // 输出区域到端口 1
        auto regionObj = QSharedPointer<DataObject>::create();
        regionObj->setHRegion(region);
        setOutputData(1, regionObj);

        // 输出二值图像到端口 0
        HTuple w, h;
        GetImageSize(HImage(m_inputImage), &w, &h);
        HObject bin;
        RegionToBin(region, &bin, 255, 0, w, h);
        m_outputImage = bin;
    } catch (const HException &) {
        m_outputImage.Clear();
        setOutputData(1, QSharedPointer<DataObject>());
    }
}

RoiShape OtsuThresholdNode::geometryRoi() const
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

void OtsuThresholdNode::applyGeometryRoi(const RoiShape &shape)
{
    // 「清除几何」= 回到全图（对整图做 Otsu 本身是有意义的）
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
