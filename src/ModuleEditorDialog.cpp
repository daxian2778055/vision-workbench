#include <QLineEdit>
#include <QDoubleSpinBox>
#include "TemplateSearchBudget.h"
#include "ModuleEditorDialog.h"
#include "HalconNode.h"
#include "OpencvTemplateMatchNode.h"
#include "TemplateConfigDialog.h"
#include <QAbstractItemView>
#include <QGroupBox>
#include <QHeaderView>
#include <QListWidget>
#include <QTabWidget>
#include <QMessageBox>
#include <QTableWidget>
#include "OpencvUtil.h"
#include "DataObject.h"
#include "FlowScene.h"
#include "NodeRegistry.h"
#include "Port.h"
#include "Connection.h"
#include "VariablePanel.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>
#include <QColorDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QHeaderView>
#include <QToolButton>
#include <QMenu>
#include <QApplication>
#include <QEvent>
#include <QSettings>
#include <QScreen>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

using namespace HalconCpp;

namespace {
/// 窗口几何持久化文件（应用目录，随包走）。
/// 不用 QSettings 默认构造（注册表）：现场实测注册表写入被静默丢弃
/// （保存日志 66 字节但注册表无键，疑似权限虚拟化/组策略），INI 直写文件可靠可见。
QString geometryStorePath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/module_editor.ini");
}
}

ModuleEditorDialog::ModuleEditorDialog(NodeBase *node, QWidget *parent)
    : QDialog(parent)
    , m_node(node)
{
    setAttribute(Qt::WA_DeleteOnClose);
    // VM 模块窗标题：「<模块号> <模块名>」（如「3 轮廓匹配」），不再写"编辑模块 — xxx"。
    setWindowTitle(node ? QStringLiteral("%1 %2").arg(node->moduleId()).arg(node->name())
                        : QStringLiteral("模块配置"));
    // 窗口几何持久化：用户拖过一次就永久记住（含位置），下次直接用；
    // 首次（无存档）按屏幕可用区自适应，绝不超出屏幕（旧固定 1100×720 在小屏/缩放屏会溢出）
    {
        QSettings settings(geometryStorePath(), QSettings::IniFormat);
        const QByteArray geo = settings.value(QStringLiteral("editor/geometry")).toByteArray();
        qDebug() << "ModuleEditor: restore geometry bytes =" << geo.size()
                 << "from" << geometryStorePath();
        if (geo.isEmpty() || !restoreGeometry(geo)) {
            QRect avail;
            if (const QScreen *scr = QGuiApplication::primaryScreen())
                avail = scr->availableGeometry();
            resize(qMax(760, qMin(1100, avail.width() * 9 / 10)),
                   qMax(540, qMin(720, avail.height() * 9 / 10)));
        }
    }

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    auto *toolbar = new QHBoxLayout();
    // P1-1：左上角帮助入口（对标 VM 模块窗的 ? 按钮）
    m_helpBtn = new QToolButton(this);
    m_helpBtn->setObjectName(QStringLiteral("moduleHelpButton"));
    m_helpBtn->setText(QStringLiteral("?"));
    m_helpBtn->setToolTip(QStringLiteral("查看本算子的帮助说明"));
    m_helpBtn->setFixedSize(26, 26);
    toolbar->addWidget(m_helpBtn);
    auto *execBtn = new QPushButton(QStringLiteral("执行算子"));
    auto *recomputeBtn = new QPushButton(QStringLiteral("重算下游"));
    recomputeBtn->setToolTip(QStringLiteral("参数改完后：作废本算子及其下游的缓存，只重跑这一段链路（不重跑无关分支）"));
    auto *drawBtn = new QPushButton(QStringLiteral("在图上绘制"));
    auto *searchBtn = new QPushButton(QStringLiteral("\u7ed8\u5236\u641c\u7d22\u533a\u57df"));   // 绘制搜索区域
    auto *polyBtn = new QPushButton(QStringLiteral("\u7ed8\u5236\u591a\u8fb9\u5f62\u533a\u57df"));   // 绘制多边形区域
    polyBtn->setToolTip(QStringLiteral("\u5355\u51fb\u9010\u70b9\u3001\u53cc\u51fb\u5b8c\u6210\uff1a\u591a\u8fb9\u5f62\u641c\u7d22\u533a\u57df\uff08\u4f18\u5148\u4e8e\u77e9\u5f62\uff09"));
    searchBtn->setToolTip(QStringLiteral("\u5728\u56fe\u4e0a\u6846\u5b9a\u641c\u7d22\u533a\u57df\uff1a\u53ea\u5728\u8fd9\u4e2a\u6846\u5185\u627e\u6a21\u677f"));
    // B2：编辑对象切换。两个互斥按钮，句柄只出现在当前对象上——
    // 之前两个框同时可拖，点哪个都不中（现场"拖不动/拖错"）。
    m_targetGeomBtn = new QToolButton();
    m_targetGeomBtn->setText(QStringLiteral("编辑：模板框"));
    m_targetGeomBtn->setCheckable(true);
    m_targetGeomBtn->setChecked(true);
    m_targetGeomBtn->setToolTip(QStringLiteral("当前编辑对象 = 模板框（绿色，带角点/旋转柄）"));
    m_targetSearchBtn = new QToolButton();
    m_targetSearchBtn->setText(QStringLiteral("编辑：搜索区域"));
    m_targetSearchBtn->setCheckable(true);
    m_targetSearchBtn->setToolTip(QStringLiteral("当前编辑对象 = 搜索区域（青色，可直接拖角点改大小）"));
    auto *clearGeomBtn = new QPushButton(QStringLiteral("清除几何"));
    auto *maskBtn = new QPushButton(QStringLiteral("编辑掩膜"));
    auto *clearMaskBtn = new QPushButton(QStringLiteral("清除掩膜"));
    auto *saveTmplBtn = new QPushButton(QStringLiteral("保存模板图像"));
    // 训练模板（r19 补全清单第 3 项）：按当前 ROI 从图像训练并落盘，
    // 训练完立刻刷新模板预览、并在画布上显示匹配结果（VM 的训练手感）。
    auto *trainTmplBtn = new QPushButton(QStringLiteral("训练模板"));
    auto *evalBtn = new QPushButton(QStringLiteral("评估模板"));
    auto *budgetBtn = new QPushButton(QStringLiteral("参数体检"));
    auto *wizardBtn = new QPushButton(QStringLiteral("向导"));
    wizardBtn->setToolTip(QStringLiteral("按步骤训练模板：画框 → 设范围 → 评估"));
    budgetBtn->setToolTip(QStringLiteral("算一下角度×尺度的组合数与预计耗时，防止现场卡顿"));
    evalBtn->setToolTip(QStringLiteral("按当前参数跑一次，给出模板质量评级与改进建议"));
    trainTmplBtn->setToolTip(QStringLiteral("按当前 ROI 从图像训练并保存模板，随后刷新预览与结果"));
    auto *fitBtn = new QPushButton(QStringLiteral("适应窗口"));
    m_autoRecomputeCheck = new QCheckBox(QStringLiteral("自动预览"));
    m_autoRecomputeCheck->setObjectName(QStringLiteral("autoRecomputeCheck"));
    m_autoRecomputeCheck->setChecked(false);
    m_autoRecomputeCheck->setToolTip(QStringLiteral(
        "勾选后：改参数 / 拖 ROI / 涂掩膜立即重算预览（去抖 400ms）。\n"
        "不勾选时改动只暂存在本窗口，点「确定」才提交并重算；点「取消」丢弃本次改动。"));
    toolbar->addWidget(execBtn);
    toolbar->addWidget(recomputeBtn);
    toolbar->addWidget(drawBtn);
    toolbar->addWidget(m_targetGeomBtn);
    toolbar->addWidget(m_targetSearchBtn);
    toolbar->addWidget(clearGeomBtn);
    toolbar->addWidget(maskBtn);
    toolbar->addWidget(clearMaskBtn);
    // P2-10：模板专属动作收进「模板工具」下拉，避免一行十几个按钮把通用动作挤没
    //（VM 的模块窗也不会把所有模板动作平铺在主工具条上）。
    auto *tmplToolsBtn = new QToolButton(this);
    tmplToolsBtn->setObjectName(QStringLiteral("templateToolsButton"));
    tmplToolsBtn->setText(QStringLiteral("模板工具"));
    tmplToolsBtn->setToolTip(QStringLiteral("模板匹配专属动作：搜索区域、训练、评估、体检、向导"));
    tmplToolsBtn->setPopupMode(QToolButton::InstantPopup);
    {
        QMenu *menu = new QMenu(tmplToolsBtn);
        auto addActionFor = [menu](const QString &text, QPushButton *button) {
            QAction *act = menu->addAction(text);
            QObject::connect(act, &QAction::triggered, button, &QPushButton::click);
        };
        addActionFor(QStringLiteral("绘制搜索区域"), searchBtn);
        addActionFor(QStringLiteral("绘制多边形区域"), polyBtn);
        addActionFor(QStringLiteral("保存模板图像"), saveTmplBtn);
        addActionFor(QStringLiteral("训练模板"), trainTmplBtn);
        addActionFor(QStringLiteral("评估模板"), evalBtn);
        addActionFor(QStringLiteral("参数体检"), budgetBtn);
        addActionFor(QStringLiteral("向导"), wizardBtn);
        tmplToolsBtn->setMenu(menu);
    }
    toolbar->addWidget(tmplToolsBtn);
    // P2-10：模板专属动作收进菜单后，按钮本体仍保留（菜单项通过 click() 触发），
    // 但不参与工具条布局。统一挂在隐藏容器里，避免无父控件时游离在窗口左上角，
    // 也保证既有按按钮文本查找的调用方/测试仍能找到它们。
    {
        auto *tmplButtonHost = new QWidget(this);
        auto *tmplButtonLay = new QHBoxLayout(tmplButtonHost);
        tmplButtonLay->setContentsMargins(0, 0, 0, 0);
        for (QPushButton *b : {searchBtn, polyBtn, saveTmplBtn, trainTmplBtn,
                               evalBtn, budgetBtn, wizardBtn}) {
            tmplButtonLay->addWidget(b);
        }
        tmplButtonHost->setVisible(false);
    }
    toolbar->addWidget(m_autoRecomputeCheck);

    // 变量引用：点开即列出可引用的 {模块号.参数名} / {global.名称}，选中插入到参数编辑框。
    // 列表在展开时实时生成（provider），因此运行中出现的新变量也能立刻引用。
    m_varRefButton = new QToolButton(this);
    m_varRefButton->setObjectName(QStringLiteral("varRefButton"));
    m_varRefButton->setText(QStringLiteral("变量引用"));
    m_varRefButton->setToolTip(QStringLiteral("把变量引用插入到「最近编辑的参数框」光标处，"
                                              "实现上游结果 → 下游参数的表达式联动"));
    m_varRefButton->setPopupMode(QToolButton::InstantPopup);
    m_varRefMenu = new QMenu(m_varRefButton);
    m_varRefMenu->setObjectName(QStringLiteral("varRefMenu"));
    m_varRefButton->setMenu(m_varRefMenu);
    connect(m_varRefMenu, &QMenu::aboutToShow, this, &ModuleEditorDialog::rebuildReferenceMenu);
    toolbar->addWidget(m_varRefButton);
    toolbar->addWidget(fitBtn);
    toolbar->addStretch();
    root->addLayout(toolbar);

    m_hintLabel = new QLabel();
    m_hintLabel->setObjectName(QStringLiteral("paramHintLabel"));
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setStyleSheet(QStringLiteral("color:#666;"));
    root->addWidget(m_hintLabel);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    m_view = new HalconWindow(splitter);
    m_view->setMinimumWidth(480);

    auto *right = new QWidget(splitter);
    auto *rightLay = new QVBoxLayout(right);
    rightLay->setContentsMargins(0, 0, 0, 0);

    m_templatePreview = new QLabel(QStringLiteral("模板预览"));
    m_templatePreview->setAlignment(Qt::AlignCenter);
    m_templatePreview->setMinimumHeight(120);
    m_templatePreview->setStyleSheet(QStringLiteral("background:#1e1e21; color:#aaa; border:1px solid #444;"));
    rightLay->addWidget(m_templatePreview);

    // VM「特征模板」页：模板列表 + 创建/编辑/删除（列表项 = 一个模板对象，不是文件路径）
    m_templateList = new QListWidget();
    m_templateList->setObjectName(QStringLiteral("templateList"));
    m_templateList->setMaximumHeight(110);
    m_templateList->setToolTip(QStringLiteral("当前算子的模板列表；点一行即切换当前匹配用的模板"));
    rightLay->addWidget(m_templateList);
    auto *tmplBtns = new QHBoxLayout();
    m_tmplNewBtn = new QPushButton(QStringLiteral("创建"));
    m_tmplLoadBtn = new QPushButton(QStringLiteral("载入"));
    m_tmplEditBtn = new QPushButton(QStringLiteral("编辑模板"));
    m_tmplDelBtn = new QPushButton(QStringLiteral("删除所有模板"));
    m_tmplNewBtn->setToolTip(QStringLiteral("打开「模板配置」窗：在图上框形状、调参数、确定即创建"));
    m_tmplLoadBtn->setToolTip(QStringLiteral("从本地图像载入一个模板（VM「特征模板」页的载入）"));
    m_tmplEditBtn->setToolTip(QStringLiteral("编辑列表里选中的模板（原地更新）"));
    m_tmplDelBtn->setToolTip(QStringLiteral("清空该算子的模板列表（文件不删）"));
    tmplBtns->addWidget(m_tmplNewBtn);
    tmplBtns->addWidget(m_tmplLoadBtn);
    tmplBtns->addWidget(m_tmplEditBtn);
    tmplBtns->addWidget(m_tmplDelBtn);
    rightLay->addLayout(tmplBtns);

    // VM 结果表口径：每个命中一行，列 = 序号/中心X/中心Y/匹配点X/匹配点Y/角度/尺度X/尺度Y/分数
    m_resultTable = new QTableWidget(0, 10);
    m_resultTable->setHorizontalHeaderLabels(
        {QStringLiteral("序号"), QStringLiteral("中心X"), QStringLiteral("中心Y"),
         QStringLiteral("匹配点X"), QStringLiteral("匹配点Y"), QStringLiteral("角度"),
         QStringLiteral("尺度X"), QStringLiteral("尺度Y"), QStringLiteral("分数"),
         QStringLiteral("模板")});
    m_resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultTable->verticalHeader()->setVisible(false);
    m_resultTable->horizontalHeader()->setStretchLastSection(true);
    m_resultTable->setMaximumHeight(130);
    rightLay->addWidget(m_resultTable);

    m_paramHost = new QScrollArea();
    m_paramHost->setWidgetResizable(true);
    // 参数区只允许纵向滚动：横向滚动条强制关闭（参数被横向裁切/来回滚看的体验根因）
    m_paramHost->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rightLay->addWidget(m_paramHost, 1);

    // 参数面板保底宽度：无论窗口怎么缩，右侧参数一列始终放得下（≥360px）
    right->setMinimumWidth(360);

    m_splitter = splitter;
    splitter->addWidget(m_view);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    // 分栏比例持久化：无存档时右侧固定 420px（参数列足够宽），左侧拿剩余
    {
        QSettings settings;
        const QByteArray splitState =
            settings.value(QStringLiteral("moduleEditor/splitter")).toByteArray();
        if (splitState.isEmpty() || !splitter->restoreState(splitState)) {
            QList<int> sizes { qMax(520, width() - 420), 420 };
            splitter->setSizes(sizes);
        }
    }
    root->addWidget(splitter, 1);

    auto *bottom = new QHBoxLayout();
    // 掩膜画笔半径入口：此前 HalconWindow::setMaskBrushRadius 有实现却**没有任何调用者**，
    // 半径永远固定为默认的 16，粗结构上涂抹太慢、细小区域又太糙。这里补上入口
    // （只在支持掩膜的算子上显示），改动立即作用于正在进行的涂抹。
    auto *maskBrushLabel = new QLabel(QStringLiteral("画笔半径:"));
    auto *maskBrushSpin = new QSpinBox();
    maskBrushSpin->setRange(2, 80);
    maskBrushSpin->setValue(16);
    maskBrushSpin->setToolTip(QStringLiteral("掩膜涂抹的画笔半径（像素），范围 2–80。"));
    if (m_view) {
        m_view->setMaskBrushRadius(maskBrushSpin->value());
    }
    connect(maskBrushSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (m_view) {
            m_view->setMaskBrushRadius(v);
        }
    });
    auto *maskEraseCheck = new QCheckBox(QStringLiteral("擦除"));
    maskEraseCheck->setToolTip(QStringLiteral("勾选后在图上拖拽即擦除掩膜；取消则涂抹保留"));
    connect(maskEraseCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_view)
            m_view->setMaskPaintKeep(!on);
    });
    bottom->addWidget(maskEraseCheck);
    bottom->addWidget(maskBrushLabel);
    bottom->addWidget(maskBrushSpin);
    // P2-8：左下角「恢复默认值」（VM 运行参数页左下角口径；只对声明式参数算子可见）
    m_restoreDefaultsBtn = new QPushButton(QStringLiteral("恢复默认值"));
    m_restoreDefaultsBtn->setToolTip(QStringLiteral("把本算子的全部参数恢复为出厂默认值；点「确定」后才提交"));
    bottom->addWidget(m_restoreDefaultsBtn);
    bottom->addStretch();

    // VM 4.4 模块窗底部按钮：连续执行 / 执行 / 确定（图 13「轮廓匹配特征模板列表」）。
    m_continuousBtn = new QToolButton();
    m_continuousBtn->setObjectName(QStringLiteral("continuousRunButton"));
    m_continuousBtn->setText(QStringLiteral("连续执行"));
    m_continuousBtn->setCheckable(true);
    m_continuousBtn->setToolTip(QStringLiteral("勾选后改动即重算（自动预览）；取消则改完点「执行」或「确定」"));
    connect(m_continuousBtn, &QToolButton::toggled, this, [this](bool on) {
        if (m_autoRecomputeCheck)
            m_autoRecomputeCheck->setChecked(on);
        if (on)
            emit executeRequested();
    });
    auto *bottomExecBtn = new QPushButton(QStringLiteral("执行"));
    bottomExecBtn->setToolTip(QStringLiteral("按当前参数执行一次本算子"));
    connect(bottomExecBtn, &QPushButton::clicked, this, &ModuleEditorDialog::executeRequested);
    m_cancelBtn = new QPushButton(QStringLiteral("取消"), this);
    m_cancelBtn->setToolTip(QStringLiteral("放弃本次修改并回滚到打开时的参数"));
    m_okBtn = new QPushButton(QStringLiteral("确定"));
    m_okBtn->setDefault(true);
    m_okBtn->setToolTip(QStringLiteral("提交本次修改并重算本算子及其下游"));
    bottom->addWidget(m_continuousBtn);
    bottom->addWidget(bottomExecBtn);
    bottom->addWidget(m_okBtn);
    // 旧「取消」按钮保留对象（既有调用/测试按文本查找），但不进入 VM 布局：
    // 关闭窗口 = 放弃未提交改动，与 VM 右上角 X 一致。
    m_cancelBtn->setVisible(false);
    root->addLayout(bottom);

    HalconNode *hn = halconNode();
    const bool hasGeom = hn && hn->geometryRoiType() != RoiType::None;
    const bool hasMask = hn && hn->supportsMaskEdit();
    const bool isTmpl = qobject_cast<OpencvTemplateMatchNode *>(m_node);
    m_restoreDefaultsBtn->setVisible(hn && !hn->paramSpecs().isEmpty());
    drawBtn->setVisible(hasGeom);
    clearGeomBtn->setVisible(hasGeom);
    maskBtn->setVisible(hasMask);
    clearMaskBtn->setVisible(hasMask);
    maskBrushLabel->setVisible(hasMask);
    maskEraseCheck->setVisible(hasMask);
    maskBrushSpin->setVisible(hasMask);
    saveTmplBtn->setVisible(isTmpl);
    // P0-3：模板专属按钮只对模板匹配算子可见（旧版对所有节点可见，点下去读不到参数）
    tmplToolsBtn->setVisible(isTmpl);
    searchBtn->setVisible(isTmpl);
    polyBtn->setVisible(isTmpl);
    trainTmplBtn->setVisible(isTmpl);
    evalBtn->setVisible(isTmpl);
    budgetBtn->setVisible(isTmpl);
    wizardBtn->setVisible(isTmpl);
    m_templatePreview->setVisible(isTmpl);
    // 模板匹配的内置图像区已隐藏（VM 模块窗不含图像），这两个"在窗内切换编辑对象"的按钮
    // 失去落点；模板框/搜索区域改在独立的「模板配置」窗与主画布编辑。
    m_targetGeomBtn->setVisible(false);
    m_targetSearchBtn->setVisible(false);
    m_templateList->setVisible(isTmpl);
    m_tmplNewBtn->setVisible(isTmpl);
    m_tmplLoadBtn->setVisible(isTmpl);
    m_tmplEditBtn->setVisible(isTmpl);
    m_tmplDelBtn->setVisible(isTmpl);
    // P0-2：结果显示是所有模块的通用页（非模板算子显示通用输出/状态/耗时）
    m_resultTable->setVisible(true);

    // VM 4.4 模块窗是"纯页签"窗口（图 13 轮廓匹配：约 400×570，无内置图像区）；
    // 模板图像/掩膜的绘制在独立的「模板配置」窗完成。因此模板匹配隐藏内置预览，
    // 其余算子的 ROI 仍在本窗绘制（保留图像区）。
    if (isTmpl) {
        if (m_view)
            m_view->setVisible(false);
        if (m_splitter)
            m_splitter->setSizes({0, 520});
        resize(qMax(width(), 520), qMax(height(), 660));
    }

    if (hasGeom) {
        m_hintLabel->setText(QStringLiteral(
            "在图上拖拽绘制；画完后可拖中心移动、拖角点缩放、拖蓝点旋转。右键取消。"));
    } else if (hasMask) {
        m_hintLabel->setText(QStringLiteral("左键涂抹保留区（白），右键擦除（品红=忽略）。掩膜会在执行时与结果按位与。"));
    } else {
        m_hintLabel->setText(QStringLiteral("在此修改完整参数并执行。画布单击只显示摘要。"));
    }

    connect(execBtn, &QPushButton::clicked, this, &ModuleEditorDialog::executeRequested);
    connect(recomputeBtn, &QPushButton::clicked, this, &ModuleEditorDialog::recomputeRequested);
    connect(drawBtn, &QPushButton::clicked, this, &ModuleEditorDialog::onDrawGeometry);
    connect(searchBtn, &QPushButton::clicked, this, &ModuleEditorDialog::onDrawSearchRegion);
    connect(polyBtn, &QPushButton::clicked, this, &ModuleEditorDialog::onDrawSearchPolygon);
    connect(m_targetGeomBtn, &QToolButton::clicked, this, &ModuleEditorDialog::onEditTargetGeometry);
    connect(m_targetSearchBtn, &QToolButton::clicked, this, &ModuleEditorDialog::onEditTargetSearch);
    connect(m_tmplNewBtn, &QPushButton::clicked, this, &ModuleEditorDialog::onNewTemplate);
    connect(m_tmplLoadBtn, &QPushButton::clicked, this, [this]() {
        OpencvTemplateMatchNode *tmplNode = qobject_cast<OpencvTemplateMatchNode *>(m_node);
        if (!tmplNode)
            return;
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("载入模板图像"), QString(),
            QStringLiteral("图像文件 (*.png *.bmp *.jpg *.jpeg *.tif *.tiff)"));
        if (path.isEmpty())
            return;
        QVector<TemplateEntry> list = tmplNode->templates();
        TemplateEntry entry;
        entry.name = QFileInfo(path).baseName();
        entry.imagePath = path;
        list.append(entry);
        tmplNode->setTemplates(list);
        tmplNode->setActiveTemplateIndex(list.size() - 1);
        refreshTemplateList();
        m_paramsDirty = true;
        scheduleAutoRecompute();
    });
    connect(m_tmplEditBtn, &QPushButton::clicked, this, &ModuleEditorDialog::onEditTemplate);
    connect(m_tmplDelBtn, &QPushButton::clicked, this, &ModuleEditorDialog::onDeleteTemplate);
    connect(m_templateList, &QListWidget::currentRowChanged, this,
            &ModuleEditorDialog::onTemplateSelected);
    connect(clearGeomBtn, &QPushButton::clicked, this, &ModuleEditorDialog::onClearGeometry);
    connect(maskBtn, &QPushButton::clicked, this, &ModuleEditorDialog::onEditMask);
    connect(clearMaskBtn, &QPushButton::clicked, this, &ModuleEditorDialog::onClearMask);
    connect(saveTmplBtn, &QPushButton::clicked, this, &ModuleEditorDialog::onSaveTemplate);
    connect(wizardBtn, &QPushButton::clicked, this, &ModuleEditorDialog::onWizardGuide);
    connect(budgetBtn, &QPushButton::clicked, this, &ModuleEditorDialog::onCheckBudget);
    connect(evalBtn, &QPushButton::clicked, this, &ModuleEditorDialog::onEvaluateTemplate);
    connect(trainTmplBtn, &QPushButton::clicked, this, [this]() {
        if (!m_node)
            return;
        m_node->setParam(QStringLiteral("trainFromImage"), true);
        emit executeRequested();       // MainWindow: executeNodeOnce（含 reload + setResultOverlay）
        refreshTemplatePreview();      // 训练后立刻刷新模板预览
        // 训练完直接给一组可用的边缘参数（阈值/特征尺度/速度尺度），省掉"画完再猜参数"
        if (auto *tmplNode = qobject_cast<OpencvTemplateMatchNode *>(m_node)) {
            if (tmplNode->applySuggestedParams()) {
                if (QWidget *panel = m_paramHost->widget())
                    tmplNode->updateParamPanel(panel);
                const QString hint = QStringLiteral(
                    "已训练，并按模板自动给出参数：阈值 %1 / 特征尺度 %2 / 速度尺度 %3")
                                         .arg(tmplNode->getParam(QStringLiteral("contrastThreshold")).toInt())
                                         .arg(tmplNode->getParam(QStringLiteral("featureScale")).toInt())
                                         .arg(tmplNode->getParam(QStringLiteral("speedScale")).toInt());
                m_hintLabel->setText(hint);
                return;
            }
        }
        m_hintLabel->setText(QStringLiteral(
            "已按 ROI 训练；图上绿点 = 模板强梯度点（GOC 用它打分，灰度法仅作质量参考）"));
    });
    connect(fitBtn, &QPushButton::clicked, m_view, &HalconWindow::fitToWindow);
    connect(m_helpBtn, &QToolButton::clicked, this, [this]() { emit helpRequested(m_node); });
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() { reject(); });
    connect(m_okBtn, &QPushButton::clicked, this, [this]() { accept(); });
    connect(m_restoreDefaultsBtn, &QPushButton::clicked, this,
            &ModuleEditorDialog::restoreParamDefaults);
    connect(m_view, &HalconWindow::roiEdited, this, &ModuleEditorDialog::onRoiEdited);
    connect(m_view, &HalconWindow::maskEdited, this, &ModuleEditorDialog::onMaskEdited);

    // 自动重算去抖：连续微调参数只触发一次重算
    m_autoRecomputeTimer = new QTimer(this);
    m_autoRecomputeTimer->setSingleShot(true);
    m_autoRecomputeTimer->setInterval(400);
    connect(m_autoRecomputeTimer, &QTimer::timeout, this, &ModuleEditorDialog::onAutoRecomputeTimeout);

    // 几何即时持久化（去抖 600ms）：编辑窗是非模态的，用户习惯"调完大小直接双击下一个"，
    // 旧窗不关则关闭钩子永远不触发（首版只在 done() 保存即踩中此坑）——拖完 600ms 就落盘
    m_geoSaveTimer = new QTimer(this);
    m_geoSaveTimer->setSingleShot(true);
    m_geoSaveTimer->setInterval(600);
    connect(m_geoSaveTimer, &QTimer::timeout, this, &ModuleEditorDialog::saveGeometryNow);

    rebuildParamPanel();
    reloadFromNode();
    // P1-10：记录打开时参数，作为「取消」的回滚基线
    snapshotParams();
    m_paramsDirty = false;
}

