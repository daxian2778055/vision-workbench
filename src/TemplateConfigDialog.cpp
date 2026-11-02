#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "TemplateConfigDialog.h"

#include "HalconWindow.h"
#include "OpencvUtil.h"
#include "ProjectPaths.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

TemplateConfigDialog::TemplateConfigDialog(OpencvTemplateMatchNode *node, int editIndex,
                                           QWidget *parent)
    : QDialog(parent)
    , m_node(node)
    , m_editIndex(editIndex)
{
    setWindowTitle(QStringLiteral("模板配置"));
    resize(1100, 720);

    auto *root = new QVBoxLayout(this);

    // ---- 顶栏：选择当前图像 / 选择其他图像 / 模板存图 ----
    auto *top = new QHBoxLayout();
    auto *useCurrentBtn = new QPushButton(QStringLiteral("选择当前图像"));
    auto *useOtherBtn = new QPushButton(QStringLiteral("选择其他图像"));
    auto *savePathBtn = new QPushButton(QStringLiteral("模板存图…"));
    top->addWidget(useCurrentBtn);
    top->addWidget(useOtherBtn);
    top->addWidget(savePathBtn);
    top->addStretch();
    auto *shapeRectBtn = new QPushButton(QStringLiteral("矩形"));
    auto *shapePolyBtn = new QPushButton(QStringLiteral("多边形"));
    auto *clearShapeBtn = new QPushButton(QStringLiteral("清除形状"));
    top->addWidget(shapeRectBtn);
    top->addWidget(shapePolyBtn);
    top->addWidget(clearShapeBtn);
    root->addLayout(top);

    // ---- 左：图像区；右：配置参数 + 形状列表 ----
    auto *split = new QHBoxLayout();
    m_view = new HalconWindow(this);
    m_view->setMinimumWidth(640);
    split->addWidget(m_view, 1);

    auto *right = new QWidget(this);
    auto *rightLay = new QVBoxLayout(right);
    rightLay->setContentsMargins(8, 0, 0, 0);

    auto *nameRow = new QHBoxLayout();
    nameRow->addWidget(new QLabel(QStringLiteral("模板名称")));
    m_nameEdit = new QLineEdit();
    nameRow->addWidget(m_nameEdit, 1);
    rightLay->addLayout(nameRow);

    rightLay->addWidget(new QLabel(QStringLiteral("配置参数")));
    auto *form = new QVBoxLayout();
    auto makeRow = [&](const QString &label, QWidget *w) {
        auto *row = new QHBoxLayout();
        row->addWidget(new QLabel(label));
        row->addStretch();
        row->addWidget(w);
        form->addLayout(row);
    };
    m_autoChk = new QCheckBox(QStringLiteral("自动"));
    m_autoChk->setChecked(true);
    makeRow(QStringLiteral("阈值模式"), m_autoChk);
    m_edgeSpin = new QDoubleSpinBox();
    m_edgeSpin->setRange(1.0, 200.0);
    m_edgeSpin->setDecimals(1);
    m_edgeSpin->setValue(20.0);
    makeRow(QStringLiteral("对比度阈值"), m_edgeSpin);
    m_apertureCombo = new QComboBox();
    m_apertureCombo->addItems({QStringLiteral("3"), QStringLiteral("5"), QStringLiteral("7")});
    makeRow(QStringLiteral("特征尺度"), m_apertureCombo);
    m_coarseSpin = new QSpinBox();
    m_coarseSpin->setRange(240, 1600);
    m_coarseSpin->setSingleStep(40);
    m_coarseSpin->setValue(480);
    makeRow(QStringLiteral("速度尺度（粗搜长边）"), m_coarseSpin);
    rightLay->addLayout(form);

    rightLay->addWidget(new QLabel(QStringLiteral("列表")));
    m_list = new QListWidget();
    m_list->setMaximumHeight(110);
    rightLay->addWidget(m_list);
    rightLay->addStretch();

    split->addWidget(right, 0);
    right->setMinimumWidth(320);
    root->addLayout(split, 1);

    m_hint = new QLabel();
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet(QStringLiteral("color:#666;"));
    root->addWidget(m_hint);

    auto *bottom = new QHBoxLayout();
    bottom->addStretch();
    auto *cancelBtn = new QPushButton(QStringLiteral("取消"));
    auto *okBtn = new QPushButton(QStringLiteral("确定"));
    okBtn->setDefault(true);
    bottom->addWidget(cancelBtn);
    bottom->addWidget(okBtn);
    root->addLayout(bottom);

    connect(useCurrentBtn, &QPushButton::clicked, this, [this]() {
        if (!m_node)
            return;
        const HObject input = m_node->getInputImage();
        if (!input.IsInitialized()) {
            m_hint->setText(QStringLiteral("当前算子还没有输入图像——先执行一次算子再建模板。"));
            return;
        }
        m_trainGray = OpencvUtil::himageToMat(HImage(input));
        if (m_trainGray.channels() == 3)
            cv::cvtColor(m_trainGray, m_trainGray, cv::COLOR_BGR2GRAY);
        m_view->setImage(HImage(input), QStringLiteral("当前输入图像"));
        m_trainLabel = QStringLiteral("当前输入图像");
        m_hint->setText(QStringLiteral("已选择当前输入图像；在图上框出模板区域。"));
        refreshFeaturePreview();
    });
    connect(useOtherBtn, &QPushButton::clicked, this, &TemplateConfigDialog::onPickOtherImage);
    connect(savePathBtn, &QPushButton::clicked, this, &TemplateConfigDialog::onPickSavePath);
    connect(shapeRectBtn, &QPushButton::clicked, this, &TemplateConfigDialog::onPickRect);
    connect(shapePolyBtn, &QPushButton::clicked, this, &TemplateConfigDialog::onPickPolygon);
    connect(clearShapeBtn, &QPushButton::clicked, this, &TemplateConfigDialog::onClearShape);
    connect(okBtn, &QPushButton::clicked, this, &TemplateConfigDialog::onConfirm);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_view, &HalconWindow::roiEdited, this, &TemplateConfigDialog::onRoiEdited);
    connect(m_autoChk, &QCheckBox::toggled, this, [this](bool on) {
        m_edgeSpin->setEnabled(!on);
        if (on)
            syncAutoParamsToUi();
    });

    // ---- 初值：编辑已有模板 或 用节点当前 ROI 预填 ----
    QString prefillName = QStringLiteral("模板%1").arg(m_node ? m_node->templates().size() + 1 : 1);
    if (m_node && m_editIndex >= 0) {
        const QVector<TemplateEntry> entries = m_node->templates();
        if (m_editIndex < entries.size()) {
            const TemplateEntry &e = entries.at(m_editIndex);
            prefillName = e.name;
            m_shape = e.shape;
            m_savePath = e.imagePath;
            m_edgeSpin->setValue(e.edgeThreshold);
            m_autoChk->setChecked(e.autoThreshold);
            const int idx = m_apertureCombo->findText(QString::number(e.aperture));
            if (idx >= 0)
                m_apertureCombo->setCurrentIndex(idx);
            m_coarseSpin->setValue(e.coarseLongSide);
        }
    } else if (m_node && !m_node->templates().isEmpty()) {
        m_shape = m_node->activeTemplate().shape;
    }
    m_nameEdit->setText(prefillName);
    m_edgeSpin->setEnabled(!m_autoChk->isChecked());

    // 图像：优先当前输入图，没有再看模板文件
    if (m_node) {
        const HObject input = m_node->getInputImage();
        if (input.IsInitialized()) {
            m_trainGray = OpencvUtil::himageToMat(HImage(input));
            if (m_trainGray.channels() == 3)
                cv::cvtColor(m_trainGray, m_trainGray, cv::COLOR_BGR2GRAY);
            m_view->setImage(HImage(input), QStringLiteral("当前输入图像"));
            m_trainLabel = QStringLiteral("当前输入图像");
        }
    }
    if (m_shape.type != RoiType::None) {
        m_view->setRoiColor(QColor(0, 200, 255));
        m_view->setRoiShape(m_shape);
        m_view->setRoiHandleEditEnabled(true);
        refreshFeaturePreview();
    }
    refreshShapeList();
    m_hint->setText(QStringLiteral(
        "在图上拖出模板区域（矩形/多边形），右侧改参数；图上绿点 = 该模板参与打分的特征点。"));
}

