// ExecutionControlPolicyTest（180 行词条切片 B 首抽）：执行控制判定核的纯判定座。
// 无控件、无执行器、无窗口——只判"点了之后按状态/模式/闸位该怎么走"（Intent 六值）。
// 红臂破坏位 = 闸位取反（startIntent 与 singleShotIntent 的 canRunFlow 条件真值取反）
// ——start 闸腿与单次闸腿当场红，其余腿臂内原样绿。
#include <QtTest/QtTest>

#include "ExecutionControlPolicy.h"

using Intent = ExecutionControlPolicy::Intent;

class ExecutionControlPolicyTest : public QObject
{
    Q_OBJECT

private slots:
    void testStartIntentTraffic();     // 点开始：运行/暂停即继续（闸不入分支）、余态看闸
    void testPauseResumeTraffic();     // 切换钮：Running→Pause、Paused→Resume、余态 None
    void testSingleShotTraffic();      // 单次：Running 与非软触发静默 None、闸位拦新发起
    void testErrorStateRouting();      // Error 态：开始走闸位、切换给 None
    void testPausedSingleShotPassthrough(); // 单次不拦暂停态（原实现原样入档）
};

void ExecutionControlPolicyTest::testStartIntentTraffic()
{
    // 运行/暂停中点开始 = 继续，闸位不影响已在跑的流程（两向都钉）
    QCOMPARE(int(ExecutionControlPolicy::startIntent(ExecutionState::Running, false)),
             int(Intent::Resume));
    QCOMPARE(int(ExecutionControlPolicy::startIntent(ExecutionState::Paused, true)),
             int(Intent::Resume));
    // 余态（空闲/停止）看闸：闸闭拒新发起，闸开给 StartNewRun（调用方先退单步再启动）
    QCOMPARE(int(ExecutionControlPolicy::startIntent(ExecutionState::Stopped, false)),
             int(Intent::GateClosedRefuse));
    QCOMPARE(int(ExecutionControlPolicy::startIntent(ExecutionState::Idle, true)),
             int(Intent::StartNewRun));
}

void ExecutionControlPolicyTest::testPauseResumeTraffic()
{
    QCOMPARE(int(ExecutionControlPolicy::pauseResumeIntent(ExecutionState::Running)),
             int(Intent::Pause));
    QCOMPARE(int(ExecutionControlPolicy::pauseResumeIntent(ExecutionState::Paused)),
             int(Intent::Resume));
    QCOMPARE(int(ExecutionControlPolicy::pauseResumeIntent(ExecutionState::Stopped)),
             int(Intent::None));
    QCOMPARE(int(ExecutionControlPolicy::pauseResumeIntent(ExecutionState::Idle)),
             int(Intent::None));
}

void ExecutionControlPolicyTest::testSingleShotTraffic()
{
    // 运行中静默早退（闸位与模式都不看）——原槽序第一判据
    QCOMPARE(int(ExecutionControlPolicy::singleShotIntent(
                 ExecutionState::Running, FlowMode::SoftwareTrigger, true)),
             int(Intent::None));
    // 非软触发静默早退（按钮禁用的不可达分支，两模式都钉）
    QCOMPARE(int(ExecutionControlPolicy::singleShotIntent(
                 ExecutionState::Idle, FlowMode::HardwareTrigger, true)),
             int(Intent::None));
    QCOMPARE(int(ExecutionControlPolicy::singleShotIntent(
                 ExecutionState::Stopped, FlowMode::Continuous, true)),
             int(Intent::None));
    // 软触发 + 停止态看闸：闸闭拒，闸开给 StartOnce（不退单步）
    QCOMPARE(int(ExecutionControlPolicy::singleShotIntent(
                 ExecutionState::Stopped, FlowMode::SoftwareTrigger, false)),
             int(Intent::GateClosedRefuse));
    QCOMPARE(int(ExecutionControlPolicy::singleShotIntent(
                 ExecutionState::Idle, FlowMode::SoftwareTrigger, true)),
             int(Intent::StartOnce));
}

void ExecutionControlPolicyTest::testErrorStateRouting()
{
    // 错误态不在运行/暂停之列：开始走闸位口径，切换给 None（余态不可达位同注释）
    QCOMPARE(int(ExecutionControlPolicy::startIntent(ExecutionState::Error, true)),
             int(Intent::StartNewRun));
    QCOMPARE(int(ExecutionControlPolicy::pauseResumeIntent(ExecutionState::Error)),
             int(Intent::None));
}

void ExecutionControlPolicyTest::testPausedSingleShotPassthrough()
{
    // 原实现单次只拦 Running，不拦 Paused——本腿手动原样入档（行为零变更钉）
    QCOMPARE(int(ExecutionControlPolicy::singleShotIntent(
                 ExecutionState::Paused, FlowMode::SoftwareTrigger, true)),
             int(Intent::StartOnce));
}

QTEST_MAIN(ExecutionControlPolicyTest)
#include "execution_control_policy_test.moc"
