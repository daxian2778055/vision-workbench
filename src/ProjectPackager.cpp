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
    return pack(QList<FlowScene *>{scene}, schemeFilePath);
}

ProjectPackager::Result ProjectPackager::pack(const QList<FlowScene *> &scenes,
                                             const QString &schemeFilePath)
{
    Result r;
    QHash<QString, QString> copiedBySource;   // 源绝对路径 -> assets 相对路径（同源只复制一次）
    if (scenes.isEmpty() || !scenes.first()) {
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
    // 两段式改写（2026-10-29 复核 P2-13）：先记下要改的参数，全部复制成功后再统一写回。
    // 原先边复制边 setParam，中途失败会留下「一半节点指 assets、一半指原路径」的半套状态。
    struct PendingRewrite {
        NodeBase *node = nullptr;
        QString param;
        QString rel;
    };
    QVector<PendingRewrite> pending;
    // 本轮已复制出来的目标（2026-10-29 r15 P2-13）：失败时删掉，否则残留文件会把
    // 下一轮成功的拷贝挤成 name_2.ext
    QStringList createdTargets;
    const QString schemeRealForPack = resolveFinalPath(schemeDir);
    // T2：单参数处理收成一份，既走 ParamSpec::FilePath，也补扫无规格节点的 filePath。
    auto processParam = [&](NodeBase *node, const QString &paramName) {
        const QString raw = node->getParam(paramName).toString().trimmed();
        if (raw.isEmpty())
            return;
        // 按入参方案文件锚定，而不是全局执行锚点：用户选的打包目录可能不是已载入方案目录
        const QString abs = ProjectPaths::resolveSchemePath(raw, schemeFilePath);
        const QFileInfo fi(abs);
        if (!fi.exists() || !fi.isFile()) {
            r.skipped << QStringLiteral("%1（不是文件：%2）").arg(paramName, raw);
            return;
        }
        const QString srcReal0 = resolveFinalPath(fi.absoluteFilePath());
        const QString srcAbs = srcReal0.isEmpty() ? fi.absoluteFilePath() : srcReal0;
        const auto already = copiedBySource.find(srcAbs);
        if (already != copiedBySource.end()) {
            pending.append(PendingRewrite{node, paramName, already.value()});
            r.skipped << QStringLiteral("%1（同源已打包）").arg(paramName);
            return;
        }
        // 真实落点不可信时 fail-closed，不能当成"目录外"去复制
        if (srcReal0.isEmpty() || schemeRealForPack.isEmpty()) {
            r.skipped << QStringLiteral("%1（真实路径解析失败，已跳过：%2）").arg(paramName, raw);
            return;
        }
        const QString schemeAbs = QDir(schemeDir).absolutePath() + QLatin1Char('/');
        const bool insideByPath = fi.absoluteFilePath().startsWith(schemeAbs, Qt::CaseInsensitive);
        const bool insideByReal = srcReal0 == schemeRealForPack
            || srcReal0.startsWith(schemeRealForPack + QLatin1Char('/'));
        if (insideByPath && insideByReal) {
            const QString rel = fi.absoluteFilePath().mid(schemeAbs.size());
            pending.append(PendingRewrite{node, paramName, rel});
            r.skipped << QStringLiteral("%1（已在方案内）").arg(paramName);
            return;
        }
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
            QStringList notRemoved;
            for (const QString &gone : createdTargets) {
                if (!QFile::remove(gone))
                    notRemoved << gone;
            }
            if (!notRemoved.isEmpty()) {
                r.error += QStringLiteral("；另有 %1 个已复制的文件未能删除，请手动清理：%2")
                               .arg(notRemoved.size())
                               .arg(notRemoved.join(QStringLiteral(", ")));
            }
            return;
        }
        createdTargets << target;
        const QString rel = QStringLiteral("assets/") + QFileInfo(target).fileName();
        pending.append(PendingRewrite{node, paramName, rel});
        copiedBySource.insert(srcAbs, rel);
        r.copied << QStringLiteral("%1 → %2").arg(paramName, rel);
    };

    QList<NodeBase *> stableOrder;
    // P9：多流程方案要打包所有页的依赖，不能只收当前页（此前换机后其它流程找不到模型）
    for (FlowScene *sc : scenes) {
        if (!sc) continue;
        for (NodeBase *n : sc->nodes())
            if (n)
                stableOrder.append(n);
    }
    std::sort(stableOrder.begin(), stableOrder.end(), [](NodeBase *a, NodeBase *b) {
        if (a->moduleId() != b->moduleId())
            return a->moduleId() < b->moduleId();
        return a->fullName() < b->fullName();
    });
    for (NodeBase *node : stableOrder) {
        if (!node)
            continue;
        auto *hn = qobject_cast<HalconNode *>(node);
        if (!hn)
            continue;
        bool filePathSpecSeen = false;
        for (const ParamSpec &spec : hn->paramSpecs()) {
            if (spec.type != ParamType::FilePath)
                continue;
            if (spec.name == QStringLiteral("filePath")) filePathSpecSeen = true;
            processParam(node, spec.name);
        }
        // MVS/Halcon 图像源本地文件模式无 ParamSpec，但 filePath 是真实依赖
        if (!filePathSpecSeen && node->getParam(QStringLiteral("filePath")).isValid())
            processParam(node, QStringLiteral("filePath"));
    }
    // 全部复制成功后统一改写（P2-13）：失败时不会留下半套路径
    for (const PendingRewrite &w : pending) {
        if (w.node)
            w.node->setParam(w.param, w.rel);
    }
    r.ok = true;
    return r;
}