void TemplateConfigDialog::onPickOtherImage()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择训练图像"), QString(),
        QStringLiteral("图像 (*.png *.bmp *.jpg *.jpeg *.tif *.tiff)"));
    if (path.isEmpty())
        return;
    cv::Mat img = cv::imread(path.toStdString(), cv::IMREAD_GRAYSCALE);
    if (img.empty()) {
        QMessageBox::warning(this, QStringLiteral("选择其他图像"),
                             QStringLiteral("图像读取失败：%1").arg(path));
        return;
    }
    m_trainGray = img;
    m_trainLabel = QFileInfo(path).fileName();
    m_view->setImage(OpencvUtil::matToHimage(img), m_trainLabel);
    m_hint->setText(QStringLiteral("已选择训练图像：%1").arg(m_trainLabel));
    refreshFeaturePreview();
}

void TemplateConfigDialog::onPickSavePath()
{
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("模板存图"), m_savePath.isEmpty() ? QStringLiteral("template.png") : m_savePath,
        QStringLiteral("PNG (*.png);;BMP (*.bmp)"));
    if (path.isEmpty())
        return;
    m_savePath = path;
    m_hint->setText(QStringLiteral("模板将保存到：%1").arg(m_savePath));
}

void TemplateConfigDialog::onPickRect()
{
    m_view->setRoiColor(QColor(0, 200, 255));
    m_view->setRoiEditable(true, RoiType::Rect);
    m_hint->setText(QStringLiteral("在图上拖出矩形模板区域；右键取消。"));
}

