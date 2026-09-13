#include "FindCircleNode.h"
#include "DataObject.h"
#include "Port.h"
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>

using namespace HalconCpp;

FindCircleNode::FindCircleNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("圆查找"));
    m_type = MEASUREMENT;
}

void FindCircleNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("测量结果"), PortDataType::Measure);
    addOutputPort(QStringLiteral("拟合轮廓"), PortDataType::XLD);
    registerParams({
        makeDoubleParam(QStringLiteral("row"), 200.0, 0.0, 100000.0,
                        QStringLiteral("圆心行"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("column"), 200.0, 0.0, 100000.0,
                        QStringLiteral("圆心列"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("radius"), 50.0, 0.0, 100000.0,
                        QStringLiteral("搜索半径"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("measureLength"), 20.0, 1.0, 1000.0,
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

void FindCircleNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        auto pv = [this](const QString &n, double d) { return m_params.value(n, d).toDouble(); };
        const double row = pv(QStringLiteral("row"), 200);
        const double col = pv(QStringLiteral("column"), 200);
        const double rad = pv(QStringLiteral("radius"), 50);
        const double ml = pv(QStringLiteral("measureLength"), 20);
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
        AddMetrologyObjectCircleMeasure(handle, row, col, rad, ml, mw, sg, th,
                                        "measure_transition", transition.toStdString().c_str(),
                                        &index);
        SetMetrologyObjectParam(handle, index, "measure_select", selectMode.toStdString().c_str());
        SetMetrologyObjectParam(handle, index, "num_measures", 36);
        ApplyMetrologyModel(HImage(m_inputImage), handle);

        HTuple rRow, rCol, rRad, score;
        // GenParamValue 需为空元组（HALCON 24.11 参数校验），传 "all" 会抛 #1304
        GetMetrologyObjectResult(handle, index, "all", "row", HTuple(), &rRow);
        GetMetrologyObjectResult(handle, index, "all", "column", HTuple(), &rCol);
        GetMetrologyObjectResult(handle, index, "all", "radius", HTuple(), &rRad);
        GetMetrologyObjectResult(handle, index, "all", "score", HTuple(), &score);

        HTuple n;
        TupleLength(score, &n);
        MeasureResult res;
        res.type = QStringLiteral("circle");
        res.valueName = QStringLiteral("半径");
        res.valid = n.I() > 0;
        if (n.I() > 0) {
            res.value = rRad.D();
            res.point1 = QPointF(rCol.D(), rRow.D());
            res.extraValues = QVector<double>({rRow.D(), rCol.D(), score.D()});
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

QWidget *FindCircleNode::createParamPanel()
{
    auto *panel = createAutoParamPanel();
    auto *layout = panel->findChild<QVBoxLayout *>();
    if (!layout) layout = new QVBoxLayout(panel);
    auto *btn = new QPushButton(QStringLiteral("在画布上设置搜索区域"));
    connect(btn, &QPushButton::clicked, this, &FindCircleNode::roiPickRequested);
    layout->addWidget(btn);
    auto *tip = new QLabel(QStringLiteral("<i>提示：点击后在图像视图上拖拽绘制搜索线/圆，"
                                          "松手后自动写回坐标参数。</i>"));
    tip->setWordWrap(true);
    layout->addWidget(tip);
    layout->addStretch();
    return panel;
}

void FindCircleNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}
