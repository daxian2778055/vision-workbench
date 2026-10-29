#include "ProjectPackager.h"

#include "FlowScene.h"
#include "NodeBase.h"
#include "HalconNode.h"
#include "ProjectPaths.h"
#include "PathSafety.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <algorithm>

ProjectPackager::Result ProjectPackager::pack(FlowScene *scene, const QString &schemeFilePath)
{
    Result r;
    QHash<QString, QString> copiedBySource;   // 源绝对路径 -> assets 相对路径（同源只复制一次）
    if (!scene) {
        r.error = QStringLiteral("没有可用流程页");
        return r;
    }
    const QFileInfo schemeInfo(schemeFilePath);
    const QString schemeDir = schemeInfo.absolutePath();
    if (schemeDir.isEmpty() || !QDir(schemeDir).exists()) {
        r.error = QStringLiteral("方案目录不存在：%1").arg(schemeDir);
        return r;
    }
    const QString assetsDir = schemeDir + QStringLiteral("/assets");
    if (!QDir().mkpath(assetsDir)) {
        r.error = QStringLiteral("无法创建 assets 目录：%1").arg(assetsDir);
        return r;
    }

    // 稳定顺序（2026-10-29 P2-2）：QHash 的 items().keys() 顺序不保证，
    // 同一方案重复打包会让 assets 文件名互换（best.onnx / best_2.onnx 对调）——
    // 功能不受影响（参数按节点各自改写），但包字节不可复现。
    QList<NodeBase *> stableOrder;
    for (NodeBase *n : scene->nodes())
        if (n)
            stableOrder.append(n);
    std::sort(stableOrder.begin(), stableOrder.end(), [](NodeBase *a, NodeBase *b) {
        if (a->moduleId() != b->moduleId())
            return a->moduleId() < b->moduleId();
        return a->fullName() < b->fullName();
    });
    for (NodeBase *node : stableOrder) {
        if (!node)
            continue;
        // paramSpecs 在 HalconNode 上（NodeBase 是基类，不假定所有节点都有）
        auto *hn = qobject_cast<HalconNode *>(node);
        if (!hn)
            continue;
        for (const ParamSpec &spec : hn->paramSpecs()) {
            if (spec.type != ParamType::FilePath)
                continue;
            const QString raw = node->getParam(spec.name).toString().trimmed();
            if (raw.isEmpty())
                continue;
            // 量的是"执行时解析后"的真实路径（相对路径可能是相对方案目录）
            const QString abs = ProjectPaths::resolveExecutionPath(raw);
            const QFileInfo fi(abs);
            if (!fi.exists() || !fi.isFile()) {
                r.skipped << QStringLiteral("%1（不是文件：%2）").arg(spec.name, raw);
                continue;
            }
            // 同源去重（2026-10-28 复核 P3）：两个节点引用同一个外部模型时只复制一份，
            // 否则 assets 里会出现 model.onnx 与 model_2.onnx 两份完全相同的内容。
            // 去重键用真实落点（2026-10-29）：字面路径不同但指向同一文件的联接/别名，
            // 否则仍会复制两份（与「是否在方案内」的真实落点口径不一致）。
            const QString srcReal0 = resolveFinalPath(fi.absoluteFilePath());
            const QString srcAbs = srcReal0.isEmpty() ? fi.absoluteFilePath() : srcReal0;
            const auto already = copiedBySource.find(srcAbs);
            if (already != copiedBySource.end()) {
                node->setParam(spec.name, already.value());
                r.skipped << QStringLiteral("%1（同源已打包）").arg(spec.name);
                continue;
            }
            // 判"是否在方案内"用真实落点（2026-10-28 复核 P3）：联接/符号链接按字面路径
            // 看是在方案内，实际目标在外部——换机后包就断。
            const QString schemeAbs = QDir(schemeDir).absolutePath() + QLatin1Char('/');
            const QString srcReal = resolveFinalPath(srcAbs);
            const QString schemeReal = resolveFinalPath(schemeDir);
            const bool insideByPath = fi.absoluteFilePath().startsWith(schemeAbs, Qt::CaseInsensitive);
            const bool insideByReal = !srcReal.isEmpty() && !schemeReal.isEmpty()
                                      && (srcReal == schemeReal
                                          || srcReal.startsWith(schemeReal + QLatin1Char('/')));
            if (insideByPath && insideByReal) {
                const QString rel = fi.absoluteFilePath().mid(schemeAbs.size());
                node->setParam(spec.name, rel);
                r.skipped << QStringLiteral("%1（已在方案内）").arg(spec.name);
                continue;
            }
            // 复制进 assets，处理同名冲突
            QString targetName = fi.fileName();
            QString target = assetsDir + QLatin1Char('/') + targetName;
            int suffix = 2;
            while (QFile::exists(target)) {
                const QString base = fi.completeBaseName();
                const QString ext = fi.suffix().isEmpty() ? QString()
                                                          : QLatin1Char('.') + fi.suffix();
                target = assetsDir + QStringLiteral("/%1_%2%3").arg(base).arg(suffix++).arg(ext);
            }
            if (!QFile::copy(fi.absoluteFilePath(), target)) {
                r.error = QStringLiteral("复制失败：%1 → %2").arg(fi.absoluteFilePath(), target);
                return r;
            }
            const QString rel = QStringLiteral("assets/") + QFileInfo(target).fileName();
            node->setParam(spec.name, rel);
            copiedBySource.insert(srcAbs, rel);
            r.copied << QStringLiteral("%1 → %2").arg(spec.name, rel);
        }
    }
    r.ok = true;
    return r;
}
