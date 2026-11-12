// 拟合点集订阅 + 初始化类型（VM「图形生成 → 直线 / 圆」的两项挂账）。
//
// 契约：
//  1. 两个拟合节点各有一个「点集」逻辑槽（下标 1），绑定后本轮不再要求输入图像；
//  2. 订阅是权威输入——订阅数据无效时直接判失败，**不得**回退到阈值图；
//  3. 支持 XLD / Region / Point / 卡尺点集（Measure.type=="caliper"，extraValues=[col,row,...]），
//     以及 B1 聚合端口对象里携带的点集子结果（图像 + 边缘轮廓）；
//  4. 初始化类型：全局法合并全部候选组拟合一次；局部最优逐组拟合取残差最小；
//     直线默认全局法（与旧行为一致），圆默认局部最优（旧行为是单轮廓）。
//
// 说明：不 show() 窗口；HALCON 只用来构造 XLD / Region 输入，不做算法调用。
#include <QtTest>
#include <QSharedPointer>
#include <QVector>
#include <QPointF>
#include <cmath>
#include <limits>

#include <halconcpp/HalconCpp.h>
#include <opencv2/imgproc.hpp>

#include <cmath>

#include "OpencvFitLineNode.h"
#include "OpencvFitCircleNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "FitPointSource.h"

using namespace HalconCpp;

namespace {

/// 单条 XLD 多边形（顶点即点集；拟合节点只关心坐标，不关心是否闭合）
QSharedPointer<DataObject> makeXldData(const QVector<QPointF> &points)
{
    HTuple rows, cols;
    for (const QPointF &p : points) {
        rows.Append(p.y());
        cols.Append(p.x());
    }
    HObject contour;
    GenContourPolygonXld(&contour, rows, cols);

    auto data = QSharedPointer<DataObject>::create();
    data->setHXLDCont(HXLDCont(contour));
    return data;
}

/// 多条 XLD 合成一个对象（订阅数据是权威点集，多组用于验证全局法/局部最优）
QSharedPointer<DataObject> makeXldGroupsData(const QVector<QVector<QPointF>> &groups)
{
    HObject merged;
    for (const QVector<QPointF> &group : groups) {
        HTuple rows, cols;
        for (const QPointF &p : group) {
            rows.Append(p.y());
            cols.Append(p.x());
        }
        HObject contour;
        GenContourPolygonXld(&contour, rows, cols);
        if (!merged.IsInitialized()) {
            merged = contour;
        } else {
            HObject concatenated;
            ConcatObj(merged, contour, &concatenated);
            merged = concatenated;
        }
    }

    auto data = QSharedPointer<DataObject>::create();
    data->setHXLDCont(HXLDCont(merged));
    return data;
}

/// 斜率为 slope 的直线点集（从 (x0,y0) 起，每步 dx 一点）
QVector<QPointF> linePoints(double x0, double y0, double slope, double dx, int count)
{
    QVector<QPointF> points;
    points.reserve(count);
    for (int i = 0; i < count; ++i)
        points.append(QPointF(x0 + dx * i, y0 + slope * dx * i));
    return points;
}

/// 圆周点集（含可选的径向抖动，用于制造"残差较大"的候选）
QVector<QPointF> circlePoints(double cx, double cy, double radius, int count, double jitter = 0.0)
{
    QVector<QPointF> points;
    points.reserve(count);
    for (int i = 0; i < count; ++i) {
        const double angle = 2.0 * 3.14159265358979323846 * i / count;
        const double r = radius + jitter * std::sin(3.0 * angle);   // 确定性抖动，不用随机数
        points.append(QPointF(cx + r * std::cos(angle), cy + r * std::sin(angle)));
    }
    return points;
}

/// 一条清晰的水平亮线（用于"订阅无效也不许回退图像"的对照图）
QSharedPointer<DataObject> makeHorizontalLineImage(int width = 400, int height = 300)
{
    cv::Mat image(height, width, CV_8UC1, cv::Scalar(0));
    cv::line(image, cv::Point(50, 150), cv::Point(350, 150), cv::Scalar(255), 2);
    auto data = QSharedPointer<DataObject>::create();
    data->setHImage(OpencvUtil::matToHimage(image));
    return data;
}

/// 数值数组（「按坐标」订阅的典型上游输出）
QSharedPointer<DataObject> makeNumberArray(const QVector<double> &values)
{
    return QSharedPointer<DataObject>::create(DataObject::DataType::Array,
                                              QVariant::fromValue(values));
}

/// 逗号分隔的数值文本（变量/脚本喂值的形态）
QSharedPointer<DataObject> makeNumberText(const QString &text)
{
    return QSharedPointer<DataObject>::create(DataObject::DataType::String, QVariant(text));
}

} // namespace

