#include "DynThresholdNode.h"
#include "DataObject.h"
#include "Port.h"

using namespace HalconCpp;

DynThresholdNode::DynThresholdNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("动态阈值"));
    m_type = IMAGE_PROCESSING;
}

void DynThresholdNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("区域"), PortDataType::Region);
    registerParams({
        makeDoubleParam(QStringLiteral("offset"), 10.0, 0.0, 255.0,
                        QStringLiteral("灰度偏移")),
        makeIntParam(QStringLiteral("maskWidth"), 15, 1, 200,
                     QStringLiteral("均值窗口宽"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("maskHeight"), 15, 1, 200,
                     QStringLiteral("均值窗口高"), QStringLiteral("px")),
        makeEnumParam(QStringLiteral("lightDark"), 1,
                      {QStringLiteral("暗"), QStringLiteral("亮"), QStringLiteral("相等"), QStringLiteral("不等")},
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

void DynThresholdNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        const double offset = m_params.value(QStringLiteral("offset"), 10.0).toDouble();
        const int mw = m_params.value(QStringLiteral("maskWidth"), 15).toInt();
        const int mh = m_params.value(QStringLiteral("maskHeight"), 15).toInt();
        const QStringList dirs = {QStringLiteral("dark"), QStringLiteral("light"),
                                  QStringLiteral("equal"), QStringLiteral("not_equal")};
        const int idx = qBound(0, m_params.value(QStringLiteral("lightDark"), 1).toInt(), 3);

        // ROI：只在矩形内做动态阈值。用 reduce_domain 而不是裁剪——它只改"域"、不动图像矩阵，
        // 区域坐标仍是整图坐标；均值图也基于 reduce 后的域计算（窗口在域边界按 HALCON 默认
        // 边界处理），输出图像用的是 **m_inputImage（原图）** 的尺寸，下游坐标系不变。
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

        HObject mean;
        MeanImage(work, &mean, mw, mh);
        HRegion region;
        DynThreshold(work, HImage(mean), &region, offset,
                     HTuple(dirs[idx].toStdString().c_str()));

        auto regionObj = QSharedPointer<DataObject>::create();
        regionObj->setHRegion(region);
        setOutputData(1, regionObj);

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

RoiShape DynThresholdNode::geometryRoi() const
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

void DynThresholdNode::applyGeometryRoi(const RoiShape &shape)
{
    // 「清除几何」= 回到全图（对整图做动态阈值本身是有意义的）
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
