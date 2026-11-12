// 线圆测量（VM 4.4「几何测量 → 线圆测量」）：交点（0/1/2 个）、圆心垂足、圆心到直线距离。
//
// 契约：
//  1. 绘制模式用直线端点 + 圆心/半径参数；绘制模式缺图判 NG 并写明原因；
//  2. 订阅模式从「直线」「圆」两个逻辑槽各取一条 Measure（line / circle），缺一判 NG；
//  3. 相离判 0 个交点、相切判 1 个（垂足）、相交判 2 个；交点按直线参数从小到大输出；
//  4. 垂足是圆心到**无限长直线**的投影（不裁到端点之间），距离 = |圆心-垂足|。
#include <QtTest>
#include <QSharedPointer>
#include <QPointF>
#include <QVector>

#include <opencv2/core.hpp>

#include "LineCircleMeasureNode.h"
#include "DataObject.h"
#include "OpencvUtil.h"

namespace {

QSharedPointer<DataObject> makeLineMeasure(const QPointF &a, const QPointF &b)
{
    MeasureResult measure;
    measure.type = QStringLiteral("line");
    measure.valid = true;
    measure.point1 = a;
    measure.point2 = b;
    auto data = QSharedPointer<DataObject>::create();
    data->setMeasureResult(measure);
    return data;
}

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

QSharedPointer<DataObject> makePointData(const QPointF &point)
{
    return QSharedPointer<DataObject>::create(DataObject::DataType::Point, QVariant(point));
}

QSharedPointer<DataObject> makeGrayImage(int width = 32, int height = 32)
{
    cv::Mat image(height, width, CV_8UC1, cv::Scalar(0));
    auto data = QSharedPointer<DataObject>::create();
    data->setHImage(OpencvUtil::matToHimage(image));
    return data;
}

void setDrawn(LineCircleMeasureNode &node, const QPointF &a, const QPointF &b,
              const QPointF &center, double radius)
{
    node.setParam(QStringLiteral("sourceMode"), 1);
    node.setParam(QStringLiteral("lineCol1"), a.x());
    node.setParam(QStringLiteral("lineRow1"), a.y());
    node.setParam(QStringLiteral("lineCol2"), b.x());
    node.setParam(QStringLiteral("lineRow2"), b.y());
    node.setParam(QStringLiteral("circleCol"), center.x());
    node.setParam(QStringLiteral("circleRow"), center.y());
    node.setParam(QStringLiteral("drawCircleRadius"), radius);
}

} // namespace

class LineCircleMeasureTest : public QObject
{
    Q_OBJECT

private slots:
    void testSeparateCircleHasNoIntersection();
    void testIntersectingCircleHasTwoPoints();
    void testTangentCircleHasOnePoint();
    void testSubscriptionFromMeasures();
    void testSubscriptionRequiresBothBindings();
    void testInvalidRadiusFails();
    void testDrawModeWithoutImageFails();
    void testResultFieldSpec();
    void testPointSubscriptionBuildsLine();
    void testPointSubscriptionNeedsBothPointsAndRejectsConflicts();
};

void LineCircleMeasureTest::testSeparateCircleHasNoIntersection()
{
    LineCircleMeasureNode node;
    node.init();
    node.setInputData(0, makeGrayImage());

    // 直线 y=100，圆 (200,180) r=60 ⇒ 距离 80 > 60，无交点
    QVERIFY(node.execute());
    QCOMPARE(node.getParam(QStringLiteral("distance")).toDouble(), 80.0);
    QCOMPARE(node.getParam(QStringLiteral("footX")).toDouble(), 200.0);
    QCOMPARE(node.getParam(QStringLiteral("footY")).toDouble(), 100.0);
    QCOMPARE(node.getParam(QStringLiteral("intersectCount")).toInt(), 0);
}

void LineCircleMeasureTest::testIntersectingCircleHasTwoPoints()
{
    LineCircleMeasureNode node;
    node.init();
    node.setInputData(0, makeGrayImage());
    setDrawn(node, QPointF(100, 100), QPointF(300, 100), QPointF(200, 150), 80.0);

    QVERIFY(node.execute());
    QCOMPARE(node.getParam(QStringLiteral("distance")).toDouble(), 50.0);
    QCOMPARE(node.getParam(QStringLiteral("intersectCount")).toInt(), 2);
    const double offset = std::sqrt(80.0 * 80.0 - 50.0 * 50.0);
    QVERIFY(qAbs(node.getParam(QStringLiteral("intersect1X")).toDouble() - (200.0 - offset)) < 1e-9);
    QVERIFY(qAbs(node.getParam(QStringLiteral("intersect2X")).toDouble() - (200.0 + offset)) < 1e-9);
    QVERIFY(qAbs(node.getParam(QStringLiteral("intersect1Y")).toDouble() - 100.0) < 1e-9);
}

