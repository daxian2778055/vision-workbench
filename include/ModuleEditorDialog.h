#pragma once

#include <QDialog>
#include <QVector>
#include <QMap>
#include <QVariant>
#include <functional>
#include "HalconWindow.h"

class NodeBase;
class HalconNode;
class QLabel;
class QScrollArea;
class QCheckBox;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTimer;
class QToolButton;
class QMenu;
class QSplitter;
class QTabWidget;
class QComboBox;
class QSpinBox;
class QLineEdit;
class QVBoxLayout;

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
    /// P1-1：请求打开本算子的帮助页（MainWindow 统一收口到 HelpViewer）
    void helpRequested(NodeBase *node);

private slots:
    void onDrawGeometry();
    void onDrawSearchRegion();
    void onDrawSearchPolygon();
    /// B2：编辑对象切换 —— 句柄只出现在当前对象上（模板框 / 搜索区域）
    void onEditTargetGeometry();
    void onEditTargetSearch();
    void onClearGeometry();
    void onEditMask();
    void onClearMask();
    void onRoiEdited(const RoiShape &shape);
    void onMaskEdited();
    void onSaveTemplate();
    // ---- VM「特征模板」页对齐：模板列表（创建/编辑/删除）----
    void onNewTemplate();
    void onEditTemplate();
    void onDeleteTemplate();
    void onTemplateSelected(int row);
    void onEvaluateTemplate();
    void onCheckBudget();
    void onWizardGuide();
    void onParamWidgetEdited();
    void onAutoRecomputeTimeout();

private:
    void loadImage();
    void rebuildParamPanel();
    void refreshTemplatePreview();
    /// 把节点的模板列表灌进左侧列表（当前模板高亮）
    void refreshTemplateList();
    /// VM 结果表口径：每个命中一行（中心/匹配点/角度/尺度/分数）
    void refreshResultTable();
    /// VM 4.4 工具窗四页签：把参数分组框/模板列表/结果表归位（所有算子通用；模板页仅模板算子）
    void rebuildVmTabs();
    /// P1-10：打开时快照全部参数；取消时回滚（确定则保留）
    void snapshotParams();
    void restoreParams();
    /// P1-9：结果显示页的「结果判断 / 图像显示 / 文本显示」配置区
    void buildResultDisplayPage(QVBoxLayout *resLay);
    /// P2-8：运行参数页的「参数订阅」表（逐参数自定义/订阅切换）
    void buildParamSubscriptionGroup(QVBoxLayout *runLay);
    /// P2-8：左下角「恢复默认值」
    void restoreParamDefaults();
    /// 结果判断条件表 → 节点参数（JSON）
    void persistJudgeConditions();
    void loadJudgeConditions();
    void echoGeometry();
    /// 搜索区域（r19 批 A 第 4 步）：把 searchRegion* 画成青色框，并与结果叠加合并
    void refreshSearchOverlay();
    OverlayShape searchRegionShape() const;
    /// 把 RoiShape 转成只读叠加图元（编辑搜索区域时仍能看到模板框）
    QVector<OverlayShape> geometryOverlayShapes() const;
    /// 按当前编辑对象重设画布上的可编辑 ROI（模板框 or 搜索区域）
    void applyEditTarget();
    /// 搜索区域矩形写回 4 个参数
    void writeSearchRect(const RoiShape &shape);
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
    /// 关闭（含 完成/右上角 X）时兜底持久化
    void done(int r) override;
    /// 调整大小/移动窗口时即时持久化（去抖）——编辑窗非模态，用户常"调完直接双击下一个"
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;

private slots:
    void saveGeometryNow();

private:
    NodeBase *m_node = nullptr;
    HalconWindow *m_view = nullptr;
    QScrollArea *m_paramHost = nullptr;
    QSplitter *m_splitter = nullptr;
    QTimer *m_geoSaveTimer = nullptr;
    QLabel *m_hintLabel = nullptr;
    QLabel *m_templatePreview = nullptr;
    QListWidget *m_templateList = nullptr;
    QTableWidget *m_resultTable = nullptr;
    QPushButton *m_tmplNewBtn = nullptr;
    QPushButton *m_tmplEditBtn = nullptr;
    QPushButton *m_tmplDelBtn = nullptr;
    QCheckBox *m_autoRecomputeCheck = nullptr;   /// 「自动重算」开关
    QTimer *m_autoRecomputeTimer = nullptr;      /// 去抖定时器
    bool m_paramHookBusy = false;
    bool m_pickingSearchRegion = false;
    bool m_pickingSearchPolygon = false;
    /// B2 当前编辑对象：0=模板框（算子几何），1=搜索区域
    int m_editTarget = 0;
    QToolButton *m_targetGeomBtn = nullptr;
    QToolButton *m_targetSearchBtn = nullptr;
    int m_wizardStep = 0;                       /// 训练向导步进（0=未启动）        /// 正在画多边形搜索区域          /// 正在画搜索区域（否则画模板框）
    QVector<OverlayShape> m_resultOverlay;      /// 测量结果叠加（与搜索框合并后再下发）                /// 程序化回填参数期间抑制挂钩回调
    QToolButton *m_varRefButton = nullptr;       /// 「变量引用」按钮
    QMenu *m_varRefMenu = nullptr;               /// 变量引用下拉菜单
    QWidget *m_lastEditor = nullptr;             /// 最近获得焦点的参数编辑控件（插入目标）
    std::function<QStringList()> m_varRefProvider;

    QToolButton *m_helpBtn = nullptr;             /// P1-1 左上角帮助
    QPushButton *m_okBtn = nullptr;               /// P1-1/P1-10 确定（提交）
    QPushButton *m_cancelBtn = nullptr;           /// P1-1/P1-10 取消（回滚）
    QPushButton *m_restoreDefaultsBtn = nullptr;  /// P2-8 恢复默认值
    QTableWidget *m_paramSubscriptionTable = nullptr; /// P2-8 逐参数订阅表
    QTabWidget *m_vmTabs = nullptr;
    QMap<QString, QVariant> m_paramSnapshot;      /// 打开时的参数快照（取消回滚）
    bool m_paramsDirty = false;                   /// 本次编辑是否改过参数

    // P1-9 结果显示页配置控件（值写入节点参数，随方案保存）
    QComboBox *m_judgeModeCombo = nullptr;
    QTableWidget *m_judgeTable = nullptr;
    QCheckBox *m_showResultImageCheck = nullptr;
    QPushButton *m_resultImageColorBtn = nullptr;
    QSpinBox *m_resultImageOpacitySpin = nullptr;
    QSpinBox *m_resultImageLineWidthSpin = nullptr;
    QCheckBox *m_showResultTextCheck = nullptr;
    QLineEdit *m_resultTextEdit = nullptr;
    QPushButton *m_resultTextColorBtn = nullptr;
    QSpinBox *m_resultTextSizeSpin = nullptr;
    QSpinBox *m_resultTextOpacitySpin = nullptr;
    QComboBox *m_resultTextPosCombo = nullptr;
    QString m_resultImageColor = QStringLiteral("#00C8FF");
    QString m_resultTextColor = QStringLiteral("#FFFFFF");
};