/// 聚合端口探针：主结果只有 XLD（没有图像），用来钉住 B1 统一输出端口 0 上的
/// "类型声明有了、图标数据丢了"这一形态——订阅与下游连线读的都是端口 0。
class AggregatedXldProbe : public HalconNode
{
    Q_OBJECT
public:
    void init() override
    {
        HalconNode::init();
        addOutputPort(QStringLiteral("边缘轮廓"), PortDataType::XLD);
    }
    bool requiresInputImage() const override { return false; }
    void run(bool) override
    {
        HTuple rows, cols;
        for (int i = 0; i < 10; ++i) {
            rows.Append(50.0);
            cols.Append(10.0 + 10.0 * i);
        }
        HObject contour;
        GenContourPolygonXld(&contour, rows, cols);

        auto xld = QSharedPointer<DataObject>::create();
        xld->setHXLDCont(HXLDCont(contour));
        setOutputData(1, xld);
        m_params[QStringLiteral("moduleStatus")] = true;
    }
};

class FitPointSourcingTest : public QObject
{
    Q_OBJECT

private slots:
    void testPointSlotIsDeclaredOnBothNodes();
    void testLineFitsSubscribedXldWithoutImage();
    void testSubscriptionWinsOverImage();
    void testInvalidSubscriptionDoesNotFallBackToImage();
    void testCaliperMeasureIsAcceptedAsPointSet();
    void testOtherMeasureIsRejected();
    void testSubscribedRegionFeedsCircleFit();
    void testLineInitTypeSelectsLocalBestContour();
    void testCircleInitTypeGlobalMergesContours();
    void testLocalInitSkipsGroupsBelowMinPoints();
    void testMaxContoursLimitsSubscriptionGroups();
    void testAggregatedXldOutputSurvivesToPortZero();
    void testPerPointResultFieldsAndRejectionMask();
    void testCircleAngularCoverageOfArc();
    void testCoordinateSubscriptionBuildsPoints();
    void testCoordinateSubscriptionParsesText();
    void testCoordinateSubscriptionRejectsMismatchedSeries();
    void testNonFiniteSubscribedPointsDoNotPoisonCircleFit();
    void testInputModeConflictsAreReported();
};

void FitPointSourcingTest::testPointSlotIsDeclaredOnBothNodes()
{
    OpencvFitLineNode line;
    line.init();
    // 1 个真实输入（图像）+ 3 个订阅槽（点集 / 点集X / 点集Y）
    QCOMPARE(line.logicalInputPortCount(), 4);
    QCOMPARE(line.inputSlotName(OpencvFitLineNode::kPointSlot), FitPointSource::slotName());
    QCOMPARE(line.inputSlotName(OpencvFitLineNode::kPointXSlot), FitPointSource::pointXSlotName());
    QCOMPARE(line.inputSlotName(OpencvFitLineNode::kPointYSlot), FitPointSource::pointYSlotName());
    QCOMPARE(line.inputSlotType(OpencvFitLineNode::kPointSlot), PortDataType::Any);
    QVERIFY(!line.hasInputBinding(OpencvFitLineNode::kPointSlot));
    QVERIFY(line.requiresInputImage());
    line.setInputBinding(OpencvFitLineNode::kPointSlot, 42, 0);
    QVERIFY(!line.requiresInputImage());
    QCOMPARE(line.getParam(QStringLiteral("initType")).toInt(), 0);   // 直线默认全局法
    QCOMPARE(line.getParam(QStringLiteral("inputMode")).toInt(), 0);  // 默认按点

    OpencvFitCircleNode circle;
    circle.init();
    QCOMPARE(circle.logicalInputPortCount(), 4);
    QCOMPARE(circle.inputSlotName(OpencvFitCircleNode::kPointSlot), FitPointSource::slotName());
    QCOMPARE(circle.inputSlotName(OpencvFitCircleNode::kPointXSlot), FitPointSource::pointXSlotName());
    QCOMPARE(circle.inputSlotName(OpencvFitCircleNode::kPointYSlot), FitPointSource::pointYSlotName());
    QVERIFY(circle.requiresInputImage());
    circle.setInputBinding(OpencvFitCircleNode::kPointSlot, 42, 0);
    QVERIFY(!circle.requiresInputImage());
    QCOMPARE(circle.getParam(QStringLiteral("initType")).toInt(), 1);   // 圆默认局部最优
    QCOMPARE(circle.getParam(QStringLiteral("inputMode")).toInt(), 0);

    // 按坐标只绑 X 也属于订阅：不再要求输入图像（具体缺 Y 由 run() 明确判失败）
    OpencvFitLineNode coordinate;
    coordinate.init();
    coordinate.setInputBinding(OpencvFitLineNode::kPointXSlot, 42, 0);
    QVERIFY(!coordinate.requiresInputImage());
}

