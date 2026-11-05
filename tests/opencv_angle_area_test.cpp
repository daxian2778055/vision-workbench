// OpenCV 角度/面积测量功能测试（🥇：替代替换版环境下被禁用的 HALCON AngleMeasure/Area）
// 1) OpencvAngleNode：两条线段夹角的纯几何计算（垂直=90、平行=0、退化=无效）
// 2) OpencvBlobNode：面积输出端口暴露最大 Blob 面积（确定性二值图验证）
// 3) OpencvBlobNode：排序/数量限制 + 每个 Blob 的 rect 检测框结果
// 4) OpencvCaliperNode：单边/边对模式（边对数与平均宽度）
#include <QtTest/QtTest>
#include <QObject>
#include <QVector>
#include <QStringList>
#include <cmath>

#include "OpencvAngleNode.h"
#include "OpencvBlobNode.h"
#include "OpencvCaliperNode.h"
#include "DataObject.h"
#include "Port.h"
#include <halconcpp/HalconCpp.h>

using namespace HalconCpp;

class OpencvAngleAreaTest : public QObject
{
    Q_OBJECT
private slots:
    void testAnglePerpendicular();
    void testAngleParallel();
    void testAngleDegenerate();
    void testBlobAreaPort();
    void testBlobResultDepth();
    void testCaliperPairMode();
};

void OpencvAngleAreaTest::testAnglePerpendicular()
{
    OpencvAngleNode node;
    node.init();
    // 线段1：竖直（列固定 100，行 100→200）；线段2：水平（行固定 100，列 100→200）
    node.setParam("r1a", 100.0); node.setParam("c1a", 100.0);
    node.setParam("r1b", 200.0); node.setParam("c1b", 100.0);
    node.setParam("r2a", 100.0); node.setParam("c2a", 100.0);
    node.setParam("r2b", 100.0); node.setParam("c2b", 200.0);
    node.run();
    const double ang = node.getParam("angle").toDouble();
    QVERIFY2(node.getParam("moduleStatus").toBool(), "垂直两线应测量成功");
    QVERIFY2(qAbs(ang - 90.0) < 1e-6,
             qPrintable(QString::fromUtf8("垂直夹角应为 90，实际 %1").arg(ang)));
    QVERIFY2(node.getOutputData(2) &&
                 qAbs(node.getOutputData(2)->getData().toDouble() - 90.0) < 1e-6,
             "夹角数值端口应输出 90");

    // 结果深度（P1）：方向角、线段长度、交点
    QVERIFY2(qAbs(node.getParam("line1Length").toDouble() - 100.0) < 1e-6
                 && qAbs(node.getParam("line2Length").toDouble() - 100.0) < 1e-6,
             "两线段长度应均为 100");
    QVERIFY2(qAbs(node.getParam("intersectionCol").toDouble() - 100.0) < 1e-6
                 && qAbs(node.getParam("intersectionRow").toDouble() - 100.0) < 1e-6,
             "两线交点应为 (100,100)");
    QVERIFY2(!node.getParam("parallel").toBool(), "垂直两线不应判平行");
    // 线段1 向下、线段2 向右：图像坐标（行向下）下从线段1 旋到线段2 = −90°
    QVERIFY2(qAbs(node.getParam("signedAngle").toDouble() + 90.0) < 1e-6,
             qPrintable(QStringLiteral("有向夹角应为 -90，实际 %1")
                            .arg(node.getParam("signedAngle").toDouble())));
    QVERIFY2(qAbs(node.getParam("angle360").toDouble() - 270.0) < 1e-6,
             "0~360 夹角应为 270");

    // 角度范围切到 0~360 时，数值端口跟着走
    node.setParam(QStringLiteral("angleRange"), 1);
    node.run();
    QVERIFY2(node.getOutputData(2) &&
                 qAbs(node.getOutputData(2)->getData().toDouble() - 270.0) < 1e-6,
             "0~360 模式下数值端口应输出 270");
}

void OpencvAngleAreaTest::testAngleParallel()
{
    OpencvAngleNode node;
    node.init();
    // 两条平行线段（方向均 (100,100)）
    node.setParam("r1a", 100.0); node.setParam("c1a", 100.0);
    node.setParam("r1b", 200.0); node.setParam("c1b", 200.0);
    node.setParam("r2a", 100.0); node.setParam("c2a", 300.0);
    node.setParam("r2b", 200.0); node.setParam("c2b", 400.0);
    node.run();
    const double ang = node.getParam("angle").toDouble();
    QVERIFY2(node.getParam("moduleStatus").toBool(), "平行两线应测量成功");
    QVERIFY2(qAbs(ang) < 1e-6,
             qPrintable(QString::fromUtf8("平行夹角应为 0，实际 %1").arg(ang)));
    QVERIFY2(node.getParam(QStringLiteral("parallel")).toBool(), "平行两线应标记 parallel=true");
    const auto res = node.getOutputData(1);
    QVERIFY2(res != nullptr, "平行两线仍有有效测量结果");
    QCOMPARE(res->getMeasureResult().extraValues.size(), 8);   // 无交点 ⇒ 只带两条线段端点
}