void LineCircleMeasureTest::testTangentCircleHasOnePoint()
{
    LineCircleMeasureNode node;
    node.init();
    node.setInputData(0, makeGrayImage());
    // 圆 (200,160) r=60 ⇒ 与 y=100 相切于 (200,100)
    setDrawn(node, QPointF(100, 100), QPointF(300, 100), QPointF(200, 160), 60.0);

    QVERIFY(node.execute());
    QCOMPARE(node.getParam(QStringLiteral("intersectCount")).toInt(), 1);
    QCOMPARE(node.getParam(QStringLiteral("intersect1X")).toDouble(), 200.0);
    QCOMPARE(node.getParam(QStringLiteral("intersect1Y")).toDouble(), 100.0);
}

void LineCircleMeasureTest::testSubscriptionFromMeasures()
{
    LineCircleMeasureNode node;
    node.init();
    node.setParam(QStringLiteral("sourceMode"), 0);
    node.setInputData(LineCircleMeasureNode::kLineSlot,
                      makeLineMeasure(QPointF(0, 0), QPointF(100, 0)));
    node.setInputData(LineCircleMeasureNode::kCircleSlot,
                      makeCircleMeasure(QPointF(50, 30), 50.0));
    node.setInputBinding(LineCircleMeasureNode::kLineSlot, 42, 0);
    node.setInputBinding(LineCircleMeasureNode::kCircleSlot, 42, 0);

    QVERIFY(!node.requiresInputImage());
    QVERIFY(node.execute());
    QCOMPARE(node.getParam(QStringLiteral("distance")).toDouble(), 30.0);
    QCOMPARE(node.getParam(QStringLiteral("circleCenterX")).toDouble(), 50.0);
    QCOMPARE(node.getParam(QStringLiteral("circleRadius")).toDouble(), 50.0);
    // 垂足为 (50,0)，交点 40 / 60（勾股 30-40-50）
    QCOMPARE(node.getParam(QStringLiteral("footX")).toDouble(), 50.0);
    QCOMPARE(node.getParam(QStringLiteral("intersectCount")).toInt(), 2);
    QVERIFY(qAbs(node.getParam(QStringLiteral("intersect1X")).toDouble() - 10.0) < 1e-9);
    QVERIFY(qAbs(node.getParam(QStringLiteral("intersect2X")).toDouble() - 90.0) < 1e-9);
}

void LineCircleMeasureTest::testSubscriptionRequiresBothBindings()
{
    LineCircleMeasureNode node;
    node.init();
    node.setParam(QStringLiteral("sourceMode"), 0);
    node.setInputData(LineCircleMeasureNode::kLineSlot,
                      makeLineMeasure(QPointF(0, 0), QPointF(100, 0)));
    node.setInputBinding(LineCircleMeasureNode::kLineSlot, 42, 0);

    QVERIFY(!node.execute());
    QVERIFY2(node.getParam(QStringLiteral("lastError")).toString().contains(QStringLiteral("同时绑定")),
             qPrintable(node.getParam(QStringLiteral("lastError")).toString()));
}

void LineCircleMeasureTest::testInvalidRadiusFails()
{
    LineCircleMeasureNode node;
    node.init();
    node.setInputData(0, makeGrayImage());
    setDrawn(node, QPointF(0, 0), QPointF(100, 0), QPointF(50, 30), 0.0);

    QVERIFY(!node.execute());
    QVERIFY2(node.getParam(QStringLiteral("lastError")).toString().contains(QStringLiteral("半径")),
             qPrintable(node.getParam(QStringLiteral("lastError")).toString()));
}

void LineCircleMeasureTest::testDrawModeWithoutImageFails()
{
    LineCircleMeasureNode node;
    node.init();
    setDrawn(node, QPointF(0, 0), QPointF(100, 0), QPointF(50, 30), 10.0);

    QVERIFY(!node.execute());
    QVERIFY2(node.getParam(QStringLiteral("lastError")).toString().contains(QStringLiteral("输入图像")),
             qPrintable(node.getParam(QStringLiteral("lastError")).toString()));
}

void LineCircleMeasureTest::testResultFieldSpec()
{
    LineCircleMeasureNode node;
    node.init();
    const QVector<NodeResultField> fields = node.resultDisplayFields();
    // 模块级 2 + 直线 5 + 圆环 6 + 交点 4 + 垂足 2 + 距离 1 = 20
    QCOMPARE(int(fields.size()), 20);
}

