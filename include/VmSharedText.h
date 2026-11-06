#pragma once

#include <QString>

/// VM 4.4 对齐批次的共享 UI 文案（G-4 重复中文句闸要求：同一句只在一个文件里落字面量）。
/// 这里放的是跨多个算子复用的参数说明与错误提示；新增同类文案请往这里加，
/// 不要在第二个 .cpp 里再抄一遍 QStringLiteral。
namespace VmSharedText
{
/// 仿射类算子「填充方式为常量」时 extensionValue 的参数说明
inline QString constantFillGrayHint()
{
    return QStringLiteral("填充方式为常量时使用的灰度值");
}

/// 目录创建失败提示（携带目标路径）
inline QString mkdirFailed(const QString &path)
{
    return QStringLiteral("无法创建目录：%1").arg(path);
}

/// VM「模块结果 → 检测区域」公共字段标签：多个算子在 resultDisplayFields()
/// 里声明同一组字段，字面量只能落在这里一份（G-4 跨文件重复中文句闸）。
inline QString roiCenterXLabel()
{
    return QStringLiteral("检测区域中心点X");
}
inline QString roiCenterYLabel()
{
    return QStringLiteral("检测区域中心点Y");
}
}   // namespace VmSharedText
