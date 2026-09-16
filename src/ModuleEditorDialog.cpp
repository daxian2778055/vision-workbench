#include "ModuleEditorDialog.h"
#include "HalconNode.h"
#include "OpencvTemplateMatchNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "FlowScene.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
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
    toolbar->addWidget(execBtn);
    toolbar->addWidget(recomputeBtn);
    toolbar->addWidget(drawBtn);
    toolbar->addWidget(clearGeomBtn);
    toolbar->addWidget(maskBtn);
    toolbar->addWidget(clearMaskBtn);
    toolbar->addWidget(saveTmplBtn);
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
    if (QWidget *panel = m_paramHost->widget())
        m_node->updateParamPanel(panel);
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
        if (QWidget *panel = m_paramHost->widget())
            hn->updateParamPanel(panel);
    }
    if (m_view)
        m_view->clearRoi();
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
    if (QWidget *panel = m_paramHost->widget())
        hn->updateParamPanel(panel);
    refreshTemplatePreview();
    m_hintLabel->setText(QStringLiteral("几何已写回。可拖角点/中心/旋转柄继续改，或执行算子看结果框。"));
}

void ModuleEditorDialog::onMaskEdited()
{
    if (HalconNode *hn = halconNode())
        hn->setEditMask(m_view->maskImage());
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
