// 圆圆测量（VM 4.4「几何测量 → 圆圆测量」）：位置关系五档、交点 0/1/2 个、圆心距。
#include <QtTest>
#include <QSharedPointer>
#include <QPointF>
#include <QVector>

#include <opencv2/core.hpp>

#include "CircleCircleMeasureNode.h"
#include "DataObject.h"
#include "OpencvUtil.h"

namespace {

QSharedPointer<DataObject> makeCircleMeasure(const QPointF &center, double radius)
{
    MeasureResult measure;
    measure.type = QStringLiteral("circle");
    measure.valid = true;
    measure.value = radius;
    measure.point1 = center;
    auto data = QSharedPointer<DataObject>::create();
    data->setMeasureResult(measure);
    return data;
}

QSharedPointer<DataObject> makeGrayImage(int width = 32, int height = 32)
{
    cv::Mat image(height, width, CV_8UC1, cv::Scalar(0));
    auto data = QSharedPointer<DataObject>::create();
    data->setHImage(OpencvUtil::matToHimage(image));
    return data;
}

void setDrawn(CircleCircleMeasureNode &node, const QPointF &c1, double r1,
              const QPointF &c2, double r2)
{
    node.setParam(QStringLiteral("sourceMode"), 1);
    node.setParam(QStringLiteral("circle1Col"), c1.x());
    node.setParam(QStringLiteral("circle1Row"), c1.y());
    node.setParam(QStringLiteral("drawCircle1Radius"), r1);
    node.setParam(QStringLiteral("circle2Col"), c2.x());
    node.setParam(QStringLiteral("circle2Row"), c2.y());
    node.setParam(QStringLiteral("drawCircle2Radius"), r2);
}

} // namespace

class CircleCircleMeasureTest : public QObject
{
    Q_OBJECT

private slots:
    void testIntersectingCircles();
    void testOutsideAndInside();
    void testTangencies();
    void testSubscriptionFromMeasures();
    void testSubscriptionRequiresBothBindings();
    void testInvalidRadiusFails();
    void testResultFieldSpec();
};

void CircleCircleMeasureTest::testIntersectingCircles()
{
    CircleCircleMeasureNode node;
    node.init();
    node.setInputData(0, makeGrayImage());

    // 默认绘制：(200,150,80) 与 (260,150,80) ⇒ d=60，交点在 x=230
    QVERIFY(node.execute());
    QCOMPARE(node.getParam(QStringLiteral("relation")).toString(), QStringLiteral("Intersect"));
    QCOMPARE(node.getParam(QStringLiteral("intersectCount")).toInt(), 2);
    QCOMPARE(node.getParam(QStringLiteral("centerDistance")).toDouble(), 60.0);
    const double h = std::sqrt(80.0 * 80.0 - 30.0 * 30.0);
    QVERIFY(qAbs(node.getParam(QStringLiteral("intersect1X")).toDouble() - 230.0) < 1e-9);
    QVERIFY(qAbs(node.getParam(QStringLiteral("intersect1Y")).toDouble() - (150.0 + h)) < 1e-9);
    QVERIFY(qAbs(node.getParam(QStringLiteral("intersect2Y")).toDouble() - (150.0 - h)) < 1e-9);
}

void CircleCircleMeasureTest::testOutsideAndInside()
{
    CircleCircleMeasureNode node;
    node.init();
    node.setInputData(0, makeGrayImage());

    setDrawn(node, QPointF(0, 0), 10.0, QPointF(100, 0), 10.0);
    QVERIFY(node.execute());
    QCOMPARE(node.getParam(QStringLiteral("relation")).toString(), QStringLiteral("Outside"));
    QCOMPARE(node.getParam(QStringLiteral("intersectCount")).toInt(), 0);

    setDrawn(node, QPointF(0, 0), 100.0, QPointF(50, 0), 20.0);
    QVERIFY(node.execute());
    QCOMPARE(node.getParam(QStringLiteral("relation")).toString(), QStringLiteral("Inside"));
    QCOMPARE(node.getParam(QStringLiteral("intersectCount")).toInt(), 0);
}