void FitPointSourcingTest::testLineFitsSubscribedXldWithoutImage()
{
    OpencvFitLineNode node;
    node.init();
    node.setParam(QStringLiteral("minPoints"), 5);

    const QVector<QPointF> points = linePoints(100.0, 200.0, 0.5, 2.0, 100);
    node.setInputData(OpencvFitLineNode::kPointSlot, makeXldData(points));
    node.setInputBinding(OpencvFitLineNode::kPointSlot, 42, 0);

    QVERIFY(node.execute());
    QVERIFY2(node.getOutputData(0) && node.getOutputData(0)->hasMeasureResult(),
             "订阅点集应能在没有输入图像时产出 Measure 结果");
    QVERIFY(qAbs(node.getParam(QStringLiteral("fitAngle")).toDouble() - 26.5651) < 0.05);
    QVERIFY(qAbs(node.getParam(QStringLiteral("fitCol")).toDouble() - 199.0) < 1.0);
    QVERIFY(qAbs(node.getParam(QStringLiteral("fitRow")).toDouble() - 249.5) < 1.0);
    QCOMPARE(node.getParam(QStringLiteral("fitPointCount")).toInt(), 100);
    QVERIFY(node.getParam(QStringLiteral("fitResidual")).toDouble() < 0.01);

    // VM「模块结果」逐点/状态字段：拟合状态 1、拟合点数 = 输入点数、匹配点数 = 参与点数
    QCOMPARE(node.getParam(QStringLiteral("fitStatus")).toInt(), 1);
    QCOMPARE(node.getParam(QStringLiteral("fitInputCount")).toInt(), 100);
    QCOMPARE(node.getParam(QStringLiteral("fitMatchedCount")).toInt(), 100);
    QCOMPARE(node.getParam(QStringLiteral("fitPointMask")).toString().split(',').size(), 100);
    QCOMPARE(node.getParam(QStringLiteral("fitPointDistances")).toString().split(',').size(), 100);
    QCOMPARE(node.getParam(QStringLiteral("fitInputPoints")).toString().split(';').size(), 100);
    QVERIFY(node.getParam(QStringLiteral("fitPointDistances")).toString().startsWith(QStringLiteral("0.00")));
}

void FitPointSourcingTest::testSubscriptionWinsOverImage()
{
    OpencvFitLineNode node;
    node.init();
    node.setParam(QStringLiteral("minPoints"), 5);
    node.setParam(QStringLiteral("maxContours"), 3);

    // 图像里是水平线（0°），订阅点集是 26.565° 的斜线：结果必须来自订阅
    node.setInputData(0, makeHorizontalLineImage());
    node.setInputData(OpencvFitLineNode::kPointSlot,
                      makeXldData(linePoints(100.0, 200.0, 0.5, 2.0, 60)));
    node.setInputBinding(OpencvFitLineNode::kPointSlot, 42, 0);

    QVERIFY(node.execute());
    QVERIFY(qAbs(node.getParam(QStringLiteral("fitAngle")).toDouble() - 26.5651) < 0.05);
    QCOMPARE(node.getParam(QStringLiteral("fitPointCount")).toInt(), 60);
}

void FitPointSourcingTest::testInvalidSubscriptionDoesNotFallBackToImage()
{
    OpencvFitLineNode node;
    node.init();
    node.setParam(QStringLiteral("minPoints"), 5);

    // 图像本身可以拟合出结果，但订阅给的是数值——按契约必须判失败，不许回退图像
    node.setInputData(0, makeHorizontalLineImage());
    node.setInputData(OpencvFitLineNode::kPointSlot,
                      QSharedPointer<DataObject>::create(DataObject::DataType::Number,
                                                         QVariant(3.14)));
    node.setInputBinding(OpencvFitLineNode::kPointSlot, 42, 0);

    QVERIFY(!node.execute());
    QVERIFY(!node.getParam(QStringLiteral("moduleStatus")).toBool());
    QCOMPARE(node.getParam(QStringLiteral("fitPointCount")).toInt(), 0);
    QCOMPARE(node.getParam(QStringLiteral("fitAngle")).toDouble(), 0.0);
    QVERIFY(!node.getOutputData(0));
    QVERIFY2(node.getParam(QStringLiteral("lastError")).toString().contains(QStringLiteral("点集")),
             qPrintable(QStringLiteral("失败原因应说明订阅不是点集，实际：%1")
                            .arg(node.getParam(QStringLiteral("lastError")).toString())));
}