void ModuleEditorDialog::done(int r)
{
    // 窗口几何的兜底保存（平时调整大小/移动即已即时落盘）
    saveGeometryNow();
    if (m_autoRecomputeTimer)
        m_autoRecomputeTimer->stop();

    // P1-10：取消 = 回滚到打开时快照；确定 = 保留并提交一次重算。
    // 右上角 X / Esc 与「取消」同义（不意外提交半改状态）。
    if (r == QDialog::Rejected) {
        const bool dirty = m_paramsDirty;
        restoreParams();
        m_paramsDirty = false;
        if (dirty)
            emit recomputeRequested();   // 让下游回到快照值
    } else if (r == QDialog::Accepted) {
        const bool dirty = m_paramsDirty;
        m_paramsDirty = false;
        if (dirty)
            emit recomputeRequested();   // 一次性提交，而不是每个控件各算一次
    }
    QDialog::done(r);
}

void ModuleEditorDialog::snapshotParams()
{
    m_paramSnapshot.clear();
    if (!m_node)
        return;
    for (const QString &name : m_node->getAllParamNames())
        m_paramSnapshot.insert(name, m_node->getParam(name));
}

void ModuleEditorDialog::restoreParams()
{
    if (!m_node || m_paramSnapshot.isEmpty())
        return;
    m_paramHookBusy = true;
    if (m_autoRecomputeTimer)
        m_autoRecomputeTimer->stop();
    for (auto it = m_paramSnapshot.cbegin(); it != m_paramSnapshot.cend(); ++it) {
        if (auto *hn = qobject_cast<HalconNode *>(m_node))
            hn->setParam(it.key(), it.value());   // 一次回滚，避免每个参数各自触发预览
        else
            m_node->setParam(it.key(), it.value());
    }
    if (QWidget *panel = m_paramHost ? m_paramHost->widget() : nullptr)
        m_node->updateParamPanel(panel);
    m_paramHookBusy = false;
    // 参数回滚后，图形 ROI/搜索框/结果表也要回到基线
    echoGeometry();
    refreshTemplatePreview();
    refreshResultTable();
    refreshParamHooks();
}

void ModuleEditorDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    if (m_geoSaveTimer)
        m_geoSaveTimer->start();
}

void ModuleEditorDialog::moveEvent(QMoveEvent *event)
{
    QDialog::moveEvent(event);
    if (m_geoSaveTimer)
        m_geoSaveTimer->start();
}

