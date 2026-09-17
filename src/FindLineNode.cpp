#include "FindLineNode.h"
#include "DataObject.h"
#include "Port.h"
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <cmath>

using namespace HalconCpp;

FindLineNode::FindLineNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("线段查找"));
    m_type = MEASUREMENT;
}

void FindLineNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("测量结果"), PortDataType::Measure);
    addOutputPort(QStringLiteral("拟合轮廓"), PortDataType::XLD);
    registerParams({
        makeDoubleParam(QStringLiteral("row1"), 100.0, 0.0, 100000.0,
                        QStringLiteral("搜索起点行"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("col1"), 100.0, 0.0, 100000.0,
                        QStringLiteral("搜索起点列"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("row2"), 200.0, 0.0, 100000.0,
                        QStringLiteral("搜索终点行"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("col2"), 200.0, 0.0, 100000.0,
                        QStringLiteral("搜索终点列"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("measureLength"), 30.0, 1.0, 1000.0,
                        QStringLiteral("测量长度"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("measureWidth"), 10.0, 1.0, 200.0,
                        QStringLiteral("测量宽度"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("sigma"), 1.0, 0.1, 10.0,
                        QStringLiteral("平滑σ")),
        makeDoubleParam(QStringLiteral("threshold"), 30.0, 0.0, 255.0,
                        QStringLiteral("边缘阈值")),
        makeEnumParam(QStringLiteral("transition"), 0, QStringList{"all", "positive", "negative"},
                      QStringLiteral("边缘极性：全部/暗到亮/亮到暗")),
        makeEnumParam(QStringLiteral("selectMode"), 0, QStringList{"all", "first", "last"},
                      QStringLiteral("边缘选择：全部/第一个/最后一个")),
    });
}

void FindLineNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        auto pv = [this](const QString &n, double d) { return m_params.value(n, d).toDouble(); };
        const double r1 = pv(QStringLiteral("row1"), 100);
        const double c1 = pv(QStringLiteral("col1"), 100);
        const double r2 = pv(QStringLiteral("row2"), 200);
        const double c2 = pv(QStringLiteral("col2"), 200);
        const double ml = pv(QStringLiteral("measureLength"), 30);
        const double mw = pv(QStringLiteral("measureWidth"), 10);
        const double sg = pv(QStringLiteral("sigma"), 1.0);
        const double th = pv(QStringLiteral("threshold"), 30);
        const QString transition =
            m_params.value(QStringLiteral("transition"), QStringLiteral("all")).toString();
        const QString selectMode =
            m_params.value(QStringLiteral("selectMode"), QStringLiteral("all")).toString();

        HTuple handle;
        CreateMetrologyModel(&handle);
        HTuple index;
        // HALCON 24.11 签名：GenParamName/GenParamValue 支持 'measure_transition' 等初始参数
        AddMetrologyObjectLineMeasure(handle, r1, c1, r2, c2, ml, mw, sg, th,
                                      "measure_transition",
                                      transition.toStdString().c_str(), &index);
        SetMetrologyObjectParam(handle, index, "measure_select", selectMode.toStdString().c_str());
        SetMetrologyObjectParam(handle, index, "num_measures", 50);
        ApplyMetrologyModel(HImage(m_inputImage), handle);

        HTuple rowB, colB, rowE, colE, score;
        // GenParamValue 需为空元组（HALCON 24.11 参数校验），传 "all" 会抛 #1304
        GetMetrologyObjectResult(handle, index, "all", "row_begin", HTuple(), &rowB);
        GetMetrologyObjectResult(handle, index, "all", "column_begin", HTuple(), &colB);
        GetMetrologyObjectResult(handle, index, "all", "row_end", HTuple(), &rowE);
        GetMetrologyObjectResult(handle, index, "all", "column_end", HTuple(), &colE);
        GetMetrologyObjectResult(handle, index, "all", "score", HTuple(), &score);

        HTuple n;
        TupleLength(score, &n);
        MeasureResult res;
        res.type = QStringLiteral("line");
        res.valueName = QStringLiteral("角度");
        res.valid = n.I() > 0;
        if (n.I() > 0) {
            double angleRad = 0.0;
            HTuple ang;
            GetMetrologyObjectResult(handle, index, "all", "angle", HTuple(), &ang);
            angleRad = ang.D();
            res.value = angleRad * 180.0 / 3.14159265358979323846;
            res.point1 = QPointF(colB.D(), rowB.D());
            res.point2 = QPointF(colE.D(), rowE.D());
            res.extraValues = QVector<double>({rowB.D(), colB.D(), rowE.D(), colE.D(), score.D()});
        }
        HObject fitted;
        GetMetrologyObjectResultContour(&fitted, handle, index, "all", 1.0);
        ClearMetrologyModel(handle);

        auto resObj = QSharedPointer<DataObject>::create();
        resObj->setMeasureResult(res);
        setOutputData(1, resObj);

        auto xldObj = QSharedPointer<DataObject>::create();
        xldObj->setHXLDCont(HXLDCont(fitted));
        setOutputData(2, xldObj);

        m_outputImage = m_inputImage;
    } catch (const HException &) {
        m_outputImage.Clear();
        setOutputData(1, QSharedPointer<DataObject>());
        setOutputData(2, QSharedPointer<DataObject>());
        m_params["moduleStatus"] = false;
    }
}

RoiShape FindLineNode::geometryRoi() const
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

void FindLineNode::applyGeometryRoi(const RoiShape &shape)
{
    // 「清除几何」：搜索线是**必需**参数，归零而不是留旧值——节点随后会以"区域无效"明确失败，
    // 比悄悄沿用上一次的搜索线安全得多。
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

QWidget *FindLineNode::createParamPanel()
{
    auto *panel = createAutoParamPanel();
    auto *layout = panel->findChild<QVBoxLayout *>();
    if (!layout) layout = new QVBoxLayout(panel);
    auto *btn = new QPushButton(QStringLiteral("在画布上设置搜索区域"));
    connect(btn, &QPushButton::clicked, this, &FindLineNode::roiPickRequested);
    layout->addWidget(btn);
    auto *tip = new QLabel(QStringLiteral("<i>提示：点击后在图像视图上拖拽绘制搜索线/圆，"
                                          "松手后自动写回坐标参数。</i>"));
    tip->setWordWrap(true);
    layout->addWidget(tip);
    layout->addStretch();
    return panel;
}

void FindLineNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}