void LineCircleMeasureTest::testPointSubscriptionBuildsLine()
{
    // 按点：起点/终点配一条直线，结果应与「按线」订阅一致
    LineCircleMeasureNode node;
    node.init();
    node.setParam(QStringLiteral("sourceMode"), 0);
    node.setParam(QStringLiteral("lineInputMode"), 1);
    node.setInputData(LineCircleMeasureNode::kLineStartSlot, makePointData(QPointF(0, 0)));
    node.setInputData(LineCircleMeasureNode::kLineEndSlot, makePointData(QPointF(100, 0)));
    node.setInputData(LineCircleMeasureNode::kCircleSlot,
                      makeCircleMeasure(QPointF(50, 30), 50.0));
    for (int slot = LineCircleMeasureNode::kLineStartSlot;
         slot <= LineCircleMeasureNode::kLineEndSlot; ++slot) {
        node.setInputBinding(slot, 42, 0);
    }
    node.setInputBinding(LineCircleMeasureNode::kCircleSlot, 43, 0);

    QVERIFY(node.execute());
    QCOMPARE(node.getParam(QStringLiteral("lineStartX")).toDouble(), 0.0);
    QCOMPARE(node.getParam(QStringLiteral("lineEndX")).toDouble(), 100.0);
    QCOMPARE(node.getParam(QStringLiteral("distance")).toDouble(), 30.0);
    QCOMPARE(node.getParam(QStringLiteral("intersectCount")).toInt(), 2);
    QVERIFY(qAbs(node.getParam(QStringLiteral("intersect1X")).toDouble() - 10.0) < 1e-9);
    QVERIFY(qAbs(node.getParam(QStringLiteral("intersect2X")).toDouble() - 90.0) < 1e-9);
}

void LineCircleMeasureTest::testPointSubscriptionNeedsBothPointsAndRejectsConflicts()
{
    // ① 按点少绑终点 ⇒ 报「同时绑定」
    {
        LineCircleMeasureNode node;
        node.init();
        node.setParam(QStringLiteral("sourceMode"), 0);
        node.setParam(QStringLiteral("lineInputMode"), 1);
        node.setInputData(LineCircleMeasureNode::kLineStartSlot, makePointData(QPointF(0, 0)));
        node.setInputData(LineCircleMeasureNode::kCircleSlot,
                          makeCircleMeasure(QPointF(50, 30), 50.0));
        node.setInputBinding(LineCircleMeasureNode::kLineStartSlot, 42, 0);
        node.setInputBinding(LineCircleMeasureNode::kCircleSlot, 43, 0);

        QVERIFY(!node.execute());
        QVERIFY2(node.getParam(QStringLiteral("lastError")).toString().contains(
                     QStringLiteral("同时绑定")),
                 qPrintable(node.getParam(QStringLiteral("lastError")).toString()));
    }
    // ② 按点却绑了「直线」槽 ⇒ 报冲突
    {
        LineCircleMeasureNode node;
        node.init();
        node.setParam(QStringLiteral("sourceMode"), 0);
        node.setParam(QStringLiteral("lineInputMode"), 1);
        node.setInputBinding(LineCircleMeasureNode::kLineSlot, 42, 0);
        QVERIFY(!node.execute());
        QVERIFY2(node.getParam(QStringLiteral("lastError")).toString().contains(
                     QStringLiteral("不参与计算")),
                 qPrintable(node.getParam(QStringLiteral("lastError")).toString()));
    }
    // ③ 按线却绑了点槽 ⇒ 报冲突
    {
        LineCircleMeasureNode node;
        node.init();
        node.setParam(QStringLiteral("sourceMode"), 0);
        node.setParam(QStringLiteral("lineInputMode"), 0);
        node.setInputBinding(LineCircleMeasureNode::kLineStartSlot, 42, 0);
        QVERIFY(!node.execute());
        QVERIFY2(node.getParam(QStringLiteral("lastError")).toString().contains(
                     QStringLiteral("只使用")),
                 qPrintable(node.getParam(QStringLiteral("lastError")).toString()));
    }
    // ④ 点槽喂了非 Point 数据 ⇒ 判 NG 且写明类型
    {
        LineCircleMeasureNode node;
        node.init();
        node.setParam(QStringLiteral("sourceMode"), 0);
        node.setParam(QStringLiteral("lineInputMode"), 1);
        node.setInputData(LineCircleMeasureNode::kLineStartSlot,
                          makeLineMeasure(QPointF(0, 0), QPointF(100, 0)));
        node.setInputData(LineCircleMeasureNode::kLineEndSlot, makePointData(QPointF(100, 0)));
        node.setInputData(LineCircleMeasureNode::kCircleSlot,
                          makeCircleMeasure(QPointF(50, 30), 50.0));
        for (int slot = LineCircleMeasureNode::kLineStartSlot;
             slot <= LineCircleMeasureNode::kLineEndSlot; ++slot) {
            node.setInputBinding(slot, 42, 0);
        }
        node.setInputBinding(LineCircleMeasureNode::kCircleSlot, 43, 0);

        QVERIFY(!node.execute());
        QVERIFY2(node.getParam(QStringLiteral("lastError")).toString().contains(
                     QStringLiteral("不是 Point 类型")),
                 qPrintable(node.getParam(QStringLiteral("lastError")).toString()));
    }
}

QTEST_MAIN(LineCircleMeasureTest)
#include "line_circle_measure_test.moc"
