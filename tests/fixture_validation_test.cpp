// FixtureValidationTest（词条切片 B 夹具族首抽）：命名夹具的载入侧校验与降级与写入侧两道闸的真数据座。
// 纯数据断言：只喂 JSON 条目，断言装出的夹具（矩阵与位姿的有无与值）与回报的原因；
// 不触场景、不触控件（座不需 GUI 与图形栈）。
// 红臂破坏位 = 矩阵项数闸由「恰好六项」放宽为「少于六项」——项数闸腿当场红，余腿臂内原样绿。
#include <QtTest>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include "FixtureValidation.h"

namespace {

QJsonObject homEntry(const QJsonArray &hom)
{
    QJsonObject e;
    e.insert(QStringLiteral("hasHom"), true);
    e.insert(QStringLiteral("hom"), hom);
    return e;
}

QJsonArray sixNumbers()
{
    QJsonArray a;
    for (int i = 1; i <= 6; ++i)
        a.append(i);
    return a;
}

QJsonObject poseEntry(double row, double col, double angle, bool withScale, double scale)
{
    QJsonObject e;
    e.insert(QStringLiteral("hasPose"), true);
    e.insert(QStringLiteral("poseRow"), row);
    e.insert(QStringLiteral("poseCol"), col);
    e.insert(QStringLiteral("poseAngle"), angle);
    if (withScale)
        e.insert(QStringLiteral("poseScale"), scale);
    return e;
}

}   // namespace

class FixtureValidationTest : public QObject
{
    Q_OBJECT

private slots:
    void testNameGate();                   // 名字闸（两侧同口径）
    void testHomographyCountGate();        // 矩阵项数闸（两侧同口径，红臂靶腿）
    void testLoadHomographyValid();        // 矩阵合格：装出六值且不报原因
    void testLoadHomographyCountShort();   // 矩阵项数不合：降级为没有矩阵并报实际项数
    void testLoadHomographyNonNumeric();   // 矩阵逐项非数值：降级并报第几项
    void testLoadPoseValidAndScaleFallback();  // 位姿合格与缩放键缺省回落
    void testLoadPoseInvalidDegrades();    // 位姿不合形：降级为没有位姿并报原因
    void testLoadEmptyNameRejected();      // 名字为空：整条跳过并报原因
};

void FixtureValidationTest::testNameGate()
{
    QVERIFY(!FixtureValidation::isNameAccepted(QString()));
    QVERIFY(!FixtureValidation::isNameAccepted(QStringLiteral("   ")));
    QVERIFY(FixtureValidation::isNameAccepted(QStringLiteral("cam")));
    QVERIFY(FixtureValidation::isNameAccepted(QStringLiteral(" cam ")));
}

void FixtureValidationTest::testHomographyCountGate()
{
    QVERIFY(FixtureValidation::isHomographyCountAccepted(6));
    QVERIFY(!FixtureValidation::isHomographyCountAccepted(5));
    QVERIFY(!FixtureValidation::isHomographyCountAccepted(7));
    QVERIFY(!FixtureValidation::isHomographyCountAccepted(0));
}

void FixtureValidationTest::testLoadHomographyValid()
{
    FlowFixture fx;
    QStringList reasons;
    QVERIFY(FixtureValidation::loadFixture(QStringLiteral("fx"), homEntry(sixNumbers()),
                                           &fx, &reasons));
    QVERIFY(fx.hasHom);
    QVERIFY(!fx.hasPose);
    QCOMPARE(fx.name, QStringLiteral("fx"));
    QCOMPARE(fx.hom.size(), 6);
    for (int i = 0; i < 6; ++i)
        QVERIFY(qAbs(fx.hom[i] - (i + 1)) < 1e-12);
    QVERIFY(reasons.isEmpty());
}