void TemplateConfigDialog::onPickPolygon()
{
    m_view->setRoiColor(QColor(0, 200, 255));
    m_view->setRoiEditable(true, RoiType::Polygon);
    m_hint->setText(QStringLiteral("单击逐点、双击完成多边形模板区域；右键取消。"));
}

void TemplateConfigDialog::onClearShape()
{
    m_shape = RoiShape();
    m_view->clearRoi();
    m_view->setFeaturePoints({});
    refreshShapeList();
}

void TemplateConfigDialog::onRoiEdited(const RoiShape &shape)
{
    m_shape = shape;
    m_view->setRoiEditable(false);
    m_view->setRoiHandleEditEnabled(true);
    refreshShapeList();
    syncAutoParamsToUi();
    refreshFeaturePreview();
}

void TemplateConfigDialog::refreshShapeList()
{
    m_list->clear();
    if (m_shape.type == RoiType::None)
        return;
    QString text;
    if (m_shape.type == RoiType::Rect) {
        const QRectF r = QRectF(m_shape.p1, m_shape.p2).normalized();
        text = QStringLiteral("矩形  %1×%2 @ (%3, %4)")
                   .arg(int(r.width())).arg(int(r.height()))
                   .arg(int(r.x())).arg(int(r.y()));
    } else if (m_shape.type == RoiType::RotatedRect) {
        text = QStringLiteral("旋转矩形  %1×%2  %3°")
                   .arg(int(m_shape.width)).arg(int(m_shape.height))
                   .arg(m_shape.angleDeg, 0, 'f', 1);
    } else if (m_shape.type == RoiType::Polygon) {
        text = QStringLiteral("多边形  %1 点").arg(m_shape.points.size());
    }
    if (!text.isEmpty())
        m_list->addItem(text);
}

cv::Mat TemplateConfigDialog::cropTemplate() const
{
    if (m_trainGray.empty() || m_shape.type == RoiType::None)
        return {};
    return OpencvTemplateMatchNode::extractTemplatePatch(m_trainGray, m_shape);
}

void TemplateConfigDialog::syncAutoParamsToUi()
{
    if (!m_autoChk->isChecked())
        return;
    const cv::Mat patch = cropTemplate();
    if (patch.empty())
        return;
    const int aperture = m_apertureCombo->currentText().toInt();
    const double autoThreshold = OpencvTemplateMatchNode::effectiveEdgeThresholdFor(
        patch, aperture, m_edgeSpin->value(), true);
    m_edgeSpin->setValue(autoThreshold);
}

