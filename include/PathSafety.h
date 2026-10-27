#pragma once

#include <QString>

/// 解析路径的**真实落点**（2026-10-26 复核）：Windows 上 `QFileInfo::canonicalFilePath()`
/// **不解析 junction（目录联接）**，用它做"是否在标注目录内"的判据会被 junction 绕过
/// （实测：root/link → outside，canonical 仍返回 root/link，包含检查直接放行）。
/// 本函数在 Windows 走 GetFinalPathNameByHandleW（对 junction / symlink 都有效），
/// 其它平台退回 canonicalFilePath。解析失败返回空串，调用方按"不可信"处理。
QString resolveFinalPath(const QString &path);

/// 让"节点 ↔ 受限脚本"之间的临时交换文件对受限令牌可读写（2026-10-27）：
/// 受限令牌的写权限要求 DACL 同时覆盖用户 SID **和**受限 SID 集合，默认 TEMP 文件的
/// ACL 只给用户/管理员，脚本一写就 Permission denied。这里把该文件的 DACL 置空
/// （等价于 Everyone-Full，仅限我们自己创建的短生命周期交换文件）。
bool makeWorldWritable(const QString &path);
