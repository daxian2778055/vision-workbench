// RoiGeometryTest（词条切片 B 图元族首抽）：ROI 手柄命中与拖拽映射的纯几何座。
// 纯几何断言：只构造形状与点，断言手柄命中、整体移动判定与拖拽后的形状；
// 不触控件、不触画布、不触图像（座不需 GUI 与 HALCON 图）。
// 红臂破坏位 = 线段整体移动的投影距离阈值（八像素）——线段腿当场红，余腿臂内原样绿。
#include <QtTest>
#include <QPointF>

#include "RoiGeometry.h"
#include "RoiShape.h"

namespace {

// 统一变换参数：缩放一、偏移零（图像坐标即控件坐标）
constexpr double kScale = 1.0;
constexpr qreal kOffX = 0.0;
constexpr qreal kOffY = 0.0;

RoiShape makeRect()
{
    RoiShape s;
    s.type = RoiType::Rect;
    s.p1 = QPointF(10, 10);
    s.p2 = QPointF(50, 40);
    return s;
}

RoiShape makeRotated()
{
    RoiShape s;
    s.type = RoiType::RotatedRect;
    s.p1 = QPointF(100, 100);
    s.width = 40.0;
    s.height = 20.0;
    s.angleDeg = 0.0;
    return s;
}

}   // namespace

class RoiGeometryTest : public QObject
{
    Q_OBJECT

private slots:
    void testRectHandlesMoveAndDrag();   // 直角矩形族
    void testRotatedRectHandlesAndDrag();// 旋转矩形族
    void testLineHandlesMoveAndDrag();   // 线段族（红臂靶腿）
    void testCircleAndPoint();           // 圆与点族
    void testNoShapeThresholdAndGuards();// 无形状、阈值边界与两条守卫
};

void RoiGeometryTest::testRectHandlesMoveAndDrag()
{
    const RoiShape rect = makeRect();

    // 四角手柄命中（控件坐标即图像坐标）
    QCOMPARE(RoiGeometry::hitTest(rect, QPoint(10, 10), kScale, kOffX, kOffY, true), RoiHandle::C0);
    QCOMPARE(RoiGeometry::hitTest(rect, QPoint(50, 10), kScale, kOffX, kOffY, true), RoiHandle::C1);
    QCOMPARE(RoiGeometry::hitTest(rect, QPoint(50, 40), kScale, kOffX, kOffY, true), RoiHandle::C2);
    QCOMPARE(RoiGeometry::hitTest(rect, QPoint(10, 40), kScale, kOffX, kOffY, true), RoiHandle::C3);
    // 内部点给整体移动；远离点无命中
    QCOMPARE(RoiGeometry::hitTest(rect, QPoint(30, 25), kScale, kOffX, kOffY, true), RoiHandle::Move);
    QCOMPARE(RoiGeometry::hitTest(rect, QPoint(200, 200), kScale, kOffX, kOffY, true), RoiHandle::None);

    // 角点拖拽：C1 分轴（左上取新 y、右下取新 x），随后最小二像素夹持生效
    const RoiShape afterC1 = RoiGeometry::dragShape(rect, RoiHandle::C1, QPointF(60, 50), QPointF(50, 10));
    QVERIFY(qAbs(afterC1.p1.x() - 10.0) < 1e-9);
    QVERIFY(qAbs(afterC1.p1.y() - 50.0) < 1e-9);
    QVERIFY(qAbs(afterC1.p2.x() - 60.0) < 1e-9);
    QVERIFY(qAbs(afterC1.p2.y() - 52.0) < 1e-9);   // 高被夹到 左上 y 加二

    // 整体平移：两角同步加位移
    const RoiShape afterMove = RoiGeometry::dragShape(rect, RoiHandle::Move, QPointF(35, 30), QPointF(30, 25));
    QVERIFY(qAbs(afterMove.p1.x() - 15.0) < 1e-9);
    QVERIFY(qAbs(afterMove.p1.y() - 15.0) < 1e-9);
    QVERIFY(qAbs(afterMove.p2.x() - 55.0) < 1e-9);
    QVERIFY(qAbs(afterMove.p2.y() - 45.0) < 1e-9);
}

void RoiGeometryTest::testRotatedRectHandlesAndDrag()
{
    const RoiShape rot = makeRotated();

    // 角点手柄（零角时即四角）与旋转柄（中心上方 高半加二十八）
    QCOMPARE(RoiGeometry::hitTest(rot, QPoint(80, 90), kScale, kOffX, kOffY, true), RoiHandle::C0);
    QCOMPARE(RoiGeometry::hitTest(rot, QPoint(120, 110), kScale, kOffX, kOffY, true), RoiHandle::C2);
    QCOMPARE(RoiGeometry::hitTest(rot, QPoint(100, 62), kScale, kOffX, kOffY, true), RoiHandle::Rotate);
    // 局部坐标内含给整体移动
    QCOMPARE(RoiGeometry::hitTest(rot, QPoint(105, 102), kScale, kOffX, kOffY, true), RoiHandle::Move);

    // 角点拖拽：按对角重算宽高与中心
    const RoiShape afterC0 = RoiGeometry::dragShape(rot, RoiHandle::C0, QPointF(70, 80), QPointF(80, 90));
    QVERIFY(qAbs(afterC0.width - 50.0) < 1e-9);
    QVERIFY(qAbs(afterC0.height - 30.0) < 1e-9);
    QVERIFY(qAbs(afterC0.p1.x() - 95.0) < 1e-9);
    QVERIFY(qAbs(afterC0.p1.y() - 95.0) < 1e-9);

    // 旋转柄：角度按向量算
    const RoiShape afterRot = RoiGeometry::dragShape(rot, RoiHandle::Rotate, QPointF(140, 60), QPointF(100, 100));
    QVERIFY(qAbs(afterRot.angleDeg - 45.0) < 1e-6);
}

