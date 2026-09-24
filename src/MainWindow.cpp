#include "MainWindow.h"
#include "ResultTablePanel.h"
#include "VariablePanel.h"
#include "AuxPanelManager.h"
#include "ImageDisplayController.h"
#include "ExecutionStatusController.h"
#include "RecentFilesMenu.h"
#include "ui_MainWindow.h"
#include "FlowScene.h"
#include "NodeBase.h"
#include "NodeGroupItem.h"
#include "FlowSnippet.h"
#include "YieldMonitor.h"
#include "InspectionRecord.h"
#include "ReportAutoExport.h"
#include <QClipboard>
#include <algorithm>
#include "HalconEnvCheck.h"
#include "HalconNode.h"
#include "HelpDialog.h"
#include "FindLineNode.h"
#include "FindCircleNode.h"
#include "CaliperMeasureNode.h"
#include "MvsImageSourceNode.h"
#include "HalconImageSourceNode.h"
#include "DeepOcrNode.h"
#include "ColorConversionNode.h"
#include "ImageReadNode.h"
#include "DataObject.h"
#include "FlowExecutor.h"
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QInputDialog>
#include <QDialogButtonBox>
#include "Port.h"
#include "Connection.h"
#include "NodeGraphicsItem.h"
#include "ProjectManager.h"
#include "AppDatabase.h"
#include "BrandHeader.h"
#include "GlobalVariableManager.h"
#include "GlobalVariableDialog.h"
#include "FlowVariableDialog.h"
#include "UserLoginDialog.h"
#include "UserManagementDialog.h"
#include "RecipeManager.h"
#include "RecipeDialog.h"
#include "AlarmHistoryDialog.h"
#include "InspectionResultDialog.h"
#include "CommunicationManager.h"
#include "CommunicationManagerDialog.h"
#include "CommMonitorDialog.h"
#include "GlobalTriggerManager.h"
#include "GlobalTriggerDialog.h"
#include "SessionManager.h"
#include "OperationLogDialog.h"
#include "ParameterSearchDialog.h"
#include "CodeExportDialog.h"
#include "ReportDialog.h"
#include "RuntimeInterfaceDesigner.h"
#include "RuntimeInterfaceView.h"
#include <QShortcut>
#include "HeartbeatManager.h"
#include "GlobalCameraManager.h"
#include "GlobalCameraDialog.h"
#include "HalconWindow.h"
#include "PerformancePanel.h"
#include "StatisticsReportDialog.h"
#include "OutputDataViewer.h"
#include "NodeSearchWidget.h"
#include "HelpViewer.h"
#include "ModuleEditorDialog.h"
#include <HalconCpp.h>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QThreadPool>
#include <QTimer>
#include <QGraphicsView>
#include <QPainter>
#include <QDateTime>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSettings>
#include <QEvent>
#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QGraphicsView>
#include <QToolBar>
#include <QScrollBar>

#include "VisionWorkbenchStyle.h"
#include "NodeFactory.h"
#include "NodeRegistry.h"

using namespace HalconCpp;
#include <QCheckBox>
#include <QLineEdit>
#include <QLabel>
#include <QShortcut>
#include <QSettings>
#include <QMenu>
#include <cmath>
#include <QPushButton>
#include <QComboBox>
#include <QDockWidget>
#include <QSizePolicy>
#include "AppLog.h"

namespace {

/// 流程模式 → 标签页后缀（F-3：此前组合框/载入/切页签各写一份，且越界行为不一致，统一到此处）
QString flowModeSuffix(FlowMode mode)
{
    switch (mode) {
    case FlowMode::Continuous:
        return QStringLiteral(" [连续]");
    case FlowMode::HardwareTrigger:
        return QStringLiteral(" [硬触发]");
    case FlowMode::SoftwareTrigger:
    default:
        return QStringLiteral(" [软触发]");
    }
}

// 枚举顺序 = 组合框索引 = scene->flowMode 的持久化取值，三者必须同步，改动即在此报错
static_assert(static_cast<int>(FlowMode::Continuous) == 0
                  && static_cast<int>(FlowMode::SoftwareTrigger) == 1
                  && static_cast<int>(FlowMode::HardwareTrigger) == 2,
              "FlowMode 顺序变更必须同步：组合框顺序 / scene->flowMode 持久化取值 / flowModeSuffix");

}   // namespace

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(nullptr),
    m_executor(nullptr),
    m_imageView(nullptr),
    m_projectManager(nullptr),
    m_flowModeCombo(nullptr),
    m_selectedNode(nullptr),
    m_currentLanguage(Language::Chinese)
{
    VFP_DEBUG << "MainWindow constructor started";
    
    try {
        // 初始化UI
        VFP_DEBUG << "Creating Ui::MainWindow";
        ui = new Ui::MainWindow;
        if (!ui) {
            VFP_DEBUG << "Failed to create Ui::MainWindow";
            return;
        }
        
        VFP_DEBUG << "Calling setupUi";
        ui->setupUi(this);

        if (auto *qa = qobject_cast<QApplication *>(QApplication::instance())) {
            VisionWorkbenchStyle::applyFusionDarkPalette(qa);
        }
        setStyleSheet(VisionWorkbenchStyle::globalWidgetsStylesheet());

        // === Dock widgets 布局设置 ===
        ui->flowTabs->setDocumentMode(true);
        // 辅助面板管理器：必须在 setupDockWidgets 之前创建（其末尾按上次显隐恢复面板）
        m_auxPanels = new AuxPanelManager(this, this);
        setupDockWidgets();

        // S2：流程名同名覆盖告警 → 状态栏提示（避免触发路由被静默重定向）
        connect(GlobalTriggerManager::instance(), &GlobalTriggerManager::flowNameCollision,
                this, [this](const QString &name) {
                    statusBar()->showMessage(
                        tr("警告：流程名 \"%1\" 已存在，触发路由可能被重定向").arg(name), 8000);
                });

        VFP_DEBUG << "setupUi completed";
        
        // 初始化图像视图
        VFP_DEBUG << "Creating HalconWindow";
        m_imageView = new HalconWindow(this);
        if (m_imageView) {
            QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(ui->imageDockContents->layout());
            if (!layout) {
                layout = new QVBoxLayout(ui->imageDockContents);
            }
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(0);
            // 旧版 .ui 里的空 imageView 占位会抢走一半高度，必须先移除
            if (QWidget *placeholder = ui->imageDockContents->findChild<QWidget *>(QStringLiteral("imageView"))) {
                layout->removeWidget(placeholder);
                placeholder->hide();
                placeholder->deleteLater();
            }
            m_imageView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            m_imageView->setMinimumSize(320, 240);
            layout->addWidget(m_imageView, 1);

            // 画布 ROI 绘制完成 → 写回测量节点参数
            connect(m_imageView, &HalconWindow::roiEdited, this, &MainWindow::handleRoiEdited);
            
            // 添加图像来源提示
            m_imageSourceLabel = new QLabel(this);
            m_imageSourceLabel->setText(QStringLiteral("图像来源: 无"));
            m_imageSourceLabel->setAlignment(Qt::AlignCenter);
            m_imageSourceLabel->setStyleSheet(VisionWorkbenchStyle::imageSourceStripStylesheet());
            m_imageSourceLabel->setFixedHeight(22);
            layout->addWidget(m_imageSourceLabel);

            // 图像显示路由与画布交互：显示决策（下拉框 > 画布选中 > 兜底）、叠加图元收集、
            // ROI 取点状态集中到这里；场景与画布选中通过 provider 注入，控制器不反向依赖主窗口。
            m_imageDisplay = new ImageDisplayController(m_imageView, m_imageSourceLabel, this);
            m_imageDisplay->setSceneProvider([this]() -> FlowScene * {
                const int idx = ui->flowTabs->currentIndex();
                return (idx >= 0 && idx < m_flowScenes.size()) ? m_flowScenes[idx] : nullptr;
            });
            m_imageDisplay->setCanvasSelectionProvider([this]() { return m_selectedNode; });

            // ---- 图像显示工具栏：适应 / 100% / 放大 / 缩小 / 十字线 ----
            {
                auto *imgBar = new QWidget(ui->imageDockContents);
                auto *imgBarLay = new QHBoxLayout(imgBar);
                imgBarLay->setContentsMargins(4, 2, 4, 2);
                imgBarLay->setSpacing(4);

                const QString btnStyle =
                    "QPushButton {"
                    "  background-color: #2b2b3d; color: #d8d8d8;"
                    "  border: 1px solid #4a4a6a; border-radius: 4px;"
                    "  padding: 3px 10px; font-size: 12px;"
                    "}"
                    "QPushButton:hover { background-color: #3a3a55; }"
                    "QPushButton:checked { background-color: #3a6ea5; color: white; border-color: #5a8ec5; }"
                    "QPushButton:disabled { color: #666; border-color: #3a3a4a; }";

                auto makeBtn = [&](const QString &text, const char *objName) {
                    auto *b = new QPushButton(text, imgBar);
                    b->setObjectName(objName);
                    b->setStyleSheet(btnStyle);
                    imgBarLay->addWidget(b);
                    return b;
                };

                QPushButton *fitBtn = makeBtn(QStringLiteral("\u9002\u5E94\u7A97\u53E3"), "imgFitBtn");
                QPushButton *actualBtn = makeBtn(QStringLiteral("100%"), "imgActualBtn");
                QPushButton *zoomInBtn = makeBtn(QStringLiteral("\u653E\u5927"), "imgZoomInBtn");
                QPushButton *zoomOutBtn = makeBtn(QStringLiteral("\u7F29\u5C0F"), "imgZoomOutBtn");
                QPushButton *crossBtn = makeBtn(QStringLiteral("\u5341\u5B57\u7EBF"), "imgCrossBtn");
                crossBtn->setCheckable(true);
                QPushButton *saveImgBtn = makeBtn(QStringLiteral("\u4FDD\u5B58\u56FE\u50CF"), "imgSaveBtn");
                QPushButton *floatBtn = makeBtn(QStringLiteral("\u5F39\u51FA"), "imgFloatBtn");

                fitBtn->setToolTip(QStringLiteral("\u81EA\u9002\u5E94\u7A97\u53E3\u5927\u5C0F"));
                actualBtn->setToolTip(QStringLiteral("\u5B9E\u9645\u50CF\u7D20 100% \u663E\u793A"));
                zoomInBtn->setToolTip(QStringLiteral("\u653E\u5927"));
                zoomOutBtn->setToolTip(QStringLiteral("\u7F29\u5C0F"));
                crossBtn->setToolTip(QStringLiteral("\u663E\u793A/\u9690\u85CF\u4E2D\u5FC3\u5341\u5B57\u7EBF"));
                saveImgBtn->setToolTip(QStringLiteral("\u4FDD\u5B58\u5F53\u524D\u56FE\u50CF"));
                floatBtn->setToolTip(QStringLiteral("\u5F39\u51FA\u4E3A\u72EC\u7ACB\u7A97\u53E3\uFF0C\u53EF\u62D6\u5927\uFF1B\u518D\u70B9\u8FD8\u539F"));

                connect(fitBtn, &QPushButton::clicked, m_imageView, &HalconWindow::fitToWindow);
                connect(actualBtn, &QPushButton::clicked, m_imageView, &HalconWindow::setActualSize);
                connect(zoomInBtn, &QPushButton::clicked, m_imageView, &HalconWindow::zoomIn);
                connect(zoomOutBtn, &QPushButton::clicked, m_imageView, &HalconWindow::zoomOut);
                connect(crossBtn, &QPushButton::toggled, m_imageView, &HalconWindow::setCrosshairVisible);
                connect(saveImgBtn, &QPushButton::clicked, m_imageView, &HalconWindow::saveImage);
                connect(floatBtn, &QPushButton::clicked, this, &MainWindow::onToggleImageFloat);

                imgBarLay->addStretch();
                layout->insertWidget(0, imgBar, 0);
                layout->setStretch(layout->indexOf(m_imageView), 1);
                if (m_imageSourceLabel)
                    layout->setStretch(layout->indexOf(m_imageSourceLabel), 0);
            }

            ui->imageDockContents->setStyleSheet(QStringLiteral(
                "QWidget { background-color: #252528; border: none; }"));
            VFP_DEBUG << "HalconWindow created and added to dock";
        }

        // 初始化执行器
        VFP_DEBUG << "Creating FlowExecutor";
        m_executor = new FlowExecutor(this);
        m_flowExecutors.insert(nullptr, m_executor);  // 占位：首个流程创建时复用
        // 应用失败中断策略（setupDockWidgets 早于此处调用，故在此真正生效）
        {
            QSettings settings;
            m_executor->setStopOnFailure(
                settings.value(QStringLiteral("flow/stopOnFailure"), true).toBool());
        }
        // 启动恢复阶段可能已按上次的显隐把性能面板打开（那时 m_executor 还不存在），
        // 这里补一次绑定，否则面板会一直是空表。
        if (auto *pp = m_auxPanels->performancePanel()) {
            pp->bindExecutor(m_executor);
        }

        VFP_DEBUG << "FlowExecutor created";

        // ── 流程运行模式组合框（放在顶部主工具栏，确保用户一眼看到）──
        {
            QToolBar *mainToolBar = addToolBar(QStringLiteral("流程控制"));
            mainToolBar->setObjectName("mainToolBar");
            mainToolBar->setMovable(false);
            mainToolBar->setIconSize(QSize(20, 20));
            mainToolBar->setStyleSheet(
                "QToolBar {"
                "  background-color: #1e1e2e;"
                "  border-bottom: 1px solid #3a3a5c;"
                "  spacing: 6px;"
                "  padding: 2px 8px;"
                "}"
            );

            // ── 文件操作：新建 / 打开 / 保存 ──
            mainToolBar->addAction(ui->actionNew);
            mainToolBar->addAction(ui->actionOpenScheme);
            mainToolBar->addAction(ui->actionSaveScheme);

            mainToolBar->addSeparator();

            // 开始按钮
            mainToolBar->addAction(ui->actionStartExecution);
            // 停止按钮（补全 VM 4.4 的停止功能）
            mainToolBar->addAction(ui->actionStopExecution);

            // 单次执行按钮：软触发模式下运行一次，不影响流程模式设置
            m_singleShotBtn = new QToolButton();
            m_singleShotBtn->setObjectName("singleShotBtn");
            m_singleShotBtn->setText(QStringLiteral("单次执行"));
            m_singleShotBtn->setToolTip(QStringLiteral("以软触发方式运行当前流程一次"));
            m_singleShotBtn->setStyleSheet(
                "QToolButton {"
                "  background-color: #2b2b3d; color: #e0e0e0;"
                "  border: 1px solid #4a6a9c; border-radius: 4px;"
                "  padding: 3px 10px; min-height: 24px;"
                "  font-size: 13px;"
                "}"
                "QToolButton:hover { background-color: #3a3a55; }"
                "QToolButton:disabled { color: #777; border-color: #444; }"
            );
            mainToolBar->addWidget(m_singleShotBtn);
            connect(m_singleShotBtn, &QToolButton::clicked, this, &MainWindow::onSingleShotExecution);

            // 单步执行按钮：每执行一个节点后暂停（步进模式）
            m_stepBtn = new QToolButton();
            m_stepBtn->setObjectName("stepBtn");
            m_stepBtn->setText(QStringLiteral("单步"));
            m_stepBtn->setToolTip(QStringLiteral("逐节点执行：每运行一个算子后暂停（可配合单次执行排查流程）"));
            m_stepBtn->setStyleSheet(
                "QToolButton {"
                "  background-color: #2b2b3d; color: #e0e0e0;"
                "  border: 1px solid #4a6a9c; border-radius: 4px;"
                "  padding: 3px 10px; min-height: 24px;"
                "  font-size: 13px;"
                "}"
                "QToolButton:hover { background-color: #3a3a55; }"
                "QToolButton:disabled { color: #777; border-color: #444; }"
            );
            mainToolBar->addWidget(m_stepBtn);
            connect(m_stepBtn, &QToolButton::clicked, this, [this]() {
                if (m_executor) m_executor->stepExecution();
            });

            // 暂停/继续按钮：运行中→暂停；暂停中→继续（连续模式下的"启动后暂停切换"就靠它）
            m_pauseBtn = new QToolButton();
            m_pauseBtn->setObjectName("pauseBtn");
            m_pauseBtn->setText(QStringLiteral("暂停"));
            m_pauseBtn->setToolTip(QStringLiteral("暂停当前流程（不清输入缓存）；再按继续"));
            m_pauseBtn->setStyleSheet(m_stepBtn->styleSheet());
            mainToolBar->addWidget(m_pauseBtn);
            connect(m_pauseBtn, &QToolButton::clicked, this, &MainWindow::onPauseResumeExecution);

            // 分隔
            mainToolBar->addSeparator();

            // 标签
            QLabel *modeLabel = new QLabel(QStringLiteral(" 流程模式: "));
            modeLabel->setStyleSheet("color: #c0c0c0; font-size: 13px;");
            mainToolBar->addWidget(modeLabel);

            // 下拉框
            m_flowModeCombo = new QComboBox();
            m_flowModeCombo->setObjectName("flowModeCombo");
            m_flowModeCombo->addItem(QStringLiteral("连续模式"),    static_cast<int>(FlowMode::Continuous));
            m_flowModeCombo->addItem(QStringLiteral("软触发模式"),  static_cast<int>(FlowMode::SoftwareTrigger));
            m_flowModeCombo->addItem(QStringLiteral("硬触发模式"),  static_cast<int>(FlowMode::HardwareTrigger));
            m_flowModeCombo->setCurrentIndex(1); // 默认软触发模式
            m_flowModeCombo->setMinimumWidth(130);
            m_flowModeCombo->setToolTip(QStringLiteral(
                "连续模式：切换到该模式后自动循环执行流程\n"
                "软触发模式：需手动点击「开始执行」运行一次流程\n"
                "硬触发模式：仅相机硬件触发时自动执行一次"
            ));
            m_flowModeCombo->setStyleSheet(
                "QComboBox {"
                "  background-color: #2b2b3d; color: #e0e0e0;"
                "  border: 1px solid #4a6a9c; border-radius: 4px;"
                "  padding: 3px 10px; min-height: 24px;"
                "  font-size: 13px;"
                "}"
                "QComboBox::drop-down { border: none; width: 22px; }"
                "QComboBox::down-arrow { image: none; }"
                "QComboBox QAbstractItemView {"
                "  background-color: #2b2b3d; color: #e0e0e0;"
                "  selection-background-color: #3a6ea5;"
                "  border: 1px solid #4a6a9c;"
                "}"
            );
            mainToolBar->addWidget(m_flowModeCombo);

            // 模式切换逻辑 —— 保存到当前流程，同步执行器
            connect(m_flowModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this](int idx) {
                auto mode = static_cast<FlowMode>(
                    m_flowModeCombo->itemData(idx).toInt());
                m_executor->setFlowMode(mode);

                // 保存到当前流程并更新标签页标题
                int tabIdx = ui->flowTabs->currentIndex();
                if (tabIdx >= 0 && tabIdx < m_flowScenes.size()) {
                    m_flowModes[m_flowScenes[tabIdx]] = mode;
                    m_flowScenes[tabIdx]->setFlowMode(static_cast<int>(mode));   // 每流程模式写回场景，随方案保存
                    const QString suffix = flowModeSuffix(mode);   // F-3：后缀映射单一来源
                    // tab 标题用真实流程名（可能与 tab 序号不同，删除流程后更明显），避免显示与触发路由名不一致
                    const QString base = (m_executor && !m_executor->flowName().isEmpty())
                                            ? m_executor->flowName()
                                            : QStringLiteral("流程 %1").arg(tabIdx + 1);
                    ui->flowTabs->setTabText(tabIdx, base + suffix);
                }

                // 停止当前运行中的流程
                if (m_executor->getState() == ExecutionState::Running) {
                    m_executor->stopExecution();
                    m_executor->wait(500);
                }

                // 运行控制与模式选择解耦（对齐 VisionMaster）：切模式只决定"怎么跑"，不再自动开跑，
                // 一律由「开始执行」启动。此前连续/硬触发模式一切过去就自动 start：用户既找不到"启动"
                // 入口，也不理解为什么停止后起不来（开始按钮还被"仅软触发可用"规则禁用）。
                switch (mode) {
                case FlowMode::Continuous:
                    logMessage(QStringLiteral("已切换为连续模式：点「开始执行」开始循环运行，运行中可用「暂停」"));
                    break;
                case FlowMode::SoftwareTrigger:
                    logMessage(QStringLiteral("已切换为软触发模式：点「开始执行」运行一次流程"));
                    break;
                case FlowMode::HardwareTrigger:
                    logMessage(QStringLiteral("已切换为硬触发模式：点「开始执行」进入等待，相机每来一帧执行一次"));
                    break;
                }
                // 按钮态单一来源：开始/停止/单次执行可用性由 ExecutionStatusController 统一决定，
                // 不再在此手动写 setEnabled（消除「软触发才可开始」规则的第三份副本）。
                if (m_execStatus)
                    m_execStatus->updateButtons(m_executor ? m_executor->getState()
                                                          : ExecutionState::Stopped);
                updateEditLockForCurrentScene();
            });
        }

        // ---- 状态栏运行信息：运行状态 / 本次耗时 / 触发计数 ----
        // 状态机、三项常驻标签与「开始/停止/单次执行」按钮态集中到 ExecutionStatusController
        m_execStatus = new ExecutionStatusController(ui->statusBar, this);
        m_execStatus->setControls(ui->actionStartExecution, ui->actionStopExecution, m_singleShotBtn,
                                  m_pauseBtn);
        m_execStatus->setExecutorProvider([this]() { return m_executor; });

        // 统一执行器信号连接（N3 富反馈统一）：首执行器不再直连（旧实现无门槛——后台跑首流程
        // 会污染状态栏/结果表），与 executorForScene 新建的执行器走**同一套**连接（见
        // connectExecutorSignals）：全局 UI 按 ex == m_executor 门槛，流程域反馈按执行器自身场景。
        connectExecutorSignals(m_executor);

        // 「视图 → 变量 / 性能统计」：动作在代码中创建（避免改动 .ui）；打开逻辑统一走
        // openAuxPanel，与「关闭时记住显隐、启动时恢复」共用同一条创建路径。
        if (ui->menuView) {
            ui->menuView->addSeparator();
            QAction *varAction = ui->menuView->addAction(QStringLiteral("变量"));
            varAction->setObjectName(QStringLiteral("actionVariablePanel"));
            connect(varAction, &QAction::triggered, this,
                    [this]() { openAuxPanel(QStringLiteral("variable")); });

            // 性能面板（PerformancePanel）早已实现却没有任何入口（不可达代码），这里补上接线
            QAction *perfAction = ui->menuView->addAction(QStringLiteral("性能统计"));
            perfAction->setObjectName(QStringLiteral("actionPerformancePanel"));
            connect(perfAction, &QAction::triggered, this,
                    [this]() { openAuxPanel(QStringLiteral("performance")); });

            // 统计报表（P1-11）：良率 / 按天趋势 / NG 按节点分布，可导出 CSV / HTML / 图表 PNG。
            // 做成对话框而非停靠面板：报表是"按需看一眼"的入口，无需参与面板显隐持久化。
            QAction *reportAction = ui->menuView->addAction(QStringLiteral("统计报表"));
            reportAction->setObjectName(QStringLiteral("actionStatisticsReport"));
            connect(reportAction, &QAction::triggered, this, [this]() {
                StatisticsReportDialog dlg(this);
                dlg.exec();
            });
        }

        // 「视图 → 结果表」菜单项（动作定义在 .ui 中，接线方式与其它视图项一致）
        if (ui->actionResultTable) {
            connect(ui->actionResultTable, &QAction::triggered, this,
                    &MainWindow::onOpenResultTable);
        }


            
            

                

                

        
        // 初始化动作
        VFP_DEBUG << "Calling initActions";
        initActions();
        VFP_DEBUG << "initActions completed";
        
        // 初始化界面组件
        VFP_DEBUG << "Checking toolLibrary";
        if (ui->toolLibrary) {
            VFP_DEBUG << "Calling setupToolLibrary";
            setupToolLibrary();
            VFP_DEBUG << "setupToolLibrary completed";
        }
        
        VFP_DEBUG << "Calling setupFlowEditor";
        setupFlowEditor();
        VFP_DEBUG << "setupFlowEditor completed";
        
        VFP_DEBUG << "Calling setupRightPanel";
        setupRightPanel();
        VFP_DEBUG << "setupRightPanel completed";
        
        VFP_DEBUG << "Checking language menu";
        if (ui->menuLanguage && ui->actionChinese && ui->actionEnglish) {
            VFP_DEBUG << "Calling setupLanguageMenu";
            setupLanguageMenu();
            VFP_DEBUG << "setupLanguageMenu completed";
        }
        
        VFP_DEBUG << "Calling setupSchemeMenu";
        setupSchemeMenu();
        VFP_DEBUG << "setupSchemeMenu completed";

        VFP_DEBUG << "Calling setupCommunicationMenu";
        setupCommunicationMenu();
        VFP_DEBUG << "setupCommunicationMenu completed";

        VFP_DEBUG << "Calling setupSystemMenu";
        setupSystemMenu();
        VFP_DEBUG << "setupSystemMenu completed";
        
        // 设置默认语言为中文
        VFP_DEBUG << "Setting default language";
        if (ui->actionChinese && ui->actionEnglish) {
            m_currentLanguage = Language::Chinese;
            ui->actionChinese->setChecked(true);
            ui->actionEnglish->setChecked(false);
            VFP_DEBUG << "Calling updateLanguage";
            updateLanguage();
            VFP_DEBUG << "updateLanguage completed";
        }
        
        // 设置默认语言为中文
        VFP_DEBUG << "Setting default language";
        createNewFlow();
        VFP_DEBUG << "createNewFlow completed";

        // 自动保存/崩溃恢复：须在 createNewFlow 之后（首个流程已存在，序列化才有意义）。
        // 恢复询问延到窗口显示之后（见 showEvent → checkRecoveryOnStartup），
        // 否则构造期的模态框会挡住登录框。
        initCrashRecovery();
        initReportAutoExport();
        
        // Phase 4: 弹出登录对话框
        VFP_DEBUG << "Showing login dialog";
        {
            UserLoginDialog loginDialog(this);
            if (loginDialog.exec() == QDialog::Accepted) {
                QString user = loginDialog.loggedInUser();
                QString role = loginDialog.loggedInRole();
                SessionManager::instance()->login(user, role);
                VFP_DEBUG << "User logged in:" << user << "Role:" << role;
                setWindowTitle(QStringLiteral("视觉方案工作台 - %1 [%2]").arg(user, role));
                applyPermissionRestrictions();

                // 安全提示：出厂默认口令 admin/admin 若未修改，等于系统无鉴权。
                // 用一次校验代替直接读哈希（AppDatabase 不对外暴露 password_hash），
                // 校验通过即说明口令仍是默认值。此处只告警不阻断，避免影响现场既有流程。
                if (AppDatabase::instance()->authenticateUser(QStringLiteral("admin"),
                                                             QStringLiteral("admin"))) {
                    QMessageBox::warning(
                        this, tr("安全提示"),
                        tr("管理员账户 admin 仍在使用出厂默认口令，任何人都可登录并修改方案。\n\n"
                           "请通过「系统 → 用户管理」立即修改密码。"));
                }
            } else {
                // 取消登录则默认以 Operator 身份进入
                SessionManager::instance()->login(QStringLiteral("guest"), QStringLiteral("Operator"));
                applyPermissionRestrictions();
            }
        }
        VFP_DEBUG << "Login dialog completed";
        
        // 显示窗口
        VFP_DEBUG << "Showing MainWindow";
        show();
        VFP_DEBUG << "MainWindow shown";
        
        VFP_DEBUG << "MainWindow created successfully";
    } catch (const std::exception &e) {
        VFP_DEBUG << "Exception in MainWindow constructor:" << e.what();
    } catch (...) {
        VFP_DEBUG << "Unknown exception in MainWindow constructor";
    }
}

