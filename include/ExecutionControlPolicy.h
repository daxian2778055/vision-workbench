#pragma once

#include "FlowExecutor.h"   // ExecutionState / FlowMode

/// 执行控制判定策略（180 行词条切片 B 首抽，从 MainWindow 四控制槽抽出）：
/// "点了执行控制后，按状态/模式/闸位该怎么走"的判定核——纯函数、无对象状态、无副作用；
/// 执行器调用、日志文案与按钮可用态仍在 MainWindow 与 ExecutionStatusController
/// （判定归本面、表现归既有位，分工见头件 Public 区注）。
///
/// 判据（与四槽内注释同基，165/173 行闸纪拍板原样）：
///  · 开始执行（任何模式皆可点，暂停中按 = 继续，不留死按钮）：
///      Running/Paused → Resume（闸位不影响已在跑的流程——"正在跑的流程不受影响"）；
///      其余态、写闸闭 → GateClosedRefuse（新一次发起被拦）；
///      其余态、闸开 → StartNewRun（调用方须先退单步再启动——
///      "明确点开始 = 正常跑一遍，退出上次遗留的单步模式"）。
///  · 暂停/继续切换：Running → Pause；Paused → Resume；其余 → None（按钮禁用态不可达）。
///  · 单次执行：Running → None（原槽静默早退，无提示）；非软触发 → None
///      （按钮在连续/硬触发下禁用，不可达）；写闸闭 → GateClosedRefuse；
///      否则 → StartOnce（不退单步——与开始执行的关键语义差）。
///      注意：Paused 态不拦单次（原实现如此，一并原样入档）。
///
/// 停止执行无判定位：按钮无条件停止，槽内一行直达，不入本面。
class ExecutionControlPolicy final {
public:
    enum class Intent {
        Resume,           /// 继续执行（运行/暂停中点开始，或暂停中点切换）
        Pause,            /// 暂停（运行中点切换）
        StartNewRun,      /// 新一次发起：调用方先退单步再启动（开始执行）
        StartOnce,        /// 单次发起：调用方直接启动（单次执行）
        GateClosedRefuse, /// 写闸闭时不受理新发起（调用方走文案提示）
        None,             /// 什么都不做（原槽静默早退形态）
    };

    /// 开始执行的意图判定（闸位只看"新发起是否被拦"，运行/暂停的继续不看闸）
    static Intent startIntent(ExecutionState state, bool canRunFlow);

    /// 暂停/继续切换的意图判定（仅运行/暂停两态有效，其余 None）
    static Intent pauseResumeIntent(ExecutionState state);

    /// 单次执行的意图判定（序与原槽直落序同向：Running → 非软触发 → 闸位）
    static Intent singleShotIntent(ExecutionState state, FlowMode mode, bool canRunFlow);
};