void ModuleEditorDialog::saveGeometryNow()
{
    const QByteArray geo = saveGeometry();
    QSettings settings(geometryStorePath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("editor/geometry"), geo);
    if (m_splitter)
        settings.setValue(QStringLiteral("editor/splitter"), m_splitter->saveState());
    qDebug() << "ModuleEditor: geometry saved bytes =" << geo.size()
             << "size =" << size() << "to" << geometryStorePath();
}

HalconNode *ModuleEditorDialog::halconNode() const
{
    return qobject_cast<HalconNode *>(m_node);
}

void ModuleEditorDialog::reloadFromNode()
{
    loadImage();
    echoGeometry();
    if (HalconNode *hn = halconNode())
        m_view->setMaskImage(hn->editMask());
    refreshTemplatePreview();
    refreshTemplateList();
    refreshResultTable();
    // 程序化回填参数不应触发自动重算（否则会与用户操作形成回路）
    m_paramHookBusy = true;
    if (QWidget *panel = m_paramHost->widget())
        m_node->updateParamPanel(panel);
    m_paramHookBusy = false;
    refreshParamHooks();   // 节点可能在回填时重建控件，重新挂钩
}

void ModuleEditorDialog::loadImage()
{
    if (!m_node || !m_view)
        return;
    HImage img;
    if (auto *hn = halconNode()) {
        img = HImage(hn->getInputImage());
        if (!img.IsInitialized())
            img = HImage(hn->getOutputImage());
    }
    if (!img.IsInitialized()) {
        if (auto data = m_node->getOutputData(0))
            img = data->getHImage();
    }
    if (img.IsInitialized())
        m_view->setImage(img, m_node->fullName());
    else
        m_view->clear();
}

void ModuleEditorDialog::rebuildParamPanel()
{
    if (!m_node || !m_paramHost)
        return;
    QWidget *panel = m_node->createParamPanel();

    // 布局修复（r19 批 A 第 1 项，现场反馈"右边参数部分没看全"）：
    // ① 包一层 QScrollArea——参数多时能滚；② 内容给最小宽度，长数值不再被裁；
    // ③ 数值输入框统一最小宽度，`2391.00` 这类不再掉一位。
    if (panel) {
        panel->setMinimumWidth(420);
        for (QLineEdit *w : panel->findChildren<QLineEdit *>())
            w->setMinimumWidth(120);
        for (QDoubleSpinBox *w : panel->findChildren<QDoubleSpinBox *>())
            w->setMinimumWidth(120);
        for (QSpinBox *w : panel->findChildren<QSpinBox *>())
            w->setMinimumWidth(120);

        auto *scroll = new QScrollArea();
        scroll->setWidget(panel);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        m_paramHost->setWidget(scroll);
    } else {
        m_paramHost->setWidget(panel);
    }
    installParamHooks(panel);
    rebuildVmTabs();
}

// VM 4.4 工具窗 = 基本参数 / 特征模板 / 运行参数 / 结果显示 四页（截图 1~4）。
// 此前右侧是一长条平铺：参数组、模板列表、结果表挤在一起，看着乱。
// 这里把面板里按 ParamSpec.group 生成的 QGroupBox 归位到四页签上；
// 所有控件仍留在 panel 的子树里，updateParamPanel 的 findChild 查找不受影响。
void ModuleEditorDialog::rebuildVmTabs()
{
    if (!m_paramHost)
        return;
    QWidget *panel = m_paramHost->widget();
    while (panel && qobject_cast<QScrollArea *>(panel))
        panel = qobject_cast<QScrollArea *>(panel)->widget();
    if (!panel || panel->findChild<QTabWidget *>(QStringLiteral("vmTabs"),
                                                 Qt::FindDirectChildrenOnly))
        return;

    // P0-2：基本/运行/结果显示三页签对**所有模块**生效；「特征模板」页仅模板匹配算子有。
    const bool isTmpl = qobject_cast<OpencvTemplateMatchNode *>(m_node) != nullptr;

    m_vmTabs = new QTabWidget(panel);
    m_vmTabs->setObjectName(QStringLiteral("vmTabs"));
    auto makePage = [&](QWidget **page, QVBoxLayout **lay) {
        *page = new QWidget();
        *lay = new QVBoxLayout(*page);
        (*lay)->setContentsMargins(8, 10, 8, 8);
        (*lay)->setSpacing(8);
    };
    QWidget *basicPage, *runPage, *resPage;
    QVBoxLayout *basicLay, *runLay, *resLay;
    makePage(&basicPage, &basicLay);
    makePage(&runPage, &runLay);
    makePage(&resPage, &resLay);

    QWidget *tmplPage = nullptr;
    QVBoxLayout *tmplLay = nullptr;
    if (isTmpl)
        makePage(&tmplPage, &tmplLay);

    // ① 参数分组框按标题归位（图像输入/ROI → 基本；模板配置 → 特征模板；其余 → 运行）
    const QList<QGroupBox *> boxes =
        panel->findChildren<QGroupBox *>(QString(), Qt::FindDirectChildrenOnly);
    for (QGroupBox *box : boxes) {
        const QString t = box->title();
        if (t == QStringLiteral("图像输入") || t == QStringLiteral("ROI区域")
            || t.contains(QStringLiteral("基本"))) {
            basicLay->addWidget(box);
        } else if (t == QStringLiteral("模板配置") && isTmpl) {
            tmplLay->addWidget(box);   // 模板配置参数与模板同页（VM 放在模板配置窗右下）
        } else {
            runLay->addWidget(box);
        }
    }
    // 非分组式面板（部分节点直接平铺控件）：把 panel 直属控件整体归入「基本参数」。
    // 必须跳过 m_vmTabs 本身：它也是 panel 的直属子控件，若被塞进自己的子页布局，
    // 同一控件会被两套布局反复设置几何 → 布局激活死循环（实测直接卡住测试与界面）。
    for (QWidget *w : panel->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
        if (qobject_cast<QGroupBox *>(w) || w == m_vmTabs)
            continue;
        basicLay->addWidget(w);
    }
    // 旧「工具特性」页的启用开关并入基本参数：VM 模块窗没有单独的特性页，
    // 但"禁用此模块"是执行语义，不能因为改布局而丢掉。
    {
        auto *enabledCheck = new QCheckBox(QStringLiteral("启用此模块（禁用后执行时跳过）"), basicPage);
        enabledCheck->setChecked(m_node->isEnabled());
        connect(enabledCheck, &QCheckBox::toggled, this,
                [this](bool on) {
                    if (m_node)
                        m_node->setEnabled(on);
                });
        basicLay->addWidget(enabledCheck);
    }
    basicLay->addStretch();
    // VM「运行参数」页 = 运行参数本身；不再额外插入旧框架的「参数订阅」表。
    runLay->addStretch();
    if (runLay->count() <= 1) {
        auto *emptyHint = new QLabel(QStringLiteral("该算子无独立运行参数。"));
        emptyHint->setStyleSheet(QStringLiteral("color:#888;"));
        runLay->insertWidget(0, emptyHint);
    }

    // ② 特征模板页：预览 + 列表 + 创建/编辑/删除（仅模板算子）
    if (isTmpl) {
        // VM「特征模板」页布局（帮助文档图 13）：顶部 创建 / 载入；中间模板列表；
        // 右侧缩略图 + 编辑模板 / 删除所有模板。这里用纵向等价排布，不改变操作顺序。
        auto *createRow = new QWidget();
        auto *createLay = new QHBoxLayout(createRow);
        createLay->setContentsMargins(0, 0, 0, 0);
        for (QPushButton *b : {m_tmplNewBtn, m_tmplLoadBtn}) {
            if (b)
                createLay->addWidget(b);
        }
        createLay->addStretch();
        tmplLay->addWidget(createRow);
        if (m_templateList)
            tmplLay->addWidget(m_templateList, 1);
        if (m_templatePreview)
            tmplLay->addWidget(m_templatePreview);
        auto *manageRow = new QWidget();
        auto *manageLay = new QHBoxLayout(manageRow);
        manageLay->setContentsMargins(0, 0, 0, 0);
        for (QPushButton *b : {m_tmplEditBtn, m_tmplDelBtn}) {
            if (b)
                manageLay->addWidget(b);
        }
        manageLay->addStretch();
        tmplLay->addWidget(manageRow);
    }

    // ③ 结果显示页：结果表 + VM「结果判断/图像显示/文本显示」配置
    if (m_resultTable) {
        m_resultTable->setMaximumHeight(QWIDGETSIZE_MAX);
        m_resultTable->setMinimumHeight(120);
        resLay->addWidget(m_resultTable, 1);
    }
    buildResultDisplayPage(resLay);

    // ── 实装 4.4 工具通用框架：关于 / 输入参数 / 输出参数 / ROI配置 /
    //    参数面板 / 连线 / 工具特性（每个工具都有，模块专属页排在其后）──
    auto makeReadOnlyTable = [](const QStringList &headers) {
        auto *table = new QTableWidget(0, headers.size());
        table->setHorizontalHeaderLabels(headers);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->verticalHeader()->setVisible(false);
        table->horizontalHeader()->setStretchLastSection(true);
        table->setAlternatingRowColors(true);
        return table;
    };
    auto nodeTypeText = [](NodeBase::NodeType type) {
        switch (type) {
        case NodeBase::IMAGE_ACQUISITION: return QStringLiteral("图像采集");
        case NodeBase::IMAGE_PROCESSING: return QStringLiteral("图像处理");
        case NodeBase::SHAPE_ANALYSIS: return QStringLiteral("特征定位");
        case NodeBase::MEASUREMENT: return QStringLiteral("几何测量");
        case NodeBase::LOGIC: return QStringLiteral("逻辑控制");
        case NodeBase::OUTPUT: return QStringLiteral("结果输出");
        }
        return QStringLiteral("未知");
    };
    auto roiTypeText = [](RoiType type) {
        switch (type) {
        case RoiType::Rect: return QStringLiteral("矩形");
        case RoiType::Circle: return QStringLiteral("圆");
        case RoiType::Line: return QStringLiteral("线段");
        case RoiType::Polygon: return QStringLiteral("多边形");
        case RoiType::RotatedRect: return QStringLiteral("旋转矩形");
        case RoiType::Point: return QStringLiteral("点");
        default: return QStringLiteral("无");
        }
    };
    auto sectionLabel = [](const QString &text) {
        auto *label = new QLabel(QStringLiteral("<b>%1</b>").arg(text));
        label->setStyleSheet(QStringLiteral("color:#cfd8e3; margin-top:6px;"));
        return label;
    };

    const NodeRegistration *reg =
        NodeRegistry::instance().findById(m_node->property("vfpNodeTypeId").toString());

    // 关于
    auto *aboutPage = new QWidget();
    auto *aboutLay = new QVBoxLayout(aboutPage);
    aboutLay->setContentsMargins(10, 10, 10, 10);
    aboutLay->setSpacing(8);
    {
        auto *title = new QLabel(m_node->fullName());
        title->setStyleSheet(QStringLiteral("font-size:16px; font-weight:bold; color:#ffffff;"));
        aboutLay->addWidget(title);
        auto *meta = new QLabel(QStringLiteral("分组：%1　类型：%2　模块号：%3")
                                    .arg(reg ? reg->group : QStringLiteral("未知"),
                                         nodeTypeText(m_node->type()))
                                    .arg(m_node->moduleId()));
        meta->setStyleSheet(QStringLiteral("color:#9fb3c8;"));
        aboutLay->addWidget(meta);
        auto *desc = new QLabel(reg && !reg->description.isEmpty()
                                    ? reg->description
                                    : QStringLiteral("该算子暂无简介。"));
        desc->setWordWrap(true);
        desc->setStyleSheet(QStringLiteral("color:#d8dee9;"));
        aboutLay->addWidget(desc);
        auto *helpBtn = new QPushButton(QStringLiteral("查看帮助"));
        connect(helpBtn, &QPushButton::clicked, this, [this]() { emit helpRequested(m_node); });
        aboutLay->addWidget(helpBtn, 0, Qt::AlignLeft);
        aboutLay->addStretch();
    }

    // 输入参数：端口来源摘要 + 原有图像输入/ROI 参数组
    {
        auto *inputTable = makeReadOnlyTable({QStringLiteral("输入端口"), QStringLiteral("来源模块")});
        const QList<Port *> inputs = m_node->inputPorts();
        for (Port *port : inputs) {
            QStringList sources;
            for (MyProject::Connection *conn : port->connections()) {
                if (conn && conn->getSourceNode())
                    sources << conn->getSourceNode()->fullName();
            }
            const int row = inputTable->rowCount();
            inputTable->insertRow(row);
            inputTable->setItem(row, 0, new QTableWidgetItem(port->name()));
            inputTable->setItem(row, 1, new QTableWidgetItem(
                                           sources.isEmpty() ? QStringLiteral("未连接")
                                                             : sources.join(QStringLiteral("、"))));
        }
        if (inputs.isEmpty()) {
            inputTable->insertRow(0);
            inputTable->setItem(0, 0, new QTableWidgetItem(QStringLiteral("无输入端口")));
        }
        inputTable->setMaximumHeight(150);
        basicLay->insertWidget(0, inputTable);
    }

    // B2 单输入契约：逻辑输入槽的「订阅」选择（VM 基本参数页的输入源/掩膜图像订阅）。
    // 画布上不再有第 2 个输入点，多图/多值来源统一在这里选前序模块的输出。
    {
        const int realInputs = m_node->inputPorts().size();
        const int logicalInputs = m_node->logicalInputPortCount();
        if (logicalInputs > realInputs) {
            auto *box = new QGroupBox(QStringLiteral("输入订阅"));
            auto *boxLay = new QVBoxLayout(box);
            boxLay->setContentsMargins(8, 8, 8, 8);
            boxLay->setSpacing(6);
            const QMap<int, NodeBase::InputBinding> currentBindings = m_node->inputBindings();
            FlowScene *scene = m_node->flowSceneRef();
            for (int slot = realInputs; slot < logicalInputs; ++slot) {
                const QString slotName = m_node->inputSlotName(slot);
                if (slotName.isEmpty())
                    continue;
                auto *row = new QWidget(box);
                auto *rowLay = new QHBoxLayout(row);
                rowLay->setContentsMargins(0, 0, 0, 0);
                auto *label = new QLabel(slotName, row);
                label->setMinimumWidth(76);
                auto *combo = new QComboBox(row);
                combo->setObjectName(QStringLiteral("inputBindingCombo_%1").arg(slot));
                combo->addItem(QStringLiteral("未订阅"), QVariant());
                if (scene) {
                    for (NodeBase *src : scene->nodes()) {
                        if (!src || src == m_node)
                            continue;
                        const QList<Port *> outs = src->outputPorts();
                        for (int p = 0; p < outs.size(); ++p) {
                            combo->addItem(QStringLiteral("%1 [%2]").arg(src->fullName(), outs.at(p)->name()),
                                           QVariant::fromValue(QPair<int, int>(src->moduleId(), p)));
                        }
                    }
                }
                if (currentBindings.contains(slot)) {
                    const NodeBase::InputBinding b = currentBindings.value(slot);
                    for (int i = 1; i < combo->count(); ++i) {
                        const QPair<int, int> pair = combo->itemData(i).value<QPair<int, int>>();
                        if (pair.first == b.sourceModuleId && pair.second == b.sourcePort) {
                            combo->setCurrentIndex(i);
                            break;
                        }
                    }
                }
                connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                        [this, combo, slot](int index) {
                            if (!m_node)
                                return;
                            if (index <= 0) {
                                m_node->clearInputBinding(slot);
                            } else {
                                const QPair<int, int> pair =
                                    combo->itemData(index).value<QPair<int, int>>();
                                m_node->setInputBinding(slot, pair.first, pair.second);
                            }
                            m_paramsDirty = true;
                            scheduleAutoRecompute();
                        });
                rowLay->addWidget(label);
                rowLay->addWidget(combo, 1);
                boxLay->addWidget(row);
            }
            basicLay->insertWidget(1, box);
        }
    }

    // 输出参数
    auto *outputPage = new QWidget();
    auto *outputLay = new QVBoxLayout(outputPage);
    outputLay->setContentsMargins(10, 10, 10, 10);
    outputLay->setSpacing(8);
    {
        outputLay->addWidget(sectionLabel(QStringLiteral("输出端口与最近结果")));
        auto *outputTable = makeReadOnlyTable(
            {QStringLiteral("输出端口"), QStringLiteral("最近输出")});
        const QList<Port *> outputs = m_node->outputPorts();
        for (int i = 0; i < outputs.size(); ++i) {
            auto data = m_node->getOutputData(i);
            const int row = outputTable->rowCount();
            outputTable->insertRow(row);
            outputTable->setItem(row, 0, new QTableWidgetItem(outputs.at(i)->name()));
            outputTable->setItem(row, 1, new QTableWidgetItem(
                                           data ? QStringLiteral("有数据")
                                                : QStringLiteral("无数据（尚未执行）")));
        }
        if (outputs.isEmpty()) {
            outputTable->insertRow(0);
            outputTable->setItem(0, 0, new QTableWidgetItem(QStringLiteral("无输出端口")));
        }
        outputLay->addWidget(outputTable, 1);
        auto *note = new QLabel(QStringLiteral("运行后的详细数值见「结果显示」页或主界面结果区。"));
        note->setStyleSheet(QStringLiteral("color:#8f9aa8;"));
        note->setWordWrap(true);
        outputLay->addWidget(note);
    }

    // ROI 配置
    auto *roiPage = new QWidget();
    auto *roiLay = new QVBoxLayout(roiPage);
    roiLay->setContentsMargins(10, 10, 10, 10);
    roiLay->setSpacing(8);
    {
        HalconNode *halcon = halconNode();
        if (halcon) {
            roiLay->addWidget(sectionLabel(QStringLiteral("当前 ROI")));
            const RoiShape shape = halcon->geometryRoi();
            const QString info = QStringLiteral("类型：%1　P1=(%2, %3)　P2=(%4, %5)　点数：%6")
                                     .arg(roiTypeText(halcon->geometryRoiType()))
                                     .arg(shape.p1.x(), 0, 'f', 1)
                                     .arg(shape.p1.y(), 0, 'f', 1)
                                     .arg(shape.p2.x(), 0, 'f', 1)
                                     .arg(shape.p2.y(), 0, 'f', 1)
                                     .arg(shape.points.size());
            auto *infoLabel = new QLabel(info);
            infoLabel->setWordWrap(true);
            infoLabel->setStyleSheet(QStringLiteral("color:#d8dee9;"));
            roiLay->addWidget(infoLabel);
        } else {
            auto *infoLabel = new QLabel(QStringLiteral("该算子没有几何 ROI，使用参数面板中的坐标参数。"));
            infoLabel->setWordWrap(true);
            infoLabel->setStyleSheet(QStringLiteral("color:#8f9aa8;"));
            roiLay->addWidget(infoLabel);
        }

        auto *btnRow = new QHBoxLayout();
        if (halcon && halcon->geometryRoiType() != RoiType::None) {
            auto *drawBtn = new QPushButton(QStringLiteral("在图上绘制"));
            auto *clearBtn = new QPushButton(QStringLiteral("清除几何"));
            connect(drawBtn, &QPushButton::clicked, this, &ModuleEditorDialog::onDrawGeometry);
            connect(clearBtn, &QPushButton::clicked, this, &ModuleEditorDialog::onClearGeometry);
            btnRow->addWidget(drawBtn);
            btnRow->addWidget(clearBtn);
        }
        if (isTmpl) {
            auto *searchBtn = new QPushButton(QStringLiteral("绘制搜索区域"));
            auto *polyBtn = new QPushButton(QStringLiteral("绘制多边形区域"));
            connect(searchBtn, &QPushButton::clicked, this,
                    &ModuleEditorDialog::onDrawSearchRegion);
            connect(polyBtn, &QPushButton::clicked, this,
                    &ModuleEditorDialog::onDrawSearchPolygon);
            btnRow->addWidget(searchBtn);
            btnRow->addWidget(polyBtn);
        }
        btnRow->addStretch();
        roiLay->addLayout(btnRow);

        auto *note = new QLabel(
            QStringLiteral("画完后可拖中心移动、拖角点缩放、拖蓝点旋转；运行中修改在下一轮执行生效。"));
        note->setWordWrap(true);
        note->setStyleSheet(QStringLiteral("color:#8f9aa8;"));
        roiLay->addWidget(note);
        roiLay->addStretch();
    }

    // 连线
    auto *connPage = new QWidget();
    auto *connLay = new QVBoxLayout(connPage);
    connLay->setContentsMargins(10, 10, 10, 10);
    connLay->setSpacing(8);
    {
        connLay->addWidget(sectionLabel(QStringLiteral("上游 / 下游连线")));
        auto *connTable = makeReadOnlyTable({QStringLiteral("方向"), QStringLiteral("本端端口"),
                                             QStringLiteral("对端模块"), QStringLiteral("对端端口")});
        auto addRow = [connTable](const QString &dir, const QString &port,
                                  const QString &nodeName, const QString &otherPort) {
            const int row = connTable->rowCount();
            connTable->insertRow(row);
            connTable->setItem(row, 0, new QTableWidgetItem(dir));
            connTable->setItem(row, 1, new QTableWidgetItem(port));
            connTable->setItem(row, 2, new QTableWidgetItem(nodeName));
            connTable->setItem(row, 3, new QTableWidgetItem(otherPort));
        };
        for (Port *port : m_node->inputPorts()) {
            for (MyProject::Connection *conn : port->connections()) {
                if (!conn || !conn->getSourceNode())
                    continue;
                addRow(QStringLiteral("输入"), port->name(),
                       conn->getSourceNode()->fullName(),
                       conn->sourcePort() ? conn->sourcePort()->name() : QString());
            }
        }
        for (Port *port : m_node->outputPorts()) {
            for (MyProject::Connection *conn : port->connections()) {
                if (!conn || !conn->getDestinationNode())
                    continue;
                addRow(QStringLiteral("输出"), port->name(),
                       conn->getDestinationNode()->fullName(),
                       conn->targetPort() ? conn->targetPort()->name() : QString());
            }
        }
        if (connTable->rowCount() == 0) {
            connTable->insertRow(0);
            connTable->setItem(0, 0, new QTableWidgetItem(QStringLiteral("暂无连线")));
        }
        connLay->addWidget(connTable, 1);
    }

    // 工具特性
    auto *traitPage = new QWidget();
    auto *traitLay = new QVBoxLayout(traitPage);
    traitLay->setContentsMargins(10, 10, 10, 10);
    traitLay->setSpacing(8);
    {
        traitLay->addWidget(sectionLabel(QStringLiteral("执行特性")));
        auto *info = new QLabel(
            QStringLiteral("节点类型：%1\n模块号：%2\n输出聚合：%3\n缓存复用：%4\n自动预览：%5")
                .arg(nodeTypeText(m_node->type()))
                .arg(m_node->moduleId())
                .arg(m_node->aggregatesOutputPorts() ? QStringLiteral("是") : QStringLiteral("否"))
                .arg(m_node->reusesCachedOutput() ? QStringLiteral("是（确定性输出）")
                                                 : QStringLiteral("否"))
                .arg(m_autoRecomputeCheck && m_autoRecomputeCheck->isChecked()
                         ? QStringLiteral("开启（顶部工具栏切换）")
                         : QStringLiteral("关闭（顶部工具栏切换）")));
        info->setWordWrap(true);
        info->setStyleSheet(QStringLiteral("color:#d8dee9;"));
        traitLay->addWidget(info);
        auto *enabledCheck = new QCheckBox(QStringLiteral("启用此模块（禁用后执行时跳过）"));
        enabledCheck->setChecked(m_node->isEnabled());
        connect(enabledCheck, &QCheckBox::toggled, this,
                [this](bool on) {
                    if (m_node)
                        m_node->setEnabled(on);
                });
        traitLay->addWidget(enabledCheck);
        traitLay->addStretch();
    }

    // VM 4.4 模块窗只有这四页（帮助文档「轮廓匹配」：基本参数 / 特征模板 / 运行参数 / 结果显示）。
    // 关于/输入参数/输出参数/ROI配置/参数面板/连线/工具特性是旧框架，已移除；
    // 其中仍需保留的内容（输入源、ROI 绘制入口、启用开关）并入「基本参数」。
    m_vmTabs->addTab(basicPage, QStringLiteral("基本参数"));
    if (isTmpl)
        m_vmTabs->addTab(tmplPage, QStringLiteral("特征模板"));
    m_vmTabs->addTab(runPage, QStringLiteral("运行参数"));
    m_vmTabs->addTab(resPage, QStringLiteral("结果显示"));

    // 未挂到页签的旧页面没有父控件，不显式释放会随着每次打开工具窗泄漏。
    delete aboutPage;
    delete outputPage;
    delete roiPage;
    delete connPage;
    delete traitPage;

    // ④ panel 布局清空后放页签（GroupBox 已被 addWidget 移走，剩下的是 stretch/空项）
    QLayout *lay = panel->layout();
    if (lay) {
        while (QLayoutItem *it = lay->takeAt(0))
            delete it;
        lay->addWidget(m_vmTabs);
    }
}

