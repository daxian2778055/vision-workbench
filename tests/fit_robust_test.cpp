// FitRobustTest（VM「图形生成 → 直线」稳健拟合参数面）：
// 角度归一/夹角、最小二乘与 cv::fitLine 同口径、Huber/Tukey 抗离群、剔除点/剔除距离、
// 角度约束失败原因、退化与非法输入。纯函数面，不需要图像管线。
#include <QPointF>
#include <QtTest>
#include <cmath>

#include "FitRobust.h"

class FitRobustTest : public QObject
{
    Q_OBJECT

private slots:
    void testAngleNormalization();
    void testAngleDifference();
    void testLeastSquaresMatchesLine();
    void testOutlierRejection();
    void testRejectionDisabledByDefault();
    void testRobustWeightsResistOutliers();
    void testAngleConstraint();
    void testDegenerateAndInvalidInput();
};

namespace {

/// 生成一条直线上的等间距点（点数 count，起点 (x0,y0)，方向 (dx,dy) 已归一）
QVector<QPointF> linePoints(int count, double x0, double y0, double dx, double dy)
{
    QVector<QPointF> pts;
    pts.reserve(count);
    for (int i = 0; i < count; ++i)
        pts.append(QPointF(x0 + dx * i, y0 + dy * i));
    return pts;
}

} // namespace

void FitRobustTest::testAngleNormalization()
{
    QCOMPARE(FitRobust::normalizeLineAngle(0.0), 0.0);
    QCOMPARE(FitRobust::normalizeLineAngle(180.0), 0.0);
    QCOMPARE(FitRobust::normalizeLineAngle(-180.0), 0.0);
    QCOMPARE(FitRobust::normalizeLineAngle(-45.0), 135.0);
    QCOMPARE(FitRobust::normalizeLineAngle(190.0), 10.0);
    QCOMPARE(FitRobust::normalizeLineAngle(360.0), 0.0);
    // 非有限值不能污染结果（返回 0 而不是 NaN）
    QCOMPARE(FitRobust::normalizeLineAngle(qQNaN()), 0.0);
}

void FitRobustTest::testAngleDifference()
{
    QCOMPARE(FitRobust::lineAngleDifference(0.0, 180.0), 0.0);
    QCOMPARE(FitRobust::lineAngleDifference(0.0, 90.0), 90.0);
    QCOMPARE(FitRobust::lineAngleDifference(10.0, 175.0), 15.0);
    // 89° 与 -89°（=91°）只差 2°，直线无方向
    QCOMPARE(FitRobust::lineAngleDifference(89.0, -89.0), 2.0);
}

void FitRobustTest::testLeastSquaresMatchesLine()
{
    // 水平线：角度应为 0（vx>0 归一）
    {
        const FitLineResult r =
            FitRobust::fitLine(linePoints(100, 0, 100, 1, 0), FitLineParams());
        QVERIFY2(r.valid, qPrintable(r.failureReason));
        QVERIFY(std::fabs(r.angleDeg) < 1e-9);
        QCOMPARE(r.usedPoints, 100);
        QCOMPARE(r.rejectedPoints, 0);
        QVERIFY(std::fabs(r.y0 - 100.0) < 1e-9);
        QVERIFY(std::fabs(r.x0 - 49.5) < 1e-9);
        QVERIFY(r.vx > 0.0);
    }
    // 45° 线
    {
        const FitLineResult r =
            FitRobust::fitLine(linePoints(50, 0, 0, 0.7071067811865476, 0.7071067811865476),
                               FitLineParams());
        QVERIFY2(r.valid, qPrintable(r.failureReason));
        QVERIFY(std::fabs(r.angleDeg - 45.0) < 1e-6);
    }
    // 竖直线：vx≈0 时按 vy>0 归一，角度 = 90
    {
        const FitLineResult r =
            FitRobust::fitLine(linePoints(30, 50, 0, 0, 1), FitLineParams());
        QVERIFY2(r.valid, qPrintable(r.failureReason));
        QVERIFY(std::fabs(r.angleDeg - 90.0) < 1e-6);
    }
}

void FitRobustTest::testOutlierRejection()
{
    // 40 个内点（y=50，x=0..39）+ 3 个横向远端离群点：开启剔除后角度回到 0，且剔除数准确。
    // 离群点刻意放在 x 远超出内点范围处——L2 初拟合会被它们拉成近似竖直，横向距离仍然很大，
    // 这样"剔除"测的是真实几何，而不是靠初值恰好水平。
    QVector<QPointF> pts = linePoints(40, 0, 50, 1, 0);
    pts << QPointF(200, 400) << QPointF(260, 380) << QPointF(150, 420);

    FitLineParams params;
    params.rejectPoints = 3;
    params.rejectDistance = 10.0;
    const FitLineResult r = FitRobust::fitLine(pts, params);
    QVERIFY2(r.valid, qPrintable(r.failureReason));
    QCOMPARE(r.rejectedPoints, 3);
    QCOMPARE(r.usedPoints, 40);
    QVERIFY2(std::fabs(r.angleDeg) < 1.0, qPrintable(QString::number(r.angleDeg)));
    QVERIFY(std::fabs(r.y0 - 50.0) < 1.0);

    // 匹配掩码：与输入点一一对应，被剔除的 3 个离群点必须是 0（VM「匹配点」口径）
    QCOMPARE(r.usedMask.size(), pts.size());
    int maskedOut = 0;
    for (bool used : r.usedMask)
        maskedOut += used ? 0 : 1;
    QCOMPARE(maskedOut, 3);
    QVERIFY(r.usedMask.at(0) && r.usedMask.at(39));
    QVERIFY(!r.usedMask.at(40) && !r.usedMask.at(41) && !r.usedMask.at(42));

    // 剔除点数上限：只剔 1 个时仍剩 2 个离群点，角度会被拉偏（证明不是"全剔"）
    params.rejectPoints = 1;
    const FitLineResult partial = FitRobust::fitLine(pts, params);
    QVERIFY2(partial.valid, qPrintable(partial.failureReason));
    QCOMPARE(partial.rejectedPoints, 1);
    QVERIFY(std::fabs(partial.angleDeg) > std::fabs(r.angleDeg));
}

