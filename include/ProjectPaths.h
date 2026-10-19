#pragma once

#include <QList>
#include <QString>
#include <QVector>

class FlowScene;

/// 方案内文件路径面（179 行，客户报告 P1-1② 拍板落地）：
///  · 拍板原样保持：不给现场方案做硬限位（存量方案携带绝对路径，硬白名单断既有现场）；
///  · 新方案默认方案内相对：拾取入口把方案目录内的绝对拾取换成相对形态（makeSchemeRelativeIfInside）；
///  · 相对路径执行锚定方案目录：不再锚定进程 cwd（旧行为本就无人专用——对话框拾取永远落绝对路径）；
///  · 目录外绝对路径载入时提示确认（报告面一句 loadNote，非阻断）：清单面走 reviewScenes。
namespace ProjectPaths {

/// 分类结果：空参 / 相对形态（按方案目录解析）/ 方案目录内绝对 / 目录外绝对
enum class PathScope
{
    Empty,
    RelativeRaw,
    InSchemeDir,
    OutsideSchemeDir
};

const char *pathScopeName(PathScope scope);

/// 方案基目录：方案文件所在目录；方案未落盘（空参）时回落进程 cwd（与旧 cwd 行为同锚，不发明新语义）
QString schemeBaseDir(const QString &schemeFilePath);

PathScope classifySchemePath(const QString &raw, const QString &schemeFilePath);

/// 执行时取真路径：空→空；绝对→cleanPath 原样（存量方案行为不变）；相对→方案目录锚定。
QString resolveSchemePath(const QString &raw, const QString &schemeFilePath);

/// 执行期便捷位：取本轮记录的方案基目录（进程级快照位，Guardian 见 cpp）。节点执行线程可调。
QString resolveExecutionPath(const QString &raw);

/// 进程级方案基目录快照位：ProjectManager 三处记录点与界面恢复点喂数，执行线程读。
/// 加互斥而非裸静态：执行线程与界面线程并发读写同一份 QString。
void setCurrentSchemeFilePath(const QString &path);
QString currentSchemeFilePath();

/// 拾取落参（新方案默认方案内相对拍板位）：方案目录内的绝对拾取换成相对形态，
/// 目录外原样保留；方案未落盘时无锚不做转换（保持旧行为）。
QString makeSchemeRelativeIfInside(const QString &schemeFilePath, const QString &pickedAbsolute);

struct PathReviewHit
{
    QString nodeName;
    QString paramName;
    QString rawPath;
    PathScope scope = PathScope::Empty;
};

/// 载入期路径审查结果：目录外绝对路径逐条在案（提示面载荷），相对路径只给计数。
struct PathReview
{
    int outsideCount = 0;
    int relativeCount = 0;
    QVector<PathReviewHit> outsideHits;
    QVector<PathReviewHit> relativeHits;
};

/// 扫描场景列表中全部节点的文件路径参数（规格型 + 遗传三键 + MVS 快照位）。
PathReview reviewScenes(const QList<FlowScene *> &scenes, const QString &schemeFilePath);

/// 一句话总纲：非空时给 ProjectManager::reportLoadNote。
QString pathReviewSummary(const PathReview &review);

}   // namespace ProjectPaths
