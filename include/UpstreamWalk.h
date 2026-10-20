#pragma once

#include <QList>

class NodeBase;

/// 图上游闭包游走面（词条切片 B 图谱族首抽，从 MainWindow::getUpstreamNodes 抽出）：
/// 沿输入端口连线递归上溯（port 连线 → 源港 → 源节点 → 更上游），返回上游序列。
///
/// 契约钉（与原体逐行同语义）：
///  · 游走只读 port 与连线数据（port->connections()、conn->sourcePort()、port->node()），
///    不触场景与图锁；原成员携带的 scene 参数体中从未读取（dead 位原状在案——面内 API
///    不载殡参数，调用点已同步瘦身）；
///  · 去重口径 = **现层 contains**（同层同一源只挂一次；跨层同名可复现——菱形图 A→B→D、
///    A→C→D 得 [B, A, C, A]，为原体既成行为，如实钉不修正）；
///  · 空入参返回空序列；无连线节点返回空序列。
class UpstreamWalk final {
public:
    /// 沿输入端口递归上溯，返回上游序列（顺序即连线遍历序；行为面原状）。
    static QList<NodeBase *> upstreamOf(NodeBase *node);
};
