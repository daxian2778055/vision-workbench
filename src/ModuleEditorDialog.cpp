#include "ModuleEditorDialog.h"
#include "HalconNode.h"
#include "OpencvTemplateMatchNode.h"
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
#include <QTimer>
#include <QToolButton>
#include <QMenu>
#include <QApplication>
#include <QEvent>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

using namespace HalconCpp;

ModuleEditorDialog::ModuleEditorDialog(NodeBase *node, QWidget *parent)
    : QDialog(parent)
    , m_node(node)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(node ? QStringLiteral("编辑模块 — %1").arg(node->fullName())
                        : QStringLiteral("编辑模块"));
    resize(1100, 720);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    auto *toolbar = new QHBoxLayout();
    auto *execBtn = new QPushButton(QStringLiteral("执行算子"));
    auto *recomputeBtn = new QPushButton(QStringLiteral("重算下游"));
    recomputeBtn->setToolTip(QStringLiteral("参数改完后：作废本算子及其下游的缓存，只重跑这一段链路（不重跑无关分支）"));
    auto *drawBtn = new QPushButton(QStringLiteral("在图上绘制"));
    auto *clearGeomBtn = new QPushButton(QStringLiteral("清除几何"));
    auto *maskBtn = new QPushButton(QStringLiteral("编辑掩膜"));
    auto *clearMaskBtn = new QPushButton(QStringLiteral("清除掩膜"));
    auto *saveTmplBtn = new QPushButton(QStringLiteral("保存模板图像"));
    auto *fitBtn = new QPushButton(QStringLiteral("适应窗口"));
    m_autoRecomputeCheck = new QCheckBox(QStringLiteral("自动重算"));
    m_autoRecomputeCheck->setChecked(true);
    m_autoRecomputeCheck->setToolTip(QStringLiteral("改参数 / 拖 ROI / 涂掩膜后，自动重算本算子及其下游（去抖 400ms）"));
    toolbar->addWidget(execBtn);
    toolbar->addWidget(recomputeBtn);
    toolbar->addWidget(drawBtn);
    toolbar->addWidget(clearGeomBtn);
    toolbar->addWidget(maskBtn);
    toolbar->addWidget(clearMaskBtn);
    toolbar->addWidget(saveTmplBtn);
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

    m_paramHost = new QScrollArea();
    m_paramHost->setWidgetResizable(true);
    rightLay->addWidget(m_paramHost, 1);

    splitter->addWidget(m_view);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    root->addWidget(splitter, 1);

    auto *closeBtn = new QPushButton(QStringLiteral("完成"));
    auto *bottom = new QHBoxLayout();
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
    saveTmplBtn->setVisible(isTmpl);
    m_templatePreview->setVisible(isTmpl);

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
    connect(clearGeomBtn, &QPushButton::clicked, this, &ModuleEditorDialog::onClearGeometry);
    connect(maskBtn, &QPushButton::clicked, this, &ModuleEditorDialog::onEditMask);
    connect(clearMaskBtn, &QPushButton::clicked, this, &ModuleEditorDialog::onClearMask);
    connect(saveTmplBtn, &QPushButton::clicked, this, &ModuleEditorDialog::onSaveTemplate);
    connect(fitBtn, &QPushButton::clicked, m_view, &HalconWindow::fitToWindow);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_view, &HalconWindow::roiEdited, this, &ModuleEditorDialog::onRoiEdited);
    connect(m_view, &HalconWindow::maskEdited, this, &ModuleEditorDialog::onMaskEdited);

    // 自动重算去抖：连续微调参数只触发一次重算
    m_autoRecomputeTimer = new QTimer(this);
    m_autoRecomputeTimer->setSingleShot(true);
    m_autoRecomputeTimer->setInterval(400);
    connect(m_autoRecomputeTimer, &QTimer::timeout, this, &ModuleEditorDialog::onAutoRecomputeTimeout);

    rebuildParamPanel();
    reloadFromNode();
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
    m_paramHost->setWidget(panel);
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
    if (m_hintLabel)
        m_hintLabel->setText(QStringLiteral("请先点一下要插入的参数输入框，再选择变量引用"));
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
}

void ModuleEditorDialog::setResultOverlay(const QVector<OverlayShape> &shapes)
{
    if (m_view)
        m_view->setOverlay(shapes);
}

void ModuleEditorDialog::onDrawGeometry()
{
    HalconNode *hn = halconNode();
    if (!hn || !m_view)
        return;
    m_view->setMaskEditable(false);
    m_view->setRoiEditable(true, hn->geometryRoiType());
    m_hintLabel->setText(QStringLiteral("请在图像上拖拽绘制；画完后可拖改。右键取消。"));
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

void ModuleEditorDialog::onRoiEdited(const RoiShape &shape)
{
    HalconNode *hn = halconNode();
    if (!hn)
        return;
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
    scheduleAutoRecompute();
}

void ModuleEditorDialog::onMaskEdited()
{
    if (HalconNode *hn = halconNode())
        hn->setEditMask(m_view->maskImage());
    scheduleAutoRecompute();   // 掩膜在执行时才参与运算，不重算就看不到效果
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
    cv::imwrite(path.toStdString(), crop);
    tmplNode->setParam(QStringLiteral("trainFromImage"), true);
    refreshTemplatePreview();
    if (QWidget *panel = m_paramHost->widget())
        tmplNode->updateParamPanel(panel);
    m_hintLabel->setText(QStringLiteral("模板已保存：%1").arg(path));
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
    m_templatePreview->setPixmap(QPixmap::fromImage(preview).scaled(
        m_templatePreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_templatePreview->setText(QString());
}
