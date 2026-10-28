#include "ProjectPackager.h"

#include "FlowScene.h"
#include "NodeBase.h"
#include "HalconNode.h"
#include "ProjectPaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

ProjectPackager::Result ProjectPackager::pack(FlowScene *scene, const QString &schemeFilePath)
{
    Result r;
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

    for (NodeBase *node : scene->nodes()) {
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
            // 已在方案目录内：只把参数改成相对路径，不复制
            const QString schemeAbs = QDir(schemeDir).absolutePath() + QLatin1Char('/');
            if (fi.absoluteFilePath().startsWith(schemeAbs, Qt::CaseInsensitive)) {
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
            r.copied << QStringLiteral("%1 → %2").arg(spec.name, rel);
        }
    }
    r.ok = true;
    return r;
}