void FitPointSourcingTest::testCaliperMeasureIsAcceptedAsPointSet()
{
    OpencvFitLineNode node;
    node.init();
    node.setParam(QStringLiteral("minPoints"), 5);

    const QVector<QPointF> points = linePoints(80.0, 60.0, -0.5, 2.0, 40);
    MeasureResult caliper;
    caliper.type = QStringLiteral("caliper");
    caliper.valueName = QStringLiteral("边缘点");
    caliper.valid = true;
    caliper.value = points.size();
    for (const QPointF &point : points) {
        caliper.extraValues << point.x() << point.y();   // 约定：col,row 成对
    }
    auto data = QSharedPointer<DataObject>::create();
    data->setMeasureResult(caliper);

    node.setInputData(OpencvFitLineNode::kPointSlot, data);
    node.setInputBinding(OpencvFitLineNode::kPointSlot, 42, 0);

    QVERIFY(node.execute());
    // 斜率 -0.5 ⇒ -26.565°（fitLine 只保证方向向量 x>0，角度符号由斜率决定）
    QVERIFY(qAbs(node.getParam(QStringLiteral("fitAngle")).toDouble() + 26.5651) < 0.05);
    QCOMPARE(node.getParam(QStringLiteral("fitPointCount")).toInt(), 40);
}

void FitPointSourcingTest::testOtherMeasureIsRejected()
{
    OpencvFitLineNode node;
    node.init();
    node.setParam(QStringLiteral("minPoints"), 5);

    // 直线拟合自己输出的 Measure（type="line"）不是点集：不能被当成 col,row 对
    MeasureResult lineResult;
    lineResult.type = QStringLiteral("line");
    lineResult.valid = true;
    lineResult.value = 0.0;
    lineResult.extraValues = QVector<double>({1.0, 0.0, 100.0, 100.0, 40.0});
    auto data = QSharedPointer<DataObject>::create();
    data->setMeasureResult(lineResult);

    node.setInputData(OpencvFitLineNode::kPointSlot, data);
    node.setInputBinding(OpencvFitLineNode::kPointSlot, 42, 0);

    QVERIFY(!node.execute());
    QVERIFY(node.getParam(QStringLiteral("lastError")).toString().contains(QStringLiteral("点集")));
}

void FitPointSourcingTest::testSubscribedRegionFeedsCircleFit()
{
    OpencvFitCircleNode node;
    node.init();
    node.setParam(QStringLiteral("minPoints"), 10);

    HObject region;
    GenCircle(&region, 120.0, 150.0, 40.0);
    auto data = QSharedPointer<DataObject>::create();
    data->setHRegion(HRegion(region));

    node.setInputData(OpencvFitCircleNode::kPointSlot, data);
    node.setInputBinding(OpencvFitCircleNode::kPointSlot, 42, 0);

    QVERIFY(node.execute());
    QVERIFY(qAbs(node.getParam(QStringLiteral("fitCol")).toDouble() - 150.0) < 2.0);
    QVERIFY(qAbs(node.getParam(QStringLiteral("fitRow")).toDouble() - 120.0) < 2.0);
    QVERIFY(qAbs(node.getParam(QStringLiteral("fitRadius")).toDouble() - 40.0) < 2.0);
    QVERIFY(node.getParam(QStringLiteral("fitPointCount")).toInt() > 10);
    // 整圆边界点的角度范围应接近 360°（只差采样最大空隙）
    QVERIFY(node.getParam(QStringLiteral("fitAngleRange")).toDouble() > 300.0);
}

void FitPointSourcingTest::testLineInitTypeSelectsLocalBestContour()
{
    // 两条候选：水平直线（残差≈0）与上下抖动 ±18px 的折线（残差≈10）
    QVector<QPointF> clean = linePoints(50.0, 100.0, 0.0, 2.0, 50);
    QVector<QPointF> noisy;
    for (int i = 0; i < 50; ++i)
        noisy.append(QPointF(50.0 + 2.0 * i, 300.0 + ((i * 17) % 37) - 18.0));
    const QVector<QVector<QPointF>> groups({clean, noisy});

    OpencvFitLineNode node;
    node.init();
    node.setParam(QStringLiteral("minPoints"), 20);
    node.setInputData(OpencvFitLineNode::kPointSlot, makeXldGroupsData(groups));
    node.setInputBinding(OpencvFitLineNode::kPointSlot, 42, 0);

    // 局部最优：取残差最小的那条（clean）
    node.setParam(QStringLiteral("initType"), 1);
    QVERIFY(node.execute());
    QVERIFY2(qAbs(node.getParam(QStringLiteral("fitRow")).toDouble() - 100.0) < 1.0,
             "局部最优应命中水平的那条候选");
    QVERIFY(node.getParam(QStringLiteral("fitResidual")).toDouble() < 0.01);
    QCOMPARE(node.getParam(QStringLiteral("fitPointCount")).toInt(), 50);

    // 全局法：两组合并成一次拟合，残差必然远大于局部最优
    node.setParam(QStringLiteral("initType"), 0);
    QVERIFY(node.execute());
    const double globalResidual = node.getParam(QStringLiteral("fitResidual")).toDouble();
    QVERIFY2(globalResidual > 20.0,
             qPrintable(QStringLiteral("全局法应把两条相距 200px 的候选合在一起，实际残差 %1")
                            .arg(globalResidual)));
}

