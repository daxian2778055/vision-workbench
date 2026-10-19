// FlowCachePurgeLedger（FlowExecutor 抽出的缓存清理登记账，180 行词条切片 A）：
// 两枚行为（记账 / 摘账），纯账簿不触碰执行器缓存；锁的纪律三条款与账的形状
// 钉在同名头文件注释。命名自 180 行占位名 FlowGraphEditGate 落座定名而来——
// 本类不经手编辑闸与图状态，只管登记与摘账，账簿名实对齐。
//
// 仅指向性说明：记账半边（entry 组装 = 指针 + 登记时的模块号）仍在 requestNodeCachePurge；
// 摘账半边的 remove 三族仍在 applyPendingCachePurgesLocked（图锁内地执行）。
#include "FlowCachePurgeLedger.h"

void FlowCachePurgeLedger::enqueuePurge(NodeBase *node, int moduleId)
{
    QMutexLocker lock(&m_purgeMutex);   // 叶子锁短窗：持锁期间不得再取任何别的锁
    m_pendingCachePurges.append(qMakePair(node, moduleId));
}

QList<QPair<NodeBase *, int>> FlowCachePurgeLedger::takePending()
{
    QList<QPair<NodeBase *, int>> taken;
    {
        QMutexLocker lock(&m_purgeMutex);   // 叶子锁短窗
        taken.swap(m_pendingCachePurges);   // 摘账取后即空（红臂破坏位在这条：摘改窥即账不空置）
    }
    return taken;
}
