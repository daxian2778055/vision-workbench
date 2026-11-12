// 线线测量（VM 4.4「几何测量 → 线线测量」）：交点 / 夹角 / 带符号角度 / 绝对距离。
//
// 契约：
//  1. 绘制模式用 8 个坐标参数，两条直线默认给出 90° 交点（开箱可跑）；
//  2. 订阅模式从「直线1」「直线2」两个逻辑槽各取一条 Measure(type="line")，缺一个就判 NG；
//  3. 平行/重合、端点重合必须判 NG 并给出原因，不输出编造的交点；
//  4. 带符号角度 = 直线1 → 直线2，顺时针（图像 y 向下）为正；`angleRange=0` 折回 -90..90；
//  5. 绝对距离 = 4 个端点到对应另一条直线垂距的平均值（官方口径）。
//
// 说明：纯几何节点，绘制模式仍要求图像（与 VM「输入源」语义一致），订阅模式不要求。
#include <QtTest>
#include <QSharedPointer>
#include <QPointF>
#include <QVector>

#include <opencv2/core.hpp>

#include "LineLineMeasureNode.h"
#include "DataObject.h"
#include "OpencvUtil.h"

namespace {

QSharedPointer<DataObject> makeLineMeasure(const QPointF &a, const QPointF &b)
{
    MeasureResult measure;
    measure.type = QStringLiteral("line");
    measure.valueName = QStringLiteral("直线");
    measure.valid = true;
    measure.point1 = a;
    measure.point2 = b;
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

/// 绘制模式的两条直线（整图坐标：x=列，y=行）
void setDrawnLines(LineLineMeasureNode &node, const QPointF &a1, const QPointF &b1,
                   const QPointF &a2, const QPointF &b2)
{
    node.setParam(QStringLiteral("sourceMode"), 1);
    node.setParam(QStringLiteral("line1Col1"), a1.x());
    node.setParam(QStringLiteral("line1Row1"), a1.y());
    node.setParam(QStringLiteral("line1Col2"), b1.x());
    node.setParam(QStringLiteral("line1Row2"), b1.y());
    node.setParam(QStringLiteral("line2Col1"), a2.x());
    node.setParam(QStringLiteral("line2Row1"), a2.y());
    node.setParam(QStringLiteral("line2Col2"), b2.x());
    node.setParam(QStringLiteral("line2Row2"), b2.y());
}

} // namespace

class LineLineMeasureTest : public QObject
{
    Q_OBJECT

private slots:
    void testDefaultDrawModeIntersectsAtRightAngle();
    void testSignedAngleFollowsAngleRange();
    void testParallelAndCoincidentEndpointsFail();
    void testSubscriptionFromMeasureLines();
    void testSubscriptionRequiresBothBindings();
    void testSubscriptionRejectsNonLineMeasure();
    void testResultFieldSpec();
    void testPointSubscriptionBuildsLines();
    void testPointSubscriptionNeedsFourPointsAndRejectsConflicts();
};

void LineLineMeasureTest::testDefaultDrawModeIntersectsAtRightAngle()
{
    LineLineMeasureNode node;
    node.init();
    node.setInputData(0, makeGrayImage());   // 绘制模式要求图像

    QVERIFY(node.execute());
    QCOMPARE(node.getParam(QStringLiteral("intersectX")).toDouble(), 200.0);
    QCOMPARE(node.getParam(QStringLiteral("intersectY")).toDouble(), 100.0);
    QVERIFY(qAbs(node.getParam(QStringLiteral("acuteAngle")).toDouble() - 90.0) < 1e-9);
    QVERIFY(qAbs(node.getParam(QStringLiteral("signedAngle")).toDouble() - 90.0) < 1e-9);
    // 绝对距离：直线1两端到 x=200 为 100/100，直线2两端到 y=100 为 50/150
    QVERIFY(qAbs(node.getParam(QStringLiteral("absDistance")).toDouble() - 100.0) < 1e-9);

    QVERIFY(node.getOutputData(0) && node.getOutputData(0)->hasMeasureResult());
    QCOMPARE(node.getParam(QStringLiteral("moduleStatus")).toBool(), true);
}

void LineLineMeasureTest::testSignedAngleFollowsAngleRange()
{
    LineLineMeasureNode node;
    node.init();
    node.setInputData(0, makeGrayImage());

    // 直线2方向 (-1,-1)：直线1 → 直线2 为 -135°
    setDrawnLines(node, QPointF(0, 0), QPointF(100, 0),
                  QPointF(200, 100), QPointF(100, 0));

    node.setParam(QStringLiteral("angleRange"), 0);   // -90..90
    QVERIFY(node.execute());
    QVERIFY(qAbs(node.getParam(QStringLiteral("acuteAngle")).toDouble() - 45.0) < 1e-6);
    QVERIFY2(qAbs(node.getParam(QStringLiteral("signedAngle")).toDouble() - 45.0) < 1e-6,
             qPrintable(QStringLiteral("-135° 在 -90..90 档应折回 45°，实际 %1")
                            .arg(node.getParam(QStringLiteral("signedAngle")).toDouble())));

    node.setParam(QStringLiteral("angleRange"), 1);   // -180..180
    QVERIFY(node.execute());
    QVERIFY2(qAbs(node.getParam(QStringLiteral("signedAngle")).toDouble() + 135.0) < 1e-6,
             qPrintable(QStringLiteral("-135° 在 -180..180 档应保持 -135°，实际 %1")
                            .arg(node.getParam(QStringLiteral("signedAngle")).toDouble())));
}

void LineLineMeasureTest::testParallelAndCoincidentEndpointsFail()
{
    LineLineMeasureNode node;
    node.init();
    node.setInputData(0, makeGrayImage());

    // 平行（含重合）：不输出编造的交点
    setDrawnLines(node, QPointF(0, 0), QPointF(100, 0),
                  QPointF(0, 50), QPointF(100, 50));
    QVERIFY(!node.execute());
    QVERIFY2(node.getParam(QStringLiteral("lastError")).toString().contains(QStringLiteral("平行")),
             qPrintable(node.getParam(QStringLiteral("lastError")).toString()));
    QCOMPARE(node.getParam(QStringLiteral("intersectX")).toDouble(), 0.0);

    // 端点重合：方向无从确定
    setDrawnLines(node, QPointF(10, 10), QPointF(10, 10),
                  QPointF(0, 50), QPointF(100, 50));
    QVERIFY(!node.execute());
    QVERIFY2(node.getParam(QStringLiteral("lastError")).toString().contains(QStringLiteral("重合")),
             qPrintable(node.getParam(QStringLiteral("lastError")).toString()));
}

void LineLineMeasureTest::testSubscriptionFromMeasureLines()
{
    LineLineMeasureNode node;
    node.init();
    node.setParam(QStringLiteral("sourceMode"), 0);   // 订阅
    node.setInputData(LineLineMeasureNode::kLine1Slot,
                      makeLineMeasure(QPointF(0, 0), QPointF(100, 0)));
    node.setInputData(LineLineMeasureNode::kLine2Slot,
                      makeLineMeasure(QPointF(50, -50), QPointF(50, 50)));
    node.setInputBinding(LineLineMeasureNode::kLine1Slot, 42, 0);
    node.setInputBinding(LineLineMeasureNode::kLine2Slot, 42, 0);

    QVERIFY(!node.requiresInputImage());
    QVERIFY(node.execute());
    QCOMPARE(node.getParam(QStringLiteral("intersectX")).toDouble(), 50.0);
    QCOMPARE(node.getParam(QStringLiteral("intersectY")).toDouble(), 0.0);
    QVERIFY(qAbs(node.getParam(QStringLiteral("acuteAngle")).toDouble() - 90.0) < 1e-9);
    QCOMPARE(node.getParam(QStringLiteral("line1StartX")).toDouble(), 0.0);
    QCOMPARE(node.getParam(QStringLiteral("line2EndY")).toDouble(), 50.0);
}

void LineLineMeasureTest::testSubscriptionRequiresBothBindings()
{
    LineLineMeasureNode node;
    node.init();
    node.setParam(QStringLiteral("sourceMode"), 0);
    node.setInputData(LineLineMeasureNode::kLine1Slot,
                      makeLineMeasure(QPointF(0, 0), QPointF(100, 0)));
    node.setInputBinding(LineLineMeasureNode::kLine1Slot, 42, 0);

    QVERIFY(!node.execute());
    QVERIFY2(node.getParam(QStringLiteral("lastError")).toString().contains(QStringLiteral("同时绑定")),
             qPrintable(node.getParam(QStringLiteral("lastError")).toString()));
}

void LineLineMeasureTest::testSubscriptionRejectsNonLineMeasure()
{
    LineLineMeasureNode node;
    node.init();
    node.setParam(QStringLiteral("sourceMode"), 0);

    MeasureResult circle;
    circle.type = QStringLiteral("circle");
    circle.valid = true;
    circle.point1 = QPointF(10, 10);
    auto notLine = QSharedPointer<DataObject>::create();
    notLine->setMeasureResult(circle);

    node.setInputData(LineLineMeasureNode::kLine1Slot, notLine);
    node.setInputData(LineLineMeasureNode::kLine2Slot,
                      makeLineMeasure(QPointF(50, -50), QPointF(50, 50)));
    node.setInputBinding(LineLineMeasureNode::kLine1Slot, 42, 0);
    node.setInputBinding(LineLineMeasureNode::kLine2Slot, 42, 0);

    QVERIFY(!node.execute());
    QVERIFY2(node.getParam(QStringLiteral("lastError")).toString().contains(QStringLiteral("不是有效的直线")),
             qPrintable(node.getParam(QStringLiteral("lastError")).toString()));
}

void LineLineMeasureTest::testResultFieldSpec()
{
    LineLineMeasureNode node;
    node.init();
    const QVector<NodeResultField> fields = node.resultDisplayFields();
    // 模块级 2 项（耗时 / 模块状态）+ 两条直线端点 8 项 + 角度 2 项 + 交点 2 项 + 夹角/带符号角/绝对距离 3 项
    QCOMPARE(int(fields.size()), 17);
    auto hasKey = [&fields](const QString &key) {
        for (const NodeResultField &field : fields) {
            if (field.key == key)
                return true;
        }
        return false;
    };
    QVERIFY(hasKey(QStringLiteral("intersectX")));
    QVERIFY(hasKey(QStringLiteral("acuteAngle")));
    QVERIFY(hasKey(QStringLiteral("signedAngle")));
    QVERIFY(hasKey(QStringLiteral("absDistance")));
}

void LineLineMeasureTest::testPointSubscriptionBuildsLines()
{
    // 按点：4 个 Point 配成两条直线，结果应与绘制模式一致
    LineLineMeasureNode node;
    node.init();
    node.setParam(QStringLiteral("sourceMode"), 0);
    node.setParam(QStringLiteral("lineInputMode"), 1);   // 按点
    node.setInputData(LineLineMeasureNode::kLine1StartSlot, makePointData(QPointF(100, 100)));
    node.setInputData(LineLineMeasureNode::kLine1EndSlot, makePointData(QPointF(300, 100)));
    node.setInputData(LineLineMeasureNode::kLine2StartSlot, makePointData(QPointF(200, 50)));
    node.setInputData(LineLineMeasureNode::kLine2EndSlot, makePointData(QPointF(200, 250)));
    for (int slot = LineLineMeasureNode::kLine1StartSlot;
         slot <= LineLineMeasureNode::kLine2EndSlot; ++slot) {
        node.setInputBinding(slot, 42, 0);
    }

    QVERIFY(node.execute());
    QCOMPARE(node.getParam(QStringLiteral("intersectX")).toDouble(), 200.0);
    QCOMPARE(node.getParam(QStringLiteral("intersectY")).toDouble(), 100.0);
    QVERIFY(qAbs(node.getParam(QStringLiteral("acuteAngle")).toDouble() - 90.0) < 1e-9);
    QCOMPARE(node.getParam(QStringLiteral("absDistance")).toDouble(), 100.0);
}

void LineLineMeasureTest::testPointSubscriptionNeedsFourPointsAndRejectsConflicts()
{
    // 少绑一个点槽 ⇒ 明确报"需要同时绑定"
    LineLineMeasureNode missing;
    missing.init();
    missing.setParam(QStringLiteral("sourceMode"), 0);
    missing.setParam(QStringLiteral("lineInputMode"), 1);
    missing.setInputData(LineLineMeasureNode::kLine1StartSlot, makePointData(QPointF(0, 0)));
    missing.setInputData(LineLineMeasureNode::kLine1EndSlot, makePointData(QPointF(100, 0)));
    missing.setInputData(LineLineMeasureNode::kLine2StartSlot, makePointData(QPointF(50, -50)));
    for (int slot = LineLineMeasureNode::kLine1StartSlot;
         slot < LineLineMeasureNode::kLine2EndSlot; ++slot) {
        missing.setInputBinding(slot, 42, 0);
    }
    QVERIFY(!missing.execute());
    QVERIFY2(missing.getParam(QStringLiteral("lastError")).toString().contains(QStringLiteral("同时绑定")),
             qPrintable(missing.getParam(QStringLiteral("lastError")).toString()));

    // 按点 + 仍绑了「直线1」⇒ 冲突
    LineLineMeasureNode conflict;
    conflict.init();
    conflict.setParam(QStringLiteral("sourceMode"), 0);
    conflict.setParam(QStringLiteral("lineInputMode"), 1);
    conflict.setInputData(LineLineMeasureNode::kLine1Slot,
                          makeLineMeasure(QPointF(0, 0), QPointF(100, 0)));
    conflict.setInputBinding(LineLineMeasureNode::kLine1Slot, 42, 0);
    QVERIFY(!conflict.execute());
    QVERIFY2(conflict.getParam(QStringLiteral("lastError")).toString().contains(QStringLiteral("按点")),
             qPrintable(conflict.getParam(QStringLiteral("lastError")).toString()));

    // 按线 + 仍绑了点槽 ⇒ 冲突
    LineLineMeasureNode reverse;
    reverse.init();
    reverse.setParam(QStringLiteral("sourceMode"), 0);
    reverse.setParam(QStringLiteral("lineInputMode"), 0);
    reverse.setInputData(LineLineMeasureNode::kLine1StartSlot, makePointData(QPointF(0, 0)));
    reverse.setInputBinding(LineLineMeasureNode::kLine1StartSlot, 42, 0);
    QVERIFY(!reverse.execute());
    QVERIFY2(reverse.getParam(QStringLiteral("lastError")).toString().contains(QStringLiteral("按线")),
             qPrintable(reverse.getParam(QStringLiteral("lastError")).toString()));
}

QTEST_MAIN(LineLineMeasureTest)
#include "line_line_measure_test.moc"
