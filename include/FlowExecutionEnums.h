#pragma once

/// 执行状态与流程运行模式（切片 C 立项书 P2a 期：自执行器头件拆出的轻头件）。
/// 值域与序**不变**——下游判读（连续模式、软触发、硬触发；空闲、运行、暂停、停止、错误）
/// 依赖这套序与这些名字，拆位只为让共享块不牵动执行器整头（那整头带图形栈与 HALCON 依赖）。
enum class ExecutionState {
    Idle,
    Running,
    Paused,
    Stopped,
    Error
};

/// 流程运行模式
enum class FlowMode {
    Continuous      = 0,  /// 连续模式：自动周期循环执行
    SoftwareTrigger = 1,  /// 软触发模式：需手动/通讯触发执行一次
    HardwareTrigger = 2   /// 硬触发模式：仅相机触发源生效时产生帧
};