void ModuleEditorDialog::buildParamSubscriptionGroup(QVBoxLayout *runLay)
{
    HalconNode *hn = halconNode();
    if (!runLay || !hn || hn->paramSpecs().isEmpty())
        return;

    auto *box = new QGroupBox(QStringLiteral("参数订阅（自定义 / 订阅）"));
    auto *lay = new QVBoxLayout(box);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(6);

    auto *hint = new QLabel(
        QStringLiteral("「订阅」表示该参数在每轮执行前由外部变量覆盖；"
                       "支持 {global.变量名} 与 {模块号.参数名}。留空则保持自定义值。"));
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#8f9aa8; font-size:12px;"));
    lay->addWidget(hint);

    const QString subPrefix = QStringLiteral("__sub__");
    m_paramSubscriptionTable = new QTableWidget(0, 4, box);
    m_paramSubscriptionTable->setObjectName(QStringLiteral("paramSubscriptionTable"));
    m_paramSubscriptionTable->setHorizontalHeaderLabels(
        {QStringLiteral("参数"), QStringLiteral("当前值"),
         QStringLiteral("来源"), QStringLiteral("订阅表达式")});
    m_paramSubscriptionTable->horizontalHeader()->setStretchLastSection(true);
    m_paramSubscriptionTable->verticalHeader()->setVisible(false);
    m_paramSubscriptionTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_paramSubscriptionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    int row = 0;
    for (const ParamSpec &spec : hn->paramSpecs()) {
        if (spec.type != ParamType::Int && spec.type != ParamType::Double)
            continue;
        m_paramSubscriptionTable->insertRow(row);

        auto *nameItem = new QTableWidgetItem(spec.label.isEmpty() ? spec.name : spec.label);
        nameItem->setToolTip(QStringLiteral("%1（%2）").arg(spec.label, spec.name));
        nameItem->setData(Qt::UserRole, spec.name);
        m_paramSubscriptionTable->setItem(row, 0, nameItem);

        auto *valueItem = new QTableWidgetItem(m_node->getParam(spec.name).toString());
        m_paramSubscriptionTable->setItem(row, 1, valueItem);

        auto *sourceCombo = new QComboBox(m_paramSubscriptionTable);
        sourceCombo->addItem(QStringLiteral("自定义"));
        sourceCombo->addItem(QStringLiteral("订阅"));
        const QString expr = m_node->getParam(subPrefix + spec.name).toString();
        sourceCombo->setCurrentIndex(expr.isEmpty() ? 0 : 1);
        m_paramSubscriptionTable->setCellWidget(row, 2, sourceCombo);

        auto *exprEdit = new QLineEdit(m_paramSubscriptionTable);
        exprEdit->setPlaceholderText(QStringLiteral("{global.变量名} 或 {模块号.参数名}"));
        exprEdit->setText(expr);
        exprEdit->setEnabled(!expr.isEmpty());
        m_paramSubscriptionTable->setCellWidget(row, 3, exprEdit);

        connect(sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, spec, exprEdit, subPrefix](int index) {
                    const bool subscribe = index == 1;
                    exprEdit->setEnabled(subscribe);
                    if (!subscribe) {
                        m_paramHookBusy = true;
                        m_node->setParam(subPrefix + spec.name, QString());
                        m_paramHookBusy = false;
                        m_paramsDirty = true;
                        return;
                    }
                    exprEdit->setFocus();
                    const QString current = exprEdit->text().trimmed();
                    if (!current.isEmpty()) {
                        m_paramHookBusy = true;
                        m_node->setParam(subPrefix + spec.name, current);
                        m_paramHookBusy = false;
                    }
                    m_paramsDirty = true;
                });
        connect(exprEdit, &QLineEdit::editingFinished, this, [this, spec, exprEdit, subPrefix]() {
            m_paramHookBusy = true;
            m_node->setParam(subPrefix + spec.name, exprEdit->text().trimmed());
            m_paramHookBusy = false;
            m_paramsDirty = true;
        });
        ++row;
    }

    m_paramSubscriptionTable->resizeColumnsToContents();
    m_paramSubscriptionTable->setMaximumHeight(180);
    lay->addWidget(m_paramSubscriptionTable);
    runLay->insertWidget(runLay->count(), box);
}

void ModuleEditorDialog::restoreParamDefaults()
{
    HalconNode *hn = halconNode();
    if (!hn)
        return;
    const int restored = hn->restoreDefaultParams();
    if (restored <= 0) {
        m_hintLabel->setText(QStringLiteral("该算子没有可恢复的默认参数。"));
        return;
    }
    // 回填面板显示值（不触发自动重算），并把本次改动标记为待提交
    m_paramHookBusy = true;
    QWidget *panel = m_paramHost ? m_paramHost->widget() : nullptr;
    while (panel && qobject_cast<QScrollArea *>(panel))
        panel = qobject_cast<QScrollArea *>(panel)->widget();
    if (panel)
        hn->updateParamPanel(panel);
    m_paramHookBusy = false;
    refreshParamHooks();
    m_paramsDirty = true;
    m_hintLabel->setText(QStringLiteral("已恢复 %1 个参数为默认值；点「确定」提交。").arg(restored));
}

void ModuleEditorDialog::refreshParamHooks()
{
    installParamHooks(m_paramHost ? m_paramHost->widget() : nullptr);
}

