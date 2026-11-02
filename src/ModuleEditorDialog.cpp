#include <QLineEdit>
#include <QDoubleSpinBox>
#include "TemplateSearchBudget.h"
#include "ModuleEditorDialog.h"
#include "HalconNode.h"
#include "OpencvTemplateMatchNode.h"
#include "TemplateConfigDialog.h"
#include <QListWidget>
#include <QMessageBox>
#include "OpencvUtil.h"
#include "DataObject.h"
#include "FlowScene.h"
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
#include <QTimer>
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
    setWindowTitle(node ? QStringLiteral("编辑模块 — %1").arg(node->fullName())
                        : QStringLiteral("编辑模块"));
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
    m_autoRecomputeCheck = new QCheckBox(QStringLiteral("自动重算"));
    m_autoRecomputeCheck->setChecked(true);
    m_autoRecomputeCheck->setToolTip(QStringLiteral("改参数 / 拖 ROI / 涂掩膜后，自动重算本算子及其下游（去抖 400ms）"));
    toolbar->addWidget(execBtn);
    toolbar->addWidget(recomputeBtn);
    toolbar->addWidget(drawBtn);
    toolbar->addWidget(searchBtn);
    toolbar->addWidget(polyBtn);
    toolbar->addWidget(m_targetGeomBtn);
    toolbar->addWidget(m_targetSearchBtn);
    toolbar->addWidget(clearGeomBtn);
    toolbar->addWidget(maskBtn);
    toolbar->addWidget(clearMaskBtn);
    toolbar->addWidget(saveTmplBtn);
    toolbar->addWidget(trainTmplBtn);
    toolbar->addWidget(evalBtn);
    toolbar->addWidget(budgetBtn);
    toolbar->addWidget(wizardBtn);
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
    m_tmplNewBtn = new QPushButton(QStringLiteral("创建模板"));
    m_tmplEditBtn = new QPushButton(QStringLiteral("编辑模板"));
    m_tmplDelBtn = new QPushButton(QStringLiteral("删除所有模板"));
    m_tmplNewBtn->setToolTip(QStringLiteral("打开「模板配置」窗：在图上框形状、调参数、确定即创建"));
    m_tmplEditBtn->setToolTip(QStringLiteral("编辑列表里选中的模板（原地更新）"));
    m_tmplDelBtn->setToolTip(QStringLiteral("清空该算子的模板列表（文件不删）"));
    tmplBtns->addWidget(m_tmplNewBtn);
    tmplBtns->addWidget(m_tmplEditBtn);
    tmplBtns->addWidget(m_tmplDelBtn);
    rightLay->addLayout(tmplBtns);

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

    auto *closeBtn = new QPushButton(QStringLiteral("完成"));
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
    bottom->addStretch();
    bottom->addWidget(closeBtn);
    root->addLayout(bottom);

    HalconNode *hn = halconNode();
    const bool hasGeom = hn && hn->geometryRoiType() != RoiType::None;
    const bool hasMask = hn && hn->supportsMaskEdit();
    const bool isTmpl = qobject_cast<OpencvTemplateMatchNode *>(m_node);
    drawBtn->setVisible(hasGeom);
    clearGeomBtn->setVisible(hasGeom);
    maskBtn->setVisible(hasMask);
    clearMaskBtn->setVisible(hasMask);
    maskBrushLabel->setVisible(hasMask);
    maskEraseCheck->setVisible(hasMask);
    maskBrushSpin->setVisible(hasMask);
    saveTmplBtn->setVisible(isTmpl);
    m_templatePreview->setVisible(isTmpl);
    m_targetGeomBtn->setVisible(isTmpl);
    m_targetSearchBtn->setVisible(isTmpl);
    m_templateList->setVisible(isTmpl);
    m_tmplNewBtn->setVisible(isTmpl);
    m_tmplEditBtn->setVisible(isTmpl);
    m_tmplDelBtn->setVisible(isTmpl);

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
        m_hintLabel->setText(QStringLiteral(
            "已按 ROI 训练；图上绿点 = 模板强梯度点（GOC 用它打分，灰度法仅作质量参考）"));
    });
    connect(fitBtn, &QPushButton::clicked, m_view, &HalconWindow::fitToWindow);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
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
}

void ModuleEditorDialog::done(int r)
{
    // 完成 / 右上角 X / Esc 的兜底保存（平时调整大小/移动即已即时落盘）
    saveGeometryNow();
    QDialog::done(r);
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
}

void ModuleEditorDialog::refreshParamHooks()
{
    installParamHooks(m_paramHost ? m_paramHost->widget() : nullptr);
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
    // 训练向导 ②/4：步进引导（不新建对话框，复用已有三个动作）
    m_wizardStep = 1;
    onDrawGeometry();
    m_hintLabel->setText(
        QStringLiteral("向导 1/3：在图像上拖框选出模板区域（含明显轮廓）；画完自动进入下一步"));
}

void ModuleEditorDialog::onCheckBudget()
{
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
    tmplNode->setParam(QStringLiteral("edgeThreshold"), e.edgeThreshold);
    tmplNode->setParam(QStringLiteral("autoEdgeThreshold"), e.autoThreshold);
    tmplNode->setParam(QStringLiteral("edgeAperture"), e.aperture);
    tmplNode->setParam(QStringLiteral("coarseLongSide"), e.coarseLongSide);

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