void TemplateConfigDialog::refreshFeaturePreview()
{
    const cv::Mat patch = cropTemplate();
    if (patch.empty()) {
        m_view->setFeaturePoints({});
        return;
    }
    const int aperture = m_apertureCombo->currentText().toInt();
    const double threshold = OpencvTemplateMatchNode::effectiveEdgeThresholdFor(
        patch, aperture, m_edgeSpin->value(), m_autoChk->isChecked());
    m_view->setFeaturePoints(
        OpencvTemplateMatchNode::featurePointsForPatch(patch, m_shape, threshold, aperture));
}

bool TemplateConfigDialog::ensureSavePath(QString *path)
{
    if (!m_savePath.isEmpty()) {
        QFileInfo fi(m_savePath);
        if (!fi.dir().exists() && !QDir().mkpath(fi.absolutePath())) {
            QMessageBox::warning(this, QStringLiteral("模板存图"),
                                 QStringLiteral("模板目录创建失败：%1").arg(fi.absolutePath()));
            return false;
        }
        *path = m_savePath;
        return true;
    }
    QString def = m_nameEdit->text().trimmed();
    if (def.isEmpty())
        def = QStringLiteral("template");
    const QString chosen = QFileDialog::getSaveFileName(
        this, QStringLiteral("模板存图"), def + QStringLiteral(".png"),
        QStringLiteral("PNG (*.png);;BMP (*.bmp)"));
    if (chosen.isEmpty())
        return false;
    m_savePath = chosen;
    *path = chosen;
    return true;
}

void TemplateConfigDialog::onConfirm()
{
    if (!m_node) {
        reject();
        return;
    }
    if (m_trainGray.empty()) {
        QMessageBox::warning(this, QStringLiteral("模板配置"),
                             QStringLiteral("没有训练图像：先点「选择当前图像」或「选择其他图像」。"));
        return;
    }
    const cv::Mat patch = cropTemplate();
    if (patch.empty() || patch.cols < 8 || patch.rows < 8) {
        QMessageBox::warning(this, QStringLiteral("模板配置"),
                             QStringLiteral("模板区域无效：请在图上框出至少 8×8 的区域。"));
        return;
    }
    QString savePath;
    if (!ensureSavePath(&savePath))
        return;
    if (!cv::imwrite(savePath.toStdString(), patch)) {
        QMessageBox::warning(this, QStringLiteral("模板配置"),
                             QStringLiteral("模板写盘失败：%1").arg(savePath));
        return;
    }

    TemplateEntry entry;
    entry.name = m_nameEdit->text().trimmed().isEmpty()
                     ? QFileInfo(savePath).completeBaseName()
                     : m_nameEdit->text().trimmed();
    entry.imagePath = savePath;
    entry.shape = m_shape;
    entry.edgeThreshold = m_edgeSpin->value();
    entry.autoThreshold = m_autoChk->isChecked();
    entry.aperture = m_apertureCombo->currentText().toInt();
    entry.coarseLongSide = m_coarseSpin->value();

    QVector<TemplateEntry> entries = m_node->templates();
    if (m_editIndex >= 0 && m_editIndex < entries.size())
        entries[m_editIndex] = entry;
    else
        entries.append(entry);
    const int active = (m_editIndex >= 0 && m_editIndex < entries.size()) ? m_editIndex
                                                                          : entries.size() - 1;
    m_node->setTemplates(entries);
    m_node->setActiveTemplateIndex(active);
    // 与老字段同步：run() 的兜底路径、面板显示、其它读取 templatePath 的代码都还有效
    m_node->setParam(QStringLiteral("templatePath"), savePath);
    m_node->setParam(QStringLiteral("edgeThreshold"), entry.edgeThreshold);
    m_node->setParam(QStringLiteral("autoEdgeThreshold"), entry.autoThreshold);
    m_node->setParam(QStringLiteral("edgeAperture"), entry.aperture);
    m_node->setParam(QStringLiteral("coarseLongSide"), entry.coarseLongSide);

    QMessageBox::information(this, QStringLiteral("模板配置"),
                             QStringLiteral("创建模板成功：%1").arg(entry.name));
    accept();
}