void ModuleEditorDialog::buildResultDisplayPage(QVBoxLayout *resLay)
{
    if (!resLay || !m_node)
        return;

    // 写参数不触发自动重算（这些是显示配置，不是算法参数）
    auto writeParam = [this](const QString &key, const QVariant &value) {
        if (!m_node)
            return;
        m_paramHookBusy = true;
        // 显示配置不参与算法，走 setParam 跳过节点内的延迟预览
        if (auto *hn = qobject_cast<HalconNode *>(m_node))
            hn->setParam(key, value);
        else
            m_node->setParam(key, value);
        m_paramHookBusy = false;
        m_paramsDirty = true;
    };
    auto boolParam = [this](const QString &key, bool def) {
        return m_node->getAllParamNames().contains(key) ? m_node->getParam(key).toBool() : def;
    };
    auto intParam = [this](const QString &key, int def) {
        return m_node->getAllParamNames().contains(key) ? m_node->getParam(key).toInt() : def;
    };
    auto strParam = [this](const QString &key, const QString &def) {
        return m_node->getAllParamNames().contains(key) ? m_node->getParam(key).toString() : def;
    };

    // ① 结果判断（VM「结果显示」页）：全部/任一条件 + 条件表
    auto *judgeGroup = new QGroupBox(QStringLiteral("结果判断"));
    auto *judgeLay = new QVBoxLayout(judgeGroup);
    auto *modeRow = new QHBoxLayout();
    modeRow->addWidget(new QLabel(QStringLiteral("判断方式:")));
    m_judgeModeCombo = new QComboBox();
    m_judgeModeCombo->addItem(QStringLiteral("全部条件满足 (AND)"));
    m_judgeModeCombo->addItem(QStringLiteral("任一条件满足 (OR)"));
    m_judgeModeCombo->setCurrentIndex(intParam(QStringLiteral("resJudgeMode"), 0) == 1 ? 1 : 0);
    modeRow->addWidget(m_judgeModeCombo, 1);
    judgeLay->addLayout(modeRow);

    m_judgeTable = new QTableWidget(0, 3);
    m_judgeTable->setHorizontalHeaderLabels(
        {QStringLiteral("参数"), QStringLiteral("运算符"), QStringLiteral("值")});
    m_judgeTable->verticalHeader()->setVisible(false);
    m_judgeTable->horizontalHeader()->setStretchLastSection(true);
    m_judgeTable->setMinimumHeight(110);
    m_judgeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    judgeLay->addWidget(m_judgeTable);

    auto *judgeBtnRow = new QHBoxLayout();
    auto *addCondBtn = new QPushButton(QStringLiteral("添加条件"));
    auto *delCondBtn = new QPushButton(QStringLiteral("删除条件"));
    judgeBtnRow->addWidget(addCondBtn);
    judgeBtnRow->addWidget(delCondBtn);
    judgeBtnRow->addStretch();
    judgeLay->addLayout(judgeBtnRow);
    resLay->addWidget(judgeGroup);

    connect(m_judgeModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, writeParam](int index) {
                writeParam(QStringLiteral("resJudgeMode"), index);
            });
    connect(addCondBtn, &QPushButton::clicked, this, [this]() {
        if (!m_judgeTable)
            return;
        const int r = m_judgeTable->rowCount();
        m_judgeTable->insertRow(r);
        const QStringList names = m_node ? m_node->getAllParamNames() : QStringList();
        m_judgeTable->setItem(r, 0, new QTableWidgetItem(names.value(0)));
        auto *opCombo = new QComboBox();
        opCombo->addItems({QStringLiteral(">="), QStringLiteral("<="), QStringLiteral(">"),
                           QStringLiteral("<"), QStringLiteral("=="), QStringLiteral("!=")});
        m_judgeTable->setCellWidget(r, 1, opCombo);
        m_judgeTable->setItem(r, 2, new QTableWidgetItem(QStringLiteral("0")));
        connect(opCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this](int) { persistJudgeConditions(); });
        persistJudgeConditions();
    });
    connect(delCondBtn, &QPushButton::clicked, this, [this]() {
        if (!m_judgeTable)
            return;
        const int row = m_judgeTable->currentRow();
        if (row >= 0)
            m_judgeTable->removeRow(row);
        persistJudgeConditions();
    });
    connect(m_judgeTable, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem *) { persistJudgeConditions(); });

    loadJudgeConditions();

    // ①-b VM 4.4 轮廓匹配的固定结果判断行（帮助文档「结果显示 → 结果判断」）。
    // 只对模板匹配显示；其它工具继续用上面的通用条件表。
    if (qobject_cast<OpencvTemplateMatchNode *>(m_node)) {
        auto *fixedGroup = new QGroupBox(QStringLiteral("结果判断（模板匹配）"));
        auto *fixedLay = new QVBoxLayout(fixedGroup);

        auto *okZero = new QCheckBox(QStringLiteral("无目标时判定 OK（OKWhenNumIsZero）"));
        okZero->setChecked(boolParam(QStringLiteral("resJudgeOkWhenZero"), false));
        fixedLay->addWidget(okZero);
        connect(okZero, &QCheckBox::toggled, this, [writeParam](bool on) {
            writeParam(QStringLiteral("resJudgeOkWhenZero"), on);
        });

        struct JudgeRow { QString key; QString label; double lo; double hi; int decimals; };
        const QVector<JudgeRow> rows {
            {QStringLiteral("Num"), QStringLiteral("匹配数量"), 1.0, 1000.0, 0},
            {QStringLiteral("Angle"), QStringLiteral("角度"), -180.0, 180.0, 2},
            {QStringLiteral("Scale"), QStringLiteral("尺度"), 0.0, 10.0, 3},
            {QStringLiteral("ScaleX"), QStringLiteral("尺度X"), 0.0, 10.0, 3},
            {QStringLiteral("ScaleY"), QStringLiteral("尺度Y"), 0.0, 10.0, 3},
            {QStringLiteral("Score"), QStringLiteral("匹配分数"), 0.0, 1.0, 3},
            {QStringLiteral("PointX"), QStringLiteral("匹配点X"), 0.0, 100000.0, 2},
            {QStringLiteral("PointY"), QStringLiteral("匹配点Y"), 0.0, 100000.0, 2},
            {QStringLiteral("BoxX"), QStringLiteral("匹配框中心X"), 0.0, 100000.0, 2},
            {QStringLiteral("BoxY"), QStringLiteral("匹配框中心Y"), 0.0, 100000.0, 2},
        };

        auto *table = new QTableWidget(rows.size(), 4, fixedGroup);
        table->setObjectName(QStringLiteral("templateJudgeTable"));
        table->setHorizontalHeaderLabels({QStringLiteral("启用"), QStringLiteral("条件"),
                                          QStringLiteral("下限"), QStringLiteral("上限")});
        table->verticalHeader()->setVisible(false);
        table->horizontalHeader()->setStretchLastSection(true);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionMode(QAbstractItemView::NoSelection);
        table->setMinimumHeight(260);

        for (int r = 0; r < rows.size(); ++r) {
            const JudgeRow &spec = rows.at(r);
            auto *enable = new QCheckBox(table);
            enable->setObjectName(QStringLiteral("judgeEnable_%1").arg(spec.key));
            enable->setChecked(boolParam(QStringLiteral("resJudge%1Enable").arg(spec.key), false));
            auto *enableHost = new QWidget(table);
            auto *enableLay = new QHBoxLayout(enableHost);
            enableLay->setContentsMargins(0, 0, 0, 0);
            enableLay->addWidget(enable, 0, Qt::AlignCenter);
            table->setCellWidget(r, 0, enableHost);

            QTableWidgetItem *nameItem = new QTableWidgetItem(spec.label);
            nameItem->setFlags(Qt::ItemIsEnabled);
            table->setItem(r, 1, nameItem);

            auto makeSpin = [&](const QString &suffix, double def) {
                auto *spin = new QDoubleSpinBox(table);
                spin->setRange(-100000.0, 100000.0);
                spin->setDecimals(spec.decimals);
                spin->setValue(m_node->getParam(QStringLiteral("resJudge%1%2").arg(spec.key, suffix))
                                   .toDouble());
                if (!m_node->getAllParamNames().contains(
                        QStringLiteral("resJudge%1%2").arg(spec.key, suffix)))
                    spin->setValue(def);
                return spin;
            };
            auto *lowSpin = makeSpin(QStringLiteral("Low"), spec.lo);
            auto *highSpin = makeSpin(QStringLiteral("High"), spec.hi);
            table->setCellWidget(r, 2, lowSpin);
            table->setCellWidget(r, 3, highSpin);

            connect(enable, &QCheckBox::toggled, this,
                    [writeParam, key = spec.key](bool on) {
                        writeParam(QStringLiteral("resJudge%1Enable").arg(key), on);
                    });
            connect(lowSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                    [writeParam, key = spec.key](double v) {
                        writeParam(QStringLiteral("resJudge%1Low").arg(key), v);
                    });
            connect(highSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                    [writeParam, key = spec.key](double v) {
                        writeParam(QStringLiteral("resJudge%1High").arg(key), v);
                    });
        }
        fixedLay->addWidget(table);
        resLay->addWidget(fixedGroup);
    }

    // ② 图像显示
    auto *imageGroup = new QGroupBox(QStringLiteral("图像显示"));
    auto *imageLay = new QVBoxLayout(imageGroup);
    m_showResultImageCheck = new QCheckBox(QStringLiteral("显示结果图像"));
    m_showResultImageCheck->setChecked(boolParam(QStringLiteral("resShowImage"), true));
    imageLay->addWidget(m_showResultImageCheck);

    auto *imageColorRow = new QHBoxLayout();
    imageColorRow->addWidget(new QLabel(QStringLiteral("叠加颜色:")));
    m_resultImageColor = strParam(QStringLiteral("resImageColor"), QStringLiteral("#00C8FF"));
    m_resultImageColorBtn = new QPushButton(m_resultImageColor);
    m_resultImageColorBtn->setFixedWidth(110);
    m_resultImageColorBtn->setStyleSheet(
        QStringLiteral("background-color:%1; color:#000;").arg(m_resultImageColor));
    imageColorRow->addWidget(m_resultImageColorBtn);
    imageColorRow->addStretch();
    imageLay->addLayout(imageColorRow);

    auto *imageOpacityRow = new QHBoxLayout();
    imageOpacityRow->addWidget(new QLabel(QStringLiteral("透明度:")));
    m_resultImageOpacitySpin = new QSpinBox();
    m_resultImageOpacitySpin->setRange(0, 100);
    m_resultImageOpacitySpin->setSuffix(QStringLiteral(" %"));
    m_resultImageOpacitySpin->setValue(intParam(QStringLiteral("resImageOpacity"), 100));
    imageOpacityRow->addWidget(m_resultImageOpacitySpin);
    imageOpacityRow->addSpacing(16);
    imageOpacityRow->addWidget(new QLabel(QStringLiteral("线宽:")));
    m_resultImageLineWidthSpin = new QSpinBox();
    m_resultImageLineWidthSpin->setRange(1, 10);
    m_resultImageLineWidthSpin->setValue(intParam(QStringLiteral("resImageLineWidth"), 2));
    imageOpacityRow->addWidget(m_resultImageLineWidthSpin);
    imageOpacityRow->addStretch();
    imageLay->addLayout(imageOpacityRow);
    resLay->addWidget(imageGroup);

    connect(m_showResultImageCheck, &QCheckBox::toggled, this, [writeParam](bool on) {
        writeParam(QStringLiteral("resShowImage"), on);
    });
    connect(m_resultImageColorBtn, &QPushButton::clicked, this, [this, writeParam]() {
        const QColor color = QColorDialog::getColor(QColor(m_resultImageColor), this,
                                                    QStringLiteral("选择叠加颜色"));
        if (!color.isValid())
            return;
        m_resultImageColor = color.name(QColor::HexRgb).toUpper();
        m_resultImageColorBtn->setText(m_resultImageColor);
        m_resultImageColorBtn->setStyleSheet(
            QStringLiteral("background-color:%1; color:%2;")
                .arg(m_resultImageColor,
                     color.lightness() > 128 ? QStringLiteral("#000") : QStringLiteral("#fff")));
        writeParam(QStringLiteral("resImageColor"), m_resultImageColor);
    });
    connect(m_resultImageOpacitySpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [writeParam](int v) { writeParam(QStringLiteral("resImageOpacity"), v); });
    connect(m_resultImageLineWidthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [writeParam](int v) { writeParam(QStringLiteral("resImageLineWidth"), v); });

    // ③ 文本显示
    auto *textGroup = new QGroupBox(QStringLiteral("文本显示"));
    auto *textLay = new QVBoxLayout(textGroup);
    m_showResultTextCheck = new QCheckBox(QStringLiteral("显示结果文本"));
    m_showResultTextCheck->setChecked(boolParam(QStringLiteral("resShowText"), true));
    textLay->addWidget(m_showResultTextCheck);

    auto *textRow = new QHBoxLayout();
    textRow->addWidget(new QLabel(QStringLiteral("内容:")));
    m_resultTextEdit = new QLineEdit(strParam(QStringLiteral("resTextContent"),
                                              QStringLiteral("{module}: {status}  {elapsed}ms")));
    textRow->addWidget(m_resultTextEdit, 1);
    textLay->addLayout(textRow);

    auto *textStyleRow = new QHBoxLayout();
    textStyleRow->addWidget(new QLabel(QStringLiteral("颜色:")));
    m_resultTextColor = strParam(QStringLiteral("resTextColor"), QStringLiteral("#FFFFFF"));
    m_resultTextColorBtn = new QPushButton(m_resultTextColor);
    m_resultTextColorBtn->setFixedWidth(110);
    m_resultTextColorBtn->setStyleSheet(
        QStringLiteral("background-color:%1; color:#000;").arg(m_resultTextColor));
    textStyleRow->addWidget(m_resultTextColorBtn);
    textStyleRow->addSpacing(12);
    textStyleRow->addWidget(new QLabel(QStringLiteral("字号:")));
    m_resultTextSizeSpin = new QSpinBox();
    m_resultTextSizeSpin->setRange(8, 48);
    m_resultTextSizeSpin->setValue(intParam(QStringLiteral("resTextFontSize"), 14));
    textStyleRow->addWidget(m_resultTextSizeSpin);
    textStyleRow->addSpacing(12);
    textStyleRow->addWidget(new QLabel(QStringLiteral("透明度:")));
    m_resultTextOpacitySpin = new QSpinBox();
    m_resultTextOpacitySpin->setRange(0, 100);
    m_resultTextOpacitySpin->setSuffix(QStringLiteral(" %"));
    m_resultTextOpacitySpin->setValue(intParam(QStringLiteral("resTextOpacity"), 100));
    textStyleRow->addWidget(m_resultTextOpacitySpin);
    textStyleRow->addStretch();
    textLay->addLayout(textStyleRow);

    auto *textPosRow = new QHBoxLayout();
    textPosRow->addWidget(new QLabel(QStringLiteral("位置:")));
    m_resultTextPosCombo = new QComboBox();
    m_resultTextPosCombo->addItems({QStringLiteral("左上"), QStringLiteral("右上"),
                                    QStringLiteral("左下"), QStringLiteral("右下")});
    m_resultTextPosCombo->setCurrentIndex(intParam(QStringLiteral("resTextPosition"), 0) & 3);
    textPosRow->addWidget(m_resultTextPosCombo);
    textPosRow->addStretch();
    textLay->addLayout(textPosRow);
    resLay->addWidget(textGroup);
    resLay->addStretch();

    connect(m_showResultTextCheck, &QCheckBox::toggled, this, [writeParam](bool on) {
        writeParam(QStringLiteral("resShowText"), on);
    });
    connect(m_resultTextEdit, &QLineEdit::editingFinished, this, [this, writeParam]() {
        writeParam(QStringLiteral("resTextContent"), m_resultTextEdit->text());
    });
    connect(m_resultTextColorBtn, &QPushButton::clicked, this, [this, writeParam]() {
        const QColor color = QColorDialog::getColor(QColor(m_resultTextColor), this,
                                                    QStringLiteral("选择文本颜色"));
        if (!color.isValid())
            return;
        m_resultTextColor = color.name(QColor::HexRgb).toUpper();
        m_resultTextColorBtn->setText(m_resultTextColor);
        m_resultTextColorBtn->setStyleSheet(
            QStringLiteral("background-color:%1; color:%2;")
                .arg(m_resultTextColor,
                     color.lightness() > 128 ? QStringLiteral("#000") : QStringLiteral("#fff")));
        writeParam(QStringLiteral("resTextColor"), m_resultTextColor);
    });
    connect(m_resultTextSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [writeParam](int v) { writeParam(QStringLiteral("resTextFontSize"), v); });
    connect(m_resultTextOpacitySpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [writeParam](int v) { writeParam(QStringLiteral("resTextOpacity"), v); });
    connect(m_resultTextPosCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [writeParam](int index) { writeParam(QStringLiteral("resTextPosition"), index); });
}

