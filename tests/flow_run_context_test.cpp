// FlowRunContextTest（切片 C 立项书 P2a 期：块与承载）——运行上下文块的语义座。
// 纯数据与线程面断言：状态与模式的读写与跨线程可见、上下文存在性、可取消等待的三条语义
// （停则醒且返回假、暂停不扣预算、正常耗时返回真）。本座不触执行器主体——本期是纯新增件。
// 红臂破坏位 = 等待循环里的停止判定（去掉即停而不醒）——停则醒腿当场红，余腿臂内原样绿。
#include <QtTest>
#include <QElapsedTimer>
#include <QThread>
#include <atomic>

#include "FlowRunContext.h"

namespace {

/// 跑一段可取消等待并回传结果与耗时（工作线程）。
class Sleeper : public QThread
{
public:
    explicit Sleeper(FlowRunContext *ctx, int ms)
        : m_ctx(ctx), m_ms(ms) {}

    bool result() const { return m_result.load(); }
    qint64 elapsedMs() const { return m_elapsed.load(); }

protected:
    void run() override
    {
        QElapsedTimer t;
        t.start();
        m_result.store(m_ctx->interruptibleSleep(m_ms));
        m_elapsed.store(t.elapsed());
    }

private:
    FlowRunContext *m_ctx;
    int m_ms;
    std::atomic<bool> m_result{true};
    std::atomic<qint64> m_elapsed{0};
};

}   // namespace

class FlowRunContextTest : public QObject
{
    Q_OBJECT

private slots:
    void testInitialValues();          // 初值与今天的执行器初值同
    void testStateAndModeRoundTrip();  // 状态与模式的读写
    void testCrossThreadVisibility();  // 跨线程可见
    void testEstablishedFlag();        // 上下文存在性
    void testSleepWakesOnStop();       // 停则醒且返回假（红臂靶腿）
    void testPauseDoesNotConsumeBudget();  // 暂停不扣预算
    void testSleepReturnsTrueWhenElapsed();  // 正常耗时返回真
};

void FlowRunContextTest::testInitialValues()
{
    FlowRunContext ctx;
    QCOMPARE(ctx.state(), ExecutionState::Idle);
    QCOMPARE(ctx.flowMode(), FlowMode::SoftwareTrigger);
    QVERIFY(!ctx.established());
}

void FlowRunContextTest::testStateAndModeRoundTrip()
{
    FlowRunContext ctx;
    ctx.setState(ExecutionState::Running);
    QCOMPARE(ctx.state(), ExecutionState::Running);
    ctx.setState(ExecutionState::Paused);
    QCOMPARE(ctx.state(), ExecutionState::Paused);
    ctx.setFlowMode(FlowMode::Continuous);
    QCOMPARE(ctx.flowMode(), FlowMode::Continuous);
    ctx.setFlowMode(FlowMode::HardwareTrigger);
    QCOMPARE(ctx.flowMode(), FlowMode::HardwareTrigger);
}

void FlowRunContextTest::testCrossThreadVisibility()
{
    FlowRunContext ctx;
    std::atomic<bool> wrote{false};
    QThread writer;
    QObject::connect(&writer, &QThread::started, [&ctx, &wrote]() {
        ctx.setState(ExecutionState::Running);
        ctx.setFlowMode(FlowMode::Continuous);
        ctx.markEstablished();
        wrote.store(true);
    });
    writer.start();
    QTRY_VERIFY(wrote.load());
    QCOMPARE(ctx.state(), ExecutionState::Running);
    QCOMPARE(ctx.flowMode(), FlowMode::Continuous);
    QVERIFY(ctx.established());
    writer.quit();
    writer.wait(2000);
}

void FlowRunContextTest::testEstablishedFlag()
{
    FlowRunContext ctx;
    QVERIFY(!ctx.established());
    ctx.markEstablished();
    QVERIFY(ctx.established());
}

void FlowRunContextTest::testSleepWakesOnStop()
{
    FlowRunContext ctx;
    Sleeper sleeper(&ctx, 5000);
    sleeper.start();
    QTRY_VERIFY(sleeper.isRunning());
    QTest::qWait(50);   // 让它真的进入等待
    ctx.requestStop();
    const bool finished = sleeper.wait(3000);      // 应当被唤醒而不是睡满
    if (!finished) {
        // 破坏位下它不会醒：等它自然睡完，免得线程仍在跑时被析构（析构运行中的线程是致命错）
        sleeper.wait(8000);
    }
    QVERIFY(finished);
    QVERIFY(!sleeper.result());       // 被取消 ⇒ 返回假
    QVERIFY(sleeper.elapsedMs() < 3000);
}

void FlowRunContextTest::testPauseDoesNotConsumeBudget()
{
    FlowRunContext ctx;
    ctx.setState(ExecutionState::Paused);
    Sleeper sleeper(&ctx, 300);
    sleeper.start();
    QTest::qWait(200);   // 暂停期间：应当挂着，且预算不被扣
    QVERIFY(sleeper.isRunning());
    ctx.setState(ExecutionState::Running);   // 恢复唤醒
    QVERIFY(sleeper.wait(3000));
    QVERIFY(sleeper.result());
    // 暂停那 200 毫秒不计入延时预算 ⇒ 总耗时不少于 300 毫秒
    QVERIFY(sleeper.elapsedMs() >= 300);
}

void FlowRunContextTest::testSleepReturnsTrueWhenElapsed()
{
    FlowRunContext ctx;
    QElapsedTimer t;
    t.start();
    QVERIFY(ctx.interruptibleSleep(60));
    QVERIFY(t.elapsed() >= 50);
    QVERIFY(ctx.interruptibleSleep(0));   // 零毫秒直接成
    QVERIFY(ctx.interruptibleSleep(-1));
}

QTEST_MAIN(FlowRunContextTest)
#include "flow_run_context_test.moc"