void FitPointSourcingTest::testCircleInitTypeGlobalMergesContours()
{
    // 两个圆：一个精确 r=20，一个 r=60 带径向抖动；合并后最小外接圆会明显变大
    const QVector<QVector<QPointF>> groups({
        circlePoints(100.0, 100.0, 20.0, 64),
        circlePoints(300.0, 100.0, 60.0, 64, 4.0),
    });

    OpencvFitCircleNode node;
    node.init();
    node.setParam(QStringLiteral("minPoints"), 10);
    node.setInputData(OpencvFitCircleNode::kPointSlot, makeXldGroupsData(groups));
    node.setInputBinding(OpencvFitCircleNode::kPointSlot, 42, 0);

    // 局部最优（默认）：取径向残差最小的圆
    QVERIFY(node.execute());
    QVERIFY(qAbs(node.getParam(QStringLiteral("fitCol")).toDouble() - 100.0) < 1.5);
    QVERIFY(qAbs(node.getParam(QStringLiteral("fitRadius")).toDouble() - 20.0) < 1.0);
    QVERIFY(node.getParam(QStringLiteral("fitResidual")).toDouble() < 1.0);

    // 全局法：两个圆合并成一个最小外接圆，半径远大于 20
    node.setParam(QStringLiteral("initType"), 0);
    QVERIFY(node.execute());
    QVERIFY2(node.getParam(QStringLiteral("fitRadius")).toDouble() > 100.0,
             qPrintable(QStringLiteral("全局法半径应覆盖两个候选圆，实际 %1")
                            .arg(node.getParam(QStringLiteral("fitRadius")).toDouble())));
}

void FitPointSourcingTest::testLocalInitSkipsGroupsBelowMinPoints()
{
    // 5 点的精确直线低于 minPoints=20；30 点的抖动直线合格 ⇒ 只能选中后者
    QVector<QPointF> tiny = linePoints(10.0, 10.0, 0.0, 1.0, 5);
    QVector<QPointF> noisy;
    for (int i = 0; i < 30; ++i)
        noisy.append(QPointF(10.0 + 3.0 * i, 200.0 + ((i * 13) % 11) - 5.0));

    OpencvFitLineNode node;
    node.init();
    node.setParam(QStringLiteral("minPoints"), 20);
    node.setParam(QStringLiteral("initType"), 1);
    node.setInputData(OpencvFitLineNode::kPointSlot,
                      makeXldGroupsData(QVector<QVector<QPointF>>({tiny, noisy})));
    node.setInputBinding(OpencvFitLineNode::kPointSlot, 42, 0);

    QVERIFY(node.execute());
    QCOMPARE(node.getParam(QStringLiteral("fitPointCount")).toInt(), 30);
    QVERIFY(qAbs(node.getParam(QStringLiteral("fitRow")).toDouble() - 200.0) < 2.0);
    QVERIFY(node.getParam(QStringLiteral("fitResidual")).toDouble() > 0.5);
}

void FitPointSourcingTest::testMaxContoursLimitsSubscriptionGroups()
{
    // 三组分别 10 / 20 / 30 点；maxContours=1 只能保留点数最多的 30 点组
    const QVector<QVector<QPointF>> groups({
        linePoints(0.0, 10.0, 0.0, 1.0, 10),
        linePoints(0.0, 20.0, 0.0, 1.0, 20),
        linePoints(0.0, 30.0, 0.0, 1.0, 30),
    });

    OpencvFitLineNode node;
    node.init();
    node.setParam(QStringLiteral("minPoints"), 5);
    node.setParam(QStringLiteral("maxContours"), 1);
    node.setParam(QStringLiteral("initType"), 1);
    node.setInputData(OpencvFitLineNode::kPointSlot, makeXldGroupsData(groups));
    node.setInputBinding(OpencvFitLineNode::kPointSlot, 42, 0);

    QVERIFY(node.execute());
    QCOMPARE(node.getParam(QStringLiteral("fitPointCount")).toInt(), 30);
    QVERIFY(qAbs(node.getParam(QStringLiteral("fitRow")).toDouble() - 30.0) < 1.0);
}