void FitRobustTest::testRejectionDisabledByDefault()
{
    QVector<QPointF> pts = linePoints(20, 0, 0, 1, 0);
    pts << QPointF(10, 300);
    const FitLineResult r = FitRobust::fitLine(pts, FitLineParams());
    QVERIFY(r.valid);
    QCOMPARE(r.rejectedPoints, 0);   // 默认 0/0 不剔除，保持旧行为
    QCOMPARE(r.usedPoints, 21);
}

void FitRobustTest::testRobustWeightsResistOutliers()
{
    // 100 个 y=0 内点 + 右侧 10 个 y=200 离群点：最小二乘被显著拉偏，Tukey 应回到水平
    QVector<QPointF> pts = linePoints(100, 0, 0, 1, 0);
    for (int i = 0; i < 10; ++i)
        pts.append(QPointF(90.0 + i, 200.0));

    FitLineParams l2;
    const FitLineResult ls = FitRobust::fitLine(pts, l2);
    QVERIFY2(ls.valid, qPrintable(ls.failureReason));
    QVERIFY2(std::fabs(ls.angleDeg) > 20.0,
             qPrintable(QStringLiteral("最小二乘未被离群点拉偏：%1").arg(ls.angleDeg)));

    FitLineParams tukey;
    tukey.weight = FitWeight::Tukey;
    const FitLineResult tk = FitRobust::fitLine(pts, tukey);
    QVERIFY2(tk.valid, qPrintable(tk.failureReason));
    QVERIFY2(std::fabs(tk.angleDeg) < 5.0,
             qPrintable(QStringLiteral("Tukey 未抑制离群点：%1").arg(tk.angleDeg)));

    FitLineParams huber;
    huber.weight = FitWeight::Huber;
    huber.maxIterations = 1;   // 单次迭代也必须可用
    const FitLineResult hb = FitRobust::fitLine(pts, huber);
    QVERIFY2(hb.valid, qPrintable(hb.failureReason));
    QVERIFY2(std::fabs(hb.angleDeg) < std::fabs(ls.angleDeg),
             qPrintable(QStringLiteral("Huber 未改善：%1 vs %2").arg(hb.angleDeg).arg(ls.angleDeg)));
}

void FitRobustTest::testAngleConstraint()
{
    const QVector<QPointF> pts = linePoints(40, 0, 0, 1, 0);   // 水平线，角度 0°

    FitLineParams pass;
    pass.angleConstrained = true;
    pass.expectedAngleDeg = 5.0;
    pass.angleToleranceDeg = 10.0;
    QVERIFY(FitRobust::fitLine(pts, pass).valid);

    FitLineParams fail;
    fail.angleConstrained = true;
    fail.expectedAngleDeg = 30.0;
    fail.angleToleranceDeg = 10.0;
    const FitLineResult r = FitRobust::fitLine(pts, fail);
    QVERIFY2(!r.valid, "角度超差必须判失败");
    QVERIFY2(r.failureReason.contains(QStringLiteral("超出期望")), qPrintable(r.failureReason));
    QCOMPARE(r.usedPoints, 40);   // 失败原因里仍带实际参与点数，便于定位

    // 180° 等价 0°：期望 180 的约束不能误判
    FitLineParams equivalent;
    equivalent.angleConstrained = true;
    equivalent.expectedAngleDeg = 180.0;
    equivalent.angleToleranceDeg = 1.0;
    QVERIFY(FitRobust::fitLine(pts, equivalent).valid);
}

void FitRobustTest::testDegenerateAndInvalidInput()
{
    // 少于 2 个有效点
    for (const QVector<QPointF> &pts : {QVector<QPointF>(),
                                        QVector<QPointF>{QPointF(1, 1)}}) {
        const FitLineResult r = FitRobust::fitLine(pts, FitLineParams());
        QVERIFY(!r.valid);
        QVERIFY2(r.failureReason.contains(QStringLiteral("不足")), qPrintable(r.failureReason));
    }

    // 所有点重合：方向不可定
    QVector<QPointF> same;
    for (int i = 0; i < 10; ++i)
        same.append(QPointF(7, 7));
    const FitLineResult degenerate = FitRobust::fitLine(same, FitLineParams());
    QVERIFY(!degenerate.valid);
    QVERIFY2(degenerate.failureReason.contains(QStringLiteral("退化")),
             qPrintable(degenerate.failureReason));

    // 非有限坐标被过滤：剩 1 个有效点仍判失败
    QVector<QPointF> withNan{QPointF(0, 0), QPointF(qQNaN(), 5), QPointF(3, qInf())};
    const FitLineResult nanResult = FitRobust::fitLine(withNan, FitLineParams());
    QVERIFY(!nanResult.valid);
}

QTEST_APPLESS_MAIN(FitRobustTest)
#include "fit_robust_test.moc"
