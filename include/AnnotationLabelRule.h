#pragma once

#include "PathSafety.h"

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

/// canonical 包含谓词（2026-10-26）：dirCanon 必须等于 rootCanon，或位于其**子路径**下。
/// 抽成纯谓词的原因：这是 symlink / junction 场景的最后一道闸，而名字白名单会先把明显的
/// 逃逸名挡掉，端到端用例无法单独触发它（实测：只撤掉包含检查，五条腿仍全绿）。有了谓词
/// 就能直接钉住边界（`/a/bc` 不算 `/a/b` 的子路径）。
inline bool annotationPathIsInside(const QString &rootCanon, const QString &dirCanon)
{
    if (rootCanon.isEmpty() || dirCanon.isEmpty())
        return false;
    return dirCanon == rootCanon
           || dirCanon.startsWith(rootCanon + QLatin1Char('/'));
}

/// 在 root 之下解析并创建类别目录（2026-10-26）：名字校验 → mkpath → canonical 包含检查。
/// 抽成纯函数的原因：对话框里这段依赖 QWidget / ROI / 图像，难以单测，而它正是"样本写到
/// 标注目录之外"的最后一道闸——必须能在临时目录上真跑一遍（含 `..` 逃逸尝试）。
/// 返回 false 时 why 给出可直接展示的原因，且 **outDir 一定被清空**（不把上次的值留给调用方）；
/// 成功时 outDir 为已创建的类别目录。
inline bool annotationClassDir(const QString &root, const QString &rawLabel,
                               QString *outDir, QString *why)
{
    // 两个出参同口径：失败路径不得留下上一次的值（2026-10-26 复核——注释原先只对 outDir 成立）
    if (outDir)
        outDir->clear();
    if (why)
        why->clear();
    if (root.trimmed().isEmpty()) {
        // 空 root 会让 dir 变成 "/<label>"，而 mkpath 在包含检查之前就跑——必须提前拒
        //（当前唯一调用方总会补成 cwd/annotations，但本函数定位是可复用的最后一道闸）。
        if (why)
            *why = QStringLiteral("标注目录未设置，无法保存样本");
        return false;
    }
    if (!annotationClassLabelIsSafe(rawLabel, why))
        return false;
    const QString label = rawLabel.trimmed();
    const QString dir = root + QLatin1Char('/') + label;
    if (!QDir().mkpath(dir)) {
        if (why)
            *why = QStringLiteral("类别目录创建失败（权限/磁盘/路径长度？）：%1").arg(dir);
        return false;
    }
    // canonical 包含检查（symlink / junction / 大小写差异都在这步兜住）
    // 用 resolveFinalPath 而非 canonicalFilePath：后者在 Windows 上**不解析 junction**，
    // 会让 "root/link → 目录外" 的联接直接通过包含检查（2026-10-26 实测的绕过）。
    const QString rootCanon = resolveFinalPath(root);
    const QString dirCanon = resolveFinalPath(dir);
    // 解析失败与"越出"必须分开报（2026-10-27 复核）：resolveFinalPath 返回空的原因包括
    // 路径超长、网络盘断开、卷名异常等，一律说成"越出标注目录"会把现场引向错方向。
    if (rootCanon.isEmpty() || dirCanon.isEmpty()) {
        if (why)
            *why = QStringLiteral("无法解析最终路径（路径过长/网络盘断开？）：%1").arg(dir);
        return false;
    }
    if (!annotationPathIsInside(rootCanon, dirCanon)) {
        if (why)
            *why = QStringLiteral("类别目录越出标注目录，已拒绝写入：%1").arg(dir);
        return false;
    }
    if (outDir)
        *outDir = dir;
    return true;
}