void OpencvAngleAreaTest::testAngleDegenerate()
{
    OpencvAngleNode node;
    node.init();
    // 线段1 退化为点（长度 0）
    node.setParam("r1a", 100.0); node.setParam("c1a", 100.0);
    node.setParam("r1b", 100.0); node.setParam("c1b", 100.0);
    node.setParam("r2a", 100.0); node.setParam("c2a", 100.0);
    node.setParam("r2b", 200.0); node.setParam("c2b", 100.0);
    node.run();
    QVERIFY2(!node.getParam("moduleStatus").toBool(), "退化线段应判无效");
    QVERIFY2(node.getOutputData(1).isNull(), "无效时夹角 Measure 端口应为空");
    QVERIFY2(node.getOutputData(2) &&
                 node.getOutputData(2)->getData().toDouble() == 0.0,
             "无效时夹角数值端口应为 0");
}

void OpencvAngleAreaTest::testBlobAreaPort()
{
    OpencvBlobNode node;
    node.init();

    // B1：面积不再是独立端口，而是唯一「输出」上的具名子结果（名字仍可被读到）
    QStringList names;
    for (const Port *p : node.outputPorts()) names.append(p->name());
    for (const auto &lo : node.logicalOutputs()) names.append(lo.first);
    QVERIFY2(names.contains(QString::fromUtf8("面积")),
             "OpencvBlobNode 应暴露『面积』子结果");

    const int w = 60, h = 40;
    // 全白图：二值化后整图为一个 Blob，面积 = W*H
    QVector<unsigned char> buf(static_cast<size_t>(w) * h, 255);
    HImage img;
    GenImage1(&img, "byte", w, h, (Hlong)buf.data());   // 缓冲 buf 必须存活至 run() 用完
    node.setInputImage(img);
    node.run();
    const double area = node.getOutputData(3)
                            ? node.getOutputData(3)->getData().toDouble() : -1.0;
    QVERIFY2(qAbs(area - double(w * h)) < 1.0,
             qPrintable(QString::fromUtf8("全白图面积应≈%1，实际 %2").arg(w * h).arg(area)));
    QVERIFY2(node.getParam("blobCount").toInt() == 1, "全白图应得 1 个 Blob");

    // 全黑图：无 Blob，面积 = 0
    QVector<unsigned char> buf0(static_cast<size_t>(w) * h, 0);
    HImage img0;
    GenImage1(&img0, "byte", w, h, (Hlong)buf0.data());
    node.setInputImage(img0);
    node.run();
    const double area0 = node.getOutputData(3)
                             ? node.getOutputData(3)->getData().toDouble() : -1.0;
    QVERIFY2(qAbs(area0) < 1e-9,
             qPrintable(QString::fromUtf8("全黑图面积应为 0，实际 %1").arg(area0)));
    QVERIFY2(node.getParam("blobCount").toInt() == 0, "全黑图应得 0 个 Blob");
}

