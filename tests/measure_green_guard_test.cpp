// 测量簇的"静默绿灯"守卫（2026-10-25 复核 P2-4）。
//
// FitLine/FitCircle 的内层 HALCON 异常原来只把输出图设成 gray，随后**无条件**
// moduleStatus=true——测量端口为空却绿灯。本用例用"无边缘的均匀图"触发内层测量异常，
// 断言节点必须判红、测量端口必须为空。
#include "FitLineNode.h"
#include "FitCircleNode.h"
#include "OpencvAngleNode.h"
#include "NodeBase.h"
#include "HalconNode.h"

#include <halconcpp/HalconCpp.h>
#include <QtTest/QtTest>
#include <vector>

class MeasureGreenGuardTest : public QObject
{
    Q_OBJECT
private slots:
    void fitLineInnerExceptionJudgesRed();
    void fitCircleInnerExceptionJudgesRed();
    // 正常失败（非异常）也要带可读原因——2026-10-26 复核要求的自动化腿
    void degenerateAngleCarriesReadableReason();
};

using namespace HalconCpp;

namespace {

HImage blankImage(int w = 64, int h = 48)
{
    std::vector<uchar> px(size_t(w) * h, 128);
    HImage img;
    GenImage1(&img, "byte", w, h, reinterpret_cast<Hlong>(px.data()));
    return img;
}

} // namespace

void MeasureGreenGuardTest::fitLineInnerExceptionJudgesRed()
{
    FitLineNode node;
    node.init();
    // 阈值选不中任何区域 ⇒ 空轮廓交给 FitLineContourXld ⇒ 内层 HALCON 异常
    //（空图 128 灰 + minGray=200：Threshold 结果为空）
    node.setParam(QStringLiteral("minGray"), 200);
    node.setParam(QStringLiteral("maxGray"), 255);
    node.setInputImage(blankImage());
    node.run(false);

    QStringList reasons;
    if (node.getParam(QStringLiteral("moduleStatus")).toBool())
        reasons << QStringLiteral("moduleStatus=true（内层异常被吞后仍判绿）");
    if (!node.getOutputData(1).isNull())
        reasons << QStringLiteral("测量端口非空（异常路径不该有结果）");
    QVERIFY2(reasons.isEmpty(),
             qPrintable(QStringLiteral("FitLine 静默绿灯：") + reasons.join(QStringLiteral("; "))));
}

void MeasureGreenGuardTest::fitCircleInnerExceptionJudgesRed()
{
    FitCircleNode node;
    node.init();
    node.setParam(QStringLiteral("minGray"), 200);
    node.setParam(QStringLiteral("maxGray"), 255);
    node.setInputImage(blankImage());
    node.run(false);

    QStringList reasons;
    if (node.getParam(QStringLiteral("moduleStatus")).toBool())
        reasons << QStringLiteral("moduleStatus=true（内层异常被吞后仍判绿）");
    if (!node.getOutputData(1).isNull())
        reasons << QStringLiteral("测量端口非空（异常路径不该有结果）");
    QVERIFY2(reasons.isEmpty(),
             qPrintable(QStringLiteral("FitCircle 静默绿灯：") + reasons.join(QStringLiteral("; "))));
}

void MeasureGreenGuardTest::degenerateAngleCarriesReadableReason()
{
    // 线段 A 的两端点重合 ⇒ 夹角无定义。判红之外，面板必须能看到为什么
    //（lastError 是"原因键"，执行器会把它推到结果面板）。
    OpencvAngleNode node;
    node.init();
    node.setParam(QStringLiteral("r1a"), 100);
    node.setParam(QStringLiteral("c1a"), 100);
    node.setParam(QStringLiteral("r1b"), 100);   // 与 r1a/c1a 重合
    node.setParam(QStringLiteral("c1b"), 100);
    node.setParam(QStringLiteral("r2a"), 100);
    node.setParam(QStringLiteral("c2a"), 200);
    node.setParam(QStringLiteral("r2b"), 200);
    node.setParam(QStringLiteral("c2b"), 200);
    node.run(false);

    QVERIFY2(!node.getParam(QStringLiteral("moduleStatus")).toBool(),
             "线段退化却判了绿");
    const QString why = node.getParam(QStringLiteral("lastError")).toString();
    QVERIFY2(!why.isEmpty(), "判红但没有 lastError：面板上看不到原因");
    QVERIFY2(why.contains(QStringLiteral("线段")), qPrintable(QStringLiteral("原因不含关键词：%1").arg(why)));
}

QTEST_MAIN(MeasureGreenGuardTest)
#include "measure_green_guard_test.moc"