void ModuleEditorDialog::persistJudgeConditions()
{
    if (!m_node || !m_judgeTable)
        return;
    QJsonArray conditions;
    for (int r = 0; r < m_judgeTable->rowCount(); ++r) {
        QJsonObject condition;
        condition.insert(QStringLiteral("param"),
                         m_judgeTable->item(r, 0) ? m_judgeTable->item(r, 0)->text() : QString());
        QString op = QStringLiteral(">=");
        if (auto *combo = qobject_cast<QComboBox *>(m_judgeTable->cellWidget(r, 1)))
            op = combo->currentText();
        else if (m_judgeTable->item(r, 1))
            op = m_judgeTable->item(r, 1)->text();
        condition.insert(QStringLiteral("op"), op);
        condition.insert(QStringLiteral("value"),
                         m_judgeTable->item(r, 2) ? m_judgeTable->item(r, 2)->text() : QString());
        conditions.append(condition);
    }
    m_paramHookBusy = true;
    if (auto *hn = qobject_cast<HalconNode *>(m_node))
        hn->setParam(QStringLiteral("resJudgeConditions"),
                           QJsonDocument(conditions).toJson(QJsonDocument::Compact));
    else
        m_node->setParam(QStringLiteral("resJudgeConditions"),
                         QJsonDocument(conditions).toJson(QJsonDocument::Compact));
    m_paramHookBusy = false;
    m_paramsDirty = true;
}

void ModuleEditorDialog::loadJudgeConditions()
{
    if (!m_node || !m_judgeTable)
        return;
    m_judgeTable->blockSignals(true);
    m_judgeTable->setRowCount(0);
    const QString raw = m_node->getParam(QStringLiteral("resJudgeConditions")).toString();
    if (!raw.trimmed().isEmpty()) {
        const QJsonArray arr = QJsonDocument::fromJson(raw.toUtf8()).array();
        for (const QJsonValue &value : arr) {
            const QJsonObject condition = value.toObject();
            const int r = m_judgeTable->rowCount();
            m_judgeTable->insertRow(r);
            m_judgeTable->setItem(r, 0, new QTableWidgetItem(condition.value(QStringLiteral("param")).toString()));
            auto *opCombo = new QComboBox();
            opCombo->addItems({QStringLiteral(">="), QStringLiteral("<="), QStringLiteral(">"),
                               QStringLiteral("<"), QStringLiteral("=="), QStringLiteral("!=")});
            const QString op = condition.value(QStringLiteral("op")).toString();
            const int opIndex = opCombo->findText(op);
            if (opIndex >= 0)
                opCombo->setCurrentIndex(opIndex);
            m_judgeTable->setCellWidget(r, 1, opCombo);
            m_judgeTable->setItem(r, 2, new QTableWidgetItem(condition.value(QStringLiteral("value")).toString()));
            connect(opCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [this](int) { persistJudgeConditions(); });
        }
    }
    m_judgeTable->blockSignals(false);
}

void ModuleEditorDialog::installParamHooks(QWidget *panel)
{
    if (!panel)
        return;

    // 参数面板由各节点自行创建、控件种类不一，且 NodeBase 没有「参数已变更」信号。
    // 这里不对任何节点做改动：用元对象系统按「基类 + 信号是否存在」通用挂钩可编辑控件，
    // 变更后经去抖再请求重算下游（改参数即可立刻看到下游结果刷新）。
    // base 用于排除同名的无关控件（如滚动条的 valueChanged、分组框的 toggled）。
    static const struct { const char *base; const char *sig; } kHooks[] = {
        { "QAbstractSpinBox", "valueChanged(int)" },      // QSpinBox
        { "QAbstractSpinBox", "valueChanged(double)" },   // QDoubleSpinBox
        { "QComboBox",        "currentIndexChanged(int)" },
        { "QAbstractButton",  "toggled(bool)" },          // QCheckBox / QRadioButton
        { "QSlider",          "valueChanged(int)" },      // 注意：不含 QScrollBar
        { "QLineEdit",        "editingFinished()" },
    };

    const QList<QWidget *> widgets = panel->findChildren<QWidget *>();
    for (QWidget *w : widgets) {
        if (!w)
            continue;
        for (const auto &hook : kHooks) {
            if (!w->inherits(hook.base))
                continue;
            const QByteArray norm = QMetaObject::normalizedSignature(hook.sig);
            if (w->metaObject()->indexOfSignal(norm.constData()) < 0)
                continue;   // 控件没有该信号：先判存在，避免连接时打印告警
            // 旧式 connect 的信号串必须以成员代码 '2' 开头（等价于 SIGNAL() 宏的展开）：
            // 少这个前缀时 Qt 会把首字符当成代码、截断信号名（valueChanged → alueChanged），
            // 连接静默失效。
            const QByteArray sig = QByteArray("2") + norm;
            // 先断开旧连接：面板刷新时重复挂钩也不会重复触发
            QObject::disconnect(w, sig.constData(), this, SLOT(onParamWidgetEdited()));
            QObject::connect(w, sig.constData(), this, SLOT(onParamWidgetEdited()));
        }
        // 记录「最近被编辑的参数控件」，「变量引用」据此把引用插到正确位置
        w->installEventFilter(this);
    }
}

void ModuleEditorDialog::setVariableReferenceProvider(std::function<QStringList()> provider)
{
    m_varRefProvider = std::move(provider);
    rebuildReferenceMenu();
}

void ModuleEditorDialog::rebuildReferenceMenu()
{
    if (!m_varRefMenu)
        return;
    m_varRefMenu->clear();

    const QStringList refs = m_varRefProvider ? m_varRefProvider() : QStringList();
    if (refs.isEmpty()) {
        QAction *empty = m_varRefMenu->addAction(QStringLiteral("（暂无可引用变量：先运行一次流程）"));
        empty->setEnabled(false);
        return;
    }
    for (const QString &ref : refs) {
        QAction *act = m_varRefMenu->addAction(ref);
        connect(act, &QAction::triggered, this, [this, ref]() { insertReference(ref); });
    }
}

bool ModuleEditorDialog::insertReference(const QString &ref)
{
    if (ref.isEmpty())
        return false;

    // 菜单展开时焦点已转到按钮上，所以优先用「最近编辑过的编辑框」；
    // 直接用 focusWidget() 会把文本插到菜单/按钮上，等于丢失。
    QWidget *target = m_lastEditor ? m_lastEditor : QApplication::focusWidget();
    if (target && VariablePanel::insertReferenceInto(target, ref)) {
        if (m_hintLabel)
            m_hintLabel->setText(QStringLiteral("已插入引用 %1（执行时按上游当前值解析）").arg(ref));
        return true;
    }
    if (m_hintLabel) {
        m_hintLabel->setText(target
            ? QStringLiteral("该参数不接受变量引用（只有文本框／多行文本可以插入）")
            : QStringLiteral("请先点一下要插入的参数输入框，再选择变量引用"));
    }
    return false;
}

bool ModuleEditorDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event && event->type() == QEvent::FocusIn) {
        if (auto *w = qobject_cast<QWidget *>(watched))
            m_lastEditor = w;
    }
    return QDialog::eventFilter(watched, event);
}

void ModuleEditorDialog::onParamWidgetEdited()
{
    // 参数→图形（r19 批 A 第 4 步）：面板改数值时把几何 ROI 拉回画布，
    // 与 onRoiEdited 的「图形→参数」构成双向同步（VM 的手感就是这条）。
    if (HalconNode *hn = halconNode()) {
        if (m_view)
            m_view->setRoiShape(hn->geometryRoi());
    }
    refreshSearchOverlay();

    if (m_paramHookBusy)
        return;
    m_paramsDirty = true;   // P1-10：本次编辑有改动（确定提交 / 取消回滚的判据）
    scheduleAutoRecompute();
}

void ModuleEditorDialog::scheduleAutoRecompute()
{
    if (!m_autoRecomputeTimer)
        return;
    if (m_autoRecomputeCheck && !m_autoRecomputeCheck->isChecked())
        return;   // 用户关掉了「自动重算」
    m_autoRecomputeTimer->start();   // 去抖：连续微调只触发一次
}

void ModuleEditorDialog::onAutoRecomputeTimeout()
{
    emit autoRecomputeRequested();
}

void ModuleEditorDialog::echoGeometry()
{
    HalconNode *hn = halconNode();
    if (!hn || !m_view)
        return;
    if (hn->geometryRoiType() != RoiType::None) {
        m_view->setRoiShape(hn->geometryRoi());
        m_view->setRoiHandleEditEnabled(true);
    }
    applyEditTarget();
    // 训练后的特征点：VM 模板配置里那层绿点，画的是"这次打分真正用到的边缘"
    m_view->setFeaturePoints(hn->editorFeaturePoints());
    refreshSearchOverlay();
}

void ModuleEditorDialog::onEditTargetGeometry()
{
    m_editTarget = 0;
    m_pickingSearchRegion = false;
    m_pickingSearchPolygon = false;
    m_targetGeomBtn->setChecked(true);
    m_targetSearchBtn->setChecked(false);
    applyEditTarget();
    refreshSearchOverlay();
    m_hintLabel->setText(QStringLiteral(
        "编辑对象：模板框。可拖角点缩放、拖中心移动、拖蓝点旋转；改完自动重算。"));
}

void ModuleEditorDialog::onEditTargetSearch()
{
    if (!qobject_cast<OpencvTemplateMatchNode *>(m_node)) {
        m_hintLabel->setText(QStringLiteral("当前算子不支持搜索区域。"));
        return;
    }
    m_editTarget = 1;
    m_pickingSearchRegion = false;
    m_pickingSearchPolygon = false;
    m_targetGeomBtn->setChecked(false);
    m_targetSearchBtn->setChecked(true);
    applyEditTarget();
    refreshSearchOverlay();
    m_hintLabel->setText(QStringLiteral(
        "编辑对象：搜索区域。直接拖青框角点/中心即可改；点「绘制搜索区域」可重画。"));
}

void ModuleEditorDialog::applyEditTarget()
{
    HalconNode *hn = halconNode();
    if (!hn || !m_view)
        return;
    m_view->setMaskEditable(false);
    m_view->setRoiColor(m_editTarget == 1 ? QColor(0, 200, 255) : QColor(0, 200, 80));
    if (m_editTarget == 1 && hn->geometryRoiType() != RoiType::None) {
        // 搜索区域：取当前 searchRegion* 参数回显成可编辑矩形（句柄只在这一个对象上）
        const double col = hn->getParam(QStringLiteral("searchRegionCol")).toDouble();
        const double row = hn->getParam(QStringLiteral("searchRegionRow")).toDouble();
        const double w = hn->getParam(QStringLiteral("searchRegionWidth")).toDouble();
        const double h = hn->getParam(QStringLiteral("searchRegionHeight")).toDouble();
        m_view->setRoiEditable(false);
        if (w <= 1.0 || h <= 1.0) {
            // 没有矩形（=整图搜索，或当前生效的是多边形）：不要把 0×0 的框挂在原点当句柄
            const QString poly =
                hn->getParam(QStringLiteral("searchRegionPolygon")).toString().trimmed();
            if (!poly.isEmpty()) {
                QVector<QPointF> pts;
                const QStringList items = poly.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                for (const QString &it : items) {
                    const QStringList xy = it.split(QLatin1Char(','));
                    if (xy.size() == 2)
                        pts.append(QPointF(xy[0].toDouble(), xy[1].toDouble()));
                }
                RoiShape s;
                s.type = pts.size() >= 3 ? RoiType::Polygon : RoiType::None;
                s.points = pts;
                m_view->setRoiShape(s);
                m_view->setRoiHandleEditEnabled(false);   // 顶点不可拖：重画走「绘制多边形区域」
            } else {
                m_view->clearRoi();
                m_view->setRoiHandleEditEnabled(false);
            }
            return;
        }
        RoiShape s;
        s.type = RoiType::Rect;
        s.p1 = QPointF(col, row);
        s.p2 = QPointF(col + w, row + h);
        m_view->setRoiShape(s);
        m_view->setRoiHandleEditEnabled(true);
    } else if (hn->geometryRoiType() != RoiType::None) {
        m_view->setRoiEditable(false);
        m_view->setRoiShape(hn->geometryRoi());
        m_view->setRoiHandleEditEnabled(true);
    }
}

QVector<OverlayShape> ModuleEditorDialog::geometryOverlayShapes() const
{
    QVector<OverlayShape> out;
    HalconNode *hn = halconNode();
    if (!hn || hn->geometryRoiType() == RoiType::None)
        return out;
    const RoiShape s = hn->geometryRoi();
    OverlayShape o;
    o.color = QColor(0, 200, 80);
    switch (s.type) {
    case RoiType::Rect:
        o.type = OverlayShape::Type::RotatedRect;
        o.p1 = QPointF((s.p1.x() + s.p2.x()) * 0.5, (s.p1.y() + s.p2.y()) * 0.5);
        o.width = qAbs(s.p2.x() - s.p1.x());
        o.height = qAbs(s.p2.y() - s.p1.y());
        break;
    case RoiType::RotatedRect:
        o.type = OverlayShape::Type::RotatedRect;
        o.p1 = s.p1;
        o.width = s.width;
        o.height = s.height;
        o.angleDeg = s.angleDeg;
        break;
    case RoiType::Line:
        o.type = OverlayShape::Type::Line;
        o.p1 = s.p1;
        o.p2 = s.p2;
        break;
    case RoiType::Circle:
        o.type = OverlayShape::Type::Circle;
        o.p1 = s.p1;
        o.radius = std::hypot(s.p2.x() - s.p1.x(), s.p2.y() - s.p1.y());
        break;
    case RoiType::Point:
        o.type = OverlayShape::Type::Point;
        o.p1 = s.p1;
        break;
    case RoiType::Polygon:
        for (int i = 0; i + 1 < s.points.size(); ++i) {
            OverlayShape e;
            e.type = OverlayShape::Type::Line;
            e.p1 = s.points[i];
            e.p2 = s.points[i + 1];
            e.color = o.color;
            out.append(e);
        }
        if (s.points.size() >= 3) {
            OverlayShape e;
            e.type = OverlayShape::Type::Line;
            e.p1 = s.points.last();
            e.p2 = s.points.first();
            e.color = o.color;
            out.append(e);
        }
        return out;
    default:
        return out;
    }
    out.append(o);
    return out;
}

