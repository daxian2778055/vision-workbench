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
#include "OutputDataViewer.h"
#include "NodeSearchWidget.h"
#include "HelpViewer.h"
#include "ModuleEditorDialog.h"
#include <HalconCpp.h>
#include <QFileDialog>
#include <QFile>
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
                    static const char *modeSuffix[] = { " [连续]", " [软触发]", " [硬触发]" };
                    int mi = static_cast<int>(mode);
                    const char *suffix = (mi >= 0 && mi < 3) ? modeSuffix[mi] : "";
                    ui->flowTabs->setTabText(tabIdx, QStringLiteral("流程 %1%2").arg(tabIdx + 1).arg(suffix));
                }

                // 停止当前运行中的流程
                if (m_executor->getState() == ExecutionState::Running) {
                    m_executor->stopExecution();
                    m_executor->wait(500);
                }

                switch (mode) {
                case FlowMode::Continuous:
                    ui->actionStartExecution->setEnabled(false);
                    if (m_singleShotBtn) m_singleShotBtn->setEnabled(false);
                    logMessage(QStringLiteral("已切换为连续模式，流程自动循环执行..."));
                    m_executor->startExecution();
                    break;
                case FlowMode::SoftwareTrigger:
                    ui->actionStartExecution->setEnabled(true);
                    if (m_singleShotBtn) m_singleShotBtn->setEnabled(true);
                    logMessage(QStringLiteral("已切换为软触发模式，点击「开始执行」运行一次流程"));
                    break;
                case FlowMode::HardwareTrigger:
                    ui->actionStartExecution->setEnabled(false);
                    if (m_singleShotBtn) m_singleShotBtn->setEnabled(false);
                    logMessage(QStringLiteral("已切换为硬触发模式，等待相机触发源信号..."));
                    // 启动执行线程：循环中的 MVS 图像源会阻塞等待硬件触发帧，
                    // 每次触发源生效即执行一次流程
                    m_executor->startExecution();
                    break;
                }
                updateEditLockForCurrentScene();
            });

            connect(m_executor, &FlowExecutor::flowModeChanged, this,
                    [this](int mode) {
                for (FlowScene *scene : m_flowScenes) {
                    for (NodeBase *node : scene->nodes()) {
                        auto *mvs = qobject_cast<MvsImageSourceNode*>(node);
                        if (mvs) {
                            mvs->refreshPixelFormatEnabled();
                        }
                    }
                }
            });
        }

        // ---- 状态栏运行信息：运行状态 / 本次耗时 / 触发计数 ----
        // 状态机、三项常驻标签与「开始/停止/单次执行」按钮态集中到 ExecutionStatusController
        m_execStatus = new ExecutionStatusController(ui->statusBar, this);
        m_execStatus->setControls(ui->actionStartExecution, ui->actionStopExecution, m_singleShotBtn);
        m_execStatus->setExecutorProvider([this]() { return m_executor; });

        // 连接运行状态信号 → 状态栏（状态/耗时/触发计数）
        connect(m_executor, &FlowExecutor::executionStarted, this, &MainWindow::onExecutionStarted);
        connect(m_executor, &FlowExecutor::executionStopped, this, &MainWindow::onExecutionStopped);
        connect(m_executor, &FlowExecutor::executionFinished, this, &MainWindow::onExecutionFinished);
        connect(m_executor, &FlowExecutor::executionError, this, &MainWindow::onExecutionError);

        // 跳过三态：未激活分支 / 循环体调度的节点 → 结果面板标"跳过"（灰色，不等同失败）
        connect(m_executor, &FlowExecutor::nodeSkipped, this,
                [this](NodeBase *node, const QString &reason) {
            Q_UNUSED(reason)
            auto *rt = m_auxPanels->resultTablePanel();
            if (!node || !rt)
                return;
            rt->setModuleSkipped(node->moduleId(), node->fullName());
        });

        // 结果数据表 / 变量面板：节点执行后把该模块的输出推到面板（UI 线程排队接收）
        connect(m_executor, &FlowExecutor::nodeOutputsUpdated, this,
                [this](NodeBase *node, bool ok, qint64 elapsedMs, const QVariantMap &vars) {
            if (!node)
                return;
            if (auto *rt = m_auxPanels->resultTablePanel()) {
                rt->setModuleResult(node->moduleId(), node->fullName(), ok,
                                    elapsedMs, vars);
            }
            // 供「变量引用」菜单构造引用列表（UI 线程缓存，不读执行线程内部状态）
            m_lastModuleVars.insert(node->moduleId(), vars);
            // 变量面板只列可引用的值：失败节点本轮没有可引用输出
            if (auto *vp = m_auxPanels->variablePanel(); vp && ok)
                vp->setModuleVars(node->moduleId(), node->fullName(), vars);
        });

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
        }

        // 「视图 → 结果表」菜单项（动作定义在 .ui 中，接线方式与其它视图项一致）
        if (ui->actionResultTable) {
            connect(ui->actionResultTable, &QAction::triggered, this,
                    &MainWindow::onOpenResultTable);
        }

        // 连接imageReady信号
        connect(m_executor, &FlowExecutor::imageReady, this, [this](NodeBase *node, const HalconCpp::HImage &image) {            // 运行界面图像控件转发（不受用户显示选择影响）
            if (m_runtimeView) {
                m_runtimeView->pushImage(node->fullName(), image);
            }
            // 如果用户已通过下拉框或画布选择了一个算子，imageReady 不覆盖
            NodeBase *target = resolveDisplayNode();
            if (target != node && target != nullptr) {
                VFP_DEBUG << "用户已选择显示:" << target->fullName() << "，imageReady不覆盖";
                return;
            }
            // 兜底：自动显示最后一个节点的图像（信号负载的图像 + 该节点的叠加图元）
            m_imageDisplay->showImage(image, node->fullName(), node);
        });

        // 任意节点图像输出 → 运行界面按节点名推送（绑定中间节点图像控件可实时显示）
        connect(m_executor, &FlowExecutor::imageAvailable, this,
                [this](NodeBase *node, const HalconCpp::HImage &image) {
            if (m_runtimeView) {
                m_runtimeView->pushImage(node->fullName(), image);
            }
        });

        // 连接nodeExecuted信号，更新参数面板和节点颜色
        connect(m_executor, &FlowExecutor::nodeExecuted, this, [this](NodeBase *node, bool success) {
            QString msg = QString("节点执行结果: %1").arg(success ? "成功" : "失败");
            VFP_DEBUG << msg;
            logMessage(msg);
            
            // 更新节点的颜色渲染
            int currentIndex = ui->flowTabs->currentIndex();
            if (currentIndex >= 0 && currentIndex < m_flowScenes.size()) {
                FlowScene *scene = m_flowScenes[currentIndex];
                if (scene) {
                    NodeGraphicsItem *item = scene->getGraphicsItemForNode(node);
                    if (item) {
                        item->update();
                        VFP_DEBUG << "节点颜色渲染已更新";
                    }
                }
            }
            
            if (success) {
                msg = "算子执行成功";
                VFP_DEBUG << msg;
                logMessage(msg);
                
                // 更新参数面板，显示最新的输出参数
                if (m_selectedNode) {
                    // 先检查参数面板是否存在
                    QWidget *parameterPanel = ui->paramDockContents;
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
                                    m_selectedNode->updateParamPanel(outputTab);
                                    VFP_DEBUG << "输出参数面板已更新";
                                }
                                // 输入面板随运行刷新（图像源信息等）
                                QWidget *inputTab = tabWidget->findChild<QWidget*>("inputTab");
                                if (inputTab) {
                                    m_selectedNode->updateParamPanel(inputTab);
                                }
                            }
                        }
                    } else {
                        VFP_DEBUG << "错误：parameterPanel不存在";
                    }
                }
                
                // === 核心规则：下拉框选择 > 画布选中 > 当前执行节点 ===
                NodeBase *targetDisplayNode = resolveDisplayNode(node);
                VFP_DEBUG << "显示节点图像:" << (targetDisplayNode ? targetDisplayNode->fullName() : "null");

                if (targetDisplayNode && m_imageView) {
                    QSharedPointer<DataObject> outputData = targetDisplayNode->getOutputData(0);
                    if (outputData) {
                        HImage image = outputData->getHImage();
                        if (image.IsInitialized()) {
                            m_imageView->setImage(image, targetDisplayNode->fullName());
                            if (m_imageSourceLabel) {
                                m_imageSourceLabel->setText(QString("图像来源: %1").arg(targetDisplayNode->fullName()));
                            }
                            VFP_DEBUG << "图像已更新，来源:" << targetDisplayNode->fullName();
                        } else {
                            VFP_DEBUG << "目标节点的输出图像未初始化";
                        }
                    } else {
                        VFP_DEBUG << "目标节点的输出数据为空";
                    }
                }

                // === 转发节点输出值到运行界面（数值显示 / 状态灯 / IO状态） ===
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
            }
        });
        
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
    for (FlowExecutor *ex : qAsConst(m_flowExecutors)) {
        if (!ex)
            continue;
        ex->stopExecution();
        if (ex->isRunning() && !ex->wait(8000)) {
            VFP_DEBUG << "FlowExecutor 未能在 8s 内退出（可能在长耗时算子中），继续关闭";
        }
        delete ex;
    }
    m_flowExecutors.clear();
    m_executor = nullptr;   // 已随列表删除，置空避免悬垂
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

