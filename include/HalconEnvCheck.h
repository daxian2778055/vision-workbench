#pragma once

#include <QString>

/// HALCON 图像层自检结果（不探测区域/测量，那些链路本软件不使用）
enum class HalconEnvStatus {
    Ok,              /// 图像层可用（读图/显示）
    RegionBroken,    /// 保留枚举，当前自检不再返回
    Fatal,           /// 图像层基础调用失败（DLL 缺失/损坏等）
};

/// 帮助菜单：检测 HALCON 图像层（GenImageConst / GetImageSize / GetImagePointer1）。
/// 通过只说明读图与 DeepOCR 可用图像层；日常算法走 OpenCV，不依赖本检测。
HalconEnvStatus halconEnvironmentCheck(QString *detail = nullptr);

/// 与 halconEnvironmentCheck 相同（单遍），供内部诊断；节点执行不再据此拦截。
HalconEnvStatus halconRuntimeProbe(QString *detail = nullptr);
