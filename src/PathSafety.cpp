#include "PathSafety.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#include <sddl.h>

QString resolveFinalPath(const QString &path)
{
    if (path.trimmed().isEmpty())
        return QString();
    const QString native = QDir::toNativeSeparators(path);
    HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), 0,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return QString();

    // 先问所需长度再分配（2026-10-27 复核）：固定 MAX_PATH*8 缓冲会把 2080 字符以上的
    // 合法长路径误判成"不可信"，并与上层"无法解析"的诊断叠成误导性报错。
    const DWORD need = GetFinalPathNameByHandleW(h, nullptr, 0, FILE_NAME_NORMALIZED);
    if (need == 0) {
        CloseHandle(h);
        return QString();
    }
    std::vector<wchar_t> buf(need + 1, L'\0');
    const DWORD got = GetFinalPathNameByHandleW(h, buf.data(), DWORD(buf.size()),
                                               FILE_NAME_NORMALIZED);
    CloseHandle(h);
    if (got == 0 || got >= buf.size())
        return QString();

    QString out = QString::fromWCharArray(buf.data(), int(got));
    // 去掉 \\?\ 与 \\?\UNC\ 前缀，统一成正斜杠便于比较
    if (out.startsWith(QStringLiteral("\\\\?\\UNC\\")))
        out = QStringLiteral("\\\\") + out.mid(8);
    else if (out.startsWith(QStringLiteral("\\\\?\\")))
        out = out.mid(4);
    return QDir::fromNativeSeparators(out);
}

#else

#include <QtGlobal>

QString resolveFinalPath(const QString &path)
{
    if (path.trimmed().isEmpty())
        return QString();
    const QString canon = QFileInfo(path).canonicalFilePath();
    return QDir::fromNativeSeparators(canon);
}

#endif

#ifdef Q_OS_WIN
bool makeWorldWritable(const QString &path)
{
    SECURITY_DESCRIPTOR sd;
    if (!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION))
        return false;
    // NULL DACL = Everyone 全权。只用于节点自己创建的短生命周期交换文件。
    if (!SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE))
        return false;
    const QString native = QDir::toNativeSeparators(path);
    const BOOL aclOk = SetFileSecurityW(reinterpret_cast<LPCWSTR>(native.utf16()),
                                        DACL_SECURITY_INFORMATION, &sd);

    // 仅放开 DACL 还不够：受限脚本是**低完整性**进程，Windows 的强制完整性控制（MIC）
    // 会拒绝它写 Medium 完整性的文件（实测 PermissionError[13]）。把交换文件的完整性
    // 标签降到 Low（SDDL ML;;NW;;;LW），Low IL 才能写。只对节点自建的短生命周期交换文件用。
    PSECURITY_DESCRIPTOR lowSd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"S:(ML;;NW;;;LW)",
                                                             SDDL_REVISION_1, &lowSd, nullptr))
        return false;
    const BOOL ilOk = SetFileSecurityW(reinterpret_cast<LPCWSTR>(native.utf16()),
                                       LABEL_SECURITY_INFORMATION, lowSd);
    LocalFree(lowSd);
    return aclOk && ilOk;
}
#else
bool makeWorldWritable(const QString &path)
{
    return QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner
                                       | QFile::ReadUser | QFile::WriteUser
                                       | QFile::ReadGroup | QFile::WriteGroup
                                       | QFile::ReadOther | QFile::WriteOther);
}
#endif
