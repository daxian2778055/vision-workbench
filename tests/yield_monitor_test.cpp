#include <QtTest>
#include <QList>
#include <QString>

#include "YieldMonitor.h"

// 良率目标监控（P1-11 的"目标线 + 报警联动"判定部分）契约测试。
//
// 这里钉的是**现场口径**，不是实现细节（改了判定语义，这些用例必须跟着改，并在提交里写明原因）：
//  ① 未设目标（≤0）= 关闭，不判定；
//  ② 窗口内样本不足不判定（刚开线第一轮 NG 不该报警）；
//  ③ 只看**最近 N 轮**（滚动窗口：产线关心"刚才连续 NG"，不关心历史累计）；
//  ④ 判据是"良率 < 目标"（**等于目标算达标**）；
//  ⑤ **闭锁**：同一段低良率只报一次，恢复到目标线以上才解除，否则连续模式下每轮都报、报警历史被灌满；
//  ⑥ 改配置（目标/窗口）会重置闭锁——否则"把目标调严之后反而不报"。
class YieldMonitorTest : public QObject
{
    Q_OBJECT
private slots:
    void testDisabledWhenNoTarget();
    void testNeedsMinSamples();
    void testRaiseOnceThenLatch();
    void testClearThenReRaise();
    void testWindowUsesLastRoundsOnly();
    void testEqualTargetIsMeeting();
    void testConfigChangeResetsLatch();
    void testInvalidConfigIgnored();
    void testTextMentionsNumbers();
};

namespace {
YieldMonitor::Config makeConfig(double target, int window = 50, int minSamples = 10)
{
    YieldMonitor::Config cfg;
    cfg.targetPercent = target;
    cfg.windowRounds = window;
    cfg.minSamples = minSamples;
    return cfg;
}

/// 造一段轮次结果：ok 个 OK 后接 ng 个 NG（顺序有意义：窗口是"按时间最近的 N 轮"）
QList<bool> makeRounds(int ok, int ng)
{
    QList<bool> out;
    for (int i = 0; i < ok; ++i)
        out.append(true);
    for (int i = 0; i < ng; ++i)
        out.append(false);
    return out;
}
}   // namespace

void YieldMonitorTest::testDisabledWhenNoTarget()
{
    YieldMonitor monitor(makeConfig(0.0));   // 未设目标
    const YieldMonitor::Result r = monitor.evaluate(makeRounds(0, 50));
    QVERIFY2(!r.evaluated, "未设目标时不应判定");
    QVERIFY(!r.alarmRaised);
    QVERIFY(!r.alarmCleared);
    QVERIFY(!monitor.alarmLatched());
    QVERIFY(r.text().isEmpty());             // 未判定就没有文案（不往日志里塞噪声）
}

void YieldMonitorTest::testNeedsMinSamples()
{
    YieldMonitor monitor(makeConfig(98.0, 50, 10));

    // 9 轮全 NG（样本不足）：不判定、不报警——刚开线不该报
    YieldMonitor::Result r = monitor.evaluate(makeRounds(0, 9));
    QVERIFY2(!r.evaluated, "样本不足时不应判定");
    QVERIFY(!r.alarmRaised);

    // 第 10 轮：达到最小样本数 → 判定且低于目标 → 报警
    r = monitor.evaluate(makeRounds(0, 10));
    QVERIFY(r.evaluated);
    QCOMPARE(r.samples, 10);
    QCOMPARE(r.okRounds, 0);
    QVERIFY2(r.belowTarget, "10 轮全 NG 在 98% 目标下应判为低于目标");
    QVERIFY2(r.alarmRaised, "达到最小样本数且低于目标时应报警");
}

void YieldMonitorTest::testRaiseOnceThenLatch()
{
    YieldMonitor monitor(makeConfig(98.0, 50, 10));
    const QList<bool> bad = makeRounds(0, 20);

    const YieldMonitor::Result first = monitor.evaluate(bad);
    QVERIFY(first.alarmRaised);
    QVERIFY(monitor.alarmLatched());

    // 同一段低良率再判多次：**只报一次**（否则连续模式会刷屏、报警历史被灌满）
    for (int i = 0; i < 5; ++i) {
        const YieldMonitor::Result again = monitor.evaluate(bad);
        QVERIFY2(!again.alarmRaised, "同一段低良率重复报警（闭锁失效）");
        QVERIFY2(!again.alarmCleared, "未恢复时不应给恢复通知");
        QVERIFY(again.evaluated);
        QVERIFY(again.belowTarget);
    }
}

