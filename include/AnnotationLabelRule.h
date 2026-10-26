#pragma once

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