void ModuleEditorDialog::setResultOverlay(const QVector<OverlayShape> &shapes)
{
    // 结果叠加与搜索区域框合并（r19 批 A 第 4 步）：两者同屏，现场才看得出
    // "在整个图里找" 与 "只在这个框里找" 的区别。
    m_resultOverlay = shapes;
    refreshSearchOverlay();
}

void ModuleEditorDialog::onDrawGeometry()
{
    HalconNode *hn = halconNode();
    if (!hn || !m_view)
        return;
    m_editTarget = 0;
    if (m_targetGeomBtn) m_targetGeomBtn->setChecked(true);
    if (m_targetSearchBtn) m_targetSearchBtn->setChecked(false);
    m_view->setMaskEditable(false);
    m_view->setRoiEditable(true, hn->geometryRoiType());
    m_hintLabel->setText(QStringLiteral("请在图像上拖拽绘制；画完后可拖改。右键取消。"));
}

namespace {
/// RoiShape → 轴对齐包围盒（Rect 用 p1/p2，其余按四角求界）
QRectF roiBoundsOf(const RoiShape &shape)
{
    QRectF r;
    if (shape.type == RoiType::Rect) {
        r = QRectF(shape.p1, shape.p2).normalized();
        if (r.width() > 1.0 && r.height() > 1.0)
            return r;
    }
    const QVector<QPointF> c = rotatedRectCorners(shape);
    if (c.isEmpty())
        return QRectF();
    double x0 = c.first().x(), x1 = x0, y0 = c.first().y(), y1 = y0;
    for (const QPointF &p : c) {
        x0 = qMin(x0, p.x());
        x1 = qMax(x1, p.x());
        y0 = qMin(y0, p.y());
        y1 = qMax(y1, p.y());
    }
    return QRectF(QPointF(x0, y0), QPointF(x1, y1));
}
}   // namespace

void ModuleEditorDialog::onDrawSearchRegion()
{
    // P0-3：搜索区域仅模板匹配算子有（非模板节点直接拒绝，避免写不存在参数）
    if (!qobject_cast<OpencvTemplateMatchNode *>(m_node)) {
        m_hintLabel->setText(QStringLiteral("当前算子不支持搜索区域。"));
        return;
    }
    if (!m_view || !m_node) {
        m_hintLabel->setText(QStringLiteral("无法绘制：图像或算子未就绪（先双击打开工具并载入图像）"));
        return;
    }
    m_pickingSearchRegion = true;
    m_pickingSearchPolygon = false;
    m_editTarget = 1;
    if (m_targetGeomBtn) m_targetGeomBtn->setChecked(false);
    if (m_targetSearchBtn) m_targetSearchBtn->setChecked(true);
    m_view->setMaskEditable(false);
    m_view->clearRoi();                        // 收起模板框句柄，避免两个框混在一起点不中
    m_view->setRoiEditable(true, RoiType::Rect);
    m_hintLabel->setText(
        QStringLiteral("\u8bf7\u5728\u56fe\u50cf\u4e0a\u6846\u5b9a\u641c\u7d22\u533a\u57df\uff1b"
                       "\u6846\u5b8c\u81ea\u52a8\u5199\u56de\u53c2\u6570\u3002\u53f3\u952e\u53d6\u6d88\u3002"));
}

OverlayShape ModuleEditorDialog::searchRegionShape() const
{
    OverlayShape s;
    HalconNode *hn = halconNode();
    if (!hn)
        return s;
    const double col = hn->getParam(QStringLiteral("searchRegionCol")).toDouble();
    const double row = hn->getParam(QStringLiteral("searchRegionRow")).toDouble();
    const double w = hn->getParam(QStringLiteral("searchRegionWidth")).toDouble();
    const double h = hn->getParam(QStringLiteral("searchRegionHeight")).toDouble();
    if (w <= 1.0 || h <= 1.0)
        return s;                              // 宽/高为 0 ⇒ 整图搜索，不画框
    s.type = OverlayShape::Type::RotatedRect;
    s.p1 = QPointF(col + w * 0.5, row + h * 0.5);
    s.width = w;
    s.height = h;
    s.angleDeg = 0.0;
    s.color = QColor(0, 200, 255);             // 青色：搜索区域（绿色是结果框）
    s.text = QStringLiteral("search");
    return s;
}

void ModuleEditorDialog::refreshSearchOverlay()
{
    if (!m_view)
        return;
    if (!qobject_cast<OpencvTemplateMatchNode *>(m_node)) {
        // 非模板算子没有搜索区域，只显示测量结果叠加
        m_view->setOverlay(m_resultOverlay);
        return;
    }
    QVector<OverlayShape> all = m_resultOverlay;
    // 编辑搜索区域时：该框是画布上的可编辑 ROI（带句柄），不再重复画成叠加；
    // 反过来把模板框画成只读叠加，让"当前在改哪个"一眼可见。
    if (m_editTarget == 1) {
        all += geometryOverlayShapes();
    } else {
        const OverlayShape sr = searchRegionShape();
        if (sr.width > 1.0)
            all.append(sr);
    }
    // 多边形搜索区域：顶点串 → 闭合折线（青色），与矩形框同色系
    HalconNode *hn = halconNode();
    const QString polyText =
        hn ? hn->getParam(QStringLiteral("searchRegionPolygon")).toString().trimmed() : QString();
    if (!polyText.isEmpty()) {
        QVector<QPointF> pts;
        const QStringList items = polyText.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &it : items) {
            const QStringList xy = it.split(QLatin1Char(','));
            if (xy.size() == 2) {
                bool ok1 = false, ok2 = false;
                const double x = xy[0].toDouble(&ok1);
                const double y = xy[1].toDouble(&ok2);
                if (ok1 && ok2)
                    pts.append(QPointF(x, y));
            }
        }
        for (int i = 0; i + 1 < pts.size(); ++i) {
            OverlayShape e;
            e.type = OverlayShape::Type::Line;
            e.p1 = pts[i];
            e.p2 = pts[i + 1];
            e.color = QColor(0, 200, 255);
            all.append(e);
        }
        if (pts.size() >= 3) {
            OverlayShape e;
            e.type = OverlayShape::Type::Line;
            e.p1 = pts.last();
            e.p2 = pts.first();
            e.color = QColor(0, 200, 255);
            all.append(e);
        }
    }
    m_view->setOverlay(all);
}

void ModuleEditorDialog::onDrawSearchPolygon()
{
    if (!qobject_cast<OpencvTemplateMatchNode *>(m_node)) {
        m_hintLabel->setText(QStringLiteral("当前算子不支持搜索区域。"));
        return;
    }
    if (!m_view || !m_node) {
        m_hintLabel->setText(QStringLiteral("无法绘制：图像或算子未就绪（先双击打开工具并载入图像）"));
        return;
    }
    m_pickingSearchPolygon = true;
    m_pickingSearchRegion = false;
    m_editTarget = 1;
    if (m_targetGeomBtn) m_targetGeomBtn->setChecked(false);
    if (m_targetSearchBtn) m_targetSearchBtn->setChecked(true);
    m_view->setMaskEditable(false);
    m_view->clearRoi();
    m_view->setRoiEditable(true, RoiType::Polygon);
    m_hintLabel->setText(
        QStringLiteral("\u5355\u51fb\u9010\u70b9\u3001\u53cc\u51fb\u5b8c\u6210\uff1b"
                       "\u5b8c\u6210\u540e\u5199\u56de\u591a\u8fb9\u5f62\u641c\u7d22\u533a\u57df\u3002\u53f3\u952e\u53d6\u6d88\u3002"));
}

void ModuleEditorDialog::onClearGeometry()
{
    if (HalconNode *hn = halconNode()) {
        hn->applyGeometryRoi(RoiShape());
        m_paramHookBusy = true;
        if (QWidget *panel = m_paramHost->widget())
            hn->updateParamPanel(panel);
        m_paramHookBusy = false;
    }
    if (m_view)
        m_view->clearRoi();
    scheduleAutoRecompute();
}

void ModuleEditorDialog::onEditMask()
{
    if (!m_view)
        return;
    m_view->setRoiEditable(false);
    m_view->setMaskEditable(true);
    m_hintLabel->setText(QStringLiteral("掩膜：左键保留，右键擦除（品红区执行时忽略）。"));
}

void ModuleEditorDialog::onClearMask()
{
    if (HalconNode *hn = halconNode())
        hn->clearEditMask();
    if (m_view) {
        m_view->setMaskImage(QImage());
        m_view->setMaskEditable(false);
    }
    scheduleAutoRecompute();
}

void ModuleEditorDialog::writeSearchRect(const RoiShape &shape)
{
    HalconNode *hn = halconNode();
    if (!hn)
        return;
    const QRectF b = roiBoundsOf(shape);
    if (b.width() <= 1.0 || b.height() <= 1.0)
        return;
    if (FlowScene *fs = hn->flowSceneRef())
        fs->recordUndo();
    hn->setParam(QStringLiteral("searchRegionCol"), int(b.x() + 0.5));
    hn->setParam(QStringLiteral("searchRegionRow"), int(b.y() + 0.5));
    hn->setParam(QStringLiteral("searchRegionWidth"), qMax(1, int(b.width() + 0.5)));
    hn->setParam(QStringLiteral("searchRegionHeight"), qMax(1, int(b.height() + 0.5)));
    // 多边形优先于矩形：拖矩形 = 明确改用矩形，必须把残留的多边形清掉，
    // 否则"拖了没反应"（匹配仍按旧多边形走）。
    const bool hadPolygon =
        !hn->getParam(QStringLiteral("searchRegionPolygon")).toString().trimmed().isEmpty();
    if (hadPolygon)
        hn->setParam(QStringLiteral("searchRegionPolygon"), QString());
    m_hintLabel->setText(hadPolygon
        ? QStringLiteral("搜索区域已写回（矩形），原多边形已清除。")
        : QStringLiteral("搜索区域已写回：匹配只在这个框内进行。"));
}

void ModuleEditorDialog::onRoiEdited(const RoiShape &shape)
{
    HalconNode *hn = halconNode();
    if (!hn)
        return;

    // 搜索区域矩形：绘制一次写回；编辑对象是搜索区域时，拖角点也走这里（B2/B3）
    if (m_pickingSearchRegion
        || (m_editTarget == 1 && shape.type == RoiType::Rect
            && !m_pickingSearchPolygon)) {
        m_pickingSearchRegion = false;
        writeSearchRect(shape);
        m_view->setRoiEditable(false);
        applyEditTarget();
        m_paramHookBusy = true;
        if (QWidget *panel = m_paramHost->widget())
            hn->updateParamPanel(panel);
        m_paramHookBusy = false;
        refreshSearchOverlay();
        scheduleAutoRecompute();
        return;
    }

    // 多边形搜索区域：顶点序列写成 "x,y x,y ..."，并清掉矩形（多边形优先）
    if (m_pickingSearchPolygon) {
        m_pickingSearchPolygon = false;
        if (shape.type == RoiType::Polygon && shape.points.size() >= 3) {
            if (FlowScene *fs = hn->flowSceneRef())
                fs->recordUndo();
            QStringList pts;
            pts.reserve(shape.points.size());
            for (const QPointF &p : shape.points)
                pts << QStringLiteral("%1,%2").arg(int(p.x() + 0.5)).arg(int(p.y() + 0.5));
            hn->setParam(QStringLiteral("searchRegionPolygon"), pts.join(QLatin1Char(' ')));
            hn->setParam(QStringLiteral("searchRegionWidth"), 0);
            hn->setParam(QStringLiteral("searchRegionHeight"), 0);
            m_hintLabel->setText(QStringLiteral("多边形已写回："
                                                "匹配只在这个多边形内进行。"));
        }
        m_view->setRoiEditable(false);
        applyEditTarget();
        m_paramHookBusy = true;
        if (QWidget *panel = m_paramHost->widget())
            hn->updateParamPanel(panel);
        m_paramHookBusy = false;
        refreshSearchOverlay();
        scheduleAutoRecompute();
        return;
    }
    if (FlowScene *fs = hn->flowSceneRef())
        fs->recordUndo();
    hn->applyGeometryRoi(shape);
    m_view->setRoiEditable(false);
    m_view->setRoiShape(hn->geometryRoi());
    m_view->setRoiHandleEditEnabled(true);
    // 回填面板值属程序化更新，不重复触发自动重算
    m_paramHookBusy = true;
    if (QWidget *panel = m_paramHost->widget())
        hn->updateParamPanel(panel);
    m_paramHookBusy = false;
    refreshTemplatePreview();
    m_hintLabel->setText(QStringLiteral("几何已写回并自动重算下游；可拖角点/中心/旋转柄继续改。"));
        // 向导 ②：画完模板框 → 进入第2步（设范围 + 体检）
        if (m_wizardStep == 1) {
            m_wizardStep = 2;
            onCheckBudget();
            m_hintLabel->setText(QStringLiteral("向导 2/3：按需调整角度/尺度范围（上一行是耗时体检）；满意后点「评估模板」"));
        }
    scheduleAutoRecompute();
}

void ModuleEditorDialog::onMaskEdited()
{
    if (HalconNode *hn = halconNode())
        hn->setEditMask(m_view->maskImage());
    scheduleAutoRecompute();   // 掩膜在执行时才参与运算，不重算就看不到效果
}

void ModuleEditorDialog::onWizardGuide()
{
    if (!qobject_cast<OpencvTemplateMatchNode *>(m_node)) {
        m_hintLabel->setText(QStringLiteral("训练向导仅适用于模板匹配算子。"));
        return;
    }
    // 训练向导 ②/4：步进引导（不新建对话框，复用已有三个动作）
    m_wizardStep = 1;
    onDrawGeometry();
    m_hintLabel->setText(
        QStringLiteral("向导 1/3：在图像上拖框选出模板区域（含明显轮廓）；画完自动进入下一步"));
}

void ModuleEditorDialog::onCheckBudget()
{
    if (!qobject_cast<OpencvTemplateMatchNode *>(m_node)) {
        m_hintLabel->setText(QStringLiteral("参数体检仅适用于模板匹配算子。"));
        return;
    }
    HalconNode *hn = halconNode();
    if (!hn)
        return;
    const double aMin = hn->getParam(QStringLiteral("angleMin")).toDouble();
    const double aMax = hn->getParam(QStringLiteral("angleMax")).toDouble();
    const double aStep = hn->getParam(QStringLiteral("angleStep")).toDouble();
    const double sMin = hn->getParam(QStringLiteral("scaleMin")).toDouble();
    const double sMax = hn->getParam(QStringLiteral("scaleMax")).toDouble();
    const double sStep = hn->getParam(QStringLiteral("scaleStep")).toDouble();

    const int combos = TemplateSearchBudget::comboCount(aMin, aMax, aStep, sMin, sMax, sStep);
    m_hintLabel->setText(TemplateSearchBudget::describe(combos));
}