void YieldMonitorTest::testClearThenReRaise()
{
    YieldMonitor monitor(makeConfig(98.0, 50, 10));

    QVERIFY(monitor.evaluate(makeRounds(0, 20)).alarmRaised);

    // 恢复（窗口内全 OK）→ 给出一次恢复通知并解除闭锁
    const YieldMonitor::Result recovered = monitor.evaluate(makeRounds(20, 0));
    QVERIFY2(recovered.alarmCleared, "恢复后应给一次恢复通知");
    QVERIFY(!recovered.belowTarget);
    QVERIFY(!monitor.alarmLatched());

    // 再次跌破 → 再报（闭锁解除后才能再报）
    const YieldMonitor::Result again = monitor.evaluate(makeRounds(0, 20));
    QVERIFY2(again.alarmRaised, "恢复后再次跌破应重新报警");
}

void YieldMonitorTest::testWindowUsesLastRoundsOnly()
{
    YieldMonitor monitor(makeConfig(99.0, 5, 5));   // 窗口只有 5 轮

    // 最近 5 轮全 NG（早先的 OK 不该把良率拉高）
    YieldMonitor::Result r = monitor.evaluate(makeRounds(50, 5));
    QVERIFY(r.evaluated);
    QCOMPARE(r.samples, 5);
    QCOMPARE(r.yieldPercent, 0.0);
    QVERIFY(r.alarmRaised);

    // 最近 5 轮全 OK、早先 50 轮全 NG（顺序：老 → 新，末尾才是"最近"）→ 恢复
    QList<bool> ngThenOk = makeRounds(0, 50);
    ngThenOk += makeRounds(5, 0);
    r = monitor.evaluate(ngThenOk);
    QCOMPARE(r.samples, 5);
    QCOMPARE(r.yieldPercent, 100.0);
    QVERIFY(!r.belowTarget);
    QVERIFY(r.alarmCleared);
}

void YieldMonitorTest::testEqualTargetIsMeeting()
{
    // 口径：等于目标算达标（2 轮 1 OK → 50%，目标 50% → 不报警）
    YieldMonitor monitor(makeConfig(50.0, 2, 2));
    const YieldMonitor::Result r = monitor.evaluate(makeRounds(1, 1));
    QVERIFY(r.evaluated);
    QCOMPARE(r.yieldPercent, 50.0);
    QVERIFY2(!r.belowTarget, "良率等于目标应算达标");
    QVERIFY(!r.alarmRaised);
}

void YieldMonitorTest::testConfigChangeResetsLatch()
{
    YieldMonitor monitor(makeConfig(98.0, 50, 10));
    QVERIFY(monitor.evaluate(makeRounds(0, 20)).alarmRaised);
    QVERIFY(monitor.alarmLatched());

    // 把目标调严：必须按新口径重新判断（否则"改了目标不生效"，现场很难查）
    monitor.setConfig(makeConfig(99.5, 50, 10));
    QVERIFY2(!monitor.alarmLatched(), "改配置后闭锁未重置");
    const YieldMonitor::Result r = monitor.evaluate(makeRounds(0, 20));
    QVERIFY2(r.alarmRaised, "改配置后应按新口径重新报警");
    QCOMPARE(r.targetPercent, 99.5);
}

void YieldMonitorTest::testInvalidConfigIgnored()
{
    // 目标越界 / 窗口非法 / 最小样本大于窗口：都按"未配置"处理，不判定
    const QList<YieldMonitor::Config> bad{ makeConfig(150.0), makeConfig(-5.0), makeConfig(98.0, 0, 0),
                                           makeConfig(98.0, 5, 10) };
    for (const YieldMonitor::Config &cfg : bad) {
        QVERIFY2(!cfg.isValid(), "非法配置未被识别");
        YieldMonitor monitor(cfg);
        const YieldMonitor::Result r = monitor.evaluate(makeRounds(0, 50));
        QVERIFY2(!r.evaluated, "非法配置下不应判定");
        QVERIFY(!r.alarmRaised);
    }
}

void YieldMonitorTest::testTextMentionsNumbers()
{
    // 文案要能直接进日志/报警历史：会话窗口、良率、目标都得有，否则事后无法追溯"当时按什么判的"
    YieldMonitor monitor(makeConfig(98.0, 50, 10));
    const YieldMonitor::Result r = monitor.evaluate(makeRounds(6, 4));   // 10 轮 6 OK → 60%
    QVERIFY(r.evaluated);
    const QString text = r.text();
    QVERIFY2(text.contains(QStringLiteral("10")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("60.0")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("98.0")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("低于目标")), qPrintable(text));
}

QTEST_MAIN(YieldMonitorTest)
#include "yield_monitor_test.moc"
