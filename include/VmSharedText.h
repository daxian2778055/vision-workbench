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
}   // namespace VmSharedText