void RoiGeometryTest::testLineHandlesMoveAndDrag()
{
    RoiShape line;
    line.type = RoiType::Line;
    line.p1 = QPointF(0, 0);
    line.p2 = QPointF(100, 0);

    QCOMPARE(RoiGeometry::hitTest(line, QPoint(0, 0), kScale, kOffX, kOffY, true), RoiHandle::End1);
    QCOMPARE(RoiGeometry::hitTest(line, QPoint(100, 0), kScale, kOffX, kOffY, true), RoiHandle::End2);
    // 距线段八像素内给整体移动，远处不给
    QCOMPARE(RoiGeometry::hitTest(line, QPoint(50, 3), kScale, kOffX, kOffY, true), RoiHandle::Move);
    QCOMPARE(RoiGeometry::hitTest(line, QPoint(50, 50), kScale, kOffX, kOffY, true), RoiHandle::None);

    // 端点拖拽与整体平移
    const RoiShape afterEnd = RoiGeometry::dragShape(line, RoiHandle::End1, QPointF(10, 10), QPointF(0, 0));
    QVERIFY(qAbs(afterEnd.p1.x() - 10.0) < 1e-9);
    QVERIFY(qAbs(afterEnd.p1.y() - 10.0) < 1e-9);

    const RoiShape afterMove = RoiGeometry::dragShape(line, RoiHandle::Move, QPointF(55, 5), QPointF(50, 0));
    QVERIFY(qAbs(afterMove.p1.x() - 5.0) < 1e-9);
    QVERIFY(qAbs(afterMove.p1.y() - 5.0) < 1e-9);
    QVERIFY(qAbs(afterMove.p2.x() - 105.0) < 1e-9);
    QVERIFY(qAbs(afterMove.p2.y() - 5.0) < 1e-9);
}

void RoiGeometryTest::testCircleAndPoint()
{
    RoiShape circle;
    circle.type = RoiType::Circle;
    circle.p1 = QPointF(50, 50);
    circle.p2 = QPointF(70, 50);   // 半径二十

    QCOMPARE(RoiGeometry::hitTest(circle, QPoint(70, 50), kScale, kOffX, kOffY, true), RoiHandle::C0);
    QCOMPARE(RoiGeometry::hitTest(circle, QPoint(55, 55), kScale, kOffX, kOffY, true), RoiHandle::Move);
    QCOMPARE(RoiGeometry::hitTest(circle, QPoint(100, 100), kScale, kOffX, kOffY, true), RoiHandle::None);

    // 半径点拖拽与圆心平移
    const RoiShape afterC0 = RoiGeometry::dragShape(circle, RoiHandle::C0, QPointF(80, 50), QPointF(70, 50));
    QVERIFY(qAbs(afterC0.p2.x() - 80.0) < 1e-9);
    QVERIFY(qAbs(afterC0.p2.y() - 50.0) < 1e-9);

    const RoiShape afterMove = RoiGeometry::dragShape(circle, RoiHandle::Move, QPointF(55, 55), QPointF(50, 50));
    QVERIFY(qAbs(afterMove.p1.x() - 55.0) < 1e-9);
    QVERIFY(qAbs(afterMove.p1.y() - 55.0) < 1e-9);
    QVERIFY(qAbs(afterMove.p2.x() - 75.0) < 1e-9);

    // 点形状：单点手柄
    RoiShape point;
    point.type = RoiType::Point;
    point.p1 = QPointF(12, 13);
    QCOMPARE(RoiGeometry::hitTest(point, QPoint(12, 13), kScale, kOffX, kOffY, true), RoiHandle::C0);
    const RoiShape afterPoint = RoiGeometry::dragShape(point, RoiHandle::C0, QPointF(20, 21), QPointF(12, 13));
    QVERIFY(qAbs(afterPoint.p1.x() - 20.0) < 1e-9);
    QVERIFY(qAbs(afterPoint.p1.y() - 21.0) < 1e-9);
}

void RoiGeometryTest::testNoShapeThresholdAndGuards()
{
    RoiShape none;
    none.type = RoiType::None;
    QCOMPARE(RoiGeometry::hitTest(none, QPoint(0, 0), kScale, kOffX, kOffY, true), RoiHandle::None);

    const RoiShape rect = makeRect();
    // 阈值边界：七像素内命中、九像素外不命中
    QCOMPARE(RoiGeometry::hitTest(rect, QPoint(10, 3), kScale, kOffX, kOffY, true), RoiHandle::C0);
    QCOMPARE(RoiGeometry::hitTest(rect, QPoint(10, 1), kScale, kOffX, kOffY, true), RoiHandle::None);

    // 守卫一：无图像时手柄命中仍在（命中在守卫之前），但不给整体移动
    QCOMPARE(RoiGeometry::hitTest(rect, QPoint(10, 10), kScale, kOffX, kOffY, false), RoiHandle::C0);
    QCOMPARE(RoiGeometry::hitTest(rect, QPoint(30, 25), kScale, kOffX, kOffY, false), RoiHandle::None);
    // 守卫二：缩放退化同样不给整体移动
    QCOMPARE(RoiGeometry::hitTest(rect, QPoint(30, 25), 0.0, kOffX, kOffY, true), RoiHandle::None);
}

QTEST_MAIN(RoiGeometryTest)
#include "roi_geometry_test.moc"