MainWindow::~MainWindow()
{
    // 停止并释放**全部**流程执行器（含当前激活的 m_executor）。
    // 顺序要求：必须在 qDeleteAll(m_flowScenes) 之前完成——历史缺陷：m_executor
    // 在场景删除之后才被 delete，且非激活执行器只等 1 秒就删；等待超时后
    // QThread 析构会 terminate() 强杀线程，而紧接的场景删除又会让仍在执行的
    // 线程访问已释放节点（use-after-free + 强杀线程叠加）。
    // 安全回收**全部**流程执行器（含当前激活的 m_executor）：先快照再逐个 retireExecutor，
    // 杜绝等待超时后 terminate() 硬杀线程（可能卡在 HALCON/SQLite/锁上，叠加场景析构=UAF）。
    QList<FlowExecutor *> allExecutors;
    for (auto it = m_flowExecutors.constBegin(); it != m_flowExecutors.constEnd(); ++it) {
        if (it.value())
            allExecutors.append(it.value());
    }
    m_flowExecutors.clear();
    m_executor = nullptr;   // 已随列表摘除，置空避免悬垂
    for (FlowExecutor *ex : qAsConst(allExecutors))
        retireExecutor(ex);
    delete ui;
    qDeleteAll(m_flowScenes);
    m_flowScenes.clear();
    delete m_imageView;
}

// 多流程并发：获取场景专属执行器（不存在则创建并连接轻量信号）
FlowExecutor *MainWindow::executorForScene(FlowScene *scene)
{
    auto it = m_flowExecutors.constFind(scene);
    if (it != m_flowExecutors.cend())
        return it.value();
    // 首个流程复用初始执行器（其完整信号已在构造函数连接）
    FlowExecutor *reuse = m_flowExecutors.value(nullptr);
    if (reuse && reuse->flowScene() == nullptr) {
        m_flowExecutors.remove(nullptr);
        m_flowExecutors.insert(scene, reuse);
        return reuse;
    }
    FlowExecutor *ex = new FlowExecutor(this);
    connectExecutorSignals(ex);
    m_flowExecutors.insert(scene, ex);
    return ex;
}

void MainWindow::retireExecutor(FlowExecutor *ex)
{
    if (!ex)
        return;
    // 先从注册表摘除：避免析构遍历二次处理，也清掉指向即将失效场景的悬垂键。
    m_flowExecutors.remove(m_flowExecutors.key(ex));
    // 置 Stopped + wakeAll，run() 在中断点（interruptibleSleep / 节点边界）自然退出。
    ex->stopExecution();
    if (!ex->isRunning() || ex->wait(15000)) {
        delete ex;
        return;
    }
    // 超时仍未退出：绝不 delete（QThread 析构会 terminate() 硬杀，可能卡在 HALCON 上下文 /
    // SQLite 线程连接 / 持有锁，比一个随进程退出被 OS 回收的线程更危险）。
    // 解除父子 + 断所有信号（避免执行器回调 MainWindow 已失效槽），挂 finished→deleteLater 自删。
    VFP_DEBUG << "retireExecutor: 执行器超时未退出，放弃强杀，转 finished 自删";
    ex->disconnect();
    ex->setParent(nullptr);
    QObject::connect(ex, &QThread::finished, ex, &QObject::deleteLater);
}

// 多流程并发：**唯一**的执行器信号连接点（N3 富反馈统一）。首执行器（构造函数）与
// executorForScene 新建执行器都走这里，规则完全一致：
//  · 全局 UI（状态栏 / 结果表 / 变量面板 / 最近变量缓存 / 图像显示 / 运行界面 / 参数面板）
//    只接受"当前激活执行器"（ex == m_executor）的推送——否则后台跑首流程会污染状态栏与结果表；
//  · 流程域反馈（日志 + 节点着色）不设门槛，按执行器自身场景（ex->flowScene()）更新，
//    保证后台流程也有逐节点反馈（旧实现后台执行器完全没有逐节点反馈）。
void MainWindow::connectExecutorSignals(FlowExecutor *ex)
{
    if (!ex) return;

    // ── 全局 UI：运行状态 → 状态栏（状态/耗时/触发计数） ──
    connect(ex, &FlowExecutor::executionStarted, this, [this, ex]() {
        if (ex == m_executor) onExecutionStarted();
    });
    connect(ex, &FlowExecutor::executionStopped, this, [this, ex]() {
        if (ex == m_executor) onExecutionStopped();
    });
    connect(ex, &FlowExecutor::executionFinished, this, [this, ex]() {
        if (ex == m_executor) onExecutionFinished();
    });
    // 良率目标报警：**不按"当前激活流程"过滤**——后台流程的轮次同样要参与判定，
    // 否则多流程并发时报警会漏（哪条流程超差都得报）。节流在 checkYieldTarget 内部。
    connect(ex, &FlowExecutor::executionFinished, this, [this, ex]() {
        checkYieldTarget(ex);
    });

    // ── 图编辑锁（S1 / Phase B 小步）：按执行器自身场景更新，不按"当前激活流程"——
    //    后台流程暂停停稳/恢复时若漏更新，就会出现"后台正在跑却可编辑"的漏洞。
    //    · executionParked：worker 真停进等待点（当前节点已跑完）→ 解锁，可安全改图
    //    · executionResumed：恢复执行 → 立即加锁（否则暂停中改完图点继续，运行中仍是可编辑）
    connect(ex, &FlowExecutor::executionParked, this, [ex]() {
        if (FlowScene *s = ex->flowScene())
            s->setEditLocked(!ex->allowsGraphEditing());
    });
    connect(ex, &FlowExecutor::executionResumed, this, [ex]() {
        if (FlowScene *s = ex->flowScene())
            s->setEditLocked(!ex->allowsGraphEditing());
    });
    // 运行控制的状态反馈：暂停请求已受理 / 真停稳 / 已继续（ex == m_executor 门槛，避免后台流程
    // 污染当前状态栏与按钮态）
    connect(ex, &FlowExecutor::executionPaused, this, [this, ex]() {
        if (ex == m_executor && m_execStatus) m_execStatus->onPaused();
    });
    connect(ex, &FlowExecutor::executionParked, this, [this, ex]() {
        if (ex == m_executor && m_execStatus) m_execStatus->onParked();
    });
    connect(ex, &FlowExecutor::executionResumed, this, [this, ex]() {
        if (ex == m_executor && m_execStatus) m_execStatus->onResumed();
    });
    connect(ex, &FlowExecutor::executionError, this, [this, ex](const QString &err) {
        if (ex == m_executor) onExecutionError(err);
        else logMessage(QStringLiteral("流程错误: %1").arg(err));
    });

    // ── 全局 UI：运行模式变更 → 刷新所有场景 MVS 节点的像素格式可用性 ──
    // （⑤a：旧实现在构造函数只内联连首执行器，新/后台流程的模式切换不刷新；
    // 统一在此连接，按 ex == m_executor 门槛，保证仅当前激活流程刷新 UI。）
    connect(ex, &FlowExecutor::flowModeChanged, this, [this, ex](int mode) {
        Q_UNUSED(mode)
        if (ex != m_executor)
            return;
        for (FlowScene *scene : m_flowScenes) {
            for (NodeBase *node : scene->nodes()) {
                if (auto *mvs = qobject_cast<MvsImageSourceNode *>(node))
                    mvs->refreshPixelFormatEnabled();
            }
        }
    });

    // ── 全局 UI：跳过三态（未激活分支 / 循环体调度的节点标"跳过"） ──
    connect(ex, &FlowExecutor::nodeSkipped, this,
            [this, ex](NodeBase *node, const QString &reason) {
        Q_UNUSED(reason)
        if (ex != m_executor)
            return;
        auto *rt = m_auxPanels ? m_auxPanels->resultTablePanel() : nullptr;
        if (!node || !rt)
            return;
        rt->setModuleSkipped(node->moduleId(), node->fullName());
    });

    // ── 全局 UI：结果数据表 / 变量面板（节点执行后把该模块输出推到面板） ──
    connect(ex, &FlowExecutor::nodeOutputsUpdated, this,
            [this, ex](NodeBase *node, bool ok, qint64 elapsedMs, const QVariantMap &vars) {
        if (ex != m_executor || !node)
            return;
        if (auto *rt = m_auxPanels ? m_auxPanels->resultTablePanel() : nullptr) {
            rt->setModuleResult(node->moduleId(), node->fullName(), ok, elapsedMs, vars);
        }
        // 供「变量引用」菜单构造引用列表（UI 线程缓存，不读执行线程内部状态）
        m_lastModuleVars.insert(node->moduleId(), vars);
        // 变量面板只列可引用的值：失败节点本轮没有可引用输出
        if (auto *vp = m_auxPanels ? m_auxPanels->variablePanel() : nullptr) {
            if (ok)
                vp->setModuleVars(node->moduleId(), node->fullName(), vars);
        }
    });

    // ── 全局 UI：末端图像 → 运行界面 + 图像显示 ──
    connect(ex, &FlowExecutor::imageReady, this,
            [this, ex](NodeBase *node, const HalconCpp::HImage &image) {
        if (ex != m_executor || !node)
            return;
        // 运行界面图像控件转发（不受用户显示选择影响）
        if (m_runtimeView)
            m_runtimeView->pushImage(node->fullName(), image);
        // 如果用户已通过下拉框或画布选择了一个算子，imageReady 不覆盖
        NodeBase *target = resolveDisplayNode();
        if (target != node && target != nullptr) {
            VFP_DEBUG << "用户已选择显示:" << target->fullName() << "，imageReady不覆盖";
            return;
        }
        // 兜底：自动显示最后一个节点的图像（信号负载的图像 + 该节点的叠加图元）
        if (m_imageDisplay)
            m_imageDisplay->showImage(image, node->fullName(), node);
    });

    // ── 全局 UI：任意节点图像 → 运行界面按节点名推送（绑定中间节点控件可实时显示） ──
    connect(ex, &FlowExecutor::imageAvailable, this,
            [this, ex](NodeBase *node, const HalconCpp::HImage &image) {
        if (ex != m_executor || !node)
            return;
        if (m_runtimeView)
            m_runtimeView->pushImage(node->fullName(), image);
    });

    // ── 逐节点反馈 ──
    connect(ex, &FlowExecutor::nodeExecuted, this, [this, ex](NodeBase *node, bool success) {
        // 流程域：日志（后台流程同样输出）
        logMessage(QStringLiteral("节点执行结果: %1").arg(success ? QStringLiteral("成功")
                                                                 : QStringLiteral("失败")));
        if (!node)
            return;
        // 流程域：节点着色按执行器自身场景（旧实现只看当前标签页场景 → 后台流程无反馈/串场景）
        if (FlowScene *scene = ex->flowScene()) {
            if (NodeGraphicsItem *item = scene->getGraphicsItemForNode(node))
                item->update();
        }

        // 全局 UI 部分只在"失败为假 且 当前激活流程"时更新
        if (!success || ex != m_executor)
            return;

        // 参数面板：显示最新输出参数（选中节点）
        if (m_selectedNode) {
            QWidget *parameterPanel = ui->paramDockContents;
            if (parameterPanel) {
                // 查找参数面板中的所有子控件（输出页 + 输入页随运行刷新）
                const QList<QWidget*> childWidgets = parameterPanel->findChildren<QWidget*>();
                for (QWidget *childWidget : childWidgets) {
                    QTabWidget *tabWidget = qobject_cast<QTabWidget*>(childWidget);
                    if (!tabWidget)
                        continue;
                    if (QWidget *outputTab = tabWidget->findChild<QWidget*>("outputTab"))
                        m_selectedNode->updateParamPanel(outputTab);
                    // 输入面板随运行刷新（图像源信息等）
                    if (QWidget *inputTab = tabWidget->findChild<QWidget*>("inputTab"))
                        m_selectedNode->updateParamPanel(inputTab);
                }
            }
        }

        // 图像显示：核心规则 = 下拉框选择 > 画布选中 > 当前执行节点
        NodeBase *targetDisplayNode = resolveDisplayNode(node);
        if (targetDisplayNode && m_imageView) {
            QSharedPointer<DataObject> outputData = targetDisplayNode->getOutputData(0);
            if (outputData) {
                const HImage image = outputData->getHImage();
                if (image.IsInitialized()) {
                    m_imageView->setImage(image, targetDisplayNode->fullName());
                    if (m_imageSourceLabel) {
                        m_imageSourceLabel->setText(
                            QStringLiteral("图像来源: %1").arg(targetDisplayNode->fullName()));
                    }
                }
            }
        }

        // 运行界面：转发节点输出值（数值显示 / 状态灯 / IO状态）
        if (m_runtimeView) {
            // 全端口输出映射：端口0 走 updateNodeOutput（数值/状态灯）；
            // 全部端口按名走 updateNodePortMap（IO状态控件消费 成功/值/错误）
            QVariantMap portMap;
            const QList<Port *> outPorts = node->outputPorts();
            for (int pi = 0; pi < outPorts.size(); ++pi) {
                QSharedPointer<DataObject> out = node->getOutputData(pi);
                if (!out) continue;
                QVariant v;
                switch (out->getType()) {
                case DataObject::DataType::Number:
                case DataObject::DataType::String:
                case DataObject::DataType::Bool:
                case DataObject::DataType::Array:
                    v = out->getData();
                    break;
                case DataObject::DataType::Measure: {
                    const MeasureResult mr = out->getMeasureResult();
                    if (mr.valid) v = mr.value;
                    break;
                }
                case DataObject::DataType::Point: {
                    const QPointF p = out->getPoint();
                    v = QStringLiteral("(%1, %2)").arg(p.x(), 0, 'f', 3).arg(p.y(), 0, 'f', 3);
                    break;
                }
                default:
                    break;
                }
                if (v.isValid()) {
                    if (outPorts[pi])
                        portMap.insert(outPorts[pi]->name(), v);
                    if (pi == 0)
                        m_runtimeView->updateNodeOutput(node->fullName(), v);
                }
            }
            if (!portMap.isEmpty())
                m_runtimeView->updateNodePortMap(node->fullName(), portMap);
        }
    });
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 未保存确认必须最先做：取消关闭时不得已经产生副作用（清恢复现场/停相机/落面板配置）
    if (!confirmCloseWithUnsavedChanges()) {
        VFP_DEBUG << "closeEvent: 用户在\"未保存修改\"确认中选择取消，本次关闭被忽略";
        event->ignore();
        return;
    }

    // 关闭路径留痕：现场反馈过"关了软件但进程还在"，日志里没有本条即说明根本没走到正常关闭
    // （例如进程停在登录框/无窗口状态），据此可快速区分"没关掉"与"关掉后没退干净"。
    VFP_DEBUG << "closeEvent: 关闭主窗口，保存面板显隐并清理相机…";
    // 记住辅助面板显隐，下次启动时恢复（避免每次重启都要重新打开结果表/变量面板）
    saveAuxPanelVisibility();
    cleanupCameras();
    QMainWindow::closeEvent(event);
}

void MainWindow::cleanupCameras()
{
    VFP_DEBUG << "正在关闭所有相机...";

    // 1. 停止**全部**流程执行（释放相机采集线程）
    //    原实现只停 m_executor：多流程并发时其它 FlowExecutor 仍在
    //    MvsImageSourceNode::grabImage 的 MV_CC_GetOneFrameTimeout 中阻塞取流，
    //    随后第 3 步直接 DestroyHandle 会造成 use-after-free。
    QList<FlowExecutor *> executors;
    for (FlowExecutor *ex : m_flowExecutors) {
        if (ex && !executors.contains(ex))
            executors.append(ex);
    }
    if (m_executor && !executors.contains(m_executor))
        executors.append(m_executor);

    for (FlowExecutor *ex : executors)
        ex->stopExecution();
    for (FlowExecutor *ex : executors) {
        if (ex->isRunning())
            ex->wait(3000);
    }

    // 2. 关闭所有 MVS 图像源节点的本地相机（按顺序清理每个 flow scene）
    for (FlowScene *scene : m_flowScenes) {
        if (!scene) continue;
        for (NodeBase *node : scene->nodes()) {
            auto *mvs = qobject_cast<MvsImageSourceNode*>(node);
            if (mvs) {
                // 标记该节点的全局相机为未使用（由节点自行 closeCamera）
                mvs->closeCamera();
            }
        }
    }

    // 3. 关闭所有 GlobalCameraManager 中打开的全局相机
    GlobalCameraManager *gcm = GlobalCameraManager::instance();
    // 获取所有已注册的全局相机名称
    QMap<QString, GlobalCameraManager::Camera> allCameras = gcm->cameras();
    for (auto it = allCameras.begin(); it != allCameras.end(); ++it) {
        const QString &cameraName = it.key();
        if (gcm->isCameraOpen(cameraName)) {
            gcm->closeCamera(cameraName);
        }
    }

    VFP_DEBUG << "所有相机已关闭";
}

