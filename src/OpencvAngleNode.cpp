#include "OpencvAngleNode.h"
#include "DataObject.h"
#include <QWidget>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
}

OpencvAngleNode::OpencvAngleNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("角度测量"));
    m_type = MEASUREMENT;
}

void OpencvAngleNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("夹角"), PortDataType::Measure);
    addOutputPort(QStringLiteral("夹角数值"), PortDataType::Number);
    registerParams({
        makeDoubleParam(QStringLiteral("r1a"), 100.0, 0.0, 100000.0, QStringLiteral("线段1起点行")),
        makeDoubleParam(QStringLiteral("c1a"), 100.0, 0.0, 100000.0, QStringLiteral("线段1起点列")),
        makeDoubleParam(QStringLiteral("r1b"), 200.0, 0.0, 100000.0, QStringLiteral("线段1终点行")),
        makeDoubleParam(QStringLiteral("c1b"), 100.0, 0.0, 100000.0, QStringLiteral("线段1终点列")),
        makeDoubleParam(QStringLiteral("r2a"), 100.0, 0.0, 100000.0, QStringLiteral("线段2起点行")),
        makeDoubleParam(QStringLiteral("c2a"), 100.0, 0.0, 100000.0, QStringLiteral("线段2起点列")),
        makeDoubleParam(QStringLiteral("r2b"), 200.0, 0.0, 100000.0, QStringLiteral("线段2终点行")),
        makeDoubleParam(QStringLiteral("c2b"), 200.0, 0.0, 100000.0, QStringLiteral("线段2终点列")),
    });
    m_params[QStringLiteral("angle")] = 0.0;
}

void OpencvAngleNode::run(bool /*autoSwitch*/)
{
    const auto pv = [this](const QString &n, double d) { return m_params.value(n, d).toDouble(); };
    const double r1a = pv(QStringLiteral("r1a"), 100), c1a = pv(QStringLiteral("c1a"), 100);
    const double r1b = pv(QStringLiteral("r1b"), 200), c1b = pv(QStringLiteral("c1b"), 100);
    const double r2a = pv(QStringLiteral("r2a"), 100), c2a = pv(QStringLiteral("c2a"), 100);
    const double r2b = pv(QStringLiteral("r2b"), 200), c2b = pv(QStringLiteral("c2b"), 200);

    // 两条线段的方向向量（行、列）
    const double v1r = r1b - r1a, v1c = c1b - c1a;
    const double v2r = r2b - r2a, v2c = c2b - c2a;
    const double len1 = std::hypot(v1r, v1c);
    const double len2 = std::hypot(v2r, v2c);

    const bool valid = len1 > 1e-9 && len2 > 1e-9;
    double deg = 0.0;
    if (valid) {
        double cosA = (v1r * v2r + v1c * v2c) / (len1 * len2);
        if (cosA > 1.0)      cosA = 1.0;
        else if (cosA < -1.0) cosA = -1.0;        // 数值安全
        deg = std::acos(cosA) * 180.0 / kPi;        // 两条（无向）线段夹角 ∈ [0,180)
        m_params[QStringLiteral("angle")] = deg;
        m_params[QStringLiteral("moduleStatus")] = true;
    } else {
        m_params[QStringLiteral("angle")] = 0.0;
        m_params[QStringLiteral("moduleStatus")] = false;
    }

    // 端口1：测量结果（带一条线段端点用于可视化）
    MeasureResult res;
    res.type = QStringLiteral("angle");
    res.valueName = QStringLiteral("夹角");
    res.valid = valid;
    res.value = deg;
    res.point1 = QPointF(c1a, r1a);
    res.point2 = QPointF(c1b, r1b);
    auto resObj = QSharedPointer<DataObject>::create();
    resObj->setMeasureResult(res);
    setOutputData(1, valid ? resObj : QSharedPointer<DataObject>());

    // 端口2：纯数值（无论是否有效都给 0，便于下游公式引用）
    auto numObj = QSharedPointer<DataObject>::create();
    numObj->setValue(deg);
    setOutputData(2, numObj);
}

QWidget *OpencvAngleNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvAngleNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}
