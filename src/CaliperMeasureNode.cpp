#include "CaliperMeasureNode.h"
#include "DataObject.h"
#include "Port.h"
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>

using namespace HalconCpp;

CaliperMeasureNode::CaliperMeasureNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("卡尺测量"));
    m_type = MEASUREMENT;
}

void CaliperMeasureNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("测量结果"), PortDataType::Measure);
    addOutputPort(QStringLiteral("测量点"), PortDataType::XLD);
    registerParams({
        makeDoubleParam(QStringLiteral("row1"), 100.0, 0.0, 100000.0,
                        QStringLiteral("起点行"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("col1"), 100.0, 0.0, 100000.0,
                        QStringLiteral("起点列"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("row2"), 200.0, 0.0, 100000.0,
                        QStringLiteral("终点行"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("col2"), 200.0, 0.0, 100000.0,
                        QStringLiteral("终点列"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("measureLength"), 20.0, 1.0, 1000.0,
                        QStringLiteral("测量长度"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("measureWidth"), 5.0, 1.0, 200.0,
                        QStringLiteral("测量宽度"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("sigma"), 1.0, 0.1, 10.0,
                        QStringLiteral("平滑σ")),
        makeDoubleParam(QStringLiteral("threshold"), 30.0, 0.0, 255.0,
                        QStringLiteral("边缘阈值")),
        makeEnumParam(QStringLiteral("transition"), 0,
                      {QStringLiteral("正到负"), QStringLiteral("负到正"), QStringLiteral("全部")},
                      QStringLiteral("边缘极性")),
        makeEnumParam(QStringLiteral("select"), 0,
                      {QStringLiteral("第一个"), QStringLiteral("最后一个"), QStringLiteral("全部")},
                      QStringLiteral("边缘选择")),
    });
}

void CaliperMeasureNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        auto pv = [this](const QString &n, double d) { return m_params.value(n, d).toDouble(); };
        const double r1 = pv(QStringLiteral("row1"), 100);
        const double c1 = pv(QStringLiteral("col1"), 100);
        const double r2 = pv(QStringLiteral("row2"), 200);
        const double c2 = pv(QStringLiteral("col2"), 200);
        const double ml = pv(QStringLiteral("measureLength"), 20);
        const double mw = pv(QStringLiteral("measureWidth"), 5);
        const double sg = pv(QStringLiteral("sigma"), 1.0);
        const double th = pv(QStringLiteral("threshold"), 30);
        const QStringList trs = {QStringLiteral("positive"), QStringLiteral("negative"), QStringLiteral("all")};
        const QStringList sels = {QStringLiteral("first"), QStringLiteral("last"), QStringLiteral("all")};
        const int tr = qBound(0, m_params.value(QStringLiteral("transition"), 0).toInt(), 2);
        const int sel = qBound(0, m_params.value(QStringLiteral("select"), 0).toInt(), 2);

        HTuple handle;
        CreateMetrologyModel(&handle);
        HTuple index;
        // HALCON 24.11 签名：第 10/11 参为 GenParamName/GenParamValue（此处设置边缘极性）
        AddMetrologyObjectLineMeasure(handle, r1, c1, r2, c2, ml, mw, sg, th,
                                      "measure_transition", trs[tr].toStdString().c_str(), &index);
        SetMetrologyObjectParam(handle, index, "measure_select", sels[sel].toStdString().c_str());
        ApplyMetrologyModel(HImage(m_inputImage), handle);

        HTuple row, col;
        HObject measuresContours;
        GetMetrologyObjectMeasures(&measuresContours, handle, index, "all", &row, &col);
        HTuple n;
        TupleLength(row, &n);

        MeasureResult res;
        res.valueName = QStringLiteral("边缘点数");
        res.type = QStringLiteral("caliper");
        res.valid = n.I() > 0;
        res.value = n.I();
        if (n.I() > 0) {
            res.point1 = QPointF(col[0].D(), row[0].D());
            if (n.I() > 1)
                res.point2 = QPointF(col[n.I() - 1].D(), row[n.I() - 1].D());
        }
        ClearMetrologyModel(handle);

        auto resObj = QSharedPointer<DataObject>::create();
        resObj->setMeasureResult(res);
        setOutputData(1, resObj);

        auto xldObj = QSharedPointer<DataObject>::create();
        xldObj->setHXLDCont(HXLDCont(measuresContours));
        setOutputData(2, xldObj);

        m_outputImage = m_inputImage;
    } catch (const HException &) {
        m_outputImage.Clear();
        setOutputData(1, QSharedPointer<DataObject>());
        setOutputData(2, QSharedPointer<DataObject>());
        m_params["moduleStatus"] = false;
    }
}

RoiShape CaliperMeasureNode::geometryRoi() const
{
    RoiShape s;
    const double r1 = m_params.value(QStringLiteral("row1"), 0.0).toDouble();
    const double c1 = m_params.value(QStringLiteral("col1"), 0.0).toDouble();
    const double r2 = m_params.value(QStringLiteral("row2"), 0.0).toDouble();
    const double c2 = m_params.value(QStringLiteral("col2"), 0.0).toDouble();
    if (r1 == 0.0 && c1 == 0.0 && r2 == 0.0 && c2 == 0.0) {
        return s;   // 四个值全 0 = 还没设置过（含"清除几何"之后）：不显示
    }
    s.type = RoiType::Line;
    s.p1 = QPointF(c1, r1);   // x=列, y=行
    s.p2 = QPointF(c2, r2);
    return s;
}

void CaliperMeasureNode::applyGeometryRoi(const RoiShape &shape)
{
    // 「清除几何」：卡尺的测量线是**必需**参数，归零而不是留旧值——节点随后会以"区域无效"
    // 明确失败，比悄悄沿用上一次的测量线安全得多。
    if (shape.type == RoiType::None) {
        setParam(QStringLiteral("row1"), 0.0);
        setParam(QStringLiteral("col1"), 0.0);
        setParam(QStringLiteral("row2"), 0.0);
        setParam(QStringLiteral("col2"), 0.0);
        return;
    }
    if (shape.type != RoiType::Line) {
        return;
    }
    setParam(QStringLiteral("col1"), shape.p1.x());
    setParam(QStringLiteral("row1"), shape.p1.y());
    setParam(QStringLiteral("col2"), shape.p2.x());
    setParam(QStringLiteral("row2"), shape.p2.y());
}

QWidget *CaliperMeasureNode::createParamPanel()
{
    auto *panel = createAutoParamPanel();
    auto *layout = panel->findChild<QVBoxLayout *>();
    if (!layout) layout = new QVBoxLayout(panel);
    auto *btn = new QPushButton(QStringLiteral("在画布上设置搜索区域"));
    connect(btn, &QPushButton::clicked, this, &CaliperMeasureNode::roiPickRequested);
    layout->addWidget(btn);
    auto *tip = new QLabel(QStringLiteral("<i>提示：点击后在图像视图上拖拽绘制搜索线/圆，"
                                          "松手后自动写回坐标参数。</i>"));
    tip->setWordWrap(true);
    layout->addWidget(tip);
    layout->addStretch();
    return panel;
}

void CaliperMeasureNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}
