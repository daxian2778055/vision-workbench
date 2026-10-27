#include "PathSafety.h"

#include <QDir>
#include <QFileInfo>

#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>

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
