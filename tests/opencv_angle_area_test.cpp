// OpenCV 角度/面积测量功能测试（🥇：替代替换版环境下被禁用的 HALCON AngleMeasure/Area）
// 1) OpencvAngleNode：两条线段夹角的纯几何计算（垂直=90、平行=0、退化=无效）
// 2) OpencvBlobNode：面积输出端口暴露最大 Blob 面积（确定性二值图验证）
#include <QtTest/QtTest>
#include <QObject>
#include <QVector>
#include <QStringList>
#include <cmath>

#include "OpencvAngleNode.h"
#include "OpencvBlobNode.h"
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

    // 端口已暴露：面积（索引 3）
    QStringList names;
    for (const Port *p : node.outputPorts()) names.append(p->name());
    QVERIFY2(names.contains(QString::fromUtf8("面积")),
             "OpencvBlobNode 应暴露『面积』输出端口");

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

QTEST_MAIN(OpencvAngleAreaTest)
#include "opencv_angle_area_test.moc"
