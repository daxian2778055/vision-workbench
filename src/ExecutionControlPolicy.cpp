// ExecutionControlPolicy（180 行词条切片 B 首抽，从 MainWindow 四控制槽抽出的判定核）：
// 三枚判定位纯函数（开始 / 暂停继续切换 / 单次），Intent 六值见同名头文件钉住的判据；
// 表现（执行器调用、文案、按钮可用态）归 MainWindow 与 ExecutionStatusController，
// 停止执行无判定位（槽内一行直达，不入本面）。
#include "ExecutionControlPolicy.h"

using Intent = ExecutionControlPolicy::Intent;

Intent ExecutionControlPolicy::startIntent(ExecutionState state, bool canRunFlow)
{
    if (state == ExecutionState::Running || state == ExecutionState::Paused) {
        return Intent::Resume;   // 已在跑/暂停：点"开始"视为继续（闸位不入此分支）
    }
    if (!canRunFlow) {
        return Intent::GateClosedRefuse;   // 闸拦新发起
    }
    return Intent::StartNewRun;
}

Intent ExecutionControlPolicy::pauseResumeIntent(ExecutionState state)
{
    if (state == ExecutionState::Running) {
        return Intent::Pause;
    }
    if (state == ExecutionState::Paused) {
        return Intent::Resume;
    }
    return Intent::None;   // 其余状态按钮为禁用态（原注释口径）
}

Intent ExecutionControlPolicy::singleShotIntent(ExecutionState state, FlowMode mode,
                                                bool canRunFlow)
{
    if (state == ExecutionState::Running) {
        return Intent::None;   // 原槽静默早退，无提示
    }
    if (mode != FlowMode::SoftwareTrigger) {
        return Intent::None;   // 按钮已禁用的不可达分支
    }
    if (!canRunFlow) {
        return Intent::GateClosedRefuse;
    }
    return Intent::StartOnce;   // 不退单步（与开始执行的语义差位）
}
