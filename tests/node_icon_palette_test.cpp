// NodeIconPaletteTest（词条切片 B 图标族首抽）：类别色语义表与圆标合成的像素级座。
// 颜色腿全纯（QColor 对照七式恒定）；渲染腿走 pixmap 像素采样（透明底与圆心色与描边环），
// 需要 QApplication（链接 Qt6::Widgets 后 QTEST_MAIN 自动建）。不进 MainWindow。
// 红臂破坏位 = 测量红与逻辑紫在类别色表内互换——测量色表腿当场红，余腿臂内原样绿。
#include <QImage>
#include <QPixmap>
#include <QStringList>
#include <QtTest>

#include "NodeBase.h"
#include "NodeIconPalette.h"

class NodeIconPaletteTest : public QObject
{
    Q_OBJECT

private slots:
    void testCategoryColorsExact();      // 七式恒定（六枚举 + 越界灰）（红臂靶腿在测量位）
    void testIconCenterPixelMatches();   // 圆心像素 = 类别色（渲染腿；以逻辑紫与输出橙实证）
    void testIconCanvasAndRingShape();   // 画布 16px、角落透明、描边环亮白
};

void NodeIconPaletteTest::testCategoryColorsExact()
{
    QCOMPARE(NodeIconPalette::categoryColor(NodeBase::IMAGE_ACQUISITION),
             QColor(0x18, 0x90, 0xff));
    QCOMPARE(NodeIconPalette::categoryColor(NodeBase::IMAGE_PROCESSING),
             QColor(0x52, 0xc4, 0x1a));
    QCOMPARE(NodeIconPalette::categoryColor(NodeBase::SHAPE_ANALYSIS),
             QColor(0xfa, 0xad, 0x14));
    QCOMPARE(NodeIconPalette::categoryColor(NodeBase::MEASUREMENT),
             QColor(0xff, 0x4d, 0x4f));
    QCOMPARE(NodeIconPalette::categoryColor(NodeBase::LOGIC),
             QColor(0x72, 0x2e, 0xd1));
    QCOMPARE(NodeIconPalette::categoryColor(NodeBase::OUTPUT),
             QColor(0xff, 0x76, 0x32));
    // 界外枚举值（原体 default 同形）落缺省灰
    QCOMPARE(NodeIconPalette::categoryColor(static_cast<NodeBase::NodeType>(999)),
             QColor(0x8c, 0x8c, 0x8c));
}

void NodeIconPaletteTest::testIconCenterPixelMatches()
{
    // 圆内中心（8,8）应即类别色（描边只在圆周）
    const QImage logic = NodeIconPalette::categoryIcon(NodeBase::LOGIC).pixmap(16, 16).toImage();
    QCOMPARE(logic.size(), QSize(16, 16));
    QCOMPARE(logic.pixelColor(8, 8), QColor(0x72, 0x2e, 0xd1));

    const QImage output = NodeIconPalette::categoryIcon(NodeBase::OUTPUT).pixmap(16, 16).toImage();
    QCOMPARE(output.pixelColor(8, 8), QColor(0xff, 0x76, 0x32));
}

void NodeIconPaletteTest::testIconCanvasAndRingShape()
{
    const QImage image = NodeIconPalette::categoryIcon(NodeBase::OUTPUT).pixmap(16, 16).toImage();
    QCOMPARE(image.size(), QSize(16, 16));

    // 角落（1,1）在圆外：透明
    QCOMPARE(image.pixelColor(1, 1).alpha(), 0);

    // 描边环结构断言：圆顶切点（8,2）的环比芯亮（抗锯齿混形下白缘约六成混入底色圆，
    // 实测环 255/202/175/250，比芯亮度约高五成）且近乎不透明
    const QColor ring = image.pixelColor(8, 2);
    const int ringLum = (ring.red() + ring.green() + ring.blue()) / 3;
    const QColor fill = NodeIconPalette::categoryColor(NodeBase::OUTPUT);
    const int fillLum = (fill.red() + fill.green() + fill.blue()) / 3;
    QVERIFY2(ring.alpha() >= 200,
             qPrintable(QStringLiteral("ring alpha too low: %1").arg(ring.alpha())));
    QVERIFY2(ringLum >= 190,
             qPrintable(QStringLiteral("ring lum too low: %1").arg(ringLum)));
    QVERIFY2(ringLum > fillLum,
             qPrintable(QStringLiteral("ring lum %1 not above fill lum %2").arg(ringLum).arg(fillLum)));
}

QTEST_MAIN(NodeIconPaletteTest)
#include "node_icon_palette_test.moc"
