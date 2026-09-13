#include "AngleMeasureNode.h"
#include "DataObject.h"
#include "Port.h"
#include <cmath>

using namespace HalconCpp;

AngleMeasureNode::AngleMeasureNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("角度测量"));
    m_type = MEASUREMENT;
}

void AngleMeasureNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("测量结果"), PortDataType::Measure);
    addOutputPort(QStringLiteral("拟合轮廓"), PortDataType::XLD);
    registerParams({
        makeDoubleParam(QStringLiteral("r1a"), 100.0, 0.0, 100000.0, QStringLiteral("线段1起点行"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("c1a"), 100.0, 0.0, 100000.0, QStringLiteral("线段1起点列"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("r1b"), 200.0, 0.0, 100000.0, QStringLiteral("线段1终点行"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("c1b"), 100.0, 0.0, 100000.0, QStringLiteral("线段1终点列"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("r2a"), 100.0, 0.0, 100000.0, QStringLiteral("线段2起点行"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("c2a"), 100.0, 0.0, 100000.0, QStringLiteral("线段2起点列"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("r2b"), 200.0, 0.0, 100000.0, QStringLiteral("线段2终点行"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("c2b"), 200.0, 0.0, 100000.0, QStringLiteral("线段2终点列"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("measureLength"), 20.0, 1.0, 1000.0, QStringLiteral("测量长度"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("sigma"), 1.0, 0.1, 10.0, QStringLiteral("平滑σ")),
        makeDoubleParam(QStringLiteral("threshold"), 30.0, 0.0, 255.0, QStringLiteral("边缘阈值")),
    });
}

void AngleMeasureNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        auto pv = [this](const QString &n, double d) { return m_params.value(n, d).toDouble(); };
        const double ml = pv(QStringLiteral("measureLength"), 20);
        const double sg = pv(QStringLiteral("sigma"), 1.0);
        const double th = pv(QStringLiteral("threshold"), 30);

        HTuple handle;
        CreateMetrologyModel(&handle);
        HTuple idx1, idx2;
        // HALCON 24.11 签名：GenParamName/GenParamValue 明确设置边缘极性（避免空串歧义）
        AddMetrologyObjectLineMeasure(handle, pv(QStringLiteral("r1a"), 100), pv(QStringLiteral("c1a"), 100),
                                      pv(QStringLiteral("r1b"), 200), pv(QStringLiteral("c1b"), 100),
                                      ml, 5, sg, th, "measure_transition", "all", &idx1);
        AddMetrologyObjectLineMeasure(handle, pv(QStringLiteral("r2a"), 100), pv(QStringLiteral("c2a"), 100),
                                      pv(QStringLiteral("r2b"), 200), pv(QStringLiteral("c2b"), 200),
                                      ml, 5, sg, th, "measure_transition", "all", &idx2);
        ApplyMetrologyModel(HImage(m_inputImage), handle);

        HTuple a1, a2, s1, s2;
        // GenParamValue 需为空元组（HALCON 24.11 参数校验），传 "all" 会抛 #1304
        GetMetrologyObjectResult(handle, idx1, "all", "angle", HTuple(), &a1);
        GetMetrologyObjectResult(handle, idx2, "all", "angle", HTuple(), &a2);
        GetMetrologyObjectResult(handle, idx1, "all", "score", HTuple(), &s1);
        GetMetrologyObjectResult(handle, idx2, "all", "score", HTuple(), &s2);

        HObject fitted;
        HObject c1;
        GetMetrologyObjectResultContour(&c1, handle, idx1, "all", 1.0);
        HObject c2;
        GetMetrologyObjectResultContour(&c2, handle, idx2, "all", 1.0);
        ConcatObj(c1, c2, &fitted);

        const bool ok1 = (a1.Length() > 0) && (a2.Length() > 0);
        double deg = 0.0;
        if (ok1) {
            double d = std::fabs(a1.D() - a2.D()) * 180.0 / 3.14159265358979323846;
            while (d > 180.0) d -= 180.0;
            deg = d;
        }
        ClearMetrologyModel(handle);

        MeasureResult res;
        res.type = QStringLiteral("angle");
        res.valueName = QStringLiteral("夹角");
        res.valid = ok1;
        res.value = deg;
        res.point1 = QPointF(pv(QStringLiteral("c1a"), 100), pv(QStringLiteral("r1a"), 100));

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
