#pragma once

#include <QHash>
#include <functional>

class QDialog;
class NodeBase;

/// 模块编辑器登记簿（180 行词条切片 B 对话框族首抽，从 MainWindow openModuleEditor 抽出）：
/// 「一个算子至多一扇编辑窗」的单一实例簿——命中刷新返原窗、缺失建窗入簿、destroyed 当场摘除。
///
/// 键用即止：NodeBase* 只作键值比较，登记簿绝不解除引用（与 FlowCachePurgeLedger 账簿同口径）。
/// 窗位泛型 QDialog*：window 专有链（reloadFromNode / setResultOverlay / 三信号连接 /
/// 变量引用注入 / show 前置）全留 MainWindow 调用位，并以回调交回命中刷新与工厂两钩子。
/// GUI 与提示框不进本面：写闸拒因与「模块编辑被禁止」提示照旧在 MainWindow，拒序为
/// 先 closeIfPresent 收既有窗再弹提示（窗自带 WA_DeleteOnClose，关窗即销毁即摘位）。
class ModuleEditorRegistry final {
public:
    using EditorFactory = std::function<QDialog *()>;
    using HitRefresh = std::function<void(QDialog *)>;

    /// 命中：调 refresh(既有窗) 后原窗返回；缺失：工厂建窗、置入簿位、接 destroyed 摘除，
    /// 新窗返回（工厂调用序、连接序与窗侧三信号连接的先后与原体一致——摘除连接先于窗侧连接）。
    QDialog *editorFor(NodeBase *node, const EditorFactory &make, const HitRefresh &refresh);

    /// 把既有编辑窗收掉（闸拒先收用）；窗不在簿则无事。close 不改簿位——销毁由窗自身
    /// 生命周期（WA_DeleteOnClose）与摘除连接管，与原体一致。
    void closeIfPresent(NodeBase *node);

    /// 命中查询（只查不建）：供节点变化时刷新已开编辑窗的触点用，查无返回 nullptr。
    QDialog *lookup(NodeBase *node) const;

private:
    QHash<NodeBase *, QDialog *> m_editors;
};
