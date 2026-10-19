#pragma once

#include <QMutex>
#include <QList>
#include <QPair>

class NodeBase;

/// FlowExecutor 的「缓存清理登记账」（180 行词条切片 A 从 FlowExecutor 抽出）：
/// 删节点路径在界面线程同步发槽，本账把"待清理的执行缓存条目"先记下来；
/// 真正的三族缓存移除由执行线程在安全点（轮首 / 同步执行入口）已持图锁后向本账摘账。
///
/// 锁的纪律（随账随身三条，全钉在此）：
///  · 账锁是**叶子锁**：只在「记一笔 / 摘一段」两处短窗持有，持锁期间不得再取
///    任何别的锁——叶子无后沿，成环的路径在结构上被切断，不靠时序运气；
///  · 方向只许「图锁 → 账锁」：摘账（takePending）的调用方必须已持执行器图锁，
///    账内只短取即放叶子锁；记账（enqueuePurge）在界面侧完全不进图锁——
///    两侧没有交叉持有的环位；
///  · 账里的节点指针**键用即止**：入账后节点可能已被立刻析构，键比较不碰内存、
///    绝不解除引用；模块号由调用方在"此刻"读好传出，清理按模块号做（防
///    "删 A 后新建 B 复用同指针地址/同模块号"的残留继承，S4）。
///
/// 账的形状：
///  · 记账只追加不去重——同一指针重复登记无害，摘账侧的清理是幂等的；
///  · 摘账一次取走整列（取后即空），保持 FIFO 登记次序；
///  · 本类是纯账簿：不感知执行器缓存的存放形态，不给任何缓存加锁或解锁，
///    账外的一切（三族缓存在哪、怎么清）仍归 FlowExecutor。
///
/// （摘储任务书见 docs/巨型类拆分-立项词条.md §3 切片 A；行为面凭据五腿在
///  tests/flow_cache_purge_ledger_test.cpp，红臂破坏位 = 摘账改窥账。）
class FlowCachePurgeLedger final {
public:
    /// 界面侧记账：**不取图锁**，只在叶子锁短窗追加一账（指针仅作键、模块号已读好）。
    void enqueuePurge(NodeBase *node, int moduleId);

    /// 执行侧摘账：调用方必须已持执行器图锁；本函数内部只短取/释放叶子锁。
    /// 取走全部待清理账并保持登记次序；空账返回空列表。
    QList<QPair<NodeBase *, int>> takePending();

private:
    /// 叶子锁（终端位）：持有时禁止再取任何别的锁
    QMutex m_purgeMutex;
    /// 登记账：QPair<节点指针（仅作键）, 登记时的模块号>
    QList<QPair<NodeBase *, int>> m_pendingCachePurges;
};
