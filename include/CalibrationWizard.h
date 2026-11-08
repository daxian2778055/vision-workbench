#pragma once

#include <QString>
#include <QVector>

/// VM 4.4「工具 → 标定配置引导」（官方 GUID-9D4AEEFC-ED21-437E-B20D-9DEB5C85288D）。
/// 官方：选择标定类型后点击「创建」进入步骤引导，可快速完成一键标定。
/// 本文件是纯数据面（不依赖 QWidget / FlowScene），便于单测与后续 UI 复用。
enum class CalibrationWizardType {
    Static = 0,   ///< 静止单相机 + 标定板，确定图像↔物理坐标系
    Dynamic = 1,  ///< 特征点移动，确定转换关系与旋转中心
    Mapping = 2   ///< 特征点确定两个相机图像坐标系之间的转换
};

struct CalibrationWizardSpec {
    CalibrationWizardType type = CalibrationWizardType::Static;
    QString name;                 ///< 官方类型名
    QString description;          ///< 官方释义
    QString targetModule;         ///< 官方指定落地模块
    QVector<QString> steps;       ///< 官方引导步骤（按子页顺序）
    QVector<QString> flowNodes;   ///< 「创建」要生成的模块显示名（按连线顺序，用于日志/展示）
    QVector<QString> flowNodeIds; ///< 同一批模块的注册类型 ID：创建时以它为准，避免显示名匹配漂移
    bool supported = false;       ///< 本实现是否具备完整算子链
    QString unsupportedReason;   ///< 不造空功能：缺哪个模块就直说
};

namespace CalibrationWizard {
    QVector<CalibrationWizardSpec> allSpecs();
    CalibrationWizardSpec specFor(CalibrationWizardType type);
}