void FitPointSourcingTest::testAggregatedXldOutputSurvivesToPortZero()
{
    AggregatedXldProbe probe;
    probe.init();
    QVERIFY(probe.execute());

    const QSharedPointer<DataObject> aggregated = probe.getOutputData(0);
    QVERIFY(aggregated);
    QCOMPARE(aggregated->getType(), DataObject::DataType::XLD);
    QVERIFY2(aggregated->getHXLDCont().IsInitialized(),
             "B1 聚合端口必须保留主结果的 XLD 图标数据（否则订阅/下游连线拿到空对象）");

    OpencvFitLineNode node;
    node.init();
    node.setParam(QStringLiteral("minPoints"), 3);
    node.setInputData(OpencvFitLineNode::kPointSlot, aggregated);
    node.setInputBinding(OpencvFitLineNode::kPointSlot, 42, 0);

    QVERIFY(node.execute());
    QVERIFY(qAbs(node.getParam(QStringLiteral("fitRow")).toDouble() - 50.0) < 0.5);
    QCOMPARE(node.getParam(QStringLiteral("fitPointCount")).toInt(), 10);
}

void FitPointSourcingTest::testPerPointResultFieldsAndRejectionMask()
{
    // 50 个共线点 + 1 个 40px 远的离群点：匹配掩码必须恰好把它标成 0
    QVector<QPointF> points = linePoints(20.0, 100.0, 0.0, 2.0, 50);
    points.append(QPointF(60.0, 140.0));

    OpencvFitLineNode node;
    node.init();
    node.setParam(QStringLiteral("minPoints"), 10);
    node.setParam(QStringLiteral("rejectPoints"), 1);
    node.setParam(QStringLiteral("rejectDistance"), 1.0);
    node.setInputData(OpencvFitLineNode::kPointSlot, makeXldData(points));
    node.setInputBinding(OpencvFitLineNode::kPointSlot, 42, 0);

    QVERIFY(node.execute());
    QCOMPARE(node.getParam(QStringLiteral("fitStatus")).toInt(), 1);
    QCOMPARE(node.getParam(QStringLiteral("fitInputCount")).toInt(), 51);
    QCOMPARE(node.getParam(QStringLiteral("fitMatchedCount")).toInt(), 50);
    QCOMPARE(node.getParam(QStringLiteral("fitRejectedCount")).toInt(), 1);

    const QStringList mask = node.getParam(QStringLiteral("fitPointMask")).toString().split(',');
    QCOMPARE(mask.size(), 51);
    QCOMPARE(mask.count(QStringLiteral("1")), 50);
    QCOMPARE(mask.count(QStringLiteral("0")), 1);
    QCOMPARE(mask.last(), QStringLiteral("0"));   // 离群点排在最后

    const QStringList distances =
        node.getParam(QStringLiteral("fitPointDistances")).toString().split(',');
    QCOMPARE(distances.size(), 51);
    QVERIFY2(distances.last().toDouble() > 20.0,
             "被剔除的离群点仍要给出它到拟合直线的距离（官方「拟合距离」按输入点逐个输出）");
    QCOMPARE(node.getParam(QStringLiteral("fitInputPoints")).toString().split(';').size(), 50);
}

void FitPointSourcingTest::testCircleAngularCoverageOfArc()
{
    // 170° 圆弧点集（18 点）：角度范围必须明显小于整圆（360°），证明字段按点的覆盖计算。
    // 注：本节点用最小外接圆拟合，圆弧输入时圆心相对真圆会偏移，覆盖角只是近似值——
    // 因此这里只钉"小于整圆 + 起始角在合法区间"，不钉精确角度。
    QVector<QPointF> arc;
    for (int i = 0; i < 18; ++i) {
        const double angle = i * 10.0 * 3.14159265358979323846 / 180.0;
        arc.append(QPointF(200.0 + 50.0 * std::cos(angle),
                           150.0 + 50.0 * std::sin(angle)));
    }

    OpencvFitCircleNode node;
    node.init();
    node.setParam(QStringLiteral("minPoints"), 5);
    node.setInputData(OpencvFitCircleNode::kPointSlot, makeXldData(arc));
    node.setInputBinding(OpencvFitCircleNode::kPointSlot, 42, 0);

    QVERIFY(node.execute());
    QCOMPARE(node.getParam(QStringLiteral("fitStatus")).toInt(), 1);
    QCOMPARE(node.getParam(QStringLiteral("fitInputCount")).toInt(), 18);
    QCOMPARE(node.getParam(QStringLiteral("fitMatchedCount")).toInt(), 18);

    const double start = node.getParam(QStringLiteral("fitStartAngle")).toDouble();
    const double range = node.getParam(QStringLiteral("fitAngleRange")).toDouble();
    QVERIFY2(start >= 0.0 && start < 360.0,
             qPrintable(QStringLiteral("圆起始角度必须归一化到 [0,360)，实际 %1").arg(start)));
    QVERIFY2(range > 90.0 && range < 300.0,
             qPrintable(QStringLiteral("170° 圆弧的角度范围应明显小于整圆，实际 %1").arg(range)));
}

