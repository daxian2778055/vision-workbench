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
#include <QSet>
#include <QShowEvent>
#include <QJsonObject>
#include <functional>
#include <memory>

class QDockWidget;
class QDialog;
class QTimer;
class QAction;
class QGroupBox;
class QListWidget;
class QToolButton;
class QCheckBox;
class QSpinBox;
class QComboBox;
class QSystemTrayIcon;

#include <halconcpp/HalconCpp.h>
#include "HalconWindow.h"
#include "NodeBase.h"
#include "RecoveryStore.h"   // 自动保存/崩溃恢复（值成员，需要完整类型）
#include "YieldMonitor.h"    // 良率目标监控（QHash 值类型，需要完整类型）
#include "I18n.h"            // 国际化（G-P1-5）
#include "AuxDialogMenuRouter.h"   // 菜单对话框路由面（值成员，需要完整类型）
#include "ModuleEditorRegistry.h"   // 模块编辑器登记簿（值成员，需要完整类型）
#include "UITranslator.h"   // UI 文本表应用器（值成员，需要完整类型）

using namespace HalconCpp;

namespace Ui {
class MainWindow;
}

class FlowScene;
class FlowCanvasView;
class ImageReadNode;
class FlowExecutorToken;
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

namespace ProjectPaths {
struct PathReview;   // U-201 载入门禁：目录外绝对路径的确认面（清单结构）
}
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
    /// P2-1：启动时按设置决定是否弹欢迎页（main 在窗口 show 之后调用，避免测试/无界面场景被打断）
    void maybeShowWelcomeOnStartup();

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
    /// 节点删除后清理所有以裸指针键控的界面状态（选中/本地图/叠加/耗时/编辑窗/显示源）。
    /// 缺这条时 m_selectedNode 等会在节点析构后悬垂，下一次点击/刷新即随机闪退。
    void onNodeRemoved(NodeBase *node);
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
    /// 词条切片 C P3b 期（销毁权归一）：执行器本体的**唯一拥有者**是令牌，令牌析构时执行收尾
    /// （停止 ＋ 有界等待 ＋ 回收；等待超时绝不 terminate——线程析构会硬杀，比随进程退出被回收
    /// 更危险，改挂 finished→deleteLater 自删）。界面侧只负责「把令牌放掉」，不再自己 delete
    /// 执行器 ⇒ 本体的删除只有一处（令牌件），两路口径不会分叉。
    /// 用法：把令牌按引用交给本件——本件直接放掉调用方那份引用 ⇒ 最后一个令牌释放时立即收尾。
    void releaseExecutorToken(std::shared_ptr<FlowExecutorToken> &token);
    /// 连接执行器信号到主窗口（状态栏/运行界面/参数面板）
    void connectExecutorSignals(FlowExecutor *ex);
    void setupToolLibrary();
    void setupFlowEditor();
    void setupRightPanel();
    void setupDockWidgets();
    void switchToDesignMode();
    void switchToRuntimeMode();
    void logMessage(const QString &message);
    /// P2-3：日志栏折叠为「最近一条」/展开为全量（VM 日志栏默认只显示最近一条）
    void setLogExpanded(bool on);
    /// P2-2：顶栏方案名（路径为空 = 未命名方案）
    void updateSchemeLabel(const QString &filePath = QString());
    /// P2-2：右上角资源占用（CPU / 内存 / 硬盘），每 2 秒刷新
    void refreshResourceUsage();
    // ---- 后台运行（VM 4.4 口径：关闭时可选"保持后台运行"）----
    void setupSystemTray();
    void minimizeToTray();
    void restoreFromTray();
    bool anyFlowRunning() const;
    void setupSchemeMenu();    // 设置方案菜单
    void setupCommunicationMenu(); // 设置通讯管理菜单
    void setupSystemMenu();       // 设置系统菜单
    /// 实装 4.4 菜单收敛：顶层 = 文件/设置/工具/系统/帮助，编辑/视图/通讯归并
    void reorganizeMenuBar();
    void updateLanguage();     // 更新界面语言
    void retranslateUi();      // 重新翻译UI
    void showNodeParameters(NodeBase *node);
    void openModuleEditor(NodeBase *node);
    void executeNodeOnce(NodeBase *node);
    /// 参数改动后只重算该算子及其下游：先作废缓存，再执行这一段链路
    /// quiet=true 表示自动重算（不写日志、流程忙时静默跳过）
    void recomputeDownstream(NodeBase *node, bool quiet = false);
    /// P2-8：执行前把各节点的「参数订阅」表达式解析为实际值
    ///（支持 {global.变量名} / {模块号.参数名}；解析失败保持原值）
    void applyParamSubscriptions(FlowScene *scene);
    /// 构造可引用变量清单（{模块号.参数名} / {global.名称}），供模块编辑窗「变量引用」菜单
    QStringList buildVariableReferences() const;
    /// 结果区「帮助信息」页：选中模块的说明/端口/参数（随选中联动）
    QString buildNodeHelpHtml(NodeBase *node) const;
    /// 刷新图像区「图像源」下拉（当前模块 + 上游输出；按场景记忆选择）
    void refreshImageSourceCombo(NodeBase *node);
    /// 打开/聚焦辅助面板（key: resultTable / variable / performance / outputData）
    void openAuxPanel(const QString &key);
    /// 记住辅助面板的显隐（关闭时调用）
    void saveAuxPanelVisibility() const;
    /// 恢复上次退出时打开的辅助面板（懒创建，仅恢复显隐不恢复几何）
    void restoreAuxPanelVisibility();
    void setupFlowSceneDragDrop(FlowScene *scene);
    void hookFlowScene(FlowScene *scene);
    bool eventFilter(QObject *obj, QEvent *event) override;
    /// 转发 ImageDisplayController::resolveDisplayNode（保持调用点稳定）
    NodeBase *resolveDisplayNode(NodeBase *fallbackNode = nullptr) const;
    /// P2-4：当前流程对应的画布视图（用于耗时角标）
    FlowCanvasView *canvasViewForScene(FlowScene *scene) const;
    /// P2-4：按选中模块刷新画布角标的「模块耗时」
    void updateCanvasSelectionElapsed(NodeBase *node);
    /// P2-6：图像区「本地图」区随选中 ImageReadNode 联动（非读图模块隐藏）
    void refreshLocalImagePanel(NodeBase *node);
    /// P2-5：十字辅助线配置对话框（位置按百分比、颜色可配）
    void showCrosshairSettings();
    /// P2-5：全屏查看当前图像（Esc 退出）
    void showFullScreenImage();
    /// P2-5：叠加上游——收集当前模块的所有上游模块
    QList<NodeBase *> upstreamNodes(NodeBase *node) const;
    /// P2-5：把额外勾选的上游模块结果叠加到当前图像上
    void applyExtraOverlays(NodeBase *displayNode);
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
    /// 模块窗「绘制」→ 主画布重画模板匹配的检测区域（polygon=true 逐点绘制）
    void beginSearchRegionPick(NodeBase *node, bool polygon);
    /// 模块窗「绘制」→ 主画布重画模板匹配的 ROI 区域（模板框）
    void beginTemplateRoiPick(NodeBase *node, bool rotatedRect);
    /// 检测区域输入框右侧「取点」→ 主画布单击取坐标
    void beginCoordinatePick(NodeBase *node, int field);
    /// 把主画布切到该节点的图像并保证图像窗口可见（两种绘制入口共用）
    bool prepareCanvasPick(NodeBase *node);
    /// ROI 绘制完成：写回节点参数
    void handleRoiEdited(const RoiShape &shape);
    /// 模块窗「ROI区域/绘制」→ 主画布通用几何 ROI 拾取
    void beginGeometryPick(NodeBase *node);
    /// 模块窗「掩膜/编辑掩膜」→ 主画布掩膜涂抹
    void beginMaskEdit(NodeBase *node);
    /// 主画布掩膜画笔收笔：写回节点 editMask
    void handleMaskEdited();
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
    /// U-201 载入门禁：本次载入含方案目录外绝对路径时弹模态确认（逐条列路径）；
    /// 返回 true = 操作员放行；false = 拒载（当前方案保持不变）。
    bool confirmOutsideSchemePaths(const ProjectPaths::PathReview &review);
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
    /// 删除流程前的统一确认（流程名 + 模块数 + 未保存提示）。
    /// 关闭标签页 X、Ctrl+Shift+T、流程栏右键「删除流程」都必须经过这里。
    bool confirmDeleteFlow(FlowScene *scene, int index);
    /// 流程栏右键动作（P1-3，对标 VM「其他操作与配置」）
    void showFlowTabContextMenu(const QPoint &pos);
    void renameFlow(int index);
    void duplicateFlow(int index);
    void exportFlowToFile(int index);
    /// 恢复最近一次删除的流程（P0-1 的"可撤销保护"低成本实现）
    void restoreLastDeletedFlow();
    /// 按场景刷新工具栏「锁定」按钮的选中态（切页签时调用）
    void syncCanvasLockAction();
    /// 刷新右上角用户/角色按钮文本（登录、切换用户、注销后调用）
    void updateUserButton();
    /// 切换用户（保留登录审计，运行中可切换角色）
    void switchUser();
    /// 注销并返回登录框；取消登录则退出应用
    void logoutUser();
    /// 流程超时监控一拍（UI 层计时，不侵入执行器线程）
    void onFlowTimeout();
    /// 打开使用手册查看对话框（F1 / 帮助菜单）
    void openManualDialog();
    /// P2-1/P2-12：显示欢迎页（启动时按设置自动弹出；帮助菜单可随时再次打开）
    void showWelcomePage();
    /// P2-11：综合配置入口——把相机/通讯/触发/用户/报表等分散入口集中到一处
    void openComprehensiveConfig();

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

    Ui::MainWindow *ui;

    ProjectManager *m_projectManager;
    QList<FlowScene *> m_flowScenes;
    HalconWindow *m_imageView;
    /// P2-5：双画面副视图（与主视图独立缩放；默认隐藏）
    HalconWindow *m_imageView2 = nullptr;
    /// P2-5：叠加上游勾选集合
    QSet<NodeBase *> m_overlayNodes;
    QToolButton *m_overlayUpstreamBtn = nullptr;
    QSpinBox *m_overlayOpacitySpin = nullptr;   ///< 图像叠加透明度（%）
    /// 实装 4.4：图像区①区「图像源」选择（当前模块 + 上游输出）
    QComboBox *m_imageSourceCombo = nullptr;
    /// P2-6：图像区「本地图」区（仅在选中 ImageReadNode 时显示）
    QGroupBox *m_localImageGroup = nullptr;
    QListWidget *m_localImageList = nullptr;
    ImageReadNode *m_localImageNode = nullptr;
    /// P2-6/实装 4.4：本地图导航与自动切换
    QToolButton *m_localPrevBtn = nullptr;
    QToolButton *m_localNextBtn = nullptr;
    QLabel *m_localIndexLabel = nullptr;
    QCheckBox *m_localAutoSwitch = nullptr;
    /// 图像显示路由与画布交互（显示决策/叠加/ROI 取点；逻辑见 ImageDisplayController）
    ImageDisplayController *m_imageDisplay = nullptr;
    FlowExecutor *m_executor;
    /// 多流程并发：每个流程场景独立执行器（m_executor 指向当前激活流程的执行器）。
    /// 词条切片 C P3b 期：表里存的是**令牌**（执行器本体的唯一拥有者），裸指针一律经令牌
    /// 借出（token->executor()）⇒ 本体的删除只有一处（令牌件）。
    QHash<FlowScene *, std::shared_ptr<FlowExecutorToken>> m_flowExecutors;
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
    /// 系统菜单「脚本安全：执行前确认脚本」开关（2026-10-25）：权限收口要按角色灰掉，
    /// 故须留成员指针（applyPermissionRestrictions 里统一处理）
    QAction *m_confirmScriptAct = nullptr;
    /// 打包模型与依赖（2026-10-29 复核 P2-13）：它改写活方案的路径，必须随写闸/编辑锁灰掉
    QAction *m_packModelsAct = nullptr;
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

    // ---- P0-1 流程删除保护：最近一次删除的快照（可恢复）----
    QJsonObject m_lastDeletedFlowJson;
    QString m_lastDeletedFlowName;
    int m_lastDeletedFlowMode = 1;

    // ---- P1-3 流程栏运行配置（按场景，UI 侧持有；不写入执行器核心状态）----
    QMap<FlowScene *, int> m_flowRunIntervals;   ///< 运行间隔 ms（0=连续不停顿）
    QMap<FlowScene *, int> m_flowTimeouts;       ///< 流程超时 ms（0=不限）
    FlowScene *m_timeoutScene = nullptr;         ///< 本次超时计时对应的流程（切页签后仍停对应流程）
    QTimer *m_flowTimeoutTimer = nullptr;        ///< 当前流程超时监控（单次触发）

    // ---- P1-5/P1-7/P1-8 快捷工具条与锁定/用户入口 ----
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QAction *m_lockCanvasAction = nullptr;       ///< 可勾选：用户显式锁定当前流程
    QToolButton *m_userButton = nullptr;         ///< 右上角「用户：xxx [角色]」切换/注销
    QAction *m_restoreDeletedFlowAction = nullptr;

    // ---- P2-2/P2-3 顶栏信息与日志栏 ----
    QLabel *m_schemeLabel = nullptr;             ///< 顶栏「方案：xxx」
    QLabel *m_resourceLabel = nullptr;           ///< 顶栏资源占用
    QTimer *m_resourceTimer = nullptr;           ///< 资源占用刷新（2s）
    QToolButton *m_logExpandBtn = nullptr;       ///< 日志栏展开/收起
    QMenu *m_helpMenu = nullptr;                 ///< 帮助菜单（菜单收敛后语言子菜单挂这里）
    bool m_logExpanded = false;                  ///< 日志栏当前是否展开
    void updateLogExpandButton();

    // ---- 运行策略（VM 4.4 手册 11.4：普通模式静默 / 诊断模式资源超限告警）----
    bool m_resourceDiagnosticMode = false;
    QElapsedTimer m_diagWarnTimer;               ///< 告警节流（避免每 2 秒刷日志）

    // ---- 后台运行（VM 4.4：关闭时询问是否保持后台运行）----
    QSystemTrayIcon *m_trayIcon = nullptr;
    bool m_forceQuit = false;                    ///< 托盘「退出软件」置位，跳过后台运行询问
    /// 已从 QSettings 恢复过停靠布局 ⇒ 首帧不再套用默认比例（否则用户拖过的占比被覆盖）
    bool m_dockLayoutRestored = false;

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
    /// UI 文本表应用器（retranslateUi 的双语文本对全入此表；词条切片 B 就地链首抽）
    UITranslator m_texts;
    
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
    /// 菜单对话框路由面（通讯/系统菜单的展示接线经它落位；展示钩子即 showAuxDialog 转发）
    AuxDialogMenuRouter m_menuRouter;
    /// 最近一次执行各模块的输出变量（UI 线程缓存，供「变量引用」菜单构造引用列表）
    QHash<int, QVariantMap> m_lastModuleVars;
    /// P2-4：最近一轮各模块耗时（画布选中模块角标；节点执行后被覆盖）
    QHash<NodeBase *, qint64> m_nodeElapsed;
    NodeSearchWidget *m_nodeSearchWidget = nullptr;      // 算子搜索控件
    HelpViewer *m_helpViewer = nullptr;                  // 算子帮助文档查看器
    /// 模块编辑器登记簿（「一算子一窗」；词条切片 B 对话框族首抽——命中刷新/缺失建窗/摘除归簿）
    ModuleEditorRegistry m_editors;
};
