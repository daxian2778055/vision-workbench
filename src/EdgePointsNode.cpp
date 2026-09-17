#include "EdgePointsNode.h"
#include "DataObject.h"
#include "Port.h"

using namespace HalconCpp;

EdgePointsNode::EdgePointsNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("亚像素边缘点"));
    m_type = SHAPE_ANALYSIS;
}

void EdgePointsNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("边缘轮廓"), PortDataType::XLD);
    addOutputPort(QStringLiteral("结果"), PortDataType::String);
    registerParams({
        makeEnumParam(QStringLiteral("filter"), 0,
                      {QStringLiteral("canny"), QStringLiteral("deriche"), QStringLiteral("lanser"), QStringLiteral("shen")},
                      QStringLiteral("滤波器")),
        makeDoubleParam(QStringLiteral("alpha"), 1.0, 0.1, 10.0,
                        QStringLiteral("平滑系数")),
        makeDoubleParam(QStringLiteral("low"), 20.0, 0.0, 255.0,
                        QStringLiteral("低阈值")),
        makeDoubleParam(QStringLiteral("high"), 80.0, 0.0, 255.0,
                        QStringLiteral("高阈值")),
    });
}

void EdgePointsNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        const QStringList filters = {QStringLiteral("canny"), QStringLiteral("deriche"),
                                     QStringLiteral("lanser"), QStringLiteral("shen")};
        const int idx = qBound(0, m_params.value(QStringLiteral("filter"), 0).toInt(), 3);
        const double al = m_params.value(QStringLiteral("alpha"), 1.0).toDouble();
        const double lo = m_params.value(QStringLiteral("low"), 20.0).toDouble();
        const double hi = m_params.value(QStringLiteral("high"), 80.0).toDouble();

        // ROI：只在矩形内提边缘。这里用 reduce_domain 而不是 crop_rectangle1——
        // reduce_domain 保留**原图坐标系**，输出的 XLD 直接就是整图坐标，一个偏移都不用算；
        // 若换成裁剪，每个输出坐标都得手工加回偏移，很容易漏（前几批反复处理的就是这个坑）。
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

        HObject edges;
        EdgesSubPix(work, &edges, HTuple(filters[idx].toStdString().c_str()), al, lo, hi);

        auto xldObj = QSharedPointer<DataObject>::create();
        xldObj->setHXLDCont(HXLDCont(edges));
        setOutputData(1, xldObj);

        // 结果端口：边缘点数
        HTuple n;
        CountObj(edges, &n);
        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(QStringLiteral("边缘轮廓数: %1").arg(n.I()));
        setOutputData(2, strObj);

        // 透传图像
        m_outputImage = m_inputImage;
    } catch (const HException &) {
        m_outputImage.Clear();
        setOutputData(1, QSharedPointer<DataObject>());
        setOutputData(2, QSharedPointer<DataObject>());
    }
}

RoiShape EdgePointsNode::geometryRoi() const
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

void EdgePointsNode::applyGeometryRoi(const RoiShape &shape)
{
    // 「清除几何」= 回到全图（整图提边缘本身是有意义的，与拟合族一致）
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
