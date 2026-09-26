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
#include <QJsonObject>
#include <functional>

class QDockWidget;
class QDialog;
class QTimer;
class QAction;

#include <halconcpp/HalconCpp.h>
#include "HalconWindow.h"
#include "NodeBase.h"
#include "RecoveryStore.h"   // 自动保存/崩溃恢复（值成员，需要完整类型）
#include "YieldMonitor.h"    // 良率目标监控（QHash 值类型，需要完整类型）
#include "I18n.h"            // 国际化（G-P1-5）

using namespace HalconCpp;

namespace Ui {
class MainWindow;
}

class FlowScene;
class ProjectManager;
class AuxPanelManager;
class ImageDisplayController;
class ExecutionStatusController;
class RecentFilesMenu;
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

    /// 构造函数内会弹登录框：取消/关闭登录即为 false。
    /// main() 据此决定是显示窗口还是直接退出——默认 false，只有登录成功路径会置位（fail-closed）。
    bool loginAccepted() const { return m_loginAccepted; }

    /// 方案含 DeepOCR / Halcon图像源时提示需要 HALCON 运行时
    void checkHalconNodesHint();
    /// 帮助菜单：检测 HALCON 图像层（读图/显示）
    void runHalconEnvCheck();

private slots:
    /// 打开「结果表」面板（视图菜单）
    void onOpenResultTable();
    void onNewProject();
    void onSaveProject();
    void onLoadProject();
    void onAddFlowTab();
    void onCloseFlowTab(int index);
    void onNodeSelected(NodeBase *node);
    void onSwitchLanguage();  // 切换语言
    void onStartExecution();   // 开始执行（任何模式；暂停中按=继续）
    void onPauseResumeExecution();  // 暂停/继续（连续模式下即"启动后的暂停切换"）
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
    /// 安全回收执行器：stopExecution + 有限等待；超时也**不** terminate（可能卡在 HALCON /
    /// SQLite / 锁上，硬杀比随进程退出回收的线程更危险），改挂 finished→deleteLater 自删。
    void retireExecutor(FlowExecutor *ex);
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
    void showNodeParameters(NodeBase *node);
    void openModuleEditor(NodeBase *node);
    void executeNodeOnce(NodeBase *node);
    /// 参数改动后只重算该算子及其下游：先作废缓存，再执行这一段链路
    /// quiet=true 表示自动重算（不写日志、流程忙时静默跳过）
    void recomputeDownstream(NodeBase *node, bool quiet = false);
    /// 构造可引用变量清单（{模块号.参数名} / {global.名称}），供模块编辑窗「变量引用」菜单
    QStringList buildVariableReferences() const;
    /// 打开/聚焦辅助面板（key: resultTable / variable / performance / outputData）
    void openAuxPanel(const QString &key);
    /// 记住辅助面板的显隐（关闭时调用）
    void saveAuxPanelVisibility() const;
    /// 恢复上次退出时打开的辅助面板（懒创建，仅恢复显隐不恢复几何）
    void restoreAuxPanelVisibility();
    void setupFlowSceneDragDrop(FlowScene *scene);
    void hookFlowScene(FlowScene *scene);
    bool eventFilter(QObject *obj, QEvent *event) override;
    QList<NodeBase*> getUpstreamNodes(NodeBase *node, FlowScene *scene); // 获取上游节点（数据流正向）
    /// 转发 ImageDisplayController::resolveDisplayNode（保持调用点稳定）
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
    /// 出厂口令仍在用时：拉起写操作闸并弹强制改密框；改密成功即自动解锁（A1-①）
    void enforceFactoryPasswordPolicy();
    /// 打开运行界面设计器
    void openRuntimeInterfaceDesigner();
    /// 收集全部流程中的所有算子完整名（供运行界面绑定节点输出）
    QStringList allNodeFullNames() const;
    /// 载入运行界面布局并刷新运行视图
    void loadRuntimeInterfaceLayout();
    /// 转发 ImageDisplayController::collectOverlayFromNode（保持调用点稳定）
    QVector<OverlayShape> collectOverlayFromNode(NodeBase *node) const;
    /// 进入画布 ROI 编辑模式（测量节点取点）
    void startRoiPick(NodeBase *node);
    /// ROI 绘制完成：写回节点参数
    void handleRoiEdited(const RoiShape &shape);
    /// 当前流程标签页对应的场景（无则 nullptr）。分组等画布操作按"当前流程"作用。
    FlowScene *currentFlowScene() const;
    /// 创建分组（FR1.9；Ctrl+G）：把当前流程里选中的算子框成一组
    void onCreateGroup();
    /// 解散选中的分组框（Ctrl+Shift+G）；只删框，组内算子保留
    void onDissolveGroup();
    /// 折叠/展开选中的分组框（FR1.9 的 Group 折叠；纯显示，不影响执行）
    void onToggleGroupCollapse();
    /// 把当前流程里选中的算子定义为命名子流程（FR15.10；入口/出口自动推导）
    void onDefineSubFlow();
    /// 删除一个子流程定义（画布算子原样保留）
    void onRemoveSubFlow();

    // ---- 良率目标与报警联动（P1-11 剩余差距）----
    /// 每轮结束调用（**每个执行器都要接**，不能只接当前激活流程，否则后台流程的轮次不参与判定）。
    /// 数据源与统计报表**完全一致**（同一个库、同一批「整轮汇总」记录），避免"报表正常但报警乱响"。
    /// 内部按 5 秒节流：连续模式每秒可能几十轮，每轮都查库会把 UI 线程拖住。
    void checkYieldTarget(FlowExecutor *executor);

    // ---- 子图复用：复制/粘贴选中子图（含连线）+ 片段文件导入导出（FR15.10 的设计期复用半步）----
    /// 视图中心（场景坐标）：粘贴/导入时把片段放在用户正在看的位置；无视图时给一个稳妥兜底
    QPointF viewCenterInScene(FlowScene *scene) const;
    /// 复制当前流程里选中的算子为片段文本（写剪贴板）
    void copySelectionToClipboard(FlowScene *scene);
    /// 粘贴剪贴板里的片段（画布 Ctrl+V 与菜单共用）
    void pasteSnippetIntoScene(FlowScene *scene);
    /// 把片段插到场景并选中新算子（记一次撤销 + 状态栏/日志反馈）；失败时 error 说明原因
    bool insertSnippetIntoScene(FlowScene *scene, const QJsonObject &snippet, QString *error);
    /// 导出选中的算子为片段文件（.vfseg）
    void onExportSnippet();
    /// 从片段文件导入到当前流程
    void onImportSnippet();
    /// 导出加密 / 只读方案（G-P1-10）：弹出口令与只读选项，落盘为信封文件
    void onExportEncryptedProject();

    /// 加载指定方案文件（打开/最近文件/崩溃恢复共用）。
    /// recoveryRestore=true（来自崩溃恢复）：不记入最近文件、不把内容标为"已落盘"——
    /// 恢复出来的内容还没写回原方案文件，关闭时仍须提示保存。
    void loadProjectFile(const QString &fileName, bool recoveryRestore = false);
    /// 取得（必要时创建）方案序列化器：保存/加载/自动保存共用同一实例
    ProjectManager *projectManager();
    /// 交互式保存方案（文件对话框 + 结果提示）；返回是否真的保存成功
    bool saveProjectInteractively();
    /// 记录"当前内容已落盘"基线并清除恢复现场（保存/加载成功后调用）
    void markProjectSaved();
    /// 根据当前方案只读态刷新界面（禁用覆盖保存、状态栏提示），与角色权限叠加
    void applyReadonlyUI();
    /// 切换语言（G-P1-5）：写设置并重启应用以应用新语言
    void switchLanguage(I18n::Language lang);

    // ---- 自动保存 / 崩溃恢复（落盘逻辑见 RecoveryStore；本处只做策略与界面）----
    /// 创建自动保存定时器（间隔与开关取自 QSettings recovery/*）
    void initCrashRecovery();
    /// 自动保存一拍：与已落盘内容一致、或与恢复文件一致时跳过，不做无意义写盘
    void autoSaveTick();
    /// 启动后检查上次异常退出留下的恢复文件，询问是否恢复
    void checkRecoveryOnStartup();
    // ---- 定时导出（P1-11 收尾：策略与落盘在 ReportAutoExport，本处只做定时与取数）----
    /// 创建报表定时器（每分钟看一眼：配置可在报表窗口随时改，启用后无需重启）
    void initReportAutoExport();
    /// 定时导出一拍：到点则取最近 N 小时记录、按小时分桶、导出 CSV/HTML 并按保留策略清理
    void reportAutoExportTick();

    /// 关闭前确认：有未保存改动时询问（保存并退出 / 不保存退出 / 取消）
    /// 返回 false 表示用户取消，调用方应忽略本次关闭
    bool confirmCloseWithUnsavedChanges();
    /// 打开/新建方案前确认：有未保存改动时询问（保存并继续 / 不保存继续 / 取消）
    /// action 用于提示文案（如"打开方案"/"新建方案"）；返回 false 表示用户取消，调用方应放弃本次操作
    bool confirmDiscardUnsavedChanges(const QString &action);
    /// 打开使用手册查看对话框（F1 / 帮助菜单）
    void openManualDialog();

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

    Ui::MainWindow *ui;

    ProjectManager *m_projectManager;
    QList<FlowScene *> m_flowScenes;
    HalconWindow *m_imageView;
    /// 图像显示路由与画布交互（显示决策/叠加/ROI 取点；逻辑见 ImageDisplayController）
    ImageDisplayController *m_imageDisplay = nullptr;
    FlowExecutor *m_executor;
    /// 多流程并发：每个流程场景独立执行器（m_executor 指向当前激活流程的执行器）
    QHash<FlowScene *, FlowExecutor *> m_flowExecutors;
    QComboBox *m_flowModeCombo;   // 流程模式下拉框
    QToolButton *m_singleShotBtn;
    QToolButton *m_stepBtn;   /// 单步执行按钮 // 单次执行按钮
    QToolButton *m_pauseBtn = nullptr;  /// 暂停/继续按钮（运行控制与模式解耦后新增）
    NodeBase *m_selectedNode;
    /// 测量节点 ROI 按钮信号连接（防重复连接；取点状态在 ImageDisplayController）
    QMetaObject::Connection m_roiPickConn;
    /// 最近打开方案菜单（记录/去重/截断/重建见 RecentFilesMenu）
    RecentFilesMenu *m_recentFiles = nullptr;
    /// 自动保存/崩溃恢复的落盘逻辑（无 GUI 依赖，其本身由 recovery_store_test 覆盖）
    RecoveryStore m_recovery;
    /// 自动保存定时器（间隔/开关见 QSettings recovery/autoSaveIntervalSec）
    QTimer *m_autoSaveTimer = nullptr;
    /// 报表定时导出定时器（每分钟检查一次；是否导出见 reporting/autoExport*）
    QTimer *m_reportTimer = nullptr;
    /// 算子分组菜单项（FR1.9；文案在 retranslateUi 里随语言切换）
    QAction *m_actionCreateGroup = nullptr;
    QAction *m_actionDissolveGroup = nullptr;
    QAction *m_actionToggleGroup = nullptr;
    /// 子图复用菜单项（FR15.10 设计期半步：复制/粘贴/导出片段/导入片段）
    QAction *m_actionCopySnippet = nullptr;
    QAction *m_actionPasteSnippet = nullptr;
    QAction *m_actionExportSnippet = nullptr;
    QAction *m_actionExportEncrypted = nullptr;
    QAction *m_actionImportSnippet = nullptr;
    QAction *m_actionDefineSubFlow = nullptr;   ///< FR15.10：把选中算子定义为命名子流程
    QAction *m_actionRemoveSubFlow = nullptr;   ///< FR15.10：删除子流程定义
    /// 良率目标监控：**按流程**各一份（报警要指明是哪条流程），键为流程名
    QHash<QString, YieldMonitor> m_yieldMonitors;
    qint64 m_yieldLastEvalMs = 0;   ///< 上次评估时刻（节流用；0 = 尚未评估）
    QLabel *m_imageSourceLabel; // 图像来源标签
    
    // 登录会话（A1-②）：默认 false —— 只有登录框 Accepted 的路径会置位
    bool m_loginAccepted = false;

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
    
    // 保存每个流程各自的运行模式
    QMap<FlowScene *, FlowMode> m_flowModes;
    
    // 保存节点的参数状态
    QMap<NodeBase *, QMap<QString, QVariant>> m_nodeParameters;

    // ---- 状态栏运行信息 ----
    /// 执行状态显示与按钮态（状态/耗时/触发计数 + 开始停止按钮；见 ExecutionStatusController）
    ExecutionStatusController *m_execStatus = nullptr;
    /// 「每轮结束自动上报发送事件」的合并标志：连续模式下 executionFinished 高频发出，
    /// 用它把同一 UI 事件循环周期内的多轮合并成一次上报（否则会淹没 UI 线程与设备）。
    bool m_sendEventFirePending = false;
    /// 每轮结束后自动上报已启用的发送事件（含 {global.x}/{模块号.参数名} 数据注入）
    void fireSendEventsForRound();

    /// 同步执行/重算的忙碌反馈 + 防重入（executeUpTo/executeFrom/executeNode 同步占用 UI 线程）：
    /// 先亮等待光标 + 状态栏提示并强制刷新，让用户看到"正在执行"而不是"界面卡死"；
    /// 执行期间重复触发被忽略（避免排队雪崩）。quiet=true（自动重算）不弹反馈但仍防重入。
    bool runWithBusyFeedback(const QString &what, bool quiet, const std::function<void()> &fn);
    bool m_busyExecuting = false;

    /// 非模态打开主功能窗口：已开着就前置激活（不重复开），关闭后自动销毁。
    /// 主功能窗口一律非模态——打开任一窗口都不影响主界面及其它窗口的操作
    /// （历史行为是 exec() 模态，打开通讯管理后主界面完全不可用）。
    void showAuxDialog(const QString &key, const std::function<QDialog *()> &create);

    // ---- 新增组件 ----
    /// 辅助面板与辅助窗口（结果表/变量/性能/输出数据四类懒创建面板 + 非模态对话框统一入口）
    AuxPanelManager *m_auxPanels = nullptr;
    /// 最近一次执行各模块的输出变量（UI 线程缓存，供「变量引用」菜单构造引用列表）
    QHash<int, QVariantMap> m_lastModuleVars;
    NodeSearchWidget *m_nodeSearchWidget = nullptr;      // 算子搜索控件
    HelpViewer *m_helpViewer = nullptr;                  // 算子帮助文档查看器
    QHash<NodeBase *, class ModuleEditorDialog *> m_moduleEditors;
};