void MainWindow::initActions()
{
    // 创建新方案
    connect(ui->actionNew, &QAction::triggered, this, &MainWindow::onNewProject);

    // 快捷键体系（对标 VM 4.4 常用操作）
    ui->actionNew->setShortcut(QKeySequence::New);                    // Ctrl+N
    ui->actionOpenScheme->setShortcut(QKeySequence::Open);            // Ctrl+O
    ui->actionSaveScheme->setShortcut(QKeySequence::Save);            // Ctrl+S
    ui->actionStartExecution->setShortcut(QKeySequence(Qt::Key_F5));  // F5 运行
    ui->actionStopExecution->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F5)); // Shift+F5 停止

    // 添加/删除流程：Ctrl+T 快捷键 + 流程标签栏右上角「＋」按钮。
    // 此前入口只有「编辑」菜单里的英文项 "Add Flow"，中文界面下很难发现。
    ui->actionAddFlow->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
    ui->actionDeleteFlow->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+T")));
    {
        auto *addFlowBtn = new QToolButton(ui->flowTabs);
        addFlowBtn->setText(QStringLiteral("＋ 新流程"));
        addFlowBtn->setToolTip(QStringLiteral("添加新流程（Ctrl+T）"));
        addFlowBtn->setAutoRaise(true);
        connect(addFlowBtn, &QToolButton::clicked, this, &MainWindow::onAddFlowTab);
        ui->flowTabs->setCornerWidget(addFlowBtn, Qt::TopRightCorner);
    }

    // 撤销/重做（Ctrl+Z / Ctrl+Y，作用于当前流程标签页）
    auto currentScene = [this]() -> FlowScene * {
        const int idx = ui->flowTabs->currentIndex();
        if (idx >= 0 && idx < m_flowScenes.size()) return m_flowScenes[idx];
        return nullptr;
    };
    auto *undoShortcut = new QShortcut(QKeySequence::Undo, this);
    connect(undoShortcut, &QShortcut::activated, this, [currentScene]() {
        if (FlowScene *s = currentScene()) s->undo();
    });
    auto *redoShortcut = new QShortcut(QKeySequence::Redo, this);
    connect(redoShortcut, &QShortcut::activated, this, [currentScene]() {
        if (FlowScene *s = currentScene()) s->redo();
    });
    auto *deleteShortcut = new QShortcut(QKeySequence::Delete, this);
    connect(deleteShortcut, &QShortcut::activated, this, [currentScene]() {
        if (FlowScene *s = currentScene()) s->deleteSelectedItems();
    });

    // 算子分组（FR1.9）：Ctrl+G 创建 / Ctrl+Shift+G 解散。
    // 入口同时进「编辑」菜单：只在右键菜单里放，会重蹈"Add Flow 藏在英文菜单里没人发现"的覆辙。
    m_actionCreateGroup = new QAction(QStringLiteral("创建分组"), this);
    m_actionCreateGroup->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
    m_actionCreateGroup->setStatusTip(QStringLiteral("把当前选中的算子框成一组（纯视觉容器，不影响执行）"));
    m_actionDissolveGroup = new QAction(QStringLiteral("解散分组"), this);
    m_actionDissolveGroup->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+G")));
    m_actionDissolveGroup->setStatusTip(QStringLiteral("删除选中的分组框，组内算子保留"));
    // 折叠/展开（FR1.9 的"Group 折叠"）：纯显示动作，不改变执行；双击分组标题栏是同一效果。
    // 不给快捷键：折叠没有公认的通用键位，硬塞一个反而抢键。
    m_actionToggleGroup = new QAction(QStringLiteral("折叠/展开分组"), this);
    m_actionToggleGroup->setStatusTip(QStringLiteral("把选中的分组折叠成一条标题栏（纯显示，不影响执行）"));

    // 子图复用（FR15.10 的设计期半步）：复制/粘贴选中子图 + 片段文件导入导出。
    // 快捷键用 WidgetShortcut（**只在主窗口自身有焦点时**生效）：Ctrl+C/V 不抢文本框的复制粘贴
    // ——画布上的 Ctrl+C/V 由 FlowScene 的按键处理发出请求（见 hookFlowScene）。
    m_actionCopySnippet = new QAction(QStringLiteral("复制所选算子"), this);
    m_actionCopySnippet->setShortcut(QKeySequence::Copy);
    m_actionCopySnippet->setShortcutContext(Qt::WidgetShortcut);
    m_actionPasteSnippet = new QAction(QStringLiteral("粘贴"), this);
    m_actionPasteSnippet->setShortcut(QKeySequence::Paste);
    m_actionPasteSnippet->setShortcutContext(Qt::WidgetShortcut);
    m_actionExportSnippet = new QAction(QStringLiteral("导出片段…"), this);
    m_actionExportSnippet->setStatusTip(QStringLiteral("把选中的算子（含内部连线）导出为片段文件，供别的方案插入"));
    m_actionImportSnippet = new QAction(QStringLiteral("导入片段…"), this);
    m_actionImportSnippet->setStatusTip(QStringLiteral("把片段文件插入到当前流程（放在当前视图中心）"));
    if (ui->menuEdit) {
        QAction *beforeSnippet = ui->actionDeleteFlow;
        ui->menuEdit->insertAction(beforeSnippet, m_actionCopySnippet);
        ui->menuEdit->insertAction(beforeSnippet, m_actionPasteSnippet);
        ui->menuEdit->insertAction(beforeSnippet, m_actionExportSnippet);
        ui->menuEdit->insertAction(beforeSnippet, m_actionImportSnippet);
        ui->menuEdit->insertSeparator(beforeSnippet);
    }
    connect(m_actionCopySnippet, &QAction::triggered, this,
            [this]() { copySelectionToClipboard(currentFlowScene()); });
    connect(m_actionPasteSnippet, &QAction::triggered, this,
            [this]() { pasteSnippetIntoScene(currentFlowScene()); });
    connect(m_actionExportSnippet, &QAction::triggered, this, &MainWindow::onExportSnippet);
    connect(m_actionImportSnippet, &QAction::triggered, this, &MainWindow::onImportSnippet);

    // 加密 / 只读方案导出（G-P1-10）：文件菜单下追加入口（此段在 m_recentFiles 之前执行，
    // 故动作落在"最近文件"子菜单之前，顺序合理）
    m_actionExportEncrypted = new QAction(QStringLiteral("导出加密/只读方案…"), this);
    m_actionExportEncrypted->setStatusTip(QStringLiteral(
        "把当前方案加密并可选只读打包，分发后现场无法反编译/覆盖保存"));
    if (ui->menuFile)
        ui->menuFile->addAction(m_actionExportEncrypted);
    connect(m_actionExportEncrypted, &QAction::triggered,
            this, &MainWindow::onExportEncryptedProject);
    if (ui->menuEdit) {
        QAction *before = ui->actionDeleteFlow;
        ui->menuEdit->insertAction(before, m_actionCreateGroup);
        ui->menuEdit->insertAction(before, m_actionDissolveGroup);
        ui->menuEdit->insertAction(before, m_actionToggleGroup);
        ui->menuEdit->insertSeparator(before);
    }
    connect(m_actionCreateGroup, &QAction::triggered, this, &MainWindow::onCreateGroup);
    connect(m_actionDissolveGroup, &QAction::triggered, this, &MainWindow::onDissolveGroup);
    connect(m_actionToggleGroup, &QAction::triggered, this, &MainWindow::onToggleGroupCollapse);

    // 执行控制
    connect(ui->actionStartExecution, &QAction::triggered, this, &MainWindow::onStartExecution);
    connect(ui->actionStopExecution, &QAction::triggered, this, &MainWindow::onStopExecution);

    // 最近打开菜单（文件菜单下）：记录/去重/截断/清空全部由 RecentFilesMenu 负责，
    // 点击某条记录回调 loadProjectFile（与原实现一致）
    m_recentFiles = new RecentFilesMenu(ui->menuFile,
                                        [this](const QString &path) { loadProjectFile(path); },
                                        QString(), this);

    // 帮助菜单（软件内查看操作手册）
    {
        QMenu *helpMenu = new QMenu(QStringLiteral("帮助(&H)"), this);
        ui->menuBar->addMenu(helpMenu);
        QAction *manualAct = helpMenu->addAction(QStringLiteral("使用手册(&M)"));
        manualAct->setShortcut(QKeySequence(Qt::Key_F1));
        connect(manualAct, &QAction::triggered, this, [this]() { openManualDialog(); });
        QAction *envCheckAct = helpMenu->addAction(QStringLiteral("HALCON 图像层自检"));
        connect(envCheckAct, &QAction::triggered, this, [this]() { runHalconEnvCheck(); });
        helpMenu->addSeparator();
        QAction *aboutAct = helpMenu->addAction(QStringLiteral("关于"));
        connect(aboutAct, &QAction::triggered, this, [this]() {
            QMessageBox::about(this, QStringLiteral("关于 VisionFlowPlatform"),
                               QStringLiteral("VisionFlowPlatform 1.0\n"
                                              "对标 VisionMaster 4.4 的视觉流程开发平台\n"
                                              "算法：OpenCV\n"
                                              "图容器 / DeepOCR：HALCON 24.11"));
        });
    }
}

void MainWindow::hookFlowScene(FlowScene *scene)
{
    if (!scene) {
        return;
    }
    connect(scene, &FlowScene::nodeSelected, this, &MainWindow::onNodeSelected);
    connect(scene, &FlowScene::nodeAdded, this, &MainWindow::onNodeAdded);
    // 画布聚焦时的 Ctrl+C / Ctrl+V：场景只发请求，剪贴板与粘贴位置都归窗口管
    // （参数框/日志里的 Ctrl+C 因此仍是原生文本复制——那些控件不经过场景）
    connect(scene, &FlowScene::copySelectionRequested, this, [this, scene]() {
        copySelectionToClipboard(scene);
    });
    connect(scene, &FlowScene::pasteRequested, this, [this, scene]() {
        pasteSnippetIntoScene(scene);
    });
    connect(scene, &FlowScene::connectionRejected, this, [this](const QString &reason) {
        logMessage(reason);
    });

    connect(scene, &FlowScene::nodeHelpRequested, this, [this](NodeBase *node) {
        if (!m_helpViewer)
            m_helpViewer = new HelpViewer(this);
        m_helpViewer->showHelpForNode(node);
    });
    connect(scene, &FlowScene::nodeOutputDataRequested, this, [this](NodeBase *node) {
        onOpenOutputViewer();
        if (auto *ov = m_auxPanels->outputDataViewer())
            ov->setNode(node);
    });
    // 右键「重算此算子及下游」：调试时只重跑这一段，不必整图重跑
    connect(scene, &FlowScene::recomputeFromRequested, this, [this](NodeBase *node) {
        recomputeDownstream(node);
    });
    connect(scene, &FlowScene::executeToHereRequested, this, [this](NodeBase *node) {
        int idx = ui->flowTabs->currentIndex();
        if (idx < 0 || idx >= m_flowScenes.size())
            return;
        FlowExecutor *ex = executorForScene(m_flowScenes[idx]);
        if (ex)
            runWithBusyFeedback(QStringLiteral("正在执行到 %1…").arg(node->fullName()), false,
                                [ex, node]() { ex->executeUpTo(node); });
    });
    connect(scene, &FlowScene::executeFromHereRequested, this, [this](NodeBase *node) {
        int idx = ui->flowTabs->currentIndex();
        if (idx < 0 || idx >= m_flowScenes.size())
            return;
        FlowExecutor *ex = executorForScene(m_flowScenes[idx]);
        if (ex)
            runWithBusyFeedback(QStringLiteral("正在从此处执行 %1…").arg(node->fullName()), false,
                                [ex, node]() { ex->executeFrom(node); });
    });
    connect(scene, &FlowScene::nodeEditRequested, this, &MainWindow::openModuleEditor);
}

void MainWindow::createNewFlow()
{
    try {
        FlowScene *scene = new FlowScene(this);
        if (!scene) {
            VFP_DEBUG << "Failed to create FlowScene";
            return;
        }
        
        hookFlowScene(scene);

        m_flowScenes.append(scene);
        
        // 添加到标签页
        // 注意：FlowScene继承自QGraphicsScene，需要使用views()获取QGraphicsView
        QGraphicsView *view = new QGraphicsView(scene);
        VisionWorkbenchStyle::applyGraphicsViewWorkbenchDefaults(view);
        view->setDragMode(QGraphicsView::RubberBandDrag);
        view->setRubberBandSelectionMode(Qt::IntersectsItemShape);
        view->setFocusPolicy(Qt::StrongFocus);
        // 默认视图定位到左上角（延迟到布局完成后执行）
        QTimer::singleShot(0, this, [view]() {
            view->centerOn(0, 0);
        });
        // 分配全局唯一流程名：扫描 GTM 已注册名，返回首个空闲"流程 N"。避免删除流程后
        // 用 m_flowScenes.size() 复用序号 → 同名覆盖仍存流程的触发路由（L2 存量）。
        const QString flowName = GlobalTriggerManager::instance()->allocFlowName();
        scene->setFlowName(flowName);   // S2：写入场景，保存方案时随流程持久化（载入按名复原）

        if (ui && ui->flowTabs) {
            int index = ui->flowTabs->addTab(view, flowName);
            // 初始标签页标题带默认模式后缀（与 GTM 路由名一致）
            ui->flowTabs->setTabText(index, flowName + QStringLiteral(" [软触发]"));
            ui->flowTabs->setCurrentIndex(index);
        } else {
            VFP_DEBUG << "ui or ui->flowTabs is null";
            delete view;
        }

        // 设置执行器的流程场景（多流程并发：每个流程独立执行器）
        FlowExecutor *flowEx = executorForScene(scene);
        m_executor = flowEx;
        if (flowEx) {
            flowEx->setFlowScene(scene);
            // 注册流程名到全局触发管理器（与 tab 标题同名，触发按名路由）
            flowEx->setFlowName(flowName);
            GlobalTriggerManager::instance()->registerFlow(flowName, scene, flowEx);
        }
    } catch (const std::exception &e) {
        VFP_DEBUG << "Exception in createNewFlow:" << e.what();
    } catch (...) {
        VFP_DEBUG << "Unknown exception in createNewFlow";
    }
}

void MainWindow::addNode(const QString &nodeType)
{
    try {
        if (!ui || !ui->flowTabs) {
            VFP_DEBUG << "ui or ui->flowTabs is null";
            return;
        }
        
        int currentIndex = ui->flowTabs->currentIndex();
        if (currentIndex < 0 || currentIndex >= m_flowScenes.size()) {
            return;
        }
        
        FlowScene *scene = m_flowScenes[currentIndex];
        if (!scene) {
            VFP_DEBUG << "scene is null";
            return;
        }
        
        NodeBase *node = nullptr;

        const QPointF pos(100, 100);
        NodeBase::NodeType nt = NodeBase::IMAGE_ACQUISITION;
        QString createName;
        if (NodeFactory::resolvePaletteToolId(nodeType, nt, createName)) {
            node = scene->createNode(nt, pos, createName);
        }

        if (node) {
            // 节点已创建，可以在这里添加额外的初始化代码
            VFP_DEBUG << "Node created:" << node->name();
        }
    } catch (const std::exception &e) {
        VFP_DEBUG << "Exception in addNode:" << e.what();
    } catch (...) {
        VFP_DEBUG << "Unknown exception in addNode";
    }
}

void MainWindow::onNodeSelected(NodeBase *node)
{
    try {
        VFP_DEBUG << "onNodeSelected called";
        m_selectedNode = node;
        
        if (node) {
            // 选中算子即显示它的中间结果图（对标 VisionMaster 的操作性：点哪个模块就看哪个
            // 模块的图，而不用先去执行它）。数据直接取自节点输出端口，无需改执行引擎；
            // 尚未执行、没有可用图像时保持当前画面不动，避免把已有画面清空。
            m_imageDisplay->displayNodeOutput(node);

            // 测量节点画布取点：连接 ROI 按钮信号（断开旧连接防重复）
            if (m_roiPickConn) {
                QObject::disconnect(m_roiPickConn);
                m_roiPickConn = QMetaObject::Connection();
            }
            if (auto *fl = qobject_cast<FindLineNode *>(node)) {
                m_roiPickConn = connect(fl, &FindLineNode::roiPickRequested, this,
                                        [this, fl]() { startRoiPick(fl); });
            } else if (auto *fc = qobject_cast<FindCircleNode *>(node)) {
                m_roiPickConn = connect(fc, &FindCircleNode::roiPickRequested, this,
                                        [this, fc]() { startRoiPick(fc); });
            } else if (auto *cc = qobject_cast<CaliperMeasureNode *>(node)) {
                m_roiPickConn = connect(cc, &CaliperMeasureNode::roiPickRequested, this,
                                        [this, cc]() { startRoiPick(cc); });
            }

            // 延迟创建参数面板，避免阻塞主线程
            QTimer::singleShot(50, this, [=]() {
                showNodeParameters(node);
            });
            
            // 显示图像前检查优先级规则：下拉框选择 > 画布选中
            if (m_imageView) {
                QTimer::singleShot(100, this, [=]() {
                    // resolveDisplayNode(node) 只检查下拉框选择，不回退到 m_selectedNode
                    // 因为此处我们本身就是因画布选中触发的，如果下拉框无选择才显示本节点
                    NodeBase *target = resolveDisplayNode(node);
                    if (!target)
                        return;
                    if (auto outputData = target->getOutputData(0)) {
                        const HImage image = outputData->getHImage();
                        // 与原实现一致：此路径只换图像与来源标签，不动叠加图元
                        m_imageDisplay->showImage(image, target->fullName());
                    }
                });
            }
        }
    } catch (const std::exception &e) {
        VFP_DEBUG << "Exception in onNodeSelected:" << e.what();
    }
}

void MainWindow::showNodeParameters(NodeBase *node)
{
    QWidget *paramWidget = ui->paramDockContents;
    if (!paramWidget)
        return;

    // 清空旧内容（含未选中时的占位）
    if (QLayout *old = paramWidget->layout()) {
        QLayoutItem *item = nullptr;
        while ((item = old->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }
        delete old;
    }

    auto *box = new QVBoxLayout(paramWidget);
    box->setContentsMargins(8, 8, 8, 8);

    if (!node) {
        // FR4.3：参数页只显示「当前选中算子」的参数。未选中时给出明确占位，
        // 避免残留上一个算子的参数被误读为当前配置。
        auto *placeholder = new QLabel(QStringLiteral("请选择算子以查看参数"));
        placeholder->setWordWrap(true);
        placeholder->setStyleSheet(QStringLiteral("color:#9a9aa6;"));
        box->addWidget(placeholder);
        box->addStretch();
        return;
    }

    auto *title = new QLabel(QStringLiteral("<b>%1</b>").arg(node->fullName()));
    title->setWordWrap(true);
    box->addWidget(title);

    const QString status = !node->hasExecuted()
        ? QStringLiteral("尚未执行")
        : (node->executionSuccess() ? QStringLiteral("上次执行：成功")
                                    : QStringLiteral("上次执行：失败"));
    box->addWidget(new QLabel(status));

    if (auto *hn = qobject_cast<HalconNode *>(node)) {
        if (hn->geometryRoiType() != RoiType::None)
            box->addWidget(new QLabel(QStringLiteral("双击可在图上编辑 ROI / 模板 / 卡尺")));
        else if (hn->supportsMaskEdit())
            box->addWidget(new QLabel(QStringLiteral("双击可编辑掩膜")));
        else
            box->addWidget(new QLabel(QStringLiteral("双击打开完整参数")));

        int shown = 0;
        for (const ParamSpec &spec : hn->paramSpecs()) {
            if (shown >= 6)
                break;
            const QString label = spec.label.isEmpty() ? spec.name : spec.label;
            box->addWidget(new QLabel(QStringLiteral("%1：%2")
                                          .arg(label, hn->getParam(spec.name).toString())));
            ++shown;
        }
    } else {
        box->addWidget(new QLabel(QStringLiteral("双击打开完整参数")));
    }

    auto *editBtn = new QPushButton(QStringLiteral("打开编辑器"));
    connect(editBtn, &QPushButton::clicked, this, [this, node]() { openModuleEditor(node); });
    box->addWidget(editBtn);

    auto *execBtn = new QPushButton(QStringLiteral("执行算子"));
    connect(execBtn, &QPushButton::clicked, this, [this, node]() { executeNodeOnce(node); });
    box->addWidget(execBtn);

    box->addWidget(new QLabel(m_currentLanguage == Language::Chinese
                                  ? QStringLiteral("图像显示来源:")
                                  : QStringLiteral("Display source:")));
    auto *combo = new QComboBox();
    combo->addItem(node->fullName(), QVariant::fromValue(node));

    const int currentIndex = ui->flowTabs->currentIndex();
    NodeBase *savedSelectedNode = nullptr;
    if (currentIndex >= 0 && currentIndex < m_flowScenes.size()) {
        FlowScene *scene = m_flowScenes[currentIndex];
        if (scene) {
            savedSelectedNode = m_imageDisplay->selectedOutputNode(scene);
            for (NodeBase *up : getUpstreamNodes(node, scene)) {
                if (up && up != node)
                    combo->addItem(up->fullName(), QVariant::fromValue(up));
            }
        }
    }
    if (savedSelectedNode) {
        for (int i = 0; i < combo->count(); ++i) {
            if (combo->itemData(i).value<NodeBase *>() == savedSelectedNode) {
                combo->setCurrentIndex(i);
                break;
            }
        }
    }
    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, combo](int index) {
        NodeBase *selected = combo->itemData(index).value<NodeBase *>();
        if (!selected)
            return;
        const int idx = ui->flowTabs->currentIndex();
        if (idx >= 0 && idx < m_flowScenes.size())
            m_imageDisplay->setSelectedOutputNode(m_flowScenes[idx], selected);
        if (auto outputData = selected->getOutputData(0)) {
            const HImage image = outputData->getHImage();
            // 与原实现一致：此路径只换图像与来源标签，不动叠加图元
            m_imageDisplay->showImage(image, selected->fullName());
        }
    });
    box->addWidget(combo);
    box->addStretch();
}

