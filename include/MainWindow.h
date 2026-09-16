#pragma once

#include <QMainWindow>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QMap>
#include <QString>
#include <QElapsedTimer>
#include <QVector>
#include <QHash>
#include <QShowEvent>

class QDockWidget;

#include <halconcpp/HalconCpp.h>
#include "HalconWindow.h"
#include "NodeBase.h"

using namespace HalconCpp;

namespace Ui {
class MainWindow;
}

class FlowScene;
class FlowTabManager;
class ImageDisplayController;
class NodeExecutionController;
class DockLayoutManager;
class ProjectManager;
class HalconWindow;
class RuntimeInterfaceView;
class PerformancePanel;
class ResultTablePanel;
class VariablePanel;
class OutputDataViewer;
class NodeSearchWidget;
class HelpViewer;
class BrandHeader;
#include "FlowExecutor.h"

// 语言枚举
enum class Language {
    Chinese,
    English
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    /// 方案含 DeepOCR / Halcon图像源时提示需要 HALCON 运行时
    void checkHalconNodesHint();
    /// 帮助菜单：检测 HALCON 图像层（读图/显示）
    void runHalconEnvCheck();

private slots:
    /// 打开「结果表」面板（视图菜单）
    void onOpenResultTable();
    void onNewProject();
    void onOpenProject();
    void onSaveProject();
    void onLoadProject();
    void onAddFlowTab();
    void onCloseFlowTab(int index);
    void onNodeSelected(NodeBase *node);
    void onSwitchLanguage();  // 切换语言
    void onStartExecution();   // 开始执行（仅软触发模式有效）
    void onStopExecution();    // 停止执行
    void onSingleShotExecution(); // 单次执行（不改变流程模式）
    void onExecutionStarted();
    void onExecutionStopped();
    void onExecutionFinished();
    void onExecutionError(const QString &error);
    void onNodeExecuted(NodeBase *node, bool success);
    void onCurrentTabChanged(int index);
    void onNodeAdded(NodeBase *node);
    void onImageRead(const HalconCpp::HImage &image);
    void onExit();
    void onManageGlobalCameras();  // 管理全局相机
    void onOpenNodeSearch();       // 打开算子搜索对话框 (Ctrl+F)
    void onOpenPerformancePanel(); // 打开性能分析面板
    void onOpenOutputViewer();     // 打开输出数据查看器
    void onToggleImageFloat();     // 弹出/还原图像显示窗口
    void applyDefaultDockSizes();  // 右侧图像区占主要高度，可拖拽分隔条调整


private:
    void initActions();
    void createNewFlow();
    void addNode(const QString &nodeType);
    /// 多流程并发：获取场景专属执行器（不存在则创建并连接信号）
    FlowExecutor *executorForScene(FlowScene *scene);
    /// 连接执行器信号到主窗口（状态栏/运行界面/参数面板）
    void connectExecutorSignals(FlowExecutor *ex);
    void setupToolLibrary();
    void setupFlowEditor();
    void setupRightPanel();
    void setupDockWidgets();
    void switchToDesignMode();
    void switchToRuntimeMode();
    void logMessage(const QString &message);
    void setupLanguageMenu();  // 设置语言菜单
    void setupSchemeMenu();    // 设置方案菜单
    void setupCommunicationMenu(); // 设置通讯管理菜单
    void setupSystemMenu();       // 设置系统菜单
    void updateLanguage();     // 更新界面语言
    void retranslateUi();      // 重新翻译UI
    void updateExecutionButtons(ExecutionState state);
    void showNodeParameters(NodeBase *node);
    void openModuleEditor(NodeBase *node);
    void executeNodeOnce(NodeBase *node);
    /// 参数改动后只重算该算子及其下游：先作废缓存，再执行这一段链路
    /// quiet=true 表示自动重算（不写日志、流程忙时静默跳过）
    void recomputeDownstream(NodeBase *node, bool quiet = false);
    /// 构造可引用变量清单（{模块号.参数名} / {global.名称}），供模块编辑窗「变量引用」菜单
    QStringList buildVariableReferences() const;
    QWidget *createParameterWidget(const QString &name, const QVariant &value);
    void updateNodeParameters(NodeBase *node);
    void setupFlowSceneDragDrop(FlowScene *scene);
    void hookFlowScene(FlowScene *scene);
    bool eventFilter(QObject *obj, QEvent *event) override;
    QList<NodeBase*> getUpstreamNodes(NodeBase *node, FlowScene *scene); // 获取上游节点（数据流正向）
    /// 决定当前应该显示哪个算子的图像：下拉框选择 > 画布选中 > 兜底
    NodeBase *resolveDisplayNode(NodeBase *fallbackNode = nullptr) const;
    /// 根据节点类型生成默认图标
    QIcon generateNodeIcon(NodeBase::NodeType category) const;
    /// 刷新所有 MVS 图像源的像素格式控件可用状态
    void refreshAllMvsPixelFormats();
    /// 根据当前流程的运行模式更新画布编辑锁定状态
    void updateEditLockForCurrentScene();
    /// 关闭软件时释放所有相机资源
    void cleanupCameras();
    /// 根据当前用户角色启用/禁用受权限控制的菜单项
    void applyPermissionRestrictions();
    /// 打开运行界面设计器
    void openRuntimeInterfaceDesigner();
    /// 收集全部流程中的所有算子完整名（供运行界面绑定节点输出）
    QStringList allNodeFullNames() const;
    /// 载入运行界面布局并刷新运行视图
    void loadRuntimeInterfaceLayout();
    /// 收集节点输出的测量结果 → 图像叠加图元（供 HalconWindow 绘制）
    QVector<OverlayShape> collectOverlayFromNode(NodeBase *node) const;
    /// 进入画布 ROI 编辑模式（测量节点取点）
    void startRoiPick(NodeBase *node);
    /// ROI 绘制完成：写回节点参数
    void handleRoiEdited(const RoiShape &shape);
    /// 记录最近打开/保存的方案路径并刷新菜单
    void addRecentFile(const QString &filePath);
    /// 重建"最近打开"菜单
    void updateRecentMenu();
    /// 加载指定方案文件（打开/最近文件共用）
    void loadProjectFile(const QString &fileName);
    /// 打开使用手册查看对话框（F1 / 帮助菜单）
    void openManualDialog();

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