void ModuleEditorDialog::onEvaluateTemplate()
{
    if (!qobject_cast<OpencvTemplateMatchNode *>(m_node)) {
        m_hintLabel->setText(QStringLiteral("模板评估仅适用于模板匹配算子。"));
        return;
    }
    HalconNode *hn = halconNode();
    if (!hn || !m_view)
        return;

    // 先跑一次（沿用手动执行路径：执行完会刷新结果叠加）
    emit executeRequested();

    const double score = hn->getParam(QStringLiteral("matchScore")).toDouble();
    const int count = hn->getParam(QStringLiteral("matchCount")).toInt();
    const bool ok = hn->getParam(QStringLiteral("moduleStatus")).toBool();
    const QString why = hn->getParam(QStringLiteral("trainStatus")).toString();

    // 评级：VM 里"模板质量"最重要的是能不能稳定找到 + 分数是否留有余量
    QString grade;
    QString advice;
    if (!ok || score < 0.6) {
        grade = QStringLiteral("差");
        advice = QStringLiteral("分数 %1：模板可能取错区域，或目标在搜索区域外。"
                                "建议重新框选模板区（确保框内含明显轮廓），必要时勾选从图像训练。")
                     .arg(score, 0, 'f', 3);
    } else if (score < 0.85) {
        grade = QStringLiteral("一般");
        advice = QStringLiteral("分数 %1：能匹配但余量不大。建议缩小角度/尺度范围，"
                                "或提高「最低匹配度」以免误检。")
                     .arg(score, 0, 'f', 3);
    } else {
        grade = QStringLiteral("好");
        advice = QStringLiteral("分数 %1：模板轮廓清晰、定位稳定。"
                                "可考虑把「最低匹配度」提到 %2 以减少误检。")
                     .arg(score, 0, 'f', 3)
                     .arg(qBound(0.5, score - 0.15, 0.95), 0, 'f', 2);
    }

    hn->setParam(QStringLiteral("trainStatus"),
                 QStringLiteral("模板评估：%1（分数 %2，实例 %3）")
                     .arg(grade)
                     .arg(score, 0, 'f', 3)
                     .arg(count));
    m_hintLabel->setText(QStringLiteral("模板评估：%1 — %2 %3")
                             .arg(grade, advice,
                                  why.isEmpty() ? QString() : QStringLiteral("（%1）").arg(why)));

    // 面板刷新，让 trainStatus 可见
    m_paramHookBusy = true;
    if (QWidget *panel = m_paramHost->widget())
        hn->updateParamPanel(panel);
    m_paramHookBusy = false;
    refreshTemplatePreview();
    // 向导 ②：评估即收尾（评级已在提示条）
    if (m_wizardStep >= 2)
        m_wizardStep = 0;
}

void ModuleEditorDialog::onSaveTemplate()
{
    auto *tmplNode = qobject_cast<OpencvTemplateMatchNode *>(m_node);
    HalconNode *hn = halconNode();
    if (!tmplNode || !hn)
        return;
    HImage input(hn->getInputImage());
    if (!input.IsInitialized())
        return;
    cv::Mat gray = OpencvUtil::himageToMat(input);
    if (gray.empty())
        return;
    if (gray.channels() == 3)
        cv::cvtColor(gray, gray, cv::COLOR_BGR2GRAY);

    const RoiShape roi = tmplNode->geometryRoi();
    cv::Mat crop = OpencvTemplateMatchNode::extractTemplatePatch(gray, roi);
    if (crop.empty())
        crop = gray(cv::Rect(gray.cols / 4, gray.rows / 4, gray.cols / 2, gray.rows / 2)).clone();

    QString path = tmplNode->getParam(QStringLiteral("templatePath")).toString();
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, QStringLiteral("保存模板"),
                                            QStringLiteral("template.png"),
                                            QStringLiteral("PNG (*.png);;BMP (*.bmp)"));
        if (path.isEmpty())
            return;
        tmplNode->setParam(QStringLiteral("templatePath"), path);
    } else {
        QFileInfo fi(path);
        if (!fi.dir().exists())
            QDir().mkpath(fi.absolutePath());
    }
    const bool saved = cv::imwrite(path.toStdString(), crop);
    // 教学已经在这里落盘：成功就留在匹配模式，下一轮不必重教覆写；写失败才保留教学态重试
    tmplNode->setParam(QStringLiteral("trainFromImage"), !saved);
    refreshTemplatePreview();
    if (QWidget *panel = m_paramHost->widget())
        tmplNode->updateParamPanel(panel);
    m_hintLabel->setText(saved ? QStringLiteral("模板已保存：%1").arg(path)
                               : QStringLiteral("模板保存失败：%1").arg(path));
}

void ModuleEditorDialog::refreshResultTable()
{
    if (!m_resultTable)
        return;
    m_resultTable->setRowCount(0);
    auto *tmplNode = qobject_cast<OpencvTemplateMatchNode *>(m_node);
    if (!tmplNode) {
        // P0-2：非模板算子也有「结果显示」——显示本算子具名输出的值/类型/状态。
        m_resultTable->clear();
        m_resultTable->setColumnCount(4);
        m_resultTable->setHorizontalHeaderLabels({QStringLiteral("输出项"), QStringLiteral("值"),
                                                  QStringLiteral("类型"), QStringLiteral("状态")});
        const auto named = m_node ? m_node->namedOutputs() : QVector<QPair<QString, QSharedPointer<DataObject>>>();
        int row = 0;
        for (const auto &entry : named) {
            const QString name = entry.first.isEmpty()
                                     ? QStringLiteral("输出 %1").arg(row + 1)
                                     : entry.first;
            QString value = QStringLiteral("-");
            QString type = QStringLiteral("未知");
            QString status = QStringLiteral("无数据");
            if (entry.second) {
                status = QStringLiteral("有数据");
                type = entry.second->getTypeString();
                if (entry.second->getHImage().IsInitialized()) {
                    value = QStringLiteral("图像");
                } else {
                    const QVariant primary = entry.second->primaryValue();
                    if (primary.isValid())
                        value = primary.toString();
                }
            }
            m_resultTable->insertRow(row);
            m_resultTable->setItem(row, 0, new QTableWidgetItem(name));
            m_resultTable->setItem(row, 1, new QTableWidgetItem(value));
            m_resultTable->setItem(row, 2, new QTableWidgetItem(type));
            m_resultTable->setItem(row, 3, new QTableWidgetItem(status));
            ++row;
        }
        return;
    }

    m_resultTable->clear();
    m_resultTable->setColumnCount(10);
    m_resultTable->setHorizontalHeaderLabels(
        {QStringLiteral("序号"), QStringLiteral("中心X"), QStringLiteral("中心Y"),
         QStringLiteral("匹配点X"), QStringLiteral("匹配点Y"), QStringLiteral("角度"),
         QStringLiteral("尺度X"), QStringLiteral("尺度Y"), QStringLiteral("分数"),
         QStringLiteral("模板")});

    QStringList rows = tmplNode->getParam(QStringLiteral("matchList"))
                           .toString()
                           .split(QLatin1Char(';'), Qt::SkipEmptyParts);
    if (rows.isEmpty() && tmplNode->getParam(QStringLiteral("matchScore")).toDouble() > 0.0) {
        // 没有 matchList（老工程/单结果）时用单结果参数合成一行，列序与 matchList 一致
        rows << QStringLiteral("%1,%2,%3,%4,%5,%6,%7")
                    .arg(tmplNode->getParam(QStringLiteral("matchRow")).toDouble(), 0, 'f', 1)
                    .arg(tmplNode->getParam(QStringLiteral("matchCol")).toDouble(), 0, 'f', 1)
                    .arg(tmplNode->getParam(QStringLiteral("matchScore")).toDouble(), 0, 'f', 3)
                    .arg(tmplNode->getParam(QStringLiteral("matchAngle")).toDouble(), 0, 'f', 2)
                    .arg(tmplNode->getParam(QStringLiteral("matchScale")).toDouble(), 0, 'f', 4)
                    .arg(tmplNode->getParam(QStringLiteral("matchPointRow")).toDouble(), 0, 'f', 1)
                    .arg(tmplNode->getParam(QStringLiteral("matchPointCol")).toDouble(), 0, 'f', 1);
    }
    for (const QString &line : rows) {
        const QStringList f = line.split(QLatin1Char(','));
        if (f.size() < 3)
            continue;
        const int r = m_resultTable->rowCount();
        m_resultTable->insertRow(r);
        auto set = [&](int col, const QString &text) {
            m_resultTable->setItem(r, col, new QTableWidgetItem(text));
        };
        set(0, QString::number(r + 1));
        set(1, f[1]);                                        // 匹配框中心 X
        set(2, f[0]);                                        // 匹配框中心 Y
        set(3, f.size() >= 7 ? f[6] : QStringLiteral("-"));   // 匹配点 X
        set(4, f.size() >= 7 ? f[5] : QStringLiteral("-"));   // 匹配点 Y
        set(5, f.size() >= 5 ? f[3] : QStringLiteral("-"));   // 角度
        set(6, f.size() >= 5 ? f[4] : QStringLiteral("-"));   // 尺度 X
        set(7, f.size() >= 5 ? f[4] : QStringLiteral("-"));   // 尺度 Y（当前等比例）
        set(8, f[2]);                                        // 分数
        const QString tmplName =
            tmplNode->getParam(QStringLiteral("matchTemplateName")).toString();
        set(9, tmplName.isEmpty() ? QStringLiteral("-") : tmplName);   // 命中模板（多模板模式）
    }
}

void ModuleEditorDialog::refreshTemplateList()
{
    if (!m_templateList)
        return;
    auto *tmplNode = qobject_cast<OpencvTemplateMatchNode *>(m_node);
    m_templateList->blockSignals(true);
    m_templateList->clear();
    if (!tmplNode) {
        m_templateList->blockSignals(false);
        return;
    }
    const QVector<TemplateEntry> entries = tmplNode->templates();
    for (const TemplateEntry &e : entries) {
        const QString name = e.name.trimmed().isEmpty() ? QFileInfo(e.imagePath).fileName() : e.name;
        m_templateList->addItem(name);
    }
    const int active = tmplNode->activeTemplateIndex();
    if (active >= 0 && active < m_templateList->count())
        m_templateList->setCurrentRow(active);
    m_templateList->blockSignals(false);
}

void ModuleEditorDialog::onTemplateSelected(int row)
{
    auto *tmplNode = qobject_cast<OpencvTemplateMatchNode *>(m_node);
    if (!tmplNode || row < 0)
        return;
    const QVector<TemplateEntry> entries = tmplNode->templates();
    if (row >= entries.size())
        return;
    const TemplateEntry &e = entries.at(row);
    tmplNode->setActiveTemplateIndex(row);
    // 与老字段同步：run() 兜底路径、参数面板、其它读 templatePath 的代码都按当前模板走
    tmplNode->setParam(QStringLiteral("templatePath"), e.imagePath);
    tmplNode->setParam(QStringLiteral("contrastThreshold"), e.contrastThreshold);
    tmplNode->setParam(QStringLiteral("autoEdgeThreshold"), e.autoThreshold);
    tmplNode->setParam(QStringLiteral("featureScale"), e.featureScale);
    tmplNode->setParam(QStringLiteral("speedScale"), e.speedScale);

    // 模板框跟着当前模板的形状走（applyGeometryRoi 会顺手把 trainFromImage 打开，
    // 这里必须还原——切模板不等于"要重新训练"）。
    if (e.shape.type != RoiType::None) {
        const bool wasTrain = tmplNode->getParam(QStringLiteral("trainFromImage")).toBool();
        tmplNode->applyGeometryRoi(e.shape);
        tmplNode->setParam(QStringLiteral("trainFromImage"), wasTrain);
    }
    refreshTemplatePreview();
    echoGeometry();                       // 特征点/模板框按新模板刷新
    if (QWidget *panel = m_paramHost->widget())
        tmplNode->updateParamPanel(panel);
    m_hintLabel->setText(QStringLiteral("当前模板：%1").arg(m_templateList->currentItem()
                                                              ? m_templateList->currentItem()->text()
                                                              : QString()));
}

void ModuleEditorDialog::onNewTemplate()
{
    auto *tmplNode = qobject_cast<OpencvTemplateMatchNode *>(m_node);
    if (!tmplNode)
        return;
    TemplateConfigDialog dlg(tmplNode, -1, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    refreshTemplateList();
    refreshTemplatePreview();
    echoGeometry();
    if (QWidget *panel = m_paramHost->widget())
        tmplNode->updateParamPanel(panel);
    m_hintLabel->setText(QStringLiteral("模板已创建：列表里选中它即用于匹配。"));
}

void ModuleEditorDialog::onEditTemplate()
{
    auto *tmplNode = qobject_cast<OpencvTemplateMatchNode *>(m_node);
    if (!tmplNode || !m_templateList)
        return;
    const int row = m_templateList->currentRow();
    if (row < 0 || row >= tmplNode->templates().size()) {
        m_hintLabel->setText(QStringLiteral("先在列表里选中要编辑的模板。"));
        return;
    }
    TemplateConfigDialog dlg(tmplNode, row, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    refreshTemplateList();
    refreshTemplatePreview();
    echoGeometry();
    if (QWidget *panel = m_paramHost->widget())
        tmplNode->updateParamPanel(panel);
    m_hintLabel->setText(QStringLiteral("模板已更新。"));
}

void ModuleEditorDialog::onDeleteTemplate()
{
    auto *tmplNode = qobject_cast<OpencvTemplateMatchNode *>(m_node);
    if (!tmplNode || tmplNode->templates().isEmpty())
        return;
    if (QMessageBox::question(this, QStringLiteral("删除所有模板"),
                              QStringLiteral("确定清空该算子的模板列表？（模板文件不删除）"))
        != QMessageBox::Yes)
        return;
    tmplNode->setTemplates({});
    tmplNode->setActiveTemplateIndex(0);
    tmplNode->setParam(QStringLiteral("templatePath"), QString());
    refreshTemplateList();
    refreshTemplatePreview();
    echoGeometry();
    m_hintLabel->setText(QStringLiteral("模板列表已清空。"));
}

void ModuleEditorDialog::refreshTemplatePreview()
{
    if (!m_templatePreview || !m_templatePreview->isVisible())
        return;
    auto *tmplNode = qobject_cast<OpencvTemplateMatchNode *>(m_node);
    if (!tmplNode)
        return;

    QImage preview;
    const QString path = tmplNode->getParam(QStringLiteral("templatePath")).toString();
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        preview.load(path);
    } else if (HalconNode *hn = halconNode()) {
        HImage input(hn->getInputImage());
        if (input.IsInitialized()) {
            cv::Mat gray = OpencvUtil::himageToMat(input);
            if (!gray.empty()) {
                if (gray.channels() == 3)
                    cv::cvtColor(gray, gray, cv::COLOR_BGR2GRAY);
                const RoiShape roi = tmplNode->geometryRoi();
                cv::Mat crop = OpencvTemplateMatchNode::extractTemplatePatch(gray, roi);
                if (!crop.empty()) {
                    preview = QImage(crop.data, crop.cols, crop.rows, int(crop.step),
                                     QImage::Format_Grayscale8).copy();
                }
            }
        }
    }
    if (preview.isNull()) {
        m_templatePreview->setText(QStringLiteral("尚未框选或保存模板"));
        m_templatePreview->setPixmap(QPixmap());
        return;
    }
    // 训练向导 ④/4：矫正预览带徽标 —— 改参数时立刻看到"这组设置下的期望角度/尺度/分数"
    const double ang = tmplNode->getParam(QStringLiteral("roiAngle")).toDouble();
    const double scl = tmplNode->getParam(QStringLiteral("matchScale")).toDouble();
    const double sco = tmplNode->getParam(QStringLiteral("matchScore")).toDouble();
    m_templatePreview->setPixmap(QPixmap::fromImage(preview).scaled(
        m_templatePreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_templatePreview->setText(QStringLiteral("角度 %1° / 尺度 %2 / 分数 %3")
                                   .arg(ang, 0, 'f', 1)
                                   .arg(scl > 0.0 ? scl : 1.0, 0, 'f', 2)
                                   .arg(sco, 0, 'f', 3));
}
