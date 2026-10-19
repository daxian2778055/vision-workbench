#include "ProjectPaths.h"

#include "HalconNode.h"
#include "MvsImageSourceNode.h"
#include "NodeBase.h"
#include "ParamSpec.h"
#include "FlowScene.h"

#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

namespace ProjectPaths {

namespace {

QString s_currentSchemeFilePath;   // 进程级方案基目录快照位，各访问器内互斥

/// 目录内判定：cleanPath 后大小写不敏感的「同一目录或前缀成子路径」测试。
/// 头长度判断先行，杜绝前缀沾边（C:/plan !== C:/plan_other）与斜杠方向差。
bool sameOrInsideDir(const QString &candidateAbs, const QString &dirAbs)
{
    const QString c = QDir::cleanPath(candidateAbs);
    const QString d = QDir::cleanPath(dirAbs);
    if (d.isEmpty())
        return false;
    if (c.compare(d, Qt::CaseInsensitive) == 0)
        return true;
    QString prefix = d;
    if (!prefix.endsWith(QLatin1Char('/')))
        prefix += QLatin1Char('/');
    if (c.size() <= prefix.size())
        return false;
    return c.left(prefix.size()).compare(prefix, Qt::CaseInsensitive) == 0;
}

/// 遗传三节点（ImageReadNode / WriteFileNode / ToolboxOperatorNode）直接往参数表写
/// filePath，不经 makeFilePathParam 规格——审查面在此点名，规格 Array 覆盖形之外补一查。
const QLatin1String kLegacyFilePathKey("filePath");

}   // namespace

const char *pathScopeName(PathScope scope)
{
    switch (scope) {
    case PathScope::Empty: return "Empty";
    case PathScope::RelativeRaw: return "RelativeRaw";
    case PathScope::InSchemeDir: return "InSchemeDir";
    case PathScope::OutsideSchemeDir: return "OutsideSchemeDir";
    }
    return "Unknown";
}

QString schemeBaseDir(const QString &schemeFilePath)
{
    if (schemeFilePath.trimmed().isEmpty())
        return QDir::currentPath();
    // 方案文件自身路径为相对形态时也锚定 cwd（与解析同锚，不发明第二套语义）
    const QString dir = QFileInfo(schemeFilePath).absolutePath();
    return QFileInfo(dir).absoluteFilePath();
}

PathScope classifySchemePath(const QString &raw, const QString &schemeFilePath)
{
    const QString v = raw.trimmed();
    if (v.isEmpty())
        return PathScope::Empty;
    if (QFileInfo(v).isRelative())
        return PathScope::RelativeRaw;
    if (sameOrInsideDir(v, schemeBaseDir(schemeFilePath)))
        return PathScope::InSchemeDir;
    return PathScope::OutsideSchemeDir;
}

QString resolveSchemePath(const QString &raw, const QString &schemeFilePath)
{
    const QString v = raw.trimmed();
    if (v.isEmpty())
        return QString();
    const QFileInfo fi(v);
    if (fi.isRelative())
        return QFileInfo(QDir(schemeBaseDir(schemeFilePath)), v).absoluteFilePath();
    return QDir::cleanPath(fi.absoluteFilePath());
}

void setCurrentSchemeFilePath(const QString &path)
{
    static QMutex s_mutex;
    QMutexLocker locker(&s_mutex);
    s_currentSchemeFilePath = path;
}

QString currentSchemeFilePath()
{
    static QMutex s_mutex;
    QMutexLocker locker(&s_mutex);
    return s_currentSchemeFilePath;
}

QString resolveExecutionPath(const QString &raw)
{
    return resolveSchemePath(raw, currentSchemeFilePath());
}

QString makeSchemeRelativeIfInside(const QString &schemeFilePath, const QString &pickedAbsolute)
{
    if (schemeFilePath.trimmed().isEmpty())
        return pickedAbsolute;
    if (pickedAbsolute.trimmed().isEmpty())
        return pickedAbsolute;
    const QFileInfo fi(pickedAbsolute);
    if (!fi.isAbsolute())
        return pickedAbsolute;
    if (classifySchemePath(pickedAbsolute, schemeFilePath) != PathScope::InSchemeDir)
        return pickedAbsolute;
    const QString rel = QFileInfo(schemeFilePath).dir().relativeFilePath(pickedAbsolute);
    return rel.isEmpty() ? pickedAbsolute : rel;
}

static void tallyHit(PathReview &review, const QString &nodeName, const QString &paramName,
                     const QString &raw, PathScope scope)
{
    PathReviewHit hit;
    hit.nodeName = nodeName;
    hit.paramName = paramName;
    hit.rawPath = raw;
    hit.scope = scope;
    if (scope == PathScope::OutsideSchemeDir) {
        ++review.outsideCount;
        review.outsideHits.append(hit);
    } else if (scope == PathScope::RelativeRaw) {
        ++review.relativeCount;
        review.relativeHits.append(hit);
    }
}

PathReview reviewScenes(const QList<FlowScene *> &scenes, const QString &schemeFilePath)
{
    PathReview review;
    for (FlowScene *scene : scenes) {
        if (!scene)
            continue;
        const QList<NodeBase *> nodes = scene->nodes();
        for (NodeBase *nb : nodes) {
            if (!nb)
                continue;
            auto *hn = qobject_cast<HalconNode *>(nb);
            if (!hn)
                continue;
            QStringList covered;
            const ParamSpecList specs = hn->paramSpecs();
            for (const ParamSpec &spec : specs) {
                if (spec.type != ParamType::FilePath)
                    continue;
                covered.append(spec.name);
                const QString raw = hn->getParam(spec.name).toString().trimmed();
                if (raw.isEmpty())
                    continue;
                tallyHit(review, hn->name(), spec.name, raw,
                         classifySchemePath(raw, schemeFilePath));
            }
            /// MVS 图像源的目录快照位是成员不是参数表条目（序列化键同名 filePath），单独点名。
            if (auto *mvs = qobject_cast<MvsImageSourceNode *>(nb)) {
                const QString raw = mvs->getFilePath().trimmed();
                if (!raw.isEmpty())
                    tallyHit(review, hn->name(), kLegacyFilePathKey, raw,
                             classifySchemePath(raw, schemeFilePath));
            }
            /// 遗传三键：无规格覆盖时补查 filePath。
            if (!covered.contains(QString(kLegacyFilePathKey))) {
                const QString raw = hn->getParam(kLegacyFilePathKey).toString().trimmed();
                if (!raw.isEmpty())
                    tallyHit(review, hn->name(), kLegacyFilePathKey, raw,
                             classifySchemePath(raw, schemeFilePath));
            }
        }
    }
    return review;
}

QString pathReviewSummary(const PathReview &review)
{
    if (review.outsideHits.isEmpty() && review.relativeCount == 0)
        return QString();
    QStringList parts;
    if (!review.outsideHits.isEmpty()) {
        parts << QStringLiteral("方案目录外绝对路径 %1 处").arg(review.outsideCount);
    }
    if (review.relativeCount > 0) {
        parts << QStringLiteral("相对路径 %1 处（执行时按方案目录解析）").arg(review.relativeCount);
    }
    return parts.join(QStringLiteral("；"));
}

}   // namespace ProjectPaths
