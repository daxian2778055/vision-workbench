#pragma once

#include <QDir>
#include <QFileInfo>
#include <QString>

/// 标注类别名的白名单规则（2026-10-26）：类别名会被直接拼进目录路径，`..\..\x` 这类名字能把
/// 样本写到标注目录之外（该对话框经训练节点打开，是活路径）。抽取成纯函数以便单测覆盖。
/// 允许：字母 / 数字 / 下划线 / 连字符 / 中文；拒绝：空串、`.`、`..`、任何路径分隔符与其它字符。
inline bool annotationClassLabelIsSafe(const QString &raw, QString *why)
{
    const QString s = raw.trimmed();
    if (s.isEmpty()) {
        if (why) *why = QStringLiteral("类别名为空");
        return false;
    }
    if (s == QStringLiteral(".") || s == QStringLiteral("..")) {
        if (why) *why = QStringLiteral("类别名不能是 . 或 ..");
        return false;
    }
    for (const QChar &ch : s) {
        const ushort u = ch.unicode();
        const bool ok = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z')
                        || (u >= '0' && u <= '9') || ch == QLatin1Char('_')
                        || ch == QLatin1Char('-') || (u >= 0x4E00 && u <= 0x9FFF);
        if (!ok) {
            if (why) {
                *why = QStringLiteral("类别名含非法字符「%1」：只允许字母/数字/下划线/连字符/中文")
                           .arg(ch);
            }
            return false;
        }
    }
    return true;
}

/// 在 root 之下解析并创建类别目录（2026-10-26）：名字校验 → mkpath → canonical 包含检查。
/// 抽成纯函数的原因：对话框里这段依赖 QWidget / ROI / 图像，难以单测，而它正是"样本写到
/// 标注目录之外"的最后一道闸——必须能在临时目录上真跑一遍（含 `..` 逃逸尝试）。
/// 返回 false 时 why 给出可直接展示的原因；成功时 outDir 为已创建的类别目录。
inline bool annotationClassDir(const QString &root, const QString &rawLabel,
                               QString *outDir, QString *why)
{
    if (!annotationClassLabelIsSafe(rawLabel, why))
        return false;
    const QString label = rawLabel.trimmed();
    const QString dir = root + QLatin1Char('/') + label;
    if (!QDir().mkpath(dir)) {
        if (why)
            *why = QStringLiteral("类别目录创建失败（权限/磁盘/路径长度？）：%1").arg(dir);
        return false;
    }
    // canonical 包含检查：即便名字过了白名单，也不允许实际落点跑到 root 之外
    //（symlink / 大小写差异 / 平台路径规范化都在这一步兜住）。
    const QString rootCanon = QFileInfo(root).canonicalFilePath();
    const QString dirCanon = QFileInfo(dir).canonicalFilePath();
    if (rootCanon.isEmpty() || dirCanon.isEmpty()
        || !(dirCanon == rootCanon || dirCanon.startsWith(rootCanon + QLatin1Char('/')))) {
        if (why)
            *why = QStringLiteral("类别目录越出标注目录，已拒绝写入：%1").arg(dir);
        return false;
    }
    if (outDir)
        *outDir = dir;
    return true;
}