void FitPointSourcingTest::testCoordinateSubscriptionBuildsPoints()
{
    // 按坐标：X/Y 两个数值数组按序配点，拟合结果与按点订阅一致
    QVector<double> xs;
    QVector<double> ys;
    for (int i = 0; i < 60; ++i) {
        xs.append(100.0 + 2.0 * i);
        ys.append(200.0 + 1.0 * i);
    }

    OpencvFitLineNode node;
    node.init();
    node.setParam(QStringLiteral("minPoints"), 5);
    node.setParam(QStringLiteral("inputMode"), 1);   // 按坐标
    node.setInputData(OpencvFitLineNode::kPointXSlot, makeNumberArray(xs));
    node.setInputData(OpencvFitLineNode::kPointYSlot, makeNumberArray(ys));
    node.setInputBinding(OpencvFitLineNode::kPointXSlot, 42, 0);
    node.setInputBinding(OpencvFitLineNode::kPointYSlot, 42, 0);

    QVERIFY(!node.requiresInputImage());
    QVERIFY(node.execute());
    QVERIFY(qAbs(node.getParam(QStringLiteral("fitAngle")).toDouble() - 26.5651) < 0.05);
    QCOMPARE(node.getParam(QStringLiteral("fitInputCount")).toInt(), 60);
    QCOMPARE(node.getParam(QStringLiteral("fitMatchedCount")).toInt(), 60);
}

void FitPointSourcingTest::testCoordinateSubscriptionParsesText()
{
    // 文本形态的数值序列（变量/脚本常见）：逗号分隔也能按坐标订阅
    OpencvFitLineNode node;
    node.init();
    node.setParam(QStringLiteral("minPoints"), 3);
    node.setParam(QStringLiteral("inputMode"), 1);
    node.setInputData(OpencvFitLineNode::kPointXSlot,
                      makeNumberText(QStringLiteral("10,20,30,40,50")));
    node.setInputData(OpencvFitLineNode::kPointYSlot,
                      makeNumberText(QStringLiteral("10;20;30;40;50")));   // 分号同样接受
    node.setInputBinding(OpencvFitLineNode::kPointXSlot, 42, 0);
    node.setInputBinding(OpencvFitLineNode::kPointYSlot, 42, 0);

    QVERIFY(node.execute());
    QVERIFY(qAbs(node.getParam(QStringLiteral("fitAngle")).toDouble() - 45.0) < 0.05);
    QCOMPARE(node.getParam(QStringLiteral("fitInputCount")).toInt(), 5);
}

void FitPointSourcingTest::testCoordinateSubscriptionRejectsMismatchedSeries()
{
    // 数量不一致：必须判失败并说明两边个数，不能截断成较短的那一边
    OpencvFitLineNode node;
    node.init();
    node.setParam(QStringLiteral("inputMode"), 1);
    node.setInputData(OpencvFitLineNode::kPointXSlot,
                      makeNumberArray(QVector<double>({0.0, 1.0, 2.0})));
    node.setInputData(OpencvFitLineNode::kPointYSlot,
                      makeNumberArray(QVector<double>({0.0, 1.0, 2.0, 3.0})));
    node.setInputBinding(OpencvFitLineNode::kPointXSlot, 42, 0);
    node.setInputBinding(OpencvFitLineNode::kPointYSlot, 42, 0);
    QVERIFY(!node.execute());
    QVERIFY2(node.getParam(QStringLiteral("lastError")).toString().contains(QStringLiteral("数量不一致")),
             qPrintable(node.getParam(QStringLiteral("lastError")).toString()));

    // 只绑 X：明确报"需要同时绑定"，且不回退图像（这里也没给图像）
    OpencvFitLineNode half;
    half.init();
    half.setParam(QStringLiteral("inputMode"), 1);
    half.setInputData(OpencvFitLineNode::kPointXSlot,
                      makeNumberArray(QVector<double>({0.0, 1.0, 2.0})));
    half.setInputBinding(OpencvFitLineNode::kPointXSlot, 42, 0);
    QVERIFY(!half.execute());
    QVERIFY2(half.getParam(QStringLiteral("lastError")).toString().contains(QStringLiteral("同时绑定")),
             qPrintable(half.getParam(QStringLiteral("lastError")).toString()));
}