void MainWindow::openModuleEditor(NodeBase *node)
{
    if (!node)
        return;
    if (ModuleEditorDialog *existing = m_moduleEditors.value(node, nullptr)) {
        existing->reloadFromNode();
        existing->raise();
        existing->activateWindow();
        return;
    }
    auto *dlg = new ModuleEditorDialog(node, this);
    m_moduleEditors.insert(node, dlg);
    connect(dlg, &QObject::destroyed, this, [this, node]() { m_moduleEditors.remove(node); });
    connect(dlg, &ModuleEditorDialog::executeRequested, this, [this, node]() {
        executeNodeOnce(node);
    });
    connect(dlg, &ModuleEditorDialog::recomputeRequested, this, [this, node]() {
        recomputeDownstream(node);
    });
    // 自动重算（改参数/拖 ROI/涂掩膜）：静默执行，流程忙时直接跳过
    connect(dlg, &ModuleEditorDialog::autoRecomputeRequested, this, [this, node]() {
        recomputeDownstream(node, true);
    });
    // 「变量引用」菜单：每次展开时按当前变量生成列表（上游结果 → 下游参数的一键联动）
    dlg->setVariableReferenceProvider([this]() { return buildVariableReferences(); });
    dlg->show();
    dlg->raise();
}

QStringList MainWindow::buildVariableReferences() const
{
    QStringList refs;

    // 模块输出 {模块号.参数名}
    const QList<int> ids = m_lastModuleVars.keys();
    for (int id : ids) {
        const QStringList keys = m_lastModuleVars.value(id).keys();
        for (const QString &k : keys)
            refs << VariablePanel::moduleRefText(id, k);
    }

    // 全局变量 {global.名称}
    const QMap<QString, GlobalVariableManager::Variable> vars =
        GlobalVariableManager::instance()->variables();
    for (auto it = vars.cbegin(); it != vars.cend(); ++it)
        refs << VariablePanel::globalRefText(it.key());

    // 统一排序，保证菜单顺序稳定（便于按位置快速选择）
    refs.sort();
    return refs;
}

void MainWindow::openAuxPanel(const QString &key)
{
    // 创建/显示/刷新统一委托 AuxPanelManager；本函数保留"面板打开后的窗口级副作用"
    // （日志提示等），并作为菜单/启动恢复的统一入口。
    if (key == QStringLiteral("resultTable")) {
        onOpenResultTable();
        return;
    }
    if (key == QStringLiteral("performance")) {
        onOpenPerformancePanel();
        return;
    }
    if (key == QStringLiteral("outputData")) {
        onOpenOutputViewer();
        return;
    }
    if (key == QStringLiteral("variable")) {
        m_auxPanels->open(key);
        logMessage(QStringLiteral("变量面板已打开：双击某行即可复制引用表达式"));
        return;
    }
}

void MainWindow::saveAuxPanelVisibility() const
{
    // 显隐记录（QSettings 键名契约）由 AuxPanelManager 经 PanelVisibilityStore 完成
    m_auxPanels->saveVisibility();
}

void MainWindow::restoreAuxPanelVisibility()
{
    // 只恢复「显隐」，不恢复几何（理由见 AuxPanelManager::restoreVisibility）。
    // 启动恢复阶段执行器尚未创建，性能面板的 bindExecutor 由执行器创建处补做。
    m_auxPanels->restoreVisibility();
}

bool MainWindow::runWithBusyFeedback(const QString &what, bool quiet,
                                     const std::function<void()> &fn)
{
    // executeUpTo/executeFrom/executeNode 是同步执行（占用 UI 线程）：
    // 链路含耗时代工时，界面在期间不刷新——先绘制"正在执行"反馈，避免看起来像卡死。
    if (m_busyExecuting) {
        if (!quiet)
            logMessage(QStringLiteral("正在执行中，已忽略重复触发：%1").arg(what));
        return false;
    }
    m_busyExecuting = true;
    if (!quiet) {
        if (ui && ui->statusBar)
            ui->statusBar->showMessage(what);
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QApplication::processEvents();   // 先把提示/光标绘制出来，再进入同步执行
    }
    fn();
    if (!quiet) {
        QApplication::restoreOverrideCursor();
        if (ui && ui->statusBar)
            ui->statusBar->clearMessage();
    }
    m_busyExecuting = false;
    return true;
}

void MainWindow::recomputeDownstream(NodeBase *node, bool quiet)
{
    if (!node)
        return;

    const int idx = ui->flowTabs->currentIndex();
    if (idx < 0 || idx >= m_flowScenes.size())
        return;
    FlowExecutor *ex = executorForScene(m_flowScenes[idx]);
    if (!ex)
        return;

    const ExecutionState st = ex->getState();
    if (st == ExecutionState::Running || st == ExecutionState::Paused) {
        if (!quiet)
            logMessage(QStringLiteral("流程正在运行（或暂停）中，已取消「重算下游」；请先停止流程"));
        return;
    }

    // 先作废本算子及其下游的缓存，再执行这一段链路：不作废会让下游读到上一轮的旧值
    // （与 E2/P2 同类问题：本轮无输出/参数变更后必须让下游看到新值）。
    if (!runWithBusyFeedback(QStringLiteral("正在重算 %1 及其下游…").arg(node->fullName()),
                             quiet, [ex, node]() {
        ex->invalidateDownstreamOf(node);
        ex->executeFrom(node);
    }))
        return;
    if (!quiet)
        logMessage(QStringLiteral("已重算 %1 及其下游（未涉及的分支不重跑）").arg(node->fullName()));

    // 与「执行此算子」保持一致：把结果刷到图像窗口，便于立刻核对
    NodeBase *target = resolveDisplayNode(node);
    if (target && m_imageView)
        m_imageDisplay->displayNodeOutput(target);
}

void MainWindow::executeNodeOnce(NodeBase *node)
{
    if (!node)
        return;
    bool success = false;
    if (!runWithBusyFeedback(QStringLiteral("正在执行 %1…").arg(node->fullName()), false,
                             [this, node, &success]() {
        const int currentIndex = ui->flowTabs->currentIndex();
        if (currentIndex >= 0 && currentIndex < m_flowScenes.size()) {
            FlowScene *currentScene = m_flowScenes[currentIndex];
            m_executor = executorForScene(currentScene);
            if (m_executor)
                m_executor->propagateData(node);
        }
        success = node->execute();
    }))
        return;   // 执行中重复触发被忽略（已记日志）
    logMessage(success ? QStringLiteral("算子执行成功") : QStringLiteral("算子执行失败"));
    if (!success)
        return;

    NodeBase *target = resolveDisplayNode(node);
    if (target && m_imageView)
        m_imageDisplay->displayNodeOutput(target);
    if (m_selectedNode == node)
        showNodeParameters(node);
    if (ModuleEditorDialog *dlg = m_moduleEditors.value(node, nullptr)) {
        dlg->reloadFromNode();
        dlg->setResultOverlay(collectOverlayFromNode(node));
    }
}

void MainWindow::onNodeAdded(NodeBase *node)
{
    // 节点添加时的处理
    VFP_DEBUG << "Node added:" << node->name();
    
    // 连接ImageReadNode的imageRead信号 - 更新图像显示
    ImageReadNode *imageReadNode = dynamic_cast<ImageReadNode*>(node);
    if (imageReadNode) {
        connect(imageReadNode, &ImageReadNode::imageRead, this, [this, imageReadNode](const HalconCpp::HImage &image) {
            VFP_DEBUG << "ImageReadNode emitted imageRead signal";
            // 使用统一规则：下拉框选择 > 画布选中 > 兜底
            NodeBase *target = resolveDisplayNode(imageReadNode);
            if (target == imageReadNode && m_imageView) {
                m_imageView->setImage(image, imageReadNode->fullName());
                if (m_imageSourceLabel) {
                    m_imageSourceLabel->setText(QString("图像来源: %1").arg(imageReadNode->fullName()));
                }
            }
        });
        
        // 连接ImageReadNode的thumbnailUpdated信号 - 更新缩略图
        connect(imageReadNode, &ImageReadNode::thumbnailUpdated, this, [this, imageReadNode]() {
            VFP_DEBUG << "ImageReadNode emitted thumbnailUpdated signal";
            // 检查参数面板是否存在
            QWidget *parameterPanel = ui->paramDockContents;
            if (parameterPanel) {
                // 查找参数面板中的所有子控件，寻找缩略图布局
                QList<QWidget*> childWidgets = parameterPanel->findChildren<QWidget*>();
                for (QWidget *childWidget : childWidgets) {
                    // 查找滚动区域，缩略图可能在滚动区域中
                    QScrollArea *scrollArea = qobject_cast<QScrollArea*>(childWidget);
                    if (scrollArea) {
                        QWidget *scrollContent = scrollArea->widget();
                        if (scrollContent) {
                            QHBoxLayout *thumbnailLayout = qobject_cast<QHBoxLayout*>(scrollContent->layout());
                            if (thumbnailLayout) {
                                // 直接更新缩略图
                                imageReadNode->updateThumbnails(thumbnailLayout);
                                VFP_DEBUG << "缩略图已更新，黄色框应该显示";
                                return;
                            }
                        }
                    }
                }
            }
        });
        VFP_DEBUG << "Connected ImageReadNode signals";
    }
    
    // 连接MvsImageSourceNode的imageAcquired信号 - 只用于调试，不自动更新显示
    MvsImageSourceNode *mvsImageSourceNode = dynamic_cast<MvsImageSourceNode*>(node);
    if (mvsImageSourceNode) {
        connect(mvsImageSourceNode, &MvsImageSourceNode::imageAcquired, this, [this]() {
            VFP_DEBUG << "MvsImageSourceNode emitted imageAcquired signal";
        });
        // 连接logMessage信号
        connect(mvsImageSourceNode, &MvsImageSourceNode::logMessage, this, &MainWindow::logMessage);
        VFP_DEBUG << "Connected MvsImageSourceNode signal";
    }
    
    // 连接HalconImageSourceNode的imageAcquired信号 - 只用于调试，不自动更新显示
    HalconImageSourceNode *halconImageSourceNode = dynamic_cast<HalconImageSourceNode*>(node);
    if (halconImageSourceNode) {
        connect(halconImageSourceNode, &HalconImageSourceNode::imageAcquired, this, [this](const HalconCpp::HImage &image) {
            VFP_DEBUG << "HalconImageSourceNode emitted imageAcquired signal";
        });
        VFP_DEBUG << "Connected HalconImageSourceNode signal";
    }
    
    // 连接ColorConversionNode的图像显示 - 只用于调试，不自动更新显示
    ColorConversionNode *colorConversionNode = dynamic_cast<ColorConversionNode*>(node);
    if (colorConversionNode) {
        connect(colorConversionNode, &ColorConversionNode::imageConverted, this, [this, colorConversionNode](const HalconCpp::HImage &image) {
            VFP_DEBUG << "ColorConversionNode emitted imageConverted signal";
        });
        VFP_DEBUG << "Connected ColorConversionNode signal";
    }
}

void MainWindow::onImageRead(const HalconCpp::HImage &image)
{
    // 显示读取的图像
    m_imageView->setImage(image, "读取图像");
    if (m_imageSourceLabel) {
        m_imageSourceLabel->setText("图像来源: 读取图像");
    }
}

void MainWindow::onNewProject()
{
    // 新建会丢弃当前方案：有未保存改动时先确认（与关窗/打开方案同一机制，
    // 否则只保护了"退出"这一条路，新建/打开仍是静默丢弃）
    if (!confirmDiscardUnsavedChanges(tr("新建方案")))
        return;

    // 新建方案
    logMessage("新建方案");

    // 先停掉并摘除全部执行器，再删除场景——否则运行中的执行线程会在节点被删除后
    // 继续访问（use-after-free），且 m_flowExecutors 会残留指向已删场景的悬垂键。
    // 超时则中止本次新建（N1）：保留旧方案场景与执行器，不删场景、不回收执行器，
    // 避免运行中的线程在场景被释放后访问（UAF）；旧流程被 stop 后会在算子返回自然退出。
    QList<FlowExecutor *> oldExecutors;
    for (FlowExecutor *ex : qAsConst(m_flowExecutors)) {
        if (ex && !oldExecutors.contains(ex))
            oldExecutors.append(ex);
    }
    if (m_executor && !oldExecutors.contains(m_executor))
        oldExecutors.append(m_executor);

    for (FlowExecutor *ex : oldExecutors)
        ex->stopExecution();
    // 统一等待真正退出：共享 15s 总预算（与 retireExecutor 阈值一致），避免 N 个执行器串行
    // wait(15000) 致 UI 最坏冻结 15s×N。超时即中止。
    const qint64 deadlineMs = QDateTime::currentMSecsSinceEpoch() + 15000;
    QList<FlowExecutor *> stuckExecutors;
    for (FlowExecutor *ex : oldExecutors) {
        if (!ex->isRunning())
            continue;
        const qint64 left = qMax(qint64(0), deadlineMs - QDateTime::currentMSecsSinceEpoch());
        if (left <= 0 || !ex->wait(left))
            stuckExecutors.append(ex);
    }
    if (!stuckExecutors.isEmpty()) {
        logMessage(tr("有流程执行线程超时未退出，已取消新建方案"));
        QMessageBox::warning(this, tr("提示"),
            tr("有流程仍在执行且未能在限定时间内退出，已取消新建方案以避免程序崩溃。"));
        return;
    }

    FlowExecutor *spareExecutor = nullptr;
    for (FlowExecutor *ex : oldExecutors) {
        if (!ex)
            continue;
        GlobalTriggerManager::instance()->unregisterExecutor(ex);
        ex->stopExecution();          // 停掉旧方案可能运行中的流程（新方案首个流程会复用 spare）
        ex->setFlowScene(nullptr);
        if (!spareExecutor)
            spareExecutor = ex;   // 保留一个占位，供新方案首个流程复用
        else
            retireExecutor(ex);   // 安全回收：stop+wait，超时绝不 terminate
    }
    m_flowExecutors.clear();
    m_executor = spareExecutor;
    if (spareExecutor)
        m_flowExecutors.insert(nullptr, spareExecutor);

    // 清掉旧场景在图像显示控制器里的显式显示源键，避免悬垂指针（removeScene 漏 loadProjectFile 调用点）
    for (FlowScene *scene : qAsConst(m_flowScenes))
        m_imageDisplay->removeScene(scene);

    // N11 陈旧 row：结果表 + 变量缓存属于"上一方案"的数据，切方案后必须清空，
    // 否则一直显示旧流程的模块与输出、变量引用菜单给出已失效的 moduleId。
    if (m_auxPanels) {
        if (auto *rt = m_auxPanels->resultTablePanel())
            rt->clearResults();
    }
    m_lastModuleVars.clear();

    // 清空现有的流程场景
    qDeleteAll(m_flowScenes);
    m_flowScenes.clear();
    
    // 清空标签页
    while (ui->flowTabs->count() > 0) {
        QWidget *widget = ui->flowTabs->widget(0);
        ui->flowTabs->removeTab(0);
        delete widget;
    }
    
    // 清空用户选择的输出节点（显示决策状态在 ImageDisplayController）
    m_imageDisplay->clearSelection();
    
    // 创建一个新的空流程
    createNewFlow();

    // 新建后是"干净的空白方案"：重置未保存基线。否则会拿上一方案的内容当基线，
    // 空白新方案被当成"有未保存修改"（关窗时无谓提示，自动保存还会写一份空恢复文件）。
    markProjectSaved();

    // 新建清空只读态（上一个只读分发方案已丢弃）
    projectManager()->resetReadonlyFlag();
    applyReadonlyUI();

    logMessage("新方案创建完成");
}

void MainWindow::onSaveProject()
{
    saveProjectInteractively();
}

// 保存相关逻辑集中在这里：关窗前的"保存并退出"与菜单/工具栏的保存必须走同一条路径，
// 否则很容易出现"手动保存会清恢复现场、关窗保存不会"这类隐性分叉。
ProjectManager *MainWindow::projectManager()
{
    // 懒创建集中到一处：原先两个入口各自 if(!m_projectManager) new，易漏且一旦漏就是两份实例
    // （各自的 lastFilePath 互不相同 → 自动保存/恢复拿到的是错的方案路径）。
    if (!m_projectManager)
        m_projectManager = new ProjectManager(this);
    return m_projectManager;
}

bool MainWindow::saveProjectInteractively()
{
    // 保存项目（方案扩展名 .vfp，与需求文档一致）：文件对话框与结果提示在 ProjectManager
    ProjectManager *pm = projectManager();
    QString fileName;
    const bool success = pm->saveProjectInteractive(this, m_flowScenes, &fileName);
    if (fileName.isEmpty())
        return false;   // 用户取消

    logMessage(tr("保存项目: %1").arg(fileName));
    if (success) {
        logMessage(tr("项目保存成功: %1").arg(fileName));
        m_recentFiles->add(fileName);
        // 内容已落盘：刷新"未保存判定"基线，并清掉恢复现场（干净退出语义）
        markProjectSaved();
    } else {
        logMessage(tr("项目保存失败: %1").arg(fileName));
    }
    return success;
}

void MainWindow::markProjectSaved()
{
    if (!m_projectManager)
        return;
    m_recovery.markSaved(m_projectManager->buildProjectJson(m_flowScenes));
    m_recovery.clearRecovery();
}

// ---- 算子分组（FR1.9）----

FlowScene *MainWindow::currentFlowScene() const
{
    const int idx = ui && ui->flowTabs ? ui->flowTabs->currentIndex() : -1;
    if (idx >= 0 && idx < m_flowScenes.size())
        return m_flowScenes[idx];
    return nullptr;
}

void MainWindow::onCreateGroup()
{
    FlowScene *scene = currentFlowScene();
    if (!scene)
        return;
    if (NodeGroupItem *group = scene->createGroupFromSelection()) {
        ui->statusBar->showMessage(tr("已创建分组「%1」（%2 个算子）")
                                       .arg(group->title())
                                       .arg(group->memberCount()),
                                   4000);
        logMessage(tr("创建分组：%1（%2 个算子）")
                       .arg(group->title())
                       .arg(group->memberCount()));
    } else {
        // 明确告诉用户为什么没反应（而不是静默什么都不发生）
        ui->statusBar->showMessage(tr("请先在画布上选中至少 2 个算子，再创建分组"), 4000);
    }
}

void MainWindow::onDissolveGroup()
{
    FlowScene *scene = currentFlowScene();
    if (!scene)
        return;
    const int count = scene->dissolveSelectedGroups();
    if (count > 0) {
        ui->statusBar->showMessage(tr("已解散 %1 个分组（组内算子已保留）").arg(count), 4000);
        logMessage(tr("解散分组：%1 个").arg(count));
    } else {
        ui->statusBar->showMessage(tr("请先选中要解散的分组框（点分组标题栏）"), 4000);
    }
}

void MainWindow::onToggleGroupCollapse()
{
    FlowScene *scene = currentFlowScene();
    if (!scene)
        return;

    QList<NodeGroupItem *> groups;
    for (QGraphicsItem *item : scene->selectedItems()) {
        if (auto *group = dynamic_cast<NodeGroupItem *>(item))
            groups.append(group);
    }
    if (groups.isEmpty()) {
        ui->statusBar->showMessage(tr("请先选中要折叠/展开的分组框（点分组标题栏）"), 4000);
        return;
    }
    for (NodeGroupItem *group : groups)
        group->setCollapsed(!group->isCollapsed());
    ui->statusBar->showMessage(tr("已折叠/展开 %1 个分组").arg(groups.size()), 3000);
    logMessage(tr("折叠/展开分组：%1 个").arg(groups.size()));
}

// ---- 子图复用：复制/粘贴（含内部连线）+ 片段文件导入导出 ----
// 边界说明：这是 FR15.10 的**设计期**半步（拷一份出来，跨流程/跨方案/跨会话搬运）；
// 运行期"把一段流程当算子调用"（子流程复用）未做，接口草案见 docs/子流程复用设计草案.md。

QPointF MainWindow::viewCenterInScene(FlowScene *scene) const
{
    if (scene && ui && ui->flowTabs) {
        for (int i = 0; i < ui->flowTabs->count(); ++i) {
            if (auto *view = qobject_cast<QGraphicsView *>(ui->flowTabs->widget(i))) {
                if (view->scene() == scene)
                    return view->mapToScene(view->viewport()->rect().center());
            }
        }
    }
    // 兜底（理论上只在没有视图的测试路径）：给一个稳妥点，别把片段粘到坐标原点外面
    return QPointF(80, 80);
}

void MainWindow::copySelectionToClipboard(FlowScene *scene)
{
    if (!scene)
        return;
    const QList<NodeBase *> selected = scene->selectedNodes();
    if (selected.isEmpty()) {
        ui->statusBar->showMessage(tr("请先选中要复制的算子"), 3000);
        return;
    }

    int droppedConnections = 0;
    const QJsonObject snippet = FlowSnippet::capture(scene, selected, &droppedConnections);
    if (snippet.isEmpty()) {
        ui->statusBar->showMessage(tr("复制失败：没有可复制的算子"), 3000);
        return;
    }
    QApplication::clipboard()->setText(FlowSnippet::toText(snippet));

    QString msg = tr("已复制 %1 个算子").arg(selected.size());
    if (droppedConnections > 0) {
        // 如实告知：跨选中边界的连线不会被带走（对端不在片段里）
        msg += tr("（%1 条跨边界的连线未包含）").arg(droppedConnections);
    }
    ui->statusBar->showMessage(msg, 5000);
    logMessage(msg);
}

void MainWindow::pasteSnippetIntoScene(FlowScene *scene)
{
    if (!scene)
        return;
    QString error;
    const QJsonObject snippet = FlowSnippet::fromText(QApplication::clipboard()->text(), &error);
    if (snippet.isEmpty()) {
        ui->statusBar->showMessage(tr("粘贴失败：%1").arg(error), 6000);
        return;
    }
    insertSnippetIntoScene(scene, snippet, &error);
}

bool MainWindow::insertSnippetIntoScene(FlowScene *scene, const QJsonObject &snippet, QString *error)
{
    if (!scene) {
        if (error)
            *error = tr("当前没有流程");
        return false;
    }
    if (scene->isEditLocked()) {
        if (error)
            *error = tr("流程处于编辑锁定状态（运行中），无法插入片段");
        ui->statusBar->showMessage(error ? *error : QString(), 5000);
        return false;
    }

    scene->recordUndo();   // 一次插入 = 一步撤销（内部批量抑制分步记录）
    const QPointF at = FlowSnippet::suggestedInsertTopLeft(snippet, viewCenterInScene(scene));
    const QList<NodeBase *> created = FlowSnippet::insert(scene, snippet, at, error);
    if (created.isEmpty()) {
        if (error)
            ui->statusBar->showMessage(tr("插入片段失败：%1").arg(*error), 6000);
        if (error)
            logMessage(tr("插入片段失败：%1").arg(*error));
        return false;
    }

    // 选中刚插入的算子：用户能立刻拖走/继续连线，不用自己一个个点
    scene->clearSelection();
    for (NodeBase *n : created) {
        if (NodeGraphicsItem *item = scene->getGraphicsItemForNode(n))
            item->setSelected(true);
    }
    const QString msg = tr("已插入 %1 个算子").arg(created.size());
    ui->statusBar->showMessage(msg, 5000);
    logMessage(msg);
    return true;
}