    Ui::MainWindow *ui;
    // Controller delegates (Phase 1 refactoring)
    FlowTabManager *m_flowTabManager;
    ImageDisplayController *m_imageDisplayCtrl;
    NodeExecutionController *m_executionCtrl;
    DockLayoutManager *m_dockLayoutMgr;

    ProjectManager *m_projectManager;
    QList<FlowScene *> m_flowScenes;
    HalconWindow *m_imageView;
    FlowExecutor *m_executor;
    /// 多流程并发：每个流程场景独立执行器（m_executor 指向当前激活流程的执行器）
    QHash<FlowScene *, FlowExecutor *> m_flowExecutors;
    QComboBox *m_flowModeCombo;   // 流程模式下拉框
    QToolButton *m_singleShotBtn;
    QToolButton *m_stepBtn;   /// 单步执行按钮 // 单次执行按钮
    NodeBase *m_selectedNode;
    /// 画布 ROI 取点中的目标节点（nullptr = 未在取点）
    NodeBase *m_roiPickNode = nullptr;
    /// 测量节点 ROI 按钮信号连接（防重复连接）
    QMetaObject::Connection m_roiPickConn;
    /// 最近打开方案菜单
    QMenu *m_recentMenu = nullptr; // 当前选中的节点
    QLabel *m_imageSourceLabel; // 图像来源标签
    
    // 自定义运行界面（对齐 VisionMaster 4.4 运行界面）
    RuntimeInterfaceView *m_runtimeView = nullptr;
    QDockWidget *m_runtimeViewDock = nullptr;
    bool m_runtimeViewVisible = false; // 运行模式下是否显示自定义运行界面

    // 流程编辑器上方的品牌 Logo 栏（需求 FR4.4）
    BrandHeader *m_brandHeader = nullptr;
    
    // 语言相关
    Language m_currentLanguage = Language::Chinese;
    QMap<QString, QString> m_translationsCN;
    QMap<QString, QString> m_translationsEN;
    
    // 保存用户选择的输出算子
    QMap<FlowScene *, NodeBase *> m_selectedOutputNodes;
    
    // 保存每个流程各自的运行模式
    QMap<FlowScene *, FlowMode> m_flowModes;
    
    // 保存节点的参数状态
    QMap<NodeBase *, QMap<QString, QVariant>> m_nodeParameters;

    // ---- 状态栏运行信息 ----
    QLabel *m_statusStateLabel = nullptr;   // 运行状态
    QLabel *m_statusTimeLabel = nullptr;    // 本次耗时
    QLabel *m_statusTriggerLabel = nullptr; // 触发计数
    QElapsedTimer m_runTimer;
    qint64 m_lastRunMs = 0;
    quint64 m_triggerCount = 0;

    // ---- 新增组件 ----
    PerformancePanel *m_performancePanel = nullptr;      // 性能分析面板
    QDockWidget *m_performanceDock = nullptr;
    OutputDataViewer *m_outputDataViewer = nullptr;      // 输出数据查看器
    QDockWidget *m_outputViewerDock = nullptr;
    ResultTablePanel *m_resultTablePanel = nullptr;      // 结果数据表（整条流程的全部数值结果）
    QDockWidget *m_resultTableDock = nullptr;
    VariablePanel *m_variablePanel = nullptr;            // 变量面板（引用表达式，供表达式联动）
    QDockWidget *m_variableDock = nullptr;
    /// 最近一次执行各模块的输出变量（UI 线程缓存，供「变量引用」菜单构造引用列表）
    QHash<int, QVariantMap> m_lastModuleVars;
    NodeSearchWidget *m_nodeSearchWidget = nullptr;      // 算子搜索控件
    HelpViewer *m_helpViewer = nullptr;                  // 算子帮助文档查看器
    QHash<NodeBase *, class ModuleEditorDialog *> m_moduleEditors;
};
