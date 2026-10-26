#include "PathSafety.h"

#include <QDir>
#include <QFileInfo>

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
    wchar_t buf[MAX_PATH * 8];
    const DWORD n = GetFinalPathNameByHandleW(h, buf, DWORD(std::size(buf)),
                                              FILE_NAME_NORMALIZED);
    CloseHandle(h);
    if (n == 0 || n >= DWORD(std::size(buf)))
        return QString();
    QString out = QString::fromWCharArray(buf, int(n));
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
