#pragma once

#include <QString>

/// 计算后端（2026-10-27）：OpenCV 的透明加速（T-API / OpenCL）。
///
/// 为什么要一个显式开关：T-API 本身是"用得上就用"，但现场需要两件事——
///   ① 能**关掉**（驱动异常/远程桌面黑屏时回退 CPU）；
///   ② 能**看见**当前在用什么（性能面板显示设备名与"加速/回退"）。
/// 做法是不动各算子的 cv:: 调用（T-API 对 Mat 透明），只在进程级控制 OpenCL 开关。
class ComputeBackend
{
public:
    enum class Mode { Auto, OpenCL, Cpu };

    static ComputeBackend &instance();

    /// 读配置并应用（main() 里调一次）。配置键 compute/backend: auto | on | off
    void init();

    /// 切换并持久化
    void setMode(Mode m);
    Mode mode() const { return m_mode; }

    /// OpenCL 设备是否存在（与"当前是否启用"是两回事）
    bool openclAvailable() const;
    /// 当前是否真的在走 OpenCL
    bool openclActive() const;

    /// 供性能面板显示，例如 "OpenCL: NVIDIA RTX ...（加速）" / "CPU（回退：无 OpenCL 设备）"
    QString describe() const;

private:
    ComputeBackend() = default;
    void apply();

    Mode m_mode = Mode::Auto;
};