void FitPointSourcingTest::testInputModeConflictsAreReported()
{
    // 输入方式=按点，却绑了坐标槽：明确报冲突，不猜用户意图
    OpencvFitLineNode pointMode;
    pointMode.init();
    pointMode.setParam(QStringLiteral("inputMode"), 0);
    pointMode.setInputData(OpencvFitLineNode::kPointXSlot,
                           makeNumberArray(QVector<double>({0.0, 1.0, 2.0})));
    pointMode.setInputBinding(OpencvFitLineNode::kPointXSlot, 42, 0);
    QVERIFY(!pointMode.execute());
    QVERIFY2(pointMode.getParam(QStringLiteral("lastError")).toString().contains(QStringLiteral("按点")),
             qPrintable(pointMode.getParam(QStringLiteral("lastError")).toString()));

    // 输入方式=按坐标，却绑了「点集」槽
    OpencvFitLineNode coordinateMode;
    coordinateMode.init();
    coordinateMode.setParam(QStringLiteral("inputMode"), 1);
    coordinateMode.setInputData(OpencvFitLineNode::kPointSlot,
                                makeXldData(linePoints(0.0, 0.0, 1.0, 1.0, 10)));
    coordinateMode.setInputBinding(OpencvFitLineNode::kPointSlot, 42, 0);
    QVERIFY(!coordinateMode.execute());
    QVERIFY2(coordinateMode.getParam(QStringLiteral("lastError")).toString().contains(QStringLiteral("按坐标")),
             qPrintable(coordinateMode.getParam(QStringLiteral("lastError")).toString()));
}

// 2026-11-12 审核：非有限点进入 cv::minEnclosingCircle 会产出 NaN 圆心/半径，
// 且旧的退化检查用 points.size() 把坏点也算作有效点。这条腿经节点 run() 的真实
// 订阅链喂含 NaN/Inf 的坐标序列，断言拟合结果仍是有限的、且几何真值不变。
void FitPointSourcingTest::testNonFiniteSubscribedPointsDoNotPoisonCircleFit()
{
    QVector<double> xs;
    QVector<double> ys;
    constexpr double kPi = 3.14159265358979323846;
    for (int i = 0; i < 40; ++i) {
        const double a = 2.0 * kPi * i / 40.0;
        xs.append(100.0 + 20.0 * std::cos(a));
        ys.append(100.0 + 20.0 * std::sin(a));
    }
    // 混入三种坏点：NaN 的 X、Inf 的 X、以及 NaN 的 Y
    xs.append(std::numeric_limits<double>::quiet_NaN());
    ys.append(1.0);
    xs.append(std::numeric_limits<double>::infinity());
    ys.append(2.0);
    xs.append(3.0);
    ys.append(std::numeric_limits<double>::quiet_NaN());

    OpencvFitCircleNode circle;
    circle.init();
    circle.setParam(QStringLiteral("minPoints"), 3);
    circle.setParam(QStringLiteral("inputMode"), 1);   // 按坐标
    circle.setInputData(OpencvFitCircleNode::kPointXSlot, makeNumberArray(xs));
    circle.setInputData(OpencvFitCircleNode::kPointYSlot, makeNumberArray(ys));
    circle.setInputBinding(OpencvFitCircleNode::kPointXSlot, 42, 0);
    circle.setInputBinding(OpencvFitCircleNode::kPointYSlot, 42, 0);

    QVERIFY2(circle.execute(), "有限点仍够一组圆，不该因混入坏点整体失败");
    const double col = circle.getParam(QStringLiteral("fitCol")).toDouble();
    const double row = circle.getParam(QStringLiteral("fitRow")).toDouble();
    const double radius = circle.getParam(QStringLiteral("fitRadius")).toDouble();
    QVERIFY2(std::isfinite(col) && std::isfinite(row) && std::isfinite(radius),
             qPrintable(QStringLiteral("拟合结果含非有限值：col=%1 row=%2 r=%3")
                            .arg(col).arg(row).arg(radius)));
    QVERIFY2(qAbs(col - 100.0) < 1.0 && qAbs(row - 100.0) < 1.0,
             "圆心应仍为 (100,100)");
    QVERIFY2(qAbs(radius - 20.0) < 1.0, "半径应仍为 20");
}

QTEST_MAIN(FitPointSourcingTest)
#include "fit_point_sourcing_test.moc"