void CircleCircleMeasureTest::testTangencies()
{
    CircleCircleMeasureNode node;
    node.init();
    node.setInputData(0, makeGrayImage());

    // 外切：d = r1+r2 = 100，切点 (50,0)
    setDrawn(node, QPointF(0, 0), 50.0, QPointF(100, 0), 50.0);
    QVERIFY(node.execute());
    QCOMPARE(node.getParam(QStringLiteral("relation")).toString(), QStringLiteral("Circumscribe"));
    QCOMPARE(node.getParam(QStringLiteral("intersectCount")).toInt(), 1);
    QCOMPARE(node.getParam(QStringLiteral("intersect1X")).toDouble(), 50.0);
    QCOMPARE(node.getParam(QStringLiteral("intersect1Y")).toDouble(), 0.0);

    // 内切：d = |r1-r2| = 50，切点在大圆边上 (100,0)
    setDrawn(node, QPointF(0, 0), 100.0, QPointF(50, 0), 50.0);
    QVERIFY(node.execute());
    QCOMPARE(node.getParam(QStringLiteral("relation")).toString(), QStringLiteral("Inscribe"));
    QCOMPARE(node.getParam(QStringLiteral("intersectCount")).toInt(), 1);
    QCOMPARE(node.getParam(QStringLiteral("intersect1X")).toDouble(), 100.0);
}

void CircleCircleMeasureTest::testSubscriptionFromMeasures()
{
    CircleCircleMeasureNode node;
    node.init();
    node.setParam(QStringLiteral("sourceMode"), 0);
    node.setInputData(CircleCircleMeasureNode::kCircle1Slot,
                      makeCircleMeasure(QPointF(0, 0), 100.0));
    node.setInputData(CircleCircleMeasureNode::kCircle2Slot,
                      makeCircleMeasure(QPointF(120, 0), 100.0));
    node.setInputBinding(CircleCircleMeasureNode::kCircle1Slot, 42, 0);
    node.setInputBinding(CircleCircleMeasureNode::kCircle2Slot, 42, 0);

    QVERIFY(!node.requiresInputImage());
    QVERIFY(node.execute());
    QCOMPARE(node.getParam(QStringLiteral("centerDistance")).toDouble(), 120.0);
    QCOMPARE(node.getParam(QStringLiteral("relation")).toString(), QStringLiteral("Intersect"));
    QCOMPARE(node.getParam(QStringLiteral("circle1Radius")).toDouble(), 100.0);
    QCOMPARE(node.getParam(QStringLiteral("circle2CenterX")).toDouble(), 120.0);
    QCOMPARE(node.getParam(QStringLiteral("intersectCount")).toInt(), 2);
}

void CircleCircleMeasureTest::testSubscriptionRequiresBothBindings()
{
    CircleCircleMeasureNode node;
    node.init();
    node.setParam(QStringLiteral("sourceMode"), 0);
    node.setInputData(CircleCircleMeasureNode::kCircle1Slot,
                      makeCircleMeasure(QPointF(0, 0), 10.0));
    node.setInputBinding(CircleCircleMeasureNode::kCircle1Slot, 42, 0);

    QVERIFY(!node.execute());
    QVERIFY2(node.getParam(QStringLiteral("lastError")).toString().contains(QStringLiteral("同时绑定")),
             qPrintable(node.getParam(QStringLiteral("lastError")).toString()));
}

void CircleCircleMeasureTest::testInvalidRadiusFails()
{
    CircleCircleMeasureNode node;
    node.init();
    node.setInputData(0, makeGrayImage());
    setDrawn(node, QPointF(0, 0), 0.0, QPointF(50, 0), 10.0);

    QVERIFY(!node.execute());
    QVERIFY2(node.getParam(QStringLiteral("lastError")).toString().contains(QStringLiteral("半径")),
             qPrintable(node.getParam(QStringLiteral("lastError")).toString()));
}

void CircleCircleMeasureTest::testResultFieldSpec()
{
    CircleCircleMeasureNode node;
    node.init();
    const QVector<NodeResultField> fields = node.resultDisplayFields();
    // 模块级 2 + 两个圆各 6 + 交点 4 + 圆心距 1 + 位置关系 1 = 20
    QCOMPARE(int(fields.size()), 20);
}

QTEST_MAIN(CircleCircleMeasureTest)
#include "circle_circle_measure_test.moc"
