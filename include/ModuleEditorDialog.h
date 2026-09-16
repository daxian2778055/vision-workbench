#pragma once

#include <QDialog>
#include <QVector>
#include <functional>
#include "HalconWindow.h"

class NodeBase;
class HalconNode;
class QLabel;
class QScrollArea;
class QCheckBox;
class QTimer;
class QToolButton;
class QMenu;

/// 双击算子打开的模块编辑窗：大图 + 完整参数 + ROI/掩膜/模板
class ModuleEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ModuleEditorDialog(NodeBase *node, QWidget *parent = nullptr);

    NodeBase *node() const { return m_node; }
    void reloadFromNode();
    /// 执行后在编辑窗叠加测量结果（旋转框等）
    void setResultOverlay(const QVector<OverlayShape> &shapes);

signals:
    void executeRequested();
    /// 参数改完后：只重算本算子及其下游（避免整条流程重跑，也避免下游读到旧值）
    void recomputeRequested();
    /// 自动重算请求（改参数/拖 ROI/涂掩膜后经去抖发出）；流程运行中应静默跳过
    void autoRecomputeRequested();

private slots:
    void onDrawGeometry();
    void onClearGeometry();
    void onEditMask();
    void onClearMask();
    void onRoiEdited(const RoiShape &shape);
    void onMaskEdited();
    void onSaveTemplate();
    void onParamWidgetEdited();
    void onAutoRecomputeTimeout();

private:
    void loadImage();
    void rebuildParamPanel();
    void refreshTemplatePreview();
    void echoGeometry();
    /// 给参数面板上的可编辑控件挂钩变更信号（面板由节点自建，控件种类不定）
    void installParamHooks(QWidget *panel);
    void scheduleAutoRecompute();
    HalconNode *halconNode() const;

public:
    /// 面板内容变化（含节点重建控件）后重新挂钩；由 rebuildParamPanel()/reloadFromNode() 调用
    void refreshParamHooks();
    /// 变量引用菜单的数据源（每次展开菜单时调用，保证列表是当前值）
    void setVariableReferenceProvider(std::function<QStringList()> provider);
    /// 重建变量引用菜单（菜单展开前/流程变化后调用）
    void rebuildReferenceMenu();
    /// 把引用插入「最近获得焦点的参数编辑框」；没有可用编辑框时返回 false
    bool insertReference(const QString &ref);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    NodeBase *m_node = nullptr;
    HalconWindow *m_view = nullptr;
    QScrollArea *m_paramHost = nullptr;
    QLabel *m_hintLabel = nullptr;
    QLabel *m_templatePreview = nullptr;
    QCheckBox *m_autoRecomputeCheck = nullptr;   /// 「自动重算」开关
    QTimer *m_autoRecomputeTimer = nullptr;      /// 去抖定时器
    bool m_paramHookBusy = false;                /// 程序化回填参数期间抑制挂钩回调
    QToolButton *m_varRefButton = nullptr;       /// 「变量引用」按钮
    QMenu *m_varRefMenu = nullptr;               /// 变量引用下拉菜单
    QWidget *m_lastEditor = nullptr;             /// 最近获得焦点的参数编辑控件（插入目标）
    std::function<QStringList()> m_varRefProvider;
};