void FixtureValidationTest::testLoadHomographyCountShort()
{
    QJsonArray shortArr;
    shortArr.append(1);
    shortArr.append(2);
    shortArr.append(3);

    FlowFixture fx;
    QStringList reasons;
    // 降级而非整条丢弃：夹具仍装出，但保持没有矩阵
    QVERIFY(FixtureValidation::loadFixture(QStringLiteral("fx"), homEntry(shortArr),
                                           &fx, &reasons));
    QVERIFY(!fx.hasHom);
    QCOMPARE(reasons.size(), 1);
    QVERIFY(reasons.first().contains(QStringLiteral("矩阵有 3 项")));
    QVERIFY(reasons.first().contains(QStringLiteral("夹具保持没有矩阵")));
}

void FixtureValidationTest::testLoadHomographyNonNumeric()
{
    QJsonArray arr = sixNumbers();
    arr.replace(2, QStringLiteral("x"));

    FlowFixture fx;
    QStringList reasons;
    QVERIFY(FixtureValidation::loadFixture(QStringLiteral("fx"), homEntry(arr), &fx, &reasons));
    QVERIFY(!fx.hasHom);
    QCOMPARE(reasons.size(), 1);
    QVERIFY(reasons.first().contains(QStringLiteral("第 3 项不是数值")));
}

void FixtureValidationTest::testLoadPoseValidAndScaleFallback()
{
    FlowFixture fx;
    QStringList reasons;
    QVERIFY(FixtureValidation::loadFixture(QStringLiteral("fx"),
                                           poseEntry(1.0, 2.0, 3.0, true, 4.0), &fx, &reasons));
    QVERIFY(fx.hasPose);
    QVERIFY(qAbs(fx.poseRow - 1.0) < 1e-12);
    QVERIFY(qAbs(fx.poseCol - 2.0) < 1e-12);
    QVERIFY(qAbs(fx.poseAngle - 3.0) < 1e-12);
    QVERIFY(qAbs(fx.poseScale - 4.0) < 1e-12);
    QVERIFY(reasons.isEmpty());

    // 缩放键缺省回落一点零（兼容手写方案）
    FlowFixture fx2;
    QStringList reasons2;
    QVERIFY(FixtureValidation::loadFixture(QStringLiteral("fx"),
                                           poseEntry(1.0, 2.0, 3.0, false, 0.0), &fx2, &reasons2));
    QVERIFY(fx2.hasPose);
    QVERIFY(qAbs(fx2.poseScale - 1.0) < 1e-12);
    QVERIFY(reasons2.isEmpty());
}

void FixtureValidationTest::testLoadPoseInvalidDegrades()
{
    QJsonObject missing = poseEntry(1.0, 2.0, 3.0, true, 4.0);
    missing.remove(QStringLiteral("poseRow"));

    FlowFixture fx;
    QStringList reasons;
    QVERIFY(FixtureValidation::loadFixture(QStringLiteral("fx"), missing, &fx, &reasons));
    QVERIFY(!fx.hasPose);
    QCOMPARE(reasons.size(), 1);
    QVERIFY(reasons.first().contains(QStringLiteral("位姿缺 poseRow")));
    QVERIFY(reasons.first().contains(QStringLiteral("夹具保持没有位姿")));

    QJsonObject bad = poseEntry(1.0, 2.0, 3.0, true, 4.0);
    bad.insert(QStringLiteral("poseCol"), QStringLiteral("x"));
    FlowFixture fx2;
    QStringList reasons2;
    QVERIFY(FixtureValidation::loadFixture(QStringLiteral("fx"), bad, &fx2, &reasons2));
    QVERIFY(!fx2.hasPose);
    QCOMPARE(reasons2.size(), 1);
    QVERIFY(reasons2.first().contains(QStringLiteral("位姿 poseCol 不是数值")));
}

void FixtureValidationTest::testLoadEmptyNameRejected()
{
    FlowFixture fx;
    QStringList reasons;
    QVERIFY(!FixtureValidation::loadFixture(QStringLiteral("   "), homEntry(sixNumbers()),
                                            &fx, &reasons));
    QCOMPARE(reasons.size(), 1);
    QVERIFY(reasons.first().contains(QStringLiteral("夹具名为空，整条跳过")));
}

QTEST_MAIN(FixtureValidationTest)
#include "fixture_validation_test.moc"