void MainWindow::onExportSnippet()
{
    FlowScene *scene = currentFlowScene();
    if (!scene)
        return;
    const QList<NodeBase *> selected = scene->selectedNodes();
    if (selected.isEmpty()) {
        ui->statusBar->showMessage(tr("请先选中要导出的算子"), 4000);
        return;
    }

    int droppedConnections = 0;
    const QJsonObject snippet = FlowSnippet::capture(scene, selected, &droppedConnections);
    if (snippet.isEmpty()) {
        ui->statusBar->showMessage(tr("导出失败：没有可导出的算子"), 4000);
        return;
    }

    QString path = QFileDialog::getSaveFileName(
        this, tr("导出方案片段"), QString(),
        tr("方案片段 (*%1)").arg(FlowSnippet::fileExtension()));
    if (path.isEmpty())
        return;   // 用户取消
    if (!path.endsWith(FlowSnippet::fileExtension(), Qt::CaseInsensitive))
        path += FlowSnippet::fileExtension();

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("导出方案片段"),
                             tr("无法写入文件：\n%1\n%2").arg(path, file.errorString()));
        return;
    }
    const QByteArray payload = FlowSnippet::toText(snippet).toUtf8();
    const qint64 written = file.write(payload);
    file.close();
    if (written != payload.size()) {
        QMessageBox::warning(this, tr("导出方案片段"), tr("写入不完整：\n%1").arg(path));
        return;
    }

    QString msg = tr("已导出片段：%1（%2 个算子）").arg(path).arg(selected.size());
    if (droppedConnections > 0)
        msg += tr("；%1 条跨边界连线未包含").arg(droppedConnections);
    ui->statusBar->showMessage(msg, 6000);
    logMessage(msg);
}

void MainWindow::applyReadonlyUI()
{
    auto *session = SessionManager::instance();
    // 未登录时沿用动作默认可用态；已登录则以角色权限为准
    const bool roleAllowsEdit = !session->isLoggedIn() || session->canEditScheme();
    const bool readonly = projectManager()->isReadonlyLoaded();
    const bool saveEnabled = roleAllowsEdit && !readonly;
    if (ui->actionSave)
        ui->actionSave->setEnabled(saveEnabled);
    if (ui->actionSaveScheme)
        ui->actionSaveScheme->setEnabled(saveEnabled);
    if (readonly)
        ui->statusBar->showMessage(tr("只读方案：禁止覆盖保存（可另存为新方案）"), 0);
    else
        ui->statusBar->clearMessage();
}

void MainWindow::onExportEncryptedProject()
{
    if (m_flowScenes.isEmpty()) {
        QMessageBox::information(this, tr("导出加密/只读方案"),
                                 tr("当前没有可导出的方案。"));
        return;
    }

    // 小对话框：加密开关 + 口令 + 确认口令 + 只读开关
    QDialog dlg(this);
    dlg.setWindowTitle(tr("导出加密/只读方案"));
    auto *form = new QFormLayout(&dlg);
    auto *cbEncrypt = new QCheckBox(tr("加密（打开时需口令）"));
    cbEncrypt->setChecked(true);
    auto *lePass = new QLineEdit(&dlg);
    lePass->setEchoMode(QLineEdit::Password);
    auto *leConfirm = new QLineEdit(&dlg);
    leConfirm->setEchoMode(QLineEdit::Password);
    auto *cbReadonly = new QCheckBox(tr("只读（分发后禁止覆盖保存）"));
    form->addRow(tr("加密："), cbEncrypt);
    form->addRow(tr("口令："), lePass);
    form->addRow(tr("确认口令："), leConfirm);
    form->addRow(tr("只读："), cbReadonly);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString pass = lePass->text();
    const bool encrypt = cbEncrypt->isChecked();
    if (encrypt) {
        if (pass.isEmpty()) {
            QMessageBox::warning(this, tr("导出加密/只读方案"), tr("加密必须设置口令。"));
            return;
        }
        if (pass != leConfirm->text()) {
            QMessageBox::warning(this, tr("导出加密/只读方案"), tr("两次输入的口令不一致。"));
            return;
        }
    }
    const bool readonly = cbReadonly->isChecked();

    QString path = QFileDialog::getSaveFileName(
        this, tr("导出加密/只读方案"), QString(),
        tr("加密方案 (*.vfpe);;方案文件 (*.vfp)"));
    if (path.isEmpty())
        return; // 用户取消
    if (!path.endsWith(QStringLiteral(".vfpe"), Qt::CaseInsensitive) &&
        !path.endsWith(QStringLiteral(".vfp"), Qt::CaseInsensitive))
        path += QStringLiteral(".vfpe");

    ProjectManager *pm = projectManager();
    const bool ok = pm->exportEncryptedProject(path, m_flowScenes,
                                              encrypt ? pass : QString(), readonly);
    if (ok) {
        const QString tag = (encrypt ? tr("加密") : tr("未加密")) +
                            (readonly ? tr("+只读") : QString());
        const QString msg = tr("已导出%1方案：%2").arg(tag, path);
        ui->statusBar->showMessage(msg, 8000);
        logMessage(msg);
    } else {
        QMessageBox::warning(this, tr("导出加密/只读方案"),
                             tr("导出失败：\n%1").arg(path));
    }
}

void MainWindow::onImportSnippet()
{
    FlowScene *scene = currentFlowScene();
    if (!scene)
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("导入方案片段"), QString(),
        tr("方案片段 (*%1)").arg(FlowSnippet::fileExtension()));
    if (path.isEmpty())
        return;   // 用户取消

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("导入方案片段"),
                             tr("无法读取文件：\n%1\n%2").arg(path, file.errorString()));
        return;
    }
    const QByteArray raw = file.readAll();
    file.close();

    QString error;
    const QJsonObject snippet = FlowSnippet::fromText(QString::fromUtf8(raw), &error);
    if (snippet.isEmpty()) {
        QMessageBox::warning(this, tr("导入方案片段"), error);
        return;
    }
    logMessage(tr("导入片段：%1").arg(path));
    if (!insertSnippetIntoScene(scene, snippet, &error))
        QMessageBox::warning(this, tr("导入方案片段"), error);
}

void MainWindow::initCrashRecovery()
{
    // 间隔可配：现场无人值守机想省 I/O 就调大，置 0 则关掉自动保存
    // （关掉后仍保留"未保存改动"的关窗确认，不会变成静默丢弃）。
    QSettings settings;
    const int intervalSec =
        settings.value(QStringLiteral("recovery/autoSaveIntervalSec"), 60).toInt();
    if (intervalSec <= 0) {
        VFP_DEBUG << "自动保存已按配置关闭（recovery/autoSaveIntervalSec <= 0）";
        return;
    }

    if (!m_autoSaveTimer) {
        m_autoSaveTimer = new QTimer(this);
        connect(m_autoSaveTimer, &QTimer::timeout, this, &MainWindow::autoSaveTick);
    }
    m_autoSaveTimer->start(intervalSec * 1000);
    VFP_DEBUG << "自动保存已启用：间隔" << intervalSec << "秒，目录" << m_recovery.dirPath();
}

void MainWindow::autoSaveTick()
{
    // 同步执行/重算独占 UI 线程：此刻序列化既慢又没意义，跳过一拍即可
    if (m_busyExecuting)
        return;

    ProjectManager *pm = projectManager();
    const QJsonObject json = pm->buildProjectJson(m_flowScenes);

    // 与"已落盘内容"一致（例如此前刚保存过）：不写恢复文件。
    // 否则干净退出后还会留下一个"可恢复"的假现场——下次启动弹一个恢复框而内容其实没变。
    if (!m_recovery.hasUnsavedChanges(json))
        return;
    // 同一份内容已写过（用户改了又停手）：不重复写盘，避免每分钟一次无意义 I/O 与状态栏刷屏
    if (m_recovery.recoveryUpToDate(json))
        return;

    if (m_recovery.saveRecovery(json, pm->lastFilePath()))
        ui->statusBar->showMessage(tr("已自动保存（异常退出后可恢复）"), 3000);
}

void MainWindow::initReportAutoExport()
{
    // 定时导出（P1-11 收尾）：策略与落盘在 ReportAutoExport（无 GUI、可单测），
    // 这里只负责"每分钟看一眼"与取数。定时器**总是启动**：用户可能在报表窗口里随时开启，
    // 若只在"启动时已启用"才启定时器，就会出现"勾了开关却不生效、要重启"的怪事。
    if (!m_reportTimer) {
        m_reportTimer = new QTimer(this);
        connect(m_reportTimer, &QTimer::timeout, this, &MainWindow::reportAutoExportTick);
    }
    m_reportTimer->start(60 * 1000);

    const ReportAutoExport::Config cfg = ReportAutoExport::loadConfig();
    if (cfg.enabled && cfg.isValid()) {
        VFP_DEBUG << "定时导出已启用：每" << cfg.intervalMinutes << "分钟，统计最近"
                  << cfg.rangeHours << "小时，目录" << cfg.effectiveDir();
    } else {
        VFP_DEBUG << "定时导出未启用（可在「视图 → 统计报表」里开启）";
    }
}

void MainWindow::reportAutoExportTick()
{
    ReportAutoExport::Config cfg = ReportAutoExport::loadConfig();
    if (!cfg.enabled || !cfg.isValid())
        return;   // 未启用：静默（不打扰；开关在报表窗口里）

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!ReportAutoExport::shouldExport(cfg, now, ReportAutoExport::lastExportMs()))
        return;

    // 取数：与报表窗口**同一个查询接口、同一套计算**。口径必须一致，
    // 否则"界面看到 98%、机器导出来 96%"这种事现场无法解释。
    const QDateTime to = QDateTime::currentDateTime();
    const QDateTime from = to.addSecs(-3600LL * cfg.rangeHours);
    // 上限保护：本拍在 GUI 线程同步查询并逐行构造记录，行数直接换算成窗口冻结时长
    //（评审 P3：高频产线"每天一次、没人看着"的时刻最忌讳卡窗）。保守钉 5 万（原 50 万的
    // 1/10，具体耗时待真机标定）——queryResults 按时间倒序取最近 N 条，
    // 触顶时报表只覆盖"最近 5 万条"，状态栏如实提示。
    constexpr int kQueryLimit = 50000;
    const QList<InspectionRecord> records =
        AppDatabase::instance()->queryResults(from, to, kQueryLimit);
    const bool truncated = (records.size() >= kQueryLimit);

    // 粒度与报表窗口"自动"模式保持一致：统计窗口 ≤ 2 天（48 小时）按小时，更长按天。
    // 否则会出现"界面看的是按天、机器定时导出的是按小时"这类口径不一致，现场无法解释。
    const StatisticsReport::Granularity gran =
        (cfg.rangeHours <= 48) ? StatisticsReport::Granularity::ByHour
                               : StatisticsReport::Granularity::ByDay;
    const StatisticsReport::Summary summary =
        StatisticsReport::compute(records, QString(), gran);
    const double target =
        QSettings().value(QStringLiteral("reporting/yieldTargetPercent"), 0.0).toDouble();

    QStringList written;
    QString error;
    if (!ReportAutoExport::exportNow(cfg, summary, QString(), target, to, &written, &error)) {
        // 失败**不更新** lastExportMs：下一分钟自动重试（目录临时不可写这类问题能自愈）
        logMessage(tr("定时导出失败：%1").arg(error));
        ui->statusBar->showMessage(tr("定时导出失败：%1").arg(error), 15000);
        return;
    }

    ReportAutoExport::setLastExportMs(now);
    const int removed = ReportAutoExport::pruneOldFiles(cfg.effectiveDir(), cfg.keepFiles);

    QString msg = tr("已定时导出 %1 个文件到 %2").arg(written.size()).arg(cfg.effectiveDir());
    if (removed > 0)
        msg += tr("（清理旧报告 %1 个）").arg(removed);
    if (truncated)
        msg += tr(" ⚠ 记录数触顶（仅覆盖最近 %1 条），报表可能不完整（建议调小统计小时数）").arg(kQueryLimit);
    logMessage(msg);
    ui->statusBar->showMessage(msg, 12000);
    if (!error.isEmpty())
        logMessage(tr("定时导出部分失败：%1").arg(error));
}

void MainWindow::checkRecoveryOnStartup()
{
    const RecoveryStore::Info info = m_recovery.info();
    if (!info.exists)
        return;

    const QString filePath =
        QDir(m_recovery.dirPath()).filePath(RecoveryStore::recoveryFileName());

    if (!info.readable) {
        // 损坏的恢复文件：明说并清掉，否则每次启动都弹同一个无法处理的框
        VFP_DEBUG << "恢复文件损坏，已清除：" << filePath;
        m_recovery.clearRecovery();
        logMessage(tr("上次异常退出留下的恢复文件已损坏，无法恢复，已清除"));
        return;
    }

    QSettings settings;
    if (!settings.value(QStringLiteral("recovery/promptOnStartup"), true).toBool())
        return;   // 现场可关掉启动询问；此时保留文件，不静默丢弃内容

    const QString when = info.savedAt.isValid()
                             ? info.savedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                             : tr("时间未知");
    const QString origin = info.originalPath.isEmpty()
                               ? tr("未命名方案（从未保存过）")
                               : info.originalPath;

    QMessageBox box(this);
    box.setWindowTitle(tr("崩溃恢复"));
    box.setIcon(QMessageBox::Warning);
    box.setText(tr("检测到上次运行未正常退出，存在自动保存的内容："));
    box.setInformativeText(tr("原方案：%1\n自动保存时间：%2\n\n是否恢复到界面？\n"
                             "（选择「丢弃」将删除该恢复文件）")
                               .arg(origin, when));
    QPushButton *restoreBtn = box.addButton(tr("恢复"), QMessageBox::AcceptRole);
    box.addButton(tr("丢弃"), QMessageBox::DestructiveRole);
    box.setDefaultButton(restoreBtn);
    box.exec();

    if (box.clickedButton() != restoreBtn) {
        m_recovery.clearRecovery();
        logMessage(tr("已丢弃上次异常退出留下的自动保存内容"));
        return;
    }

    QString restorePath;
    if (!m_recovery.exportForLoad(&restorePath)) {
        QMessageBox::warning(this, tr("崩溃恢复"),
                             tr("恢复失败：无法读取恢复文件。\n%1").arg(filePath));
        return;
    }

    // 复用唯一的"接管方案"路径：停旧执行器、建标签页、注册触发、边界收尾都在 loadProjectFile 内。
    // 临时导出文件删掉即可（内容已在场景里）；恢复出来的方案不属于该临时文件，
    // 故把 lastFilePath 写回原方案路径（空 = 从未保存过的新方案）。
    loadProjectFile(restorePath, /*recoveryRestore=*/true);
    QFile::remove(restorePath);
    projectManager()->setLastFilePath(info.originalPath);

    logMessage(tr("已从自动保存内容恢复（原方案：%1；自动保存时间：%2）。"
                  "请确认后用「保存」写回方案文件")
                   .arg(origin, when));
}