// 多流程并发：非激活流程执行器的轻量信号连接（状态/日志/运行界面推送）
void MainWindow::connectExecutorSignals(FlowExecutor *ex)
{
    if (!ex) return;
    connect(ex, &FlowExecutor::executionStarted, this, [this, ex]() {
        if (ex == m_executor) onExecutionStarted();
    });
    connect(ex, &FlowExecutor::executionStopped, this, [this, ex]() {
        if (ex == m_executor) onExecutionStopped();
    });
    connect(ex, &FlowExecutor::executionFinished, this, [this, ex]() {
        if (ex == m_executor) onExecutionFinished();
    });
    connect(ex, &FlowExecutor::executionError, this, [this, ex](const QString &err) {
        if (ex == m_executor) onExecutionError(err);
        else logMessage(QStringLiteral("流程错误: %1").arg(err));
    });
    connect(ex, &FlowExecutor::imageReady, this,
            [this](NodeBase *node, const HalconCpp::HImage &image) {
        if (m_runtimeView) m_runtimeView->pushImage(node->fullName(), image);
    });
}

void MainWindow::closeEvent(QCloseEvent *event)
{
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
        if (ui && ui->flowTabs) {
            int index = ui->flowTabs->addTab(view, tr("流程 %1").arg(m_flowScenes.size()));
            // 初始标签页标题带默认模式后缀
            ui->flowTabs->setTabText(index, QStringLiteral("流程 %1 [软触发]").arg(m_flowScenes.size()));
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
            // 注册流程名到全局触发管理器
            QString flowName = QStringLiteral("\u6D41\u7A0B %1").arg(m_flowScenes.size());
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
    // 新建方案
    logMessage("新建方案");

    // 先停掉并摘除全部执行器，再删除场景——否则运行中的执行线程会在节点被删除后
    // 继续访问（use-after-free），且 m_flowExecutors 会残留指向已删场景的悬垂键。
    QList<FlowExecutor *> oldExecutors;
    for (FlowExecutor *ex : qAsConst(m_flowExecutors)) {
        if (ex && !oldExecutors.contains(ex))
            oldExecutors.append(ex);
    }
    if (m_executor && !oldExecutors.contains(m_executor))
        oldExecutors.append(m_executor);

    for (FlowExecutor *ex : oldExecutors)
        ex->stopExecution();
    for (FlowExecutor *ex : oldExecutors) {
        if (ex->isRunning() && !ex->wait(6000))
            VFP_DEBUG << "FlowExecutor 未能在 6s 内退出（可能在长耗时算子中）";
    }

    FlowExecutor *spareExecutor = nullptr;
    for (FlowExecutor *ex : oldExecutors) {
        if (!ex)
            continue;
        GlobalTriggerManager::instance()->unregisterFlow(ex->flowName());
        ex->setFlowScene(nullptr);
        if (!spareExecutor)
            spareExecutor = ex;   // 保留一个占位，供新方案首个流程复用
        else
            delete ex;
    }
    m_flowExecutors.clear();
    m_executor = spareExecutor;
    if (spareExecutor)
        m_flowExecutors.insert(nullptr, spareExecutor);

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
    
    logMessage("新方案创建完成");
}

void MainWindow::onSaveProject()
{
    // 保存项目（方案扩展名 .vfp，与需求文档一致）：文件对话框与结果提示在 ProjectManager
    if (!m_projectManager) {
        m_projectManager = new ProjectManager(this);
    }

    QString fileName;
    const bool success = m_projectManager->saveProjectInteractive(this, m_flowScenes, &fileName);
    if (fileName.isEmpty())
        return;   // 用户取消

    logMessage(tr("保存项目: %1").arg(fileName));
    if (success) {
        logMessage(tr("项目保存成功: %1").arg(fileName));
        m_recentFiles->add(fileName);
    } else {
        logMessage(tr("项目保存失败: %1").arg(fileName));
    }
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
    // 仅软触发模式有效
    if (m_executor->getFlowMode() == FlowMode::SoftwareTrigger) {
        // 明确点"开始执行"= 正常跑一遍，退出上次遗留的单步模式
        // （历史缺陷：单步模式粘住，之后每次"开始执行"仍在每个节点后暂停）
        m_executor->exitStepMode();
        m_executor->startExecution();
    }
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

void MainWindow::loadProjectFile(const QString &fileName)
{
    logMessage(tr("加载项目: %1").arg(fileName));

    // 使用ProjectManager加载项目
    if (!m_projectManager) {
        m_projectManager = new ProjectManager(this);
    }

    // 先加载到临时列表：只有加载成功才销毁当前方案。
    // 原实现是先 qDeleteAll 再加载，文件损坏时旧方案已丢、新方案为空，且仍报“加载成功”。
    QList<FlowScene*> loadedScenes;
    const bool success = m_projectManager->loadProject(fileName, loadedScenes);

    if (!success) {
        qDeleteAll(loadedScenes);
        logMessage(tr("项目加载失败：文件不存在、格式损坏或内容为空（当前方案保持不变）"));
        return;
    }

    // 释放旧方案的执行器：停止线程并把场景指针从执行器上摘掉，
    // 否则 m_flowExecutors 会残留指向已删除场景的悬垂键，后续按场景查找会踩已释放内存。
    QList<FlowExecutor *> oldExecutors;
    for (FlowExecutor *ex : m_flowExecutors) {
        if (ex && !oldExecutors.contains(ex))
            oldExecutors.append(ex);
    }
    if (m_executor && !oldExecutors.contains(m_executor))
        oldExecutors.append(m_executor);

    for (FlowExecutor *ex : oldExecutors)
        ex->stopExecution();
    // 等待真正退出再动场景：1s 太短且不看结果——超时后旧场景会在执行线程仍在跑时被删除；
    // 保留的占位执行器也会带着"仍在运行的线程"被复用（都是 use-after-free 隐患）。
    for (FlowExecutor *ex : oldExecutors) {
        if (ex->isRunning() && !ex->wait(6000))
            VFP_DEBUG << "FlowExecutor 未能在 6s 内退出（可能在长耗时算子中）";
    }

    FlowExecutor *spareExecutor = nullptr;
    for (FlowExecutor *ex : oldExecutors) {
        if (!ex)
            continue;
        GlobalTriggerManager::instance()->unregisterFlow(ex->flowName());
        ex->setFlowScene(nullptr);
        if (!spareExecutor) {
            spareExecutor = ex;   // 保留一个作为占位执行器，供新方案首个流程复用
        } else {
            delete ex;
        }
    }
    m_flowExecutors.clear();
    m_executor = spareExecutor;
    if (spareExecutor)
        m_flowExecutors.insert(nullptr, spareExecutor);

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
    m_recentFiles->add(fileName);

    // 添加加载的场景到标签页
    for (int i = 0; i < loadedScenes.size(); ++i) {
        FlowScene *scene = loadedScenes[i];
        m_flowScenes.append(scene);

        QGraphicsView *view = new QGraphicsView(scene);
        VisionWorkbenchStyle::applyGraphicsViewWorkbenchDefaults(view);
        view->setDragMode(QGraphicsView::RubberBandDrag);
        view->setRubberBandSelectionMode(Qt::IntersectsItemShape);
        view->setFocusPolicy(Qt::StrongFocus);
        ui->flowTabs->addTab(view, tr("流程 %1").arg(i + 1));

        // 连接信号
        hookFlowScene(scene);
    }

    // 设置当前场景（多流程并发：切换到该流程的执行器）
    if (!m_flowScenes.isEmpty()) {
        m_executor = executorForScene(m_flowScenes[0]);
        ui->flowTabs->setCurrentIndex(0);
    }
    checkHalconNodesHint();
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
        GlobalTriggerManager::instance()->unregisterFlow(sceneEx->flowName());
        m_flowExecutors.remove(scene);
        if (m_flowScenes.isEmpty()) {
            // 最后一个流程：执行器放回占位，等待下次复用
            m_flowExecutors.insert(nullptr, sceneEx);
            m_executor = sceneEx;
        } else if (sceneEx != m_executor) {
            delete sceneEx;
        } else {
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
        if (outputData) {
            // If output is an image, display it
            if (outputData->getType() == DataObject::DataType::Image) {
                HImage image = outputData->getHImage();
                if (image.IsInitialized()) {
                    m_imageView->setImage(image, "");
                }
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
    auto it = m_flowModes.constFind(scene);
    FlowMode mode = (it != m_flowModes.cend()) ? it.value() : FlowMode::SoftwareTrigger;

    bool locked = (mode == FlowMode::Continuous) ||
                  (m_executor && m_executor->getState() == ExecutionState::Running);
    scene->setEditLocked(locked);
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
    m_executor->setFlowScene(scene);

    // 恢复该流程存储的运行模式
    if (m_flowModeCombo) {
        auto it = m_flowModes.constFind(scene);
        FlowMode mode = (it != m_flowModes.cend()) ? it.value() : FlowMode::SoftwareTrigger;
        {
            QSignalBlocker blocker(m_flowModeCombo);
            m_flowModeCombo->setCurrentIndex(static_cast<int>(mode));
        }
        m_executor->setFlowMode(mode);
        ui->actionStartExecution->setEnabled(mode == FlowMode::SoftwareTrigger);

        // 更新标签页标题，显示模式后缀
        static const char *modeSuffix[] = { " [连续]", " [软触发]", " [硬触发]" };
        int mi = static_cast<int>(mode);
        const char *suffix = (mi >= 0 && mi < 3) ? modeSuffix[mi] : "";
        ui->flowTabs->setTabText(index, QStringLiteral("流程 %1%2").arg(index + 1).arg(suffix));

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
        QTimer::singleShot(0, this, &MainWindow::applyDefaultDockSizes);
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
