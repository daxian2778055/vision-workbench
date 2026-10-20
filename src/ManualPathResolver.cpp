#include "ManualPathResolver.h"

#include <QFile>
#include <QIODevice>

// 手册路径解析器（词条切片 B 手册解析族首抽）：
//  · 首读者胜出 = 沿链依序首个「存在且可打开」者即回（原体 break 同形）；
//  · 全者皆空返回空串——「打到没手册」的弹窗留调用方（行为零变更钉）。

QStringList ManualPathResolver::candidatePaths(const QString &exeDir)
{
    return { exeDir + QStringLiteral("/docs/用户操作手册.md"),
             QStringLiteral("docs/用户操作手册.md"),
             exeDir + QStringLiteral("/用户操作手册.md"),
             QStringLiteral(":/用户操作手册.md") };
}

QString ManualPathResolver::resolveMarkdown(const QStringList &paths)
{
    for (const QString &path : paths) {
        QFile file(path);
        if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QString::fromUtf8(file.readAll());
        }
    }
    return QString();
}