void OpencvAngleAreaTest::testBlobResultDepth()
{
    OpencvBlobNode node;
    node.init();

    const int w = 64, h = 48;
    QVector<unsigned char> buf(static_cast<size_t>(w) * h, 0);
    auto fillRect = [&buf, w](int x, int y, int rw, int rh) {
        for (int yy = y; yy < y + rh; ++yy)
            for (int xx = x; xx < x + rw; ++xx)
                buf[yy * w + xx] = 255;
    };
    fillRect(5, 5, 10, 10);    // 面积 100
    fillRect(30, 10, 20, 20);  // 面积 400（最大）
    HImage img;
    GenImage1(&img, "byte", w, h, (Hlong)buf.data());   // 缓冲必须存活至 run() 用完
    node.setInputImage(img);

    // 按面积降序、只保留 1 个 ⇒ 应留下 20x20 那个
    node.setParam(QStringLiteral("findNum"), 1);
    node.setParam(QStringLiteral("sortFeature"), 0);
    node.setParam(QStringLiteral("sortMode"), 1);   // 降序（VM：1=降序）
    node.run();

    QVERIFY2(node.getParam(QStringLiteral("moduleStatus")).toBool(), "Blob 应执行成功");
    QVERIFY2(node.getParam(QStringLiteral("blobCount")).toInt() == 1, "findNum=1 应只保留 1 个 Blob");
    QVERIFY2(qAbs(node.getParam(QStringLiteral("maxBlobArea")).toDouble() - 400.0) < 1e-9,
             "排序后最大面积应为 400");
    QVERIFY2(qAbs(node.getOutputData(3)->getData().toDouble() - 400.0) < 1e-9,
             "『面积』端口应输出最大 Blob 面积");

    // 每个保留 Blob 一条 rect 结果（extraValues=[x,y,w,h]）
    const auto list = node.getOutputData(4);
    QVERIFY2(list != nullptr, "有 Blob 时『Blob列表』端口应有数据");
    const QVector<MeasureResult> rects = list->measureResults();
    QCOMPARE(rects.size(), 1);
    QCOMPARE(rects[0].type, QStringLiteral("rect"));
    QCOMPARE(rects[0].extraValues.size(), 4);
    QVERIFY2(qAbs(rects[0].extraValues[0] - 30.0) < 1e-9
                 && qAbs(rects[0].extraValues[1] - 10.0) < 1e-9
                 && qAbs(rects[0].extraValues[2] - 20.0) < 1e-9
                 && qAbs(rects[0].extraValues[3] - 20.0) < 1e-9,
             "rect 的 [x,y,w,h] 应为 20x20 那个矩形的外接框");

    // 升序 ⇒ 应改为留下面积 100 的那个
    node.setParam(QStringLiteral("sortMode"), 0);   // 升序（VM：0=升序）
    node.run();
    QCOMPARE(node.getParam(QStringLiteral("blobCount")).toInt(), 1);
    QVERIFY2(qAbs(node.getParam(QStringLiteral("maxBlobArea")).toDouble() - 100.0) < 1e-9,
             "升序 + findNum=1 应保留最小的 Blob");
}

void OpencvAngleAreaTest::testCaliperPairMode()
{
    OpencvCaliperNode node;
    node.init();

    const int w = 100, h = 100;
    QVector<unsigned char> buf(static_cast<size_t>(w) * h, 0);
    for (int y = 40; y < 60; ++y)
        for (int x = 0; x < w; ++x)
            buf[y * w + x] = 255;   // 横向白色带：上下各一条水平边缘，间距 20px
    HImage img;
    GenImage1(&img, "byte", w, h, (Hlong)buf.data());
    node.setInputImage(img);

    node.setParam(QStringLiteral("row1"), 50.0);
    node.setParam(QStringLiteral("col1"), 10.0);
    node.setParam(QStringLiteral("row2"), 50.0);
    node.setParam(QStringLiteral("col2"), 90.0);
    node.setParam(QStringLiteral("halfWidth"), 20);
    node.setParam(QStringLiteral("step"), 5.0);
    node.setParam(QStringLiteral("threshold"), 30.0);

    // 单边模式：每条剖面 2 条边缘
    node.setParam(QStringLiteral("edgeMode"), 0);
    node.run();
    QVERIFY2(node.getParam(QStringLiteral("moduleStatus")).toBool(), "卡尺应执行成功");
    const int singleCount = node.getParam(QStringLiteral("edgeCount")).toInt();
    QVERIFY2(singleCount >= 30,
             qPrintable(QStringLiteral("单边模式应检出约 34 条边缘，实际 %1").arg(singleCount)));
    QCOMPARE(node.getParam(QStringLiteral("pairCount")).toInt(), 0);

    // 边对模式：上下边缘配成对，平均宽度≈20
    node.setParam(QStringLiteral("edgeMode"), 1);
    node.setParam(QStringLiteral("pairWidth"), 30);
    node.run();
    const int pairCount = node.getParam(QStringLiteral("pairCount")).toInt();
    QVERIFY2(pairCount >= 15,
             qPrintable(QStringLiteral("边对模式应得到约 17 对，实际 %1").arg(pairCount)));
    QVERIFY2(qAbs(node.getParam(QStringLiteral("avgWidth")).toDouble() - 20.0) < 2.0,
             qPrintable(QStringLiteral("平均边对宽度应≈20，实际 %1")
                            .arg(node.getParam(QStringLiteral("avgWidth")).toDouble())));
    const auto pairData = node.getOutputData(3);
    QVERIFY2(pairData != nullptr, "边对模式应在『边缘对』端口输出明细");
    QVERIFY2(pairData->getMeasureResult().type == QStringLiteral("caliperPair"),
             "边对明细的类型应为 caliperPair");
}

QTEST_MAIN(OpencvAngleAreaTest)
#include "opencv_angle_area_test.moc"