bool MainWindow::confirmDiscardUnsavedChanges(const QString &action)
{
    QSettings settings;
    // 与关窗提示同一个开关：无人值守现场可整体关掉"未保存"询问
    if (!settings.value(QStringLiteral("recovery/promptOnClose"), true).toBool())
        return true;

    ProjectManager *pm = projectManager();
    const QJsonObject json = pm->buildProjectJson(m_flowScenes);
    if (!m_recovery.hasUnsavedChanges(json))
        return true;

    QMessageBox box(this);
    box.setWindowTitle(tr("未保存的修改"));
    box.setIcon(QMessageBox::Warning);
    box.setText(tr("%1 前，当前方案有未保存的修改：").arg(action));
    box.setInformativeText(
        tr("方案：%1\n\n「保存并继续」写回方案文件；「不保存继续」将丢弃这些修改。")
            .arg(pm->lastFilePath().isEmpty() ? tr("未命名方案（从未保存过）")
                                              : pm->lastFilePath()));
    QPushButton *saveBtn = box.addButton(tr("保存并继续"), QMessageBox::AcceptRole);
    QPushButton *discardBtn = box.addButton(tr("不保存继续"), QMessageBox::DestructiveRole);
    QPushButton *cancelBtn = box.addButton(tr("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(saveBtn);
    box.exec();

    if (box.clickedButton() == cancelBtn)
        return false;
    if (box.clickedButton() == discardBtn) {
        // 当前方案即将被替换/丢弃：恢复现场随之作废（不留一份"已被用户放弃"的方案）
        m_recovery.clearRecovery();
        return true;
    }
    return saveProjectInteractively();   // 保存失败或用户在保存对话框取消 → 不继续
}

bool MainWindow::confirmCloseWithUnsavedChanges()
{
    QSettings settings;
    if (!settings.value(QStringLiteral("recovery/promptOnClose"), true).toBool())
        return true;   // 不询问：保留恢复文件，交给下次启动询问（不在这里静默丢弃用户改动）

    ProjectManager *pm = projectManager();
    const QJsonObject json = pm->buildProjectJson(m_flowScenes);
    if (!m_recovery.hasUnsavedChanges(json)) {
        m_recovery.clearRecovery();   // 干净退出：不留恢复现场
        return true;
    }

    QMessageBox box(this);
    box.setWindowTitle(tr("退出前确认"));
    box.setIcon(QMessageBox::Warning);
    box.setText(tr("方案有未保存的修改："));
    box.setInformativeText(
        tr("方案：%1\n\n「保存并退出」写回方案文件；「不保存退出」将丢弃这些修改，"
           "自动保存内容也会一并清除。")
            .arg(pm->lastFilePath().isEmpty() ? tr("未命名方案（从未保存过）")
                                              : pm->lastFilePath()));
    QPushButton *saveBtn = box.addButton(tr("保存并退出"), QMessageBox::AcceptRole);
    QPushButton *discardBtn = box.addButton(tr("不保存退出"), QMessageBox::DestructiveRole);
    QPushButton *cancelBtn = box.addButton(tr("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(saveBtn);
    box.exec();

    if (box.clickedButton() == cancelBtn)
        return false;
    if (box.clickedButton() == discardBtn) {
        VFP_DEBUG << "用户选择不保存退出，清除恢复现场";
        m_recovery.clearRecovery();
        return true;
    }

    // 保存并退出：保存失败或用户在保存对话框里取消都不关窗（否则等于悄悄丢修改）
    return saveProjectInteractively();
}

void MainWindow::onExit()
{
    // 退出
    close();
}

void MainWindow::onManageGlobalCameras()
{
    // 管理全局相机（非模态：开着相机管理也能继续操作主界面）
    showAuxDialog(QStringLiteral("globalCameras"), [this]() -> QDialog * {
        auto *dlg = new GlobalCameraDialog(this);
        connect(dlg, &GlobalCameraDialog::logMessage, this, &MainWindow::logMessage);
        return dlg;
    });
}



void MainWindow::onStartExecution()
{
    if (!m_executor)
        return;
    // 运行控制与流程模式解耦（对齐 VisionMaster 的操作逻辑）：任何模式下都能"开始"，暂停中按=继续。
    // 旧实现写死"仅软触发有效"，连续/硬触发模式下点它毫无反应（静默忽略、无任何提示），而这两种模式
    // 此前又靠"切模式自动开跑" → 用户停止后既找不到启动入口、点开始也没反应（现场反馈"点了没用"）。
    const ExecutionState st = m_executor->getState();
    if (st == ExecutionState::Running || st == ExecutionState::Paused) {
        m_executor->resumeExecution();   // 已在跑/暂停：按"开始"视为继续，不留"点了没反应"的死按钮
        return;
    }
    // 明确点"开始执行"= 正常跑一遍，退出上次遗留的单步模式
    // （历史缺陷：单步模式粘住，之后每次"开始执行"仍在每个节点后暂停）
    m_executor->exitStepMode();
    m_executor->startExecution();
}

void MainWindow::onPauseResumeExecution()
{
    if (!m_executor)
        return;
    const ExecutionState st = m_executor->getState();
    if (st == ExecutionState::Running)
        m_executor->pauseExecution();
    else if (st == ExecutionState::Paused)
        m_executor->resumeExecution();
    // 其余状态按钮为禁用态，不会到达这里
}

void MainWindow::onSingleShotExecution()
{
    // 单次执行：仅在软触发模式可用（按钮在连续/硬触发模式下已禁用），运行一次当前流程
    if (m_executor->getState() == ExecutionState::Running) return;
    if (m_executor->getFlowMode() != FlowMode::SoftwareTrigger) return;
    m_executor->startExecution();
}

void MainWindow::onStopExecution()
{
    // 停止执行
    m_executor->stopExecution();
}

void MainWindow::onLoadProject()
{
    // 加载项目：路径选择在 ProjectManager（取消返回空串）
    const QString fileName = ProjectManager::askOpenProjectPath(this);
    if (!fileName.isEmpty()) {
        loadProjectFile(fileName);
    }
}

void MainWindow::loadProjectFile(const QString &fileName, bool recoveryRestore)
{
    // 崩溃恢复也走这条路径（复用唯一的"接管方案"实现），但它不算一次"打开方案"：
    // 不写"加载项目"日志、不记入最近文件、不把内容标为已落盘（详见函数末尾）。
    if (!recoveryRestore) {
        // 打开方案会替换当前方案：有未保存改动时先确认（该检查覆盖"最近打开"等所有入口）
        if (!confirmDiscardUnsavedChanges(tr("打开方案")))
            return;
        logMessage(tr("加载项目: %1").arg(fileName));
    }

    // 使用ProjectManager加载项目
    ProjectManager *pm = projectManager();

    // 加密信封：打开前若需口令则弹窗索取；取消则放弃本次打开（保持当前方案）。
    QString passphrase;
    if (ProjectManager::fileNeedsPassphrase(fileName)) {
        bool ok = false;
        passphrase = QInputDialog::getText(this, tr("打开加密方案"),
            tr("该方案已加密，请输入口令："), QLineEdit::Password, QString(), &ok);
        if (!ok)
            return; // 用户取消
    }

    // 先加载到临时列表：只有加载成功才销毁当前方案。
    // 原实现是先 qDeleteAll 再加载，文件损坏时旧方案已丢、新方案为空，且仍报“加载成功”。
    QList<FlowScene*> loadedScenes;
    const bool success = pm->loadProject(fileName, loadedScenes, passphrase);

    if (!success) {
        qDeleteAll(loadedScenes);
        if (ProjectManager::fileNeedsPassphrase(fileName))
            logMessage(tr("项目加载失败：口令错误或文件损坏（当前方案保持不变）"));
        else
            logMessage(tr("项目加载失败：文件不存在、格式损坏或内容为空（当前方案保持不变）"));
        return;
    }
    applyReadonlyUI();

    // 释放旧方案的执行器：先停全部并等待真正退出；超时则中止本次打开（N1）——保留旧方案
    // 场景与执行器，不删场景、不回收执行器。运行中的线程若此刻删场景 = UAF，比崩溃更该避免的是
    // 脏数据；这里直接中止切换，旧流程被 stop 后会在算子返回自然退出，方案保持原样可再用。
    QList<FlowExecutor *> oldExecutors;
    for (FlowExecutor *ex : m_flowExecutors) {
        if (ex && !oldExecutors.contains(ex))
            oldExecutors.append(ex);
    }
    if (m_executor && !oldExecutors.contains(m_executor))
        oldExecutors.append(m_executor);

    for (FlowExecutor *ex : oldExecutors)
        ex->stopExecution();
    // 统一等待真正退出：共享 15s 总预算（与 retireExecutor 阈值一致），避免 N 个执行器串行
    // wait(15000) 致 UI 最坏冻结 15s×N。超时即中止。
    const qint64 deadlineMs = QDateTime::currentMSecsSinceEpoch() + 15000;
    QList<FlowExecutor *> stuckExecutors;
    for (FlowExecutor *ex : oldExecutors) {
        if (!ex->isRunning())
            continue;
        const qint64 left = qMax(qint64(0), deadlineMs - QDateTime::currentMSecsSinceEpoch());
        if (left <= 0 || !ex->wait(left))
            stuckExecutors.append(ex);
    }
    if (!stuckExecutors.isEmpty()) {
        // 超时分支联动中止：不删场景、不回收执行器，旧方案保持完整（含仍运行的线程，其场景未删）。
        // 顺手释放本次已加载但未启用的新场景（loadedScenes 仍持有它们），否则泄漏。
        qDeleteAll(loadedScenes);
        loadedScenes.clear();
        logMessage(tr("有流程执行线程超时未退出，已取消打开方案"));
        QMessageBox::warning(this, tr("提示"),
            tr("有流程仍在执行且未能在限定时间内退出，已取消打开方案以避免程序崩溃。"));
        return;
    }

    FlowExecutor *spareExecutor = nullptr;
    for (FlowExecutor *ex : oldExecutors) {
        if (!ex)
            continue;
        GlobalTriggerManager::instance()->unregisterExecutor(ex);
        ex->stopExecution();
        ex->setFlowScene(nullptr);
        if (!spareExecutor) {
            spareExecutor = ex;   // 保留一个作为占位执行器，供新方案首个流程复用
        } else {
            retireExecutor(ex);   // 安全回收：stop+wait，超时绝不 terminate
        }
    }
    m_flowExecutors.clear();
    m_executor = spareExecutor;
    if (spareExecutor)
        m_flowExecutors.insert(nullptr, spareExecutor);

    // 清掉旧场景在图像显示控制器里的显式显示源键，避免悬垂指针（removeScene 漏 loadProjectFile 调用点）
    for (FlowScene *scene : qAsConst(m_flowScenes))
        m_imageDisplay->removeScene(scene);

    // N11 陈旧 row：结果表 + 变量缓存属于"上一方案"的数据，切方案后必须清空，
    // 否则一直显示旧流程的模块与输出、变量引用菜单给出已失效的 moduleId。
    if (m_auxPanels) {
        if (auto *rt = m_auxPanels->resultTablePanel())
            rt->clearResults();
    }
    m_lastModuleVars.clear();

    // 清空现有的流程场景
    qDeleteAll(m_flowScenes);
    m_flowScenes.clear();

    // 清空标签页
    while (ui->flowTabs->count() > 0) {
        QWidget *widget = ui->flowTabs->widget(0);
        ui->flowTabs->removeTab(0);
        delete widget;
    }

    logMessage(tr("项目加载成功"));
    if (!recoveryRestore)
        m_recentFiles->add(fileName);

    // 添加加载的场景到标签页，并逐个注册到全局触发管理器。
    // 修复：此前只有 createNewFlow 才做 flowName/registerFlow，载入方案的所有流程在 GTM 里
    // 无名无映射 → 硬触发/外部触发全灭（触发链路静默失效）。
    m_flowModes.clear();   // S5：载入前清空旧流程模式映射，避免已删场景的 stale 条目被复用继承

    for (int i = 0; i < loadedScenes.size(); ++i) {
        FlowScene *scene = loadedScenes[i];
        m_flowScenes.append(scene);

        // S2：优先用方案持久化的流程名（保持触发绑定身份），缺失时再按全局唯一名分配，
        // 避免"按页签序号重建名"把触发绑定平移到别的流程（比空路由更危险的错触发）。
        QString flowName = scene->flowName();
        if (flowName.isEmpty())
            flowName = GlobalTriggerManager::instance()->allocFlowName();
        scene->setFlowName(flowName);   // 回写，保证重新保存时身份一致

        // 每流程运行模式：优先用方案里存的（不是所有流程都要连续），缺失/越界再回落软触发；
        // 同时写回 m_flowModes（组合框/按钮态都读它）并同步给该流程的执行器。
        const int persistedMode = scene->flowMode();
        const FlowMode flowMode = (persistedMode >= 0 && persistedMode <= 2)
                                      ? static_cast<FlowMode>(persistedMode)
                                      : FlowMode::SoftwareTrigger;
        m_flowModes[scene] = flowMode;
        scene->setFlowMode(static_cast<int>(flowMode));   // F-2：越界脏值（如手工改文件写入 7）在此规范化为 0/1/2，
                                                           // 否则下次保存会把脏值原样写回、永远靠下游兜底
        const QString modeSuffix = flowModeSuffix(flowMode);   // F-3：后缀映射单一来源（越界行为已统一）

        QGraphicsView *view = new QGraphicsView(scene);
        VisionWorkbenchStyle::applyGraphicsViewWorkbenchDefaults(view);
        view->setDragMode(QGraphicsView::RubberBandDrag);
        view->setRubberBandSelectionMode(Qt::IntersectsItemShape);
        view->setFocusPolicy(Qt::StrongFocus);
        ui->flowTabs->addTab(view, flowName + modeSuffix);   // 显示名与路由名一致，并标出该流程模式

        // 连接信号
        hookFlowScene(scene);

        // 每个流程独立执行器 + 流程名注册（多流程并发：executorForScene 复用占位 spare 给首个
        // 流程、其余各自新建；与 createNewFlow 同一套 setFlowName/registerFlow，保证触发按名路由）。
        FlowExecutor *flowEx = executorForScene(scene);
        if (flowEx) {
            flowEx->setFlowScene(scene);
            flowEx->setFlowName(flowName);
            flowEx->setFlowMode(flowMode);   // 载入即按方案里的模式（连续/软触发/硬触发）生效
            GlobalTriggerManager::instance()->registerFlow(flowName, scene, flowEx);
        }
    }

    // 载入方案后按"每流程模式"自启动（现场选 B：无人值守产线要连续流程自动开跑）。
    // 规则：只对**连续**模式自动开始；软触发/硬触发仍由操作员显式点「开始执行」
    // （硬触发是"点开始进入等待"，自动进入等待会让流程在无人确认时等待触发，故不自动）。
    // 延到事件循环下一拍执行：让载入/建标签页/注册执行器先收尾，避免在载入过程中就起线程跑图。
    QMetaObject::invokeMethod(this, [this]() {
        for (FlowScene *scene : m_flowScenes) {
            if (!scene)
                continue;
            const auto it = m_flowModes.constFind(scene);
            if (it == m_flowModes.cend() || it.value() != FlowMode::Continuous)
                continue;
            FlowExecutor *ex = executorForScene(scene);
            if (!ex)
                continue;
            const ExecutionState st = ex->getState();
            if (st == ExecutionState::Running || st == ExecutionState::Paused)
                continue;   // 已在运行的流程不重复启动
            ex->startExecution();
            logMessage(QStringLiteral("载入方案：「%1」为连续模式，已自动开始运行（可用「暂停/停止」控制）")
                           .arg(scene->flowName().isEmpty() ? QStringLiteral("未命名流程")
                                                            : scene->flowName()));
        }
    }, Qt::QueuedConnection);

    // 设置当前场景（多流程并发：切换到该流程的执行器）
    if (!m_flowScenes.isEmpty()) {
        m_executor = executorForScene(m_flowScenes[0]);
        ui->flowTabs->setCurrentIndex(0);
    }
    checkHalconNodesHint();

    // 记录"当前内容已落盘"基线（内容此时与刚读入的文件一致）。
    // 崩溃恢复例外：恢复出来的内容还没写回原方案文件，必须保持"未保存"状态，
    // 否则关窗时不会提示保存，用户会以为已经存好了——丢工作的经典路径。
    if (!recoveryRestore)
        markProjectSaved();
}

void MainWindow::checkHalconNodesHint()
{
    QSettings settings;
    if (settings.value(QStringLiteral("skipHalconNodeHint"), false).toBool()) {
        return;
    }

    QStringList names;
    for (FlowScene *scene : m_flowScenes) {
        const QList<NodeBase *> nodes = scene->nodes();
        for (NodeBase *node : nodes) {
            if (qobject_cast<DeepOcrNode *>(node)
                || qobject_cast<HalconImageSourceNode *>(node)) {
                if (names.size() < 8)
                    names << node->name();
            }
        }
    }
    if (names.isEmpty())
        return;

    QMessageBox box(QMessageBox::Information,
                    QStringLiteral("HALCON 运行时"),
                    QStringLiteral("本方案使用 HALCON 运行时：%1。\n\n"
                                   "日常算法（二值化、匹配、卡尺、缺陷等）走 OpenCV，"
                                   "不依赖 HALCON 区域/测量。\n"
                                   "请确认本机 HALCON 可用于读图与 DeepOCR。")
                        .arg(names.join(QStringLiteral("、"))),
                    QMessageBox::Ok);
    QCheckBox *rememberBox = new QCheckBox(QStringLiteral("不再提示"));
    box.setCheckBox(rememberBox);
    box.exec();
    if (rememberBox->isChecked()) {
        settings.setValue(QStringLiteral("skipHalconNodeHint"), true);
    }
}

void MainWindow::runHalconEnvCheck()
{
    QString detail;
    const HalconEnvStatus status = halconEnvironmentCheck(&detail);
    const QString title = (status == HalconEnvStatus::Ok)
                              ? QStringLiteral("HALCON 图像层可用")
                              : QStringLiteral("HALCON 图像层不可用");
    QMessageBox box(status == HalconEnvStatus::Ok ? QMessageBox::Information : QMessageBox::Warning,
                    title, detail, QMessageBox::Ok, this);
    box.exec();
}

void MainWindow::onAddFlowTab()
{
    // 添加流程标签页
    createNewFlow();
}

void MainWindow::onCloseFlowTab(int index)
{
    if (index < 0 || index >= m_flowScenes.size() || index >= ui->flowTabs->count())
        return;

    // 保存指针
    QWidget *tabWidget = ui->flowTabs->widget(index);
    FlowScene *scene = m_flowScenes[index];
    m_flowModes.remove(scene);
    m_imageDisplay->removeScene(scene);   // 清掉已删场景的显式显示源键，避免悬垂指针
    // 如果执行器正运行此场景，先停止；未确认退出前不得继续（后面会删除场景，
    // 执行线程仍在跑就是 use-after-free）。历史实现只等 1 秒且不检查返回值。
    FlowExecutor *sceneEx = executorForScene(scene);
    if (sceneEx) {
        sceneEx->stopExecution();
        if (sceneEx->isRunning() && !sceneEx->wait(6000)) {
            VFP_DEBUG << "流程未能在 6s 内停止，暂不关闭该流程页";
            ui->statusBar->showMessage(QStringLiteral("流程未能在 6 秒内停止，请稍后再关闭"), 5000);
            return;
        }
    }

    // 先从列表中移除场景（这样 removeTab 触发 currentChanged 时索引已同步）
    m_flowScenes.removeAt(index);

    // 再移除标签页（删除 QGraphicsView，使其与场景断开）
    ui->flowTabs->removeTab(index);
    delete tabWidget;

    // 断开执行器与被删场景的关联，并释放场景专属执行器
    if (sceneEx) {
        sceneEx->setFlowScene(nullptr);
        GlobalTriggerManager::instance()->unregisterExecutor(sceneEx);
        m_flowExecutors.remove(scene);
        if (m_flowScenes.isEmpty()) {
            // 最后一个流程：执行器放回占位，等待下次复用
            m_flowExecutors.insert(nullptr, sceneEx);
            m_executor = sceneEx;
        } else if (sceneEx != m_executor) {
            retireExecutor(sceneEx);
        } else {
            // 关闭的是当前激活流程：回收其专属执行器（已摘场景），再切到邻近流程的执行器；
            // 旧实现漏回收 → 每关一次激活页泄漏一个执行器（存量）。
            retireExecutor(sceneEx);
            m_executor = executorForScene(
                m_flowScenes[qMin(index, m_flowScenes.size() - 1)]);
        }
    }

    // 最后安全删除场景
    delete scene;
}

void MainWindow::onSwitchLanguage()
{
    // 切换语言
    logMessage("切换语言");
}

void MainWindow::logMessage(const QString &message)
{
    ui->logTextEdit->appendPlainText(QString("[%1] %2").arg(QDateTime::currentDateTime().toString()).arg(message));
}

void MainWindow::onNodeExecuted(NodeBase *executedNode, bool success)
{
    if (success) {
        ui->statusBar->showMessage(tr("节点 %1 执行成功").arg(executedNode->name()));

        // Get output data from the node (use port index 0)
        QSharedPointer<DataObject> outputData = executedNode->getOutputData(0);
        if (outputData && outputData->getType() == DataObject::DataType::Image) {
            const HImage image = outputData->getHImage();
            if (image.IsInitialized()) {
                // 经 ImageDisplayController 显示：刷新图像**并**叠加图元（连续运行下叠加必须
                // 每节点更新，否则停留在上一个节点）。原内联实现只 setImage 不刷叠加，是半成品。
                m_imageDisplay->showImage(image, QString(), executedNode);
            }
        }
    } else {
        ui->statusBar->showMessage(tr("节点 %1 执行失败").arg(executedNode->name()));
    }
    
    // 更新参数面板，确保模块状态和其他输出参数正确显示
    if (m_selectedNode == executedNode) {
        QDockWidget *parameterDock = findChild<QDockWidget*>("parameterDock");
        if (parameterDock) {
            QWidget *parameterPanel = parameterDock->widget();
            if (parameterPanel) {
                // 查找参数面板中的所有子控件
                QList<QWidget*> childWidgets = parameterPanel->findChildren<QWidget*>();
                for (QWidget *childWidget : childWidgets) {
                    // 检查是否是标签页控件
                    QTabWidget *tabWidget = qobject_cast<QTabWidget*>(childWidget);
                    if (tabWidget) {
                        // 查找输出标签页
                        QWidget *outputTab = tabWidget->findChild<QWidget*>("outputTab");
                        if (outputTab) {
                            // 只更新输出参数面板，不更新输入参数和图像显示参数
                            executedNode->updateParamPanel(outputTab);
                            VFP_DEBUG << "执行后输出参数面板已更新";
                        }
                    }
                }
            }
        }
    }
}

void MainWindow::onExecutionStarted()
{
    m_execStatus->onStarted();   // 状态栏 + 计时开始 + 按钮态
    refreshAllMvsPixelFormats();
    updateEditLockForCurrentScene();
}

void MainWindow::onExecutionStopped()
{
    m_execStatus->onStopped();   // 状态栏 + 耗时结算 + 按钮态
    refreshAllMvsPixelFormats();
    updateEditLockForCurrentScene();
}

void MainWindow::onExecutionFinished()
{
    m_execStatus->onFinished();   // 状态栏 + 触发计数 + 耗时 + 按钮态（连续/触发模式判定在控制器内）
    fireSendEventsForRound();     // 每轮结束自动上报已启用的发送事件
    refreshAllMvsPixelFormats();
    updateEditLockForCurrentScene();
}

void MainWindow::checkYieldTarget(FlowExecutor *executor)
{
    if (!executor)
        return;

    // 目标与窗口来自配置（报表窗口里的"良率目标"写的就是这个键；0 = 未设目标 = 关闭）
    QSettings settings;
    YieldMonitor::Config config;
    config.targetPercent = settings.value(QStringLiteral("reporting/yieldTargetPercent"), 0.0).toDouble();
    config.windowRounds = settings.value(QStringLiteral("reporting/yieldWindowRounds"), 50).toInt();
    if (!config.isValid())
        return;

    // 节流：连续模式每秒可能几十轮，每轮都查库会拖住 UI 线程；报警不需要秒级实时
    constexpr qint64 kThrottleMs = 5000;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_yieldLastEvalMs > 0 && now - m_yieldLastEvalMs < kThrottleMs)
        return;
    m_yieldLastEvalMs = now;

    const QString flowName = executor->flowName();
    YieldMonitor &monitor = m_yieldMonitors[flowName];
    monitor.setConfig(config);   // 目标改了立即生效（并重置闭锁，见 YieldMonitor::setConfig）

    // 取最近 24h 的记录，与报表同一个查询接口；limit 截断时是"最近的若干条"，够窗口用
    AppDatabase *db = AppDatabase::instance();
    const QList<InspectionRecord> records =
        db->queryResults(QDateTime::currentDateTime().addSecs(-24 * 3600), QDateTime::currentDateTime(),
                         5000);

    QList<InspectionRecord> rounds;
    for (const InspectionRecord &r : records) {
        if (r.nodeName == kRoundSummaryNodeName && r.flowName == flowName)
            rounds.append(r);
    }
    // 不假定 SQL 的返回顺序：自己按时间升序排（窗口取"最近 N 轮"，顺序错就会取到老数据）
    std::sort(rounds.begin(), rounds.end(), [](const InspectionRecord &a, const InspectionRecord &b) {
        return a.timestamp < b.timestamp;
    });
    QList<bool> okFlags;
    okFlags.reserve(rounds.size());
    for (const InspectionRecord &r : rounds)
        okFlags.append(r.passed);

    const YieldMonitor::Result result = monitor.evaluate(okFlags);
    if (!result.evaluated)
        return;   // 样本不足 / 已关闭：不打扰

    if (result.alarmRaised) {
        const QString msg = result.text();
        logMessage(QStringLiteral("⚠ %1：%2").arg(flowName, msg));
        ui->statusBar->showMessage(QStringLiteral("⚠ %1：%2").arg(flowName, msg), 15000);
        // 报警联动：写报警历史（可在「系统 → 报警历史」查看/追溯）
        db->addAlarm(flowName, QStringLiteral("Warning"), msg);
    } else if (result.alarmCleared) {
        const QString msg = result.text();
        logMessage(QStringLiteral("%1：%2").arg(flowName, msg));
        ui->statusBar->showMessage(QStringLiteral("%1：%2").arg(flowName, msg), 8000);
        db->addAlarm(flowName, QStringLiteral("Info"), msg);
    }
}

void MainWindow::fireSendEventsForRound()
{
    // 每轮结束 → 自动上报已启用的发送事件（未配置 / 被禁用 / 设备未连接都会静默跳过，
    // 返回值即"真正发出的条数"，无需在这里判断）。连续模式下本函数会被高频调用，
    // 故用一次性定时器把同一 UI 事件循环周期内的多轮合并为一次上报。
    if (m_sendEventFirePending)
        return;
    m_sendEventFirePending = true;
    QTimer::singleShot(0, this, [this]() {
        m_sendEventFirePending = false;
        // 每轮上报数据注入：{global.变量名} + {模块号.参数名}（与"变量引用"语法一致），
        // 发送事件的文本模板据此把本轮结果格式化进报文（对标 VM 的每轮结果上报）
        QVariantMap payload;
        const auto gvars = GlobalVariableManager::instance()->variables();
        for (auto it = gvars.constBegin(); it != gvars.constEnd(); ++it)
            payload.insert(QStringLiteral("global.%1").arg(it.key()), it.value().value);
        for (auto it = m_lastModuleVars.constBegin(); it != m_lastModuleVars.constEnd(); ++it) {
            const QVariantMap &vars = it.value();
            for (auto jt = vars.constBegin(); jt != vars.constEnd(); ++jt)
                payload.insert(QStringLiteral("%1.%2").arg(it.key()).arg(jt.key()), jt.value());
        }
        CommunicationManager::instance()->fireEnabledSendEvents(payload);
    });
}

void MainWindow::onExecutionError(const QString &error)
{
    m_execStatus->onError(error);   // 状态栏留痕（不弹模态框，理由见控制器注释）
    logMessage(QStringLiteral("执行错误: %1").arg(error));
}

QVector<OverlayShape> MainWindow::collectOverlayFromNode(NodeBase *node) const
{
    // 叠加规则（线/圆/模板/缺陷/卡尺 → OverlayShape）在 ImageDisplayController
    return m_imageDisplay->collectOverlayFromNode(node);
}

void MainWindow::startRoiPick(NodeBase *node)
{
    // 取点状态与 ROI 编辑开关在 ImageDisplayController；这里只做窗口级提示
    if (!m_imageDisplay->startRoiPick(node))
        return;
    ui->statusBar->showMessage(tr("请在图像上拖拽绘制搜索区域（右键取消）"), 5000);
}

void MainWindow::handleRoiEdited(const RoiShape &shape)
{
    // 参数写回（各测量节点的行列/半径）在 ImageDisplayController；这里做提示与面板刷新
    NodeBase *node = m_imageDisplay->writeRoiToNode(shape);
    if (!node)
        return;
    ui->statusBar->showMessage(tr("ROI 已写入 %1 参数").arg(node->name()), 3000);
    // 刷新参数面板显示
    showNodeParameters(m_selectedNode);
}

void MainWindow::openManualDialog()
{
    // 手册路径优先级：exe 同目录 docs/（现场可替换）> 仓库 docs/ > 嵌入资源（qrc 兜底）
    QStringList candidates;
    const QString exeDir = QCoreApplication::applicationDirPath();
    candidates << exeDir + QStringLiteral("/docs/用户操作手册.md")
               << QStringLiteral("docs/用户操作手册.md")
               << exeDir + QStringLiteral("/用户操作手册.md")
               << QStringLiteral(":/用户操作手册.md");

    QString markdown;
    for (const QString &path : candidates) {
        QFile f(path);
        if (f.exists() && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            markdown = QString::fromUtf8(f.readAll());
            break;
        }
    }

    if (markdown.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("使用手册"),
                             QStringLiteral("未找到手册文件（docs/用户操作手册.md）。\n"
                                            "请确认部署包中包含 docs 目录。"));
        return;
    }

    // 使用手册非模态：可以边看手册边操作主界面（重复打开前置已有窗口）
    showAuxDialog(QStringLiteral("manual"), [this, markdown]() -> QDialog * {
        return new HelpDialog(markdown, this);
    });
}

void MainWindow::refreshAllMvsPixelFormats(){
    for (FlowScene *scene : m_flowScenes) {
        if (!scene) continue;
        for (NodeBase *node : scene->nodes()) {
            auto *mvs = qobject_cast<MvsImageSourceNode*>(node);
            if (mvs) {
                mvs->refreshPixelFormatEnabled();
            }
        }
    }
}

void MainWindow::updateEditLockForCurrentScene()
{
    int tabIdx = ui->flowTabs->currentIndex();
    if (tabIdx < 0 || tabIdx >= m_flowScenes.size()) return;

    FlowScene *scene = m_flowScenes[tabIdx];
    // S1 / Phase B 小步：改用执行器判据——"执行线程真在跑"或"暂停已受理但 worker 尚未停稳"才锁；
    // Idle/Stopped 与"已停稳的暂停"都允许编辑。旧的"连续模式一律锁"是钝器：连续模式空闲（未开始/
    // 已停止）时也编辑不了，而暂停恰恰是用户最想改图的时刻（恢复后新图下一轮生效）。
    scene->setEditLocked(m_executor ? !m_executor->allowsGraphEditing() : false);
}

void MainWindow::onCurrentTabChanged(int index)
{
    if (index < 0 || index >= m_flowScenes.size())
        return;

    // 运行/暂停中禁止切换流程：执行器绑定的是正在执行的场景，重绑会让"本轮还在
    // 遍历旧场景节点、下一轮就去执行新场景"（现场表现为"切一下 tab 流程乱跑"），
    // 多流程并发下还可能让同一批节点被两个执行器同时执行。
    if (m_executor) {
        const ExecutionState st = m_executor->getState();
        if (st == ExecutionState::Running || st == ExecutionState::Paused) {
            ui->statusBar->showMessage(QStringLiteral("流程运行中，停止后才能切换流程"), 5000);
            const int cur = m_flowScenes.indexOf(m_executor->flowScene());
            if (cur >= 0 && cur != index) {
                QSignalBlocker blocker(ui->flowTabs);   // 回退页签，屏蔽信号防递归
                ui->flowTabs->setCurrentIndex(cur);
            }
            return;
        }
    }

    FlowScene *scene = m_flowScenes[index];
    // 一场景一执行器：切 tab 直接切换到该场景专属执行器（executorForScene 保证存在/自动创建），
    // 不再把同一个全局执行器重绑到新场景——这正是「两 tab 来回切 + 右键执行 → 两执行器并发
    // 跑同批节点」的根因（executorForScene 拉取策略与全局重绑互相打架）。
    m_executor = executorForScene(scene);
    if (m_executor)
        m_executor->setFlowScene(scene);

    // 性能面板跟随当前激活执行器重绑（⑤b）：此前只绑首个执行器、切 tab 不重绑，
    // 后台跑首流程会 clearStats 清掉正在看的统计；切 tab 后让面板显示当前流程的耗时。
    if (auto *pp = m_auxPanels ? m_auxPanels->performancePanel() : nullptr)
        pp->bindExecutor(m_executor);

    // 恢复该流程存储的运行模式
    if (m_flowModeCombo) {
        auto it = m_flowModes.constFind(scene);
        FlowMode mode = (it != m_flowModes.cend()) ? it.value() : FlowMode::SoftwareTrigger;
        {
            QSignalBlocker blocker(m_flowModeCombo);
            m_flowModeCombo->setCurrentIndex(static_cast<int>(mode));
        }
        m_executor->setFlowMode(mode);
        // 按钮态单一来源：更新 开始/停止/单次执行 可用性（连续/硬件已 startExecution → Running）
        if (m_execStatus)
            m_execStatus->updateButtons(m_executor ? m_executor->getState() : ExecutionState::Stopped);

        // 更新标签页标题，显示模式后缀
        const int mi = static_cast<int>(mode);
        const QString suffix = flowModeSuffix(mode);   // F-3：后缀映射单一来源
        // tab 标题用真实流程名，保持与触发路由名一致
        const QString base = (m_executor && !m_executor->flowName().isEmpty())
                                ? m_executor->flowName()
                                : QStringLiteral("流程 %1").arg(index + 1);
        ui->flowTabs->setTabText(index, base + suffix);

        // 状态栏提示
        static const char *modeNames[] = { "连续模式", "软触发模式", "硬触发模式" };
        const char *modeName = (mi >= 0 && mi < 3) ? modeNames[mi] : "未知";
        ui->statusBar->showMessage(QStringLiteral("流程 %1 — %2").arg(index + 1).arg(modeName), 5000);
    }
    updateEditLockForCurrentScene();
}



void MainWindow::setupToolLibrary()
{
    // 工具库由声明式注册表 NodeRegistry 自动生成
    VFP_DEBUG << "setupToolLibrary called (registry-driven)";

    // 工具库顶部搜索框（仅创建一次）
    QLineEdit *searchBox = ui->toolDockContents->findChild<QLineEdit *>(QStringLiteral("toolSearchEdit"));
    if (!searchBox) {
        searchBox = new QLineEdit(ui->toolDockContents);
        searchBox->setObjectName(QStringLiteral("toolSearchEdit"));
        searchBox->setPlaceholderText(QStringLiteral("搜索算子..."));
        searchBox->setClearButtonEnabled(true);
        searchBox->setStyleSheet(
            "QLineEdit {"
            "  background-color: #2b2b3d; color: #e0e0e0;"
            "  border: 1px solid #4a6a9c; border-radius: 4px;"
            "  padding: 4px 8px; font-size: 13px;"
            "}"
            "QLineEdit:focus { border-color: #3a6ea5; }"
        );
        ui->toolDockLayout->insertWidget(0, searchBox);
    }

    // 清空工具库
    ui->toolLibrary->clear();
    ui->toolLibrary->setHeaderHidden(true);
    ui->toolLibrary->setMinimumWidth(200);
    ui->toolLibrary->setIndentation(20);
    ui->toolLibrary->setAnimated(true);
    ui->toolLibrary->setAlternatingRowColors(true);

    registerAllNodes();
    auto &registry = NodeRegistry::instance();

    // 按分组生成树
    for (const QString &group : registry.groups()) {
        QTreeWidgetItem *groupItem = new QTreeWidgetItem(ui->toolLibrary);
        groupItem->setText(0, group);

        for (const NodeRegistration *reg : registry.byGroup(group)) {
            auto *item = new QTreeWidgetItem(groupItem);
            item->setText(0, reg->displayName);
            item->setData(0, Qt::UserRole, reg->id);
            if (!reg->description.isEmpty()) {
                item->setToolTip(0, reg->description);
            }

            // 设置图标
            if (!reg->iconPath.isEmpty()) {
                QIcon icon(reg->iconPath);
                if (!icon.isNull()) {
                    item->setIcon(0, icon);
                }
            } else {
                // 根据节点类型生成默认图标
                QIcon icon = generateNodeIcon(reg->category);
                item->setIcon(0, icon);
            }
        }
        groupItem->setExpanded(true);
    }

    // 搜索过滤：隐藏不匹配的算子，空结果显示分组隐藏
    connect(searchBox, &QLineEdit::textChanged, this, [this](const QString &text) {
        const QString q = text.trimmed();
        for (int gi = 0; gi < ui->toolLibrary->topLevelItemCount(); ++gi) {
            QTreeWidgetItem *group = ui->toolLibrary->topLevelItem(gi);
            int visible = 0;
            for (int ci = 0; ci < group->childCount(); ++ci) {
                QTreeWidgetItem *child = group->child(ci);
                const bool match = q.isEmpty()
                                   || child->text(0).contains(q, Qt::CaseInsensitive)
                                   || (q.size() >= 2 && child->data(0, Qt::UserRole).toString().contains(q, Qt::CaseInsensitive));
                child->setHidden(!match);
                if (match) ++visible;
            }
            group->setHidden(visible == 0);
            if (visible > 0) group->setExpanded(true);
        }
    });

    // 连接工具库的点击信号
    connect(ui->toolLibrary, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int column) {
        QString nodeType = item->data(0, Qt::UserRole).toString();
        if (!nodeType.isEmpty()) {
            addNode(nodeType);
        }
    });

    // 启用拖拽功能
    ui->toolLibrary->setDragEnabled(true);

    // 为工具库添加拖拽事件过滤器
    ui->toolLibrary->viewport()->installEventFilter(this);
}

QIcon MainWindow::generateNodeIcon(NodeBase::NodeType category) const
{
    // 根据节点类型生成不同颜色的图标
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor color;
    switch (category) {
    case NodeBase::IMAGE_ACQUISITION:
        color = QColor(0x18, 0x90, 0xff);  // 蓝色 - 图像采集
        break;
    case NodeBase::IMAGE_PROCESSING:
        color = QColor(0x52, 0xc4, 0x1a);  // 绿色 - 图像处理
        break;
    case NodeBase::SHAPE_ANALYSIS:
        color = QColor(0xfa, 0xad, 0x14);  // 黄色 - 形状分析
        break;
    case NodeBase::MEASUREMENT:
        color = QColor(0xff, 0x4d, 0x4f);  // 红色 - 测量
        break;
    case NodeBase::LOGIC:
        color = QColor(0x72, 0x2e, 0xd1);  // 紫色 - 逻辑控制
        break;
    case NodeBase::OUTPUT:
        color = QColor(0xff, 0x76, 0x32);  // 橙色 - 输出
        break;
    default:
        color = QColor(0x8c, 0x8c, 0x8c);  // 灰色
        break;
    }

    // 绘制圆形图标
    painter.setBrush(color);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(2, 2, 12, 12);

    // 绘制白色边框
    painter.setPen(QPen(Qt::white, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(2, 2, 12, 12);

    return QIcon(pixmap);
}

void MainWindow::setupFlowEditor()
{
    // 设置流程编辑器
    VFP_DEBUG << "setupFlowEditor called";
    
    // 为每个流程场景设置拖拽支持
    for (FlowScene *scene : m_flowScenes) {
        setupFlowSceneDragDrop(scene);
    }
    
    // 连接标签页切换信号
    connect(ui->flowTabs, &QTabWidget::currentChanged, this, &MainWindow::onCurrentTabChanged);

    // 标签页红色 X 关闭按钮
    connect(ui->flowTabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        onCloseFlowTab(index);
    });
}

void MainWindow::setupFlowSceneDragDrop(FlowScene *scene)
{
    // 设置流程场景的拖拽支持
    // FlowScene已经继承了QGraphicsScene的拖拽功能
    
    // 这里可以添加更多的拖拽相关设置
    // 例如：设置拖拽进入、拖拽移动、拖拽释放等事件处理
}

void MainWindow::setupRightPanel()
{
    VFP_DEBUG << "setupRightPanel called (dock mode)";

    if (ui->imageDock) {
        ui->imageDock->setMinimumWidth(380);
        ui->imageDock->setMinimumHeight(280);
    }
    if (ui->paramDock) {
        ui->paramDock->setMinimumWidth(280);
        ui->paramDock->setMinimumHeight(140);
    }
    if (m_imageView) {
        m_imageView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_imageView->setMinimumSize(320, 240);
    }
}

void MainWindow::setupDockWidgets()
{
    VFP_DEBUG << "setupDockWidgets called";

    setDockOptions(AnimatedDocks | AllowNestedDocks | AllowTabbedDocks);
    setDockNestingEnabled(true);

    // === 流程编辑放到中央：上方为品牌 Logo 栏（FR4.4），右侧图像/参数可拖分隔条调整 ===
    if (ui->flowTabs) {
        ui->flowTabs->setParent(nullptr);

        auto *central = new QWidget(this);
        auto *centralLayout = new QVBoxLayout(central);
        centralLayout->setContentsMargins(0, 0, 0, 0);
        centralLayout->setSpacing(0);

        m_brandHeader = new BrandHeader(central);
        centralLayout->addWidget(m_brandHeader);
        centralLayout->addWidget(ui->flowTabs, 1);

        setCentralWidget(central);
    }
    if (ui->flowDock) {
        removeDockWidget(ui->flowDock);
        ui->flowDock->hide();
    }

    // === 工具箱 (左) ===
    if (ui->toolDock) {
        ui->toolDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
        ui->toolDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        ui->toolDock->setMinimumWidth(180);
        addDockWidget(Qt::LeftDockWidgetArea, ui->toolDock);
    }

    // === 图像显示 (右上，默认可拉大) ===
    if (ui->imageDock) {
        ui->imageDock->setFeatures(QDockWidget::DockWidgetClosable
                                   | QDockWidget::DockWidgetMovable
                                   | QDockWidget::DockWidgetFloatable);
        ui->imageDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        ui->imageDock->setMinimumWidth(380);
        ui->imageDock->setMinimumHeight(280);
        addDockWidget(Qt::RightDockWidgetArea, ui->imageDock);
    }

    // === 参数面板 (右侧，与图像同为「子页面」，FR4.3) ===
    if (ui->paramDock) {
        ui->paramDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
        ui->paramDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        ui->paramDock->setMinimumWidth(280);
        addDockWidget(Qt::RightDockWidgetArea, ui->paramDock);
        if (ui->imageDock) {
            // FR4.3：右侧区域包含「图像显示」与「参数配置」两个子页面，可切换显示。
            // 用选项卡式停靠实现：点击标签页即可切换；用户若更喜欢上下并排，
            // 直接拖动标签页拆出即可（Qt 原生行为），两种习惯都保留。
            tabifyDockWidget(ui->imageDock, ui->paramDock);
            ui->imageDock->raise();   // 默认停在图像页
        }
    }

    // === 日志 (底) ===
    if (ui->logDock) {
        ui->logDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
        ui->logDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
        addDockWidget(Qt::BottomDockWidgetArea, ui->logDock);
    }

    // === 运行界面（自定义运行界面，运行时显示） ===
    {
        m_runtimeView = new RuntimeInterfaceView(this);
        m_runtimeViewDock = new QDockWidget(QStringLiteral("运行界面"), this);
        m_runtimeViewDock->setObjectName(QStringLiteral("runtimeViewDock"));
        m_runtimeViewDock->setWidget(m_runtimeView);
        m_runtimeViewDock->setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);
        m_runtimeViewDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        m_runtimeViewDock->hide();
        addDockWidget(Qt::RightDockWidgetArea, m_runtimeViewDock);

        // 载入默认布局
        loadRuntimeInterfaceLayout();

        // 运行视图按钮动作 → 流程控制
        connect(m_runtimeView, &RuntimeInterfaceView::actionTriggered, this,
                [this](const QString &actionId) {
            if (actionId == QStringLiteral("start")) {
                if (m_executor->getFlowMode() == FlowMode::SoftwareTrigger)
                    m_executor->startExecution();
            } else if (actionId == QStringLiteral("stop")) {
                m_executor->stopExecution();
            } else if (actionId == QStringLiteral("single")) {
                if (m_executor->getFlowMode() == FlowMode::SoftwareTrigger)
                    m_executor->startExecution();
            } else if (actionId == QStringLiteral("trigger")) {
                // 触发当前流程（软触发一次）
                if (m_executor->getFlowMode() != FlowMode::HardwareTrigger)
                    m_executor->startExecution();
            }
            logMessage(QStringLiteral("运行界面按钮触发: %1").arg(actionId));
        });

        // 全局变量变化 → 运行界面实时更新
        connect(GlobalVariableManager::instance(), &GlobalVariableManager::variableChanged,
                m_runtimeView, &RuntimeInterfaceView::updateVariable);

        // F11：运行界面全屏（dock 浮动 + 无边框全屏；现场大屏直接投全屏，再按 F11 还原）
        auto *runtimeFsShortcut = new QShortcut(QKeySequence(QStringLiteral("F11")), this);
        connect(runtimeFsShortcut, &QShortcut::activated, this, [this]() {
            if (!m_runtimeViewDock) return;
            if (!m_runtimeViewDock->isFullScreen()) {
                m_runtimeViewDock->setFloating(true);
                m_runtimeViewDock->showFullScreen();
                logMessage(QStringLiteral("运行界面已全屏（F11 还原）"));
            } else {
                m_runtimeViewDock->showNormal();
                m_runtimeViewDock->setFloating(false);
                logMessage(QStringLiteral("运行界面已还原"));
            }
        });
    }

    // Connect view menu toggles
    if (ui->actionToggleToolDock && ui->toolDock) {
        connect(ui->actionToggleToolDock, &QAction::toggled,
                ui->toolDock, &QDockWidget::setVisible);
        connect(ui->toolDock, &QDockWidget::visibilityChanged,
                ui->actionToggleToolDock, &QAction::setChecked);
    }
    if (ui->actionToggleParamDock && ui->paramDock) {
        connect(ui->actionToggleParamDock, &QAction::toggled,
                ui->paramDock, &QDockWidget::setVisible);
        connect(ui->paramDock, &QDockWidget::visibilityChanged,
                ui->actionToggleParamDock, &QAction::setChecked);
    }
    if (ui->actionToggleLogDock && ui->logDock) {
        connect(ui->actionToggleLogDock, &QAction::toggled,
                ui->logDock, &QDockWidget::setVisible);
        connect(ui->logDock, &QDockWidget::visibilityChanged,
                ui->actionToggleLogDock, &QAction::setChecked);
    }
    if (ui->actionToggleImageDock && ui->imageDock) {
        connect(ui->actionToggleImageDock, &QAction::toggled,
                ui->imageDock, &QDockWidget::setVisible);
        connect(ui->imageDock, &QDockWidget::visibilityChanged,
                ui->actionToggleImageDock, &QAction::setChecked);
    }
    if (ui->menuView) {
        auto *floatAct = ui->menuView->addAction(QStringLiteral("弹出/还原图像窗口"));
        connect(floatAct, &QAction::triggered, this, &MainWindow::onToggleImageFloat);
    }

    // Design/Runtime mode actions
    if (ui->actionDesignMode) {
        connect(ui->actionDesignMode, &QAction::triggered, this, &MainWindow::switchToDesignMode);
    }
    if (ui->actionRuntimeMode) {
        connect(ui->actionRuntimeMode, &QAction::triggered, this, &MainWindow::switchToRuntimeMode);
    }
    if (ui->actionRuntimeDesigner) {
        connect(ui->actionRuntimeDesigner, &QAction::triggered, this,
                &MainWindow::openRuntimeInterfaceDesigner);
    }

    // 失败中断策略（对标 VisionMaster：失败默认停止，可配置继续）
    if (ui->actionStopOnFailure) {
        QSettings settings;
        const bool stopOnFail =
            settings.value(QStringLiteral("flow/stopOnFailure"), true).toBool();
        ui->actionStopOnFailure->setChecked(stopOnFail);
        // 注意：setupDockWidgets 在 FlowExecutor 创建之前调用，此处判空；
        // 实际设置在 FlowExecutor 创建后应用（见构造函数"初始化执行器"处）
        if (m_executor)
            m_executor->setStopOnFailure(stopOnFail);
        connect(ui->actionStopOnFailure, &QAction::toggled, this, [this](bool on) {
            if (m_executor)
                m_executor->setStopOnFailure(on);
            QSettings s;
            s.setValue(QStringLiteral("flow/stopOnFailure"), on);
            logMessage(on ? QStringLiteral("已启用：节点失败时停止流程")
                          : QStringLiteral("已关闭：节点失败时流程继续执行"));
        });
    }

    // 恢复上次退出时打开的辅助面板（结果表 / 变量 / 性能统计 / 输出数据）
    restoreAuxPanelVisibility();

    VFP_DEBUG << "setupDockWidgets completed";
}

void MainWindow::switchToDesignMode()
{
    VFP_DEBUG << "Switching to Design Mode";
    // 显示所有 dock widgets
    if (ui->toolDock) ui->toolDock->show();
    if (ui->paramDock) ui->paramDock->show();
    if (ui->logDock) ui->logDock->show();

    // 运行界面（自定义运行界面）仅在运行模式显示
    if (m_runtimeViewDock) m_runtimeViewDock->hide();

    // 解锁画布编辑
    for (FlowScene *scene : m_flowScenes) {
        if (scene) {
            QList<QGraphicsView*> views = scene->views();
            for (QGraphicsView *v : views) {
                v->setInteractive(true);
            }
        }
    }
    
    logMessage(QStringLiteral("已切换至设计模式"));
}

void MainWindow::switchToRuntimeMode()
{
    VFP_DEBUG << "Switching to Runtime Mode";
    // 隐藏工具箱 dock
    if (ui->toolDock) ui->toolDock->close();
    
    // 锁定画布编辑
    for (FlowScene *scene : m_flowScenes) {
        if (scene) {
            QList<QGraphicsView*> views = scene->views();
            for (QGraphicsView *v : views) {
                v->setInteractive(false);
            }
        }
    }

    // 若已配置自定义运行界面，则显示自定义运行界面（最大化浮窗），否则退回图像显示最大化
    loadRuntimeInterfaceLayout();
    if (m_runtimeViewDock && m_runtimeView && m_runtimeView->controlCount() > 0) {
        m_runtimeViewVisible = true;
        m_runtimeViewDock->show();
        m_runtimeViewDock->raise();
        if (!m_runtimeViewDock->isFloating())
            m_runtimeViewDock->setFloating(true);
        m_runtimeViewDock->setWindowState(m_runtimeViewDock->windowState() | Qt::WindowMaximized);
    } else {
        m_runtimeViewVisible = false;
        // 最大化图像显示
        if (ui->imageDock) {
            if (!ui->imageDock->isFloating()) {
                ui->imageDock->setFloating(true);
            }
            ui->imageDock->show();
            ui->imageDock->raise();
            ui->imageDock->setWindowState(ui->imageDock->windowState() | Qt::WindowMaximized);
        }
    }
    
    logMessage(QStringLiteral("已切换至运行模式"));
}

QStringList MainWindow::allNodeFullNames() const
{
    QStringList names;
    for (FlowScene *scene : m_flowScenes) {
        if (!scene) continue;
        for (NodeBase *node : scene->nodes()) {
            if (node) names.append(node->fullName());
        }
    }
    return names;
}

void MainWindow::openRuntimeInterfaceDesigner()
{
    // 非模态：开着设计器也能继续操作主界面；「应用并进入运行模式」后刷新运行视图
    showAuxDialog(QStringLiteral("runtimeDesigner"), [this]() -> QDialog * {
        auto *dlg = new RuntimeInterfaceDesigner(allNodeFullNames(), this);
        connect(dlg, &QDialog::accepted, this, &MainWindow::loadRuntimeInterfaceLayout);
        return dlg;
    });
}

void MainWindow::loadRuntimeInterfaceLayout()
{
    if (!m_runtimeView) return;
    RuntimeInterface layout;
    if (layout.loadFromFile(RuntimeInterfaceDesigner::defaultLayoutPath()))
        m_runtimeView->setInterface(layout);
}

void MainWindow::setupLanguageMenu()
{
    // 设置语言菜单
    VFP_DEBUG << "setupLanguageMenu called";
    
    // 连接语言切换动作
    connect(ui->actionChinese, &QAction::triggered, this, [this]() {
        m_currentLanguage = Language::Chinese;
        ui->actionChinese->setChecked(true);
        ui->actionEnglish->setChecked(false);
        updateLanguage();
    });
    
    connect(ui->actionEnglish, &QAction::triggered, this, [this]() {
        m_currentLanguage = Language::English;
        ui->actionEnglish->setChecked(true);
        ui->actionChinese->setChecked(false);
        updateLanguage();
    });
    
    // 默认选择中文
    ui->actionChinese->setChecked(true);
}

void MainWindow::setupSchemeMenu()
{
    // 设置方案/文件菜单
    VFP_DEBUG << "setupSchemeMenu called";
    
    // 连接方案保存和加载动作
    connect(ui->actionSaveScheme, &QAction::triggered, this, &MainWindow::onSaveProject);
    connect(ui->actionOpenScheme, &QAction::triggered, this, &MainWindow::onLoadProject);
    
    // 连接文件菜单动作
    connect(ui->actionNew, &QAction::triggered, this, &MainWindow::onNewProject);
    connect(ui->actionSave, &QAction::triggered, this, &MainWindow::onSaveProject);
    connect(ui->actionLoad, &QAction::triggered, this, &MainWindow::onLoadProject);
    connect(ui->actionExit, &QAction::triggered, this, &MainWindow::onExit);
    
    // 连接编辑菜单动作
    connect(ui->actionAddFlow, &QAction::triggered, this, &MainWindow::onAddFlowTab);
    connect(ui->actionDeleteFlow, &QAction::triggered, this, [this]() {
        int currentIndex = ui->flowTabs->currentIndex();
        onCloseFlowTab(currentIndex);
    });
}

void MainWindow::showAuxDialog(const QString &key, const std::function<QDialog *()> &create)
{
    // 统一委托 AuxPanelManager：同 key 复用、关闭即销毁（非模态，不阻塞主界面）
    m_auxPanels->showDialog(key, create);
}

void MainWindow::setupCommunicationMenu()
{
    VFP_DEBUG << "setupCommunicationMenu called";

    if (!ui->menuCommunication) return;

    // 非模态：打开通讯管理/监视后主界面仍可正常操作；重复点击前置已有窗口而不是再开一个
    connect(ui->actionCommDevice, &QAction::triggered, this, [this]() {
        showAuxDialog(QStringLiteral("commManager"),
                      [this]() -> QDialog * { return new CommunicationManagerDialog(this); });
    });

    connect(ui->actionCommReceiveEvent, &QAction::triggered, this, [this]() {
        showAuxDialog(QStringLiteral("commManager"),
                      [this]() -> QDialog * { return new CommunicationManagerDialog(this); });
    });

    connect(ui->actionCommSendEvent, &QAction::triggered, this, [this]() {
        showAuxDialog(QStringLiteral("commManager"),
                      [this]() -> QDialog * { return new CommunicationManagerDialog(this); });
    });

    connect(ui->actionCommHeartbeat, &QAction::triggered, this, [this]() {
        showAuxDialog(QStringLiteral("commManager"),
                      [this]() -> QDialog * { return new CommunicationManagerDialog(this); });
    });

    connect(ui->actionCommMonitor, &QAction::triggered, this, [this]() {
        showAuxDialog(QStringLiteral("commMonitor"),
                      [this]() -> QDialog * { return new CommMonitorDialog(this); });
    });
}

void MainWindow::setupSystemMenu()
{
    VFP_DEBUG << "setupSystemMenu called";

    if (!ui->menuSystem) return;

    connect(ui->actionGlobalTrigger, &QAction::triggered, this, [this]() {
        showAuxDialog(QStringLiteral("globalTrigger"),
                      [this]() -> QDialog * { return new GlobalTriggerDialog(this); });
    });

    connect(ui->actionGlobalVariables, &QAction::triggered, this, [this]() {
        showAuxDialog(QStringLiteral("globalVariables"),
                      [this]() -> QDialog * { return new GlobalVariableDialog(this); });
    });

    if (ui->menuSystem) {
        QAction *flowVarAct = ui->menuSystem->addAction(QStringLiteral("流程变量 / Fixture…"));
        connect(flowVarAct, &QAction::triggered, this, [this]() {
            const int idx = ui->flowTabs->currentIndex();
            FlowScene *scene = (idx >= 0 && idx < m_flowScenes.size()) ? m_flowScenes[idx] : nullptr;
            if (!scene)
                return;
            showAuxDialog(QStringLiteral("flowVariable"), [this, scene]() -> QDialog * {
                return new FlowVariableDialog(scene, this);
            });
        });
    }

    connect(ui->actionCameraConfig, &QAction::triggered, this, &MainWindow::onManageGlobalCameras);

    connect(ui->actionUserManagement, &QAction::triggered, this, [this]() {
        showAuxDialog(QStringLiteral("userManagement"),
                      [this]() -> QDialog * { return new UserManagementDialog(this); });
    });

    connect(ui->actionRecipeManager, &QAction::triggered, this, [this]() {
        // 取当前流程的场景（与「流程变量」同一取法）：配方保存/加载都要靠它。
        // 必须把场景交进去——否则「新建配方」无从抓取参数（RecipeManager 收到 nullptr
        // 会立刻返回 false）、「加载配方」也无从写回，两处都会静默失败。
        const int idx = ui->flowTabs->currentIndex();
        FlowScene *scene = (idx >= 0 && idx < m_flowScenes.size()) ? m_flowScenes[idx] : nullptr;
        if (!scene) {
            QMessageBox::information(this, QStringLiteral("提示"),
                                     QStringLiteral("请先打开一个流程，再使用配方管理。"));
            return;
        }
        showAuxDialog(QStringLiteral("recipe"), [this, scene]() -> QDialog * {
            auto *dlg = new RecipeDialog(this);
            dlg->setFlowScene(scene);
            return dlg;
        });
    });

    connect(ui->actionAlarmHistory, &QAction::triggered, this, [this]() {
        showAuxDialog(QStringLiteral("alarmHistory"),
                      [this]() -> QDialog * { return new AlarmHistoryDialog(this); });
    });

    connect(ui->actionInspectionResults, &QAction::triggered, this, [this]() {
        showAuxDialog(QStringLiteral("inspectionResults"),
                      [this]() -> QDialog * { return new InspectionResultDialog(this); });
    });

    connect(ui->actionParameterSearch, &QAction::triggered, this, [this]() {
        showAuxDialog(QStringLiteral("paramSearch"),
                      [this]() -> QDialog * { return new ParameterSearchDialog(m_flowScenes, this); });
    });

    connect(ui->actionCodeExport, &QAction::triggered, this, [this]() {
        showAuxDialog(QStringLiteral("codeExport"),
                      [this]() -> QDialog * { return new CodeExportDialog(m_flowScenes, this); });
    });

    connect(ui->actionOperationLog, &QAction::triggered, this, [this]() {
        showAuxDialog(QStringLiteral("operationLog"),
                      [this]() -> QDialog * { return new OperationLogDialog(this); });
    });

    connect(ui->actionReport, &QAction::triggered, this, [this]() {
        showAuxDialog(QStringLiteral("report"),
                      [this]() -> QDialog * { return new ReportDialog(this); });
    });
}

void MainWindow::applyPermissionRestrictions()
{
    auto *session = SessionManager::instance();
    if (!session->isLoggedIn()) return;

    // 用户管理仅 Admin 可见/可用
    if (ui->actionUserManagement) {
        ui->actionUserManagement->setEnabled(session->canManageUsers());
    }
    // 方案编辑/保存需 Engineer 以上
    if (ui->actionSave && ui->actionLoad) {
        bool canEdit = session->canEditScheme();
        ui->actionSave->setEnabled(canEdit);
        ui->actionLoad->setEnabled(canEdit);
        ui->actionNew->setEnabled(canEdit);
    }
    if (ui->menuEdit) {
        ui->menuEdit->setEnabled(session->canEditScheme());
    }
    // 角色变化后同步只读管控（只读态优先于角色权限禁用覆盖保存）
    applyReadonlyUI();
}

void MainWindow::updateLanguage()
{
    // 更新界面语言
    VFP_DEBUG << "updateLanguage called";
    retranslateUi();
}

void MainWindow::retranslateUi()
{
    // 重新翻译UI
    VFP_DEBUG << "retranslateUi called";
    
    if (m_currentLanguage == Language::Chinese) {
        setWindowTitle(QStringLiteral("视觉方案工作台"));
        // 中文翻译
        ui->menuFile->setTitle("文件");
        ui->menuEdit->setTitle("编辑");
        ui->menuView->setTitle("视图");
        ui->menuCommunication->setTitle("通讯");
        ui->menuSystem->setTitle("系统");
        ui->menuLanguage->setTitle("语言");
        
        ui->actionNew->setText("新建方案");
        ui->actionSave->setText("保存方案");
        ui->actionLoad->setText("打开方案");
        ui->actionExit->setText("退出");
        
        ui->actionAddFlow->setText("添加流程");
        ui->actionDeleteFlow->setText("删除流程");
        if (m_actionCreateGroup) m_actionCreateGroup->setText("创建分组");
        if (m_actionDissolveGroup) m_actionDissolveGroup->setText("解散分组");
        if (m_actionToggleGroup) m_actionToggleGroup->setText("折叠/展开分组");
        if (m_actionCopySnippet) m_actionCopySnippet->setText("复制所选算子");
        if (m_actionPasteSnippet) m_actionPasteSnippet->setText("粘贴");
        if (m_actionExportSnippet) m_actionExportSnippet->setText("导出片段…");
        if (m_actionImportSnippet) m_actionImportSnippet->setText("导入片段…");
        
        ui->actionSaveScheme->setText("保存方案");
        ui->actionOpenScheme->setText("打开方案");
        
        ui->actionChinese->setText("中文");
        ui->actionEnglish->setText("英文");
        
        ui->actionStartExecution->setText("开始执行");
        ui->actionStopExecution->setText("停止执行");
        ui->actionRuntimeDesigner->setText("运行界面设计");
        if (m_runtimeViewDock) m_runtimeViewDock->setWindowTitle(QStringLiteral("运行界面"));
        
        ui->imageDock->setWindowTitle(QStringLiteral("图像显示"));
        ui->paramDock->setWindowTitle(QStringLiteral("算法参数"));
        
        // 更新参数面板标签页名称
        if (m_selectedNode) {
            showNodeParameters(m_selectedNode);
        }
    } else {
        setWindowTitle(QStringLiteral("Vision Inspection Workbench"));
        // 英文翻译
        ui->menuFile->setTitle("File");
        ui->menuEdit->setTitle("Edit");
        ui->menuView->setTitle("View");
        ui->menuCommunication->setTitle("Communication");
        ui->menuSystem->setTitle("System");
        ui->menuLanguage->setTitle("Language");
        
        ui->actionNew->setText("New Scheme");
        ui->actionSave->setText("Save Scheme");
        ui->actionLoad->setText("Open Scheme");
        ui->actionExit->setText("Exit");
        
        ui->actionAddFlow->setText("Add Flow");
        ui->actionDeleteFlow->setText("Delete Flow");
        if (m_actionCreateGroup) m_actionCreateGroup->setText("Create Group");
        if (m_actionDissolveGroup) m_actionDissolveGroup->setText("Dissolve Group");
        if (m_actionToggleGroup) m_actionToggleGroup->setText("Collapse/Expand Group");
        if (m_actionCopySnippet) m_actionCopySnippet->setText("Copy Selected Nodes");
        if (m_actionPasteSnippet) m_actionPasteSnippet->setText("Paste");
        if (m_actionExportSnippet) m_actionExportSnippet->setText("Export Snippet…");
        if (m_actionImportSnippet) m_actionImportSnippet->setText("Import Snippet…");
        
        ui->actionSaveScheme->setText("Save Scheme");
        ui->actionOpenScheme->setText("Open Scheme");
        
        ui->actionChinese->setText("Chinese");
        ui->actionEnglish->setText("English");
        
        ui->actionStartExecution->setText("Start");
        ui->actionStopExecution->setText("Stop");
        ui->actionRuntimeDesigner->setText("Runtime UI Designer");
        if (m_runtimeViewDock) m_runtimeViewDock->setWindowTitle(QStringLiteral("Runtime UI"));
        
        ui->imageDock->setWindowTitle(QStringLiteral("Image Display"));
        ui->paramDock->setWindowTitle(QStringLiteral("Tool Parameters"));
        
        // 更新参数面板标签页名称
        if (m_selectedNode) {
            showNodeParameters(m_selectedNode);
        }
    }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    static QTreeWidgetItem *draggedItem = nullptr;
    static QPoint dragStartPos;
    
    try {
        // 检查obj是否有效
        if (!obj) {
            return QMainWindow::eventFilter(obj, event);
        }
        
        // 检查ui和toolLibrary是否有效
        if (!ui || !ui->toolLibrary) {
            return QMainWindow::eventFilter(obj, event);
        }
        
        if (obj == ui->toolLibrary->viewport()) {
            if (event->type() == QEvent::MouseButtonPress) {
                QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
                if (mouseEvent && mouseEvent->button() == Qt::LeftButton) {
                    QTreeWidgetItem *item = ui->toolLibrary->itemAt(mouseEvent->pos());
                    if (item && !item->data(0, Qt::UserRole).toString().isEmpty()) {
                        draggedItem = item;
                        dragStartPos = mouseEvent->pos();
                    }
                }
            } else if (event->type() == QEvent::MouseMove) {
                QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
                if (mouseEvent && draggedItem && (mouseEvent->pos() - dragStartPos).manhattanLength() > QApplication::startDragDistance()) {
                    // 创建拖拽对象
                    QDrag *drag = new QDrag(ui->toolLibrary);
                    QMimeData *mimeData = new QMimeData;
                    
                    // 设置mimeData文本为节点名称
                    mimeData->setText(draggedItem->text(0));
                    const QString paletteId = draggedItem->data(0, Qt::UserRole).toString().trimmed();
                    if (!paletteId.isEmpty()) {
                        mimeData->setData(NodeFactory::toolPaletteMimeFormat(),
                                          paletteId.toUtf8());
                    }
                    drag->setMimeData(mimeData);
                    
                    // 执行拖拽（非阻塞）
                    drag->exec(Qt::CopyAction, Qt::CopyAction);
                    draggedItem = nullptr;
                }
            } else if (event->type() == QEvent::MouseButtonRelease) {
                draggedItem = nullptr;
            }
        }
    } catch (const std::exception &e) {
        VFP_DEBUG << "Exception in eventFilter:" << e.what();
    } catch (...) {
        VFP_DEBUG << "Unknown exception in eventFilter";
    }
    
    return QMainWindow::eventFilter(obj, event);
}

QList<NodeBase*> MainWindow::getUpstreamNodes(NodeBase *node, FlowScene *scene)
{
    QList<NodeBase*> upstreamNodes;
    if (!node || !scene) {
        return upstreamNodes;
    }
    
    // 获取当前节点的所有输入端口
    QList<Port*> inputPorts = node->inputPorts();
    for (Port *port : inputPorts) {
        // 获取连接到该输入端口的所有连接
        QList<MyProject::Connection*> connections = port->connections();
        for (MyProject::Connection *conn : connections) {
            // 获取连接的源端口（输出端口）
            Port *sourcePort = conn->sourcePort();
            if (sourcePort) {
                // 获取源端口所属的节点
                NodeBase *sourceNode = sourcePort->node();
                if (sourceNode && !upstreamNodes.contains(sourceNode)) {
                    upstreamNodes.append(sourceNode);
                    // 递归获取更上游的节点
                    upstreamNodes.append(getUpstreamNodes(sourceNode, scene));
                }
            }
        }
    }
    
    return upstreamNodes;
}

/// 转发：显示决策（下拉框选择 > 画布选中节点 > 兜底）在 ImageDisplayController
NodeBase *MainWindow::resolveDisplayNode(NodeBase *fallbackNode) const
{
    return m_imageDisplay->resolveDisplayNode(fallbackNode);
}

void MainWindow::onOpenNodeSearch()
{
    // 打开节点搜索对话框
    if (!m_nodeSearchWidget) {
        m_nodeSearchWidget = new NodeSearchWidget(this);
    }

    // 设置当前流程场景
    int currentIndex = ui->flowTabs->currentIndex();
    if (currentIndex >= 0 && currentIndex < m_flowScenes.size()) {
        m_nodeSearchWidget->setFlowScene(m_flowScenes[currentIndex]);
    }

    m_nodeSearchWidget->showAndFocus();
}

void MainWindow::onOpenPerformancePanel()
{
    // 打开性能面板（创建/显示在 AuxPanelManager）
    m_auxPanels->open(QStringLiteral("performance"));

    // 面板必须显式 bindExecutor() 才会收到耗时数据；此前无人调用，即使打开也是空表
    // （这是它长期不可达之外的**第二层**缺口）。
    if (auto *pp = m_auxPanels->performancePanel()) {
        if (m_executor)
            pp->bindExecutor(m_executor);
    }
}

void MainWindow::onOpenResultTable()
{
    // 打开结果数据表：一次运行后各模块的「输出项 / 数值 / 状态 / 耗时」
    m_auxPanels->open(QStringLiteral("resultTable"));
    logMessage(QStringLiteral("结果表已打开：运行流程后显示各模块的数值结果，可导出 CSV"));
}

void MainWindow::onOpenOutputViewer()
{
    // 打开输出数据查看器
    m_auxPanels->open(QStringLiteral("outputData"));

    // 如果有选中的节点，显示其输出数据
    if (m_selectedNode) {
        if (auto *ov = m_auxPanels->outputDataViewer())
            ov->setNode(m_selectedNode);
    }
}

void MainWindow::applyDefaultDockSizes()
{
    if (!ui)
        return;

    const int winH = qMax(height(), 720);
    const int winW = qMax(width(), 1100);
    // 右侧（图像显示 / 参数配置 标签页）默认占窗口宽度约 45%：参数编辑已改为「双击算子
    // 弹出模块编辑窗」，右侧不再需要长期驻留参数面板，故把空间让给图像显示（对标
    // VisionMaster 的「图像为主视图」）。用户仍可拖分隔条自行调整。
    const int rightW = qBound(520, static_cast<int>(winW * 0.45), 900);
    const int imageH = qBound(320, static_cast<int>(winH * 0.58), winH - 220);
    const int paramH = qBound(160, winH - imageH - 80, 360);

    QList<QDockWidget *> rightDocks;
    QList<int> rightSizes;
    if (ui->imageDock && ui->imageDock->isVisible() && !ui->imageDock->isFloating()) {
        rightDocks.append(ui->imageDock);
        rightSizes.append(imageH);
    }
    if (ui->paramDock && ui->paramDock->isVisible()) {
        rightDocks.append(ui->paramDock);
        rightSizes.append(paramH);
    }
    // 选项卡式停靠（FR4.3）时两个子页面共享同一区域，高度由区域统一决定，无需分别设置
    const bool tabbedSubPages = ui->imageDock && !tabifiedDockWidgets(ui->imageDock).isEmpty();
    if (rightDocks.size() >= 2 && !tabbedSubPages)
        resizeDocks(rightDocks, rightSizes, Qt::Vertical);
    if (ui->imageDock && !ui->imageDock->isFloating())
        resizeDocks({ui->imageDock}, {rightW}, Qt::Horizontal);
    if (ui->toolDock)
        resizeDocks({ui->toolDock}, {220}, Qt::Horizontal);
    if (ui->logDock)
        resizeDocks({ui->logDock}, {120}, Qt::Vertical);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    static bool s_firstShow = true;
    if (s_firstShow) {
        s_firstShow = false;
        QTimer::singleShot(0, this, [this]() {
            applyDefaultDockSizes();
            // 崩溃恢复询问放在界面搭好之后：模态框不在 showEvent 里直接弹，避免挡住首帧
            checkRecoveryOnStartup();
        });
    }
}

void MainWindow::onToggleImageFloat()
{
    if (!ui || !ui->imageDock)
        return;
    if (ui->imageDock->isFloating()) {
        ui->imageDock->setFloating(false);
        addDockWidget(Qt::RightDockWidgetArea, ui->imageDock);
        if (ui->paramDock) {
            // 还原为「两个可切换子页面」的默认形态（FR4.3）：选项卡式停靠
            tabifyDockWidget(ui->imageDock, ui->paramDock);
            ui->imageDock->raise();
        }
        ui->imageDock->show();
        applyDefaultDockSizes();
    } else {
        ui->imageDock->setFloating(true);
        ui->imageDock->show();
        ui->imageDock->raise();
        ui->imageDock->resize(qMax(900, width() / 2), qMax(700, height() - 80));
        if (m_imageView)
            m_imageView->fitToWindow();
    }
}
