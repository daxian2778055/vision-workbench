#pragma once

#include <QString>

/// 解析路径的**真实落点**（2026-10-26 复核）：Windows 上 `QFileInfo::canonicalFilePath()`
/// **不解析 junction（目录联接）**，用它做"是否在标注目录内"的判据会被 junction 绕过
/// （实测：root/link → outside，canonical 仍返回 root/link，包含检查直接放行）。
/// 本函数在 Windows 走 GetFinalPathNameByHandleW（对 junction / symlink 都有效），
/// 其它平台退回 canonicalFilePath。解析失败返回空串，调用方按"不可信"处理。
QString resolveFinalPath(const QString &path);
