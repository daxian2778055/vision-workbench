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
#include <cmath>

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
    auto *shapeCircleBtn = new QPushButton(QStringLiteral("圆形"));
    auto *shapeFreeBtn = new QPushButton(QStringLiteral("自由绘制"));
    auto *shapeWandBtn = new QPushButton(QStringLiteral("魔术棒"));
    auto *shapeWandPlusBtn = new QPushButton(QStringLiteral("魔术棒+"));
    m_wandTol = new QSpinBox();
    m_wandTol->setRange(1, 120);
    m_wandTol->setValue(30);
    m_wandTol->setPrefix(QStringLiteral("容差 "));
    auto *clearShapeBtn = new QPushButton(QStringLiteral("清除形状"));
    auto *delShapeBtn = new QPushButton(QStringLiteral("删除选中"));
    auto *splitBtn = new QPushButton(QStringLiteral("分割"));
    auto *scissorsBtn = new QPushButton(QStringLiteral("剪刀"));
    auto *maskModeBtn = new QPushButton(QStringLiteral("屏蔽模式"));
    maskModeBtn->setCheckable(true);
    auto *blockAllBtn = new QPushButton(QStringLiteral("全部屏蔽"));
    maskModeBtn->setToolTip(QStringLiteral(
        "勾上后画/取到的形状进「屏蔽」列表：从保留区里挖掉（logo/水印/背景）。"
        "屏蔽=保留 − 屏蔽；不画保留区时默认整块保留再挖。"));
    top->addWidget(shapeRectBtn);
    top->addWidget(shapePolyBtn);
    top->addWidget(shapeCircleBtn);
    top->addWidget(shapeFreeBtn);
    top->addWidget(shapeWandBtn);
    top->addWidget(shapeWandPlusBtn);
    top->addWidget(m_wandTol);
    top->addWidget(clearShapeBtn);
    top->addWidget(delShapeBtn);
    top->addWidget(splitBtn);
    top->addWidget(scissorsBtn);
    top->addWidget(maskModeBtn);
    top->addWidget(blockAllBtn);
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
    m_edgeSpin->setRange(1.0, 255.0);      // VM：对比度阈值 = 灰度差 1~255
    m_edgeSpin->setDecimals(0);
    m_edgeSpin->setValue(20.0);
    makeRow(QStringLiteral("对比度阈值（灰度差）"), m_edgeSpin);
    m_apertureCombo = new QComboBox();
    for (int i = 1; i <= 20; ++i)          // VM：特征尺度 1~20，1 最精细
        m_apertureCombo->addItem(QString::number(i));
    m_apertureCombo->setCurrentText(QStringLiteral("3"));
    makeRow(QStringLiteral("特征尺度（1~20）"), m_apertureCombo);
    m_coarseSpin = new QSpinBox();
    m_coarseSpin->setRange(1, 20);         // VM：速度尺度 1~20（金字塔顶层降采样倍率）
    m_coarseSpin->setValue(3);
    makeRow(QStringLiteral("速度尺度（1~20）"), m_coarseSpin);
    rightLay->addLayout(form);

    // VM「匹配中心 + 锁定」：锚点相对模板中心的偏移，结果里的「匹配点」就是它落到场景的位置
    rightLay->addWidget(new QLabel(QStringLiteral("匹配中心（相对模板中心）")));
    auto *anchorRow = new QHBoxLayout();
    m_anchorX = new QDoubleSpinBox();
    m_anchorX->setRange(-4096.0, 4096.0);
    m_anchorX->setDecimals(1);
    m_anchorX->setValue(0.0);
    m_anchorY = new QDoubleSpinBox();
    m_anchorY->setRange(-4096.0, 4096.0);
    m_anchorY->setDecimals(1);
    m_anchorY->setValue(0.0);
    m_anchorLock = new QCheckBox(QStringLiteral("锁定"));
    m_anchorLock->setToolTip(QStringLiteral("锁定后不允许改匹配中心 X/Y（防误改）"));
    auto *anchorPickBtn = new QPushButton(QStringLiteral("图上取点"));
    anchorPickBtn->setToolTip(QStringLiteral("点图上位置设为匹配中心（VM「选择匹配中心」）"));
    anchorRow->addWidget(new QLabel(QStringLiteral("X")));
    anchorRow->addWidget(m_anchorX);
    anchorRow->addWidget(new QLabel(QStringLiteral("Y")));
    anchorRow->addWidget(m_anchorY);
    anchorRow->addWidget(anchorPickBtn);
    anchorRow->addWidget(m_anchorLock);
    rightLay->addLayout(anchorRow);

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
    connect(shapeCircleBtn, &QPushButton::clicked, this, &TemplateConfigDialog::onPickCircle);
    connect(shapeFreeBtn, &QPushButton::clicked, this, &TemplateConfigDialog::onPickFreehand);
    connect(shapeWandBtn, &QPushButton::clicked, this, [this]() {
        m_wandAppend = false;      // 魔术棒：替换当前选择
        onPickWand();
    });
    connect(shapeWandPlusBtn, &QPushButton::clicked, this, [this]() {
        m_wandAppend = true;       // 魔术棒+：追加一块
        onPickWand();
    });
    connect(m_view, &HalconWindow::imageClicked, this, &TemplateConfigDialog::onImageClicked);
    connect(clearShapeBtn, &QPushButton::clicked, this, &TemplateConfigDialog::onClearShape);
    connect(splitBtn, &QPushButton::clicked, this, &TemplateConfigDialog::onSplitShape);
    connect(scissorsBtn, &QPushButton::clicked, this, &TemplateConfigDialog::onScissors);
    connect(anchorPickBtn, &QPushButton::clicked, this, [this]() {
        m_anchorPickMode = true;
        m_wandMode = false;
        m_cutMode = false;
        m_view->setRoiFreehand(false);
        m_view->setRoiEditable(false);
        m_view->setCursor(Qt::CrossCursor);
        m_hint->setText(QStringLiteral("在图上单击设匹配中心位置。"));
    });
    connect(blockAllBtn, &QPushButton::clicked, this, [this]() {
        // VM「全部屏蔽」：清空所有已画形状与匹配中心，进入刷绘模式画保留区
        m_shapes.clear();
        m_excludes.clear();
        m_shape = RoiShape();
        m_anchorX->setValue(0.0);
        m_anchorY->setValue(0.0);
        m_anchorLock->setChecked(false);
        m_anchorPickMode = false;
        m_view->clearRoi();
        m_view->setFeaturePoints({});
        m_view->setRoiFreehand(true);
        m_view->setRoiColor(QColor(0, 200, 255));
        refreshShapeList();
        refreshFeaturePreview();
        m_hint->setText(QStringLiteral(
            "已全部屏蔽（形状与匹配中心清空）；请用套索/矩形/魔术棒画出要保留的区域。"));
    });
    // VM：锁定后隐藏「图上取点」按钮（锁定=不允许再改匹配中心）
    connect(m_anchorLock, &QCheckBox::toggled, anchorPickBtn, [anchorPickBtn](bool locked) {
        anchorPickBtn->setVisible(!locked);
    });
    connect(maskModeBtn, &QPushButton::toggled, this, [this](bool on) {
        m_maskMode = on;
        m_hint->setText(on
                            ? QStringLiteral("屏蔽模式：接下来画/取到的形状都进屏蔽列表（挖掉该区域）。")
                            : QStringLiteral("保留模式：接下来画/取到的形状进保留列表。"));
    });
    connect(delShapeBtn, &QPushButton::clicked, this, [this]() {
        const int row = m_list->currentRow();
        if (row < 0)
            return;
        if (row < m_shapes.size()) {
            m_shapes.removeAt(row);                       // 保留行
        } else if (row - m_shapes.size() < m_excludes.size()) {
            m_excludes.removeAt(row - m_shapes.size());   // 屏蔽行
        } else {
            return;
        }
        m_shape = m_shapes.isEmpty() ? RoiShape() : m_shapes.first();
        refreshShapeList();
        refreshFeaturePreview();
        if (m_shapes.isEmpty() && m_excludes.isEmpty())
            m_view->clearRoi();
    });
    // 点列表项 → 画布上把该形状挂成 ROI（便于确认删哪一块）
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0)
            return;
        if (row < m_shapes.size()) {
            m_view->setRoiColor(QColor(0, 200, 255));
            m_view->setRoiShape(m_shapes.at(row));
        } else if (row - m_shapes.size() < m_excludes.size()) {
            m_view->setRoiColor(QColor(220, 60, 60));        // 屏蔽块用红色区分
            m_view->setRoiShape(m_excludes.at(row - m_shapes.size()));
        } else {
            return;
        }
        m_view->setRoiHandleEditEnabled(true);
    });
    connect(okBtn, &QPushButton::clicked, this, &TemplateConfigDialog::onConfirm);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_view, &HalconWindow::roiEdited, this, &TemplateConfigDialog::onRoiEdited);
    connect(m_anchorLock, &QCheckBox::toggled, this, [this](bool on) {
        m_anchorX->setEnabled(!on);
        m_anchorY->setEnabled(!on);
    });
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
            m_shapes = e.allShapes();
            m_excludes = e.excludes;
            m_shape = m_shapes.isEmpty() ? RoiShape() : m_shapes.first();
            m_savePath = e.imagePath;
            m_edgeSpin->setValue(e.contrastThreshold);
            m_autoChk->setChecked(e.autoThreshold);
            m_apertureCombo->setCurrentText(QString::number(qBound(1, e.featureScale, 20)));
            m_coarseSpin->setValue(qBound(1, e.speedScale, 20));
            m_anchorX->setValue(e.anchorDx);
            m_anchorY->setValue(e.anchorDy);
            m_anchorLock->setChecked(e.anchorLocked);
            m_anchorX->setEnabled(!e.anchorLocked);
            m_anchorY->setEnabled(!e.anchorLocked);
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
    m_wandMode = false;
    m_view->setRoiFreehand(false);
    m_view->setRoiColor(QColor(0, 200, 255));
    m_view->setRoiEditable(true, RoiType::Rect);
    m_hint->setText(QStringLiteral("在图上拖出矩形模板区域；右键取消。"));
}

void TemplateConfigDialog::onPickPolygon()
{
    m_wandMode = false;
    m_view->setRoiFreehand(false);
    m_view->setRoiColor(QColor(0, 200, 255));
    m_view->setRoiEditable(true, RoiType::Polygon);
    m_hint->setText(QStringLiteral("单击逐点、双击完成多边形模板区域；右键取消。"));
}

void TemplateConfigDialog::onPickCircle()
{
    m_wandMode = false;
    m_view->setRoiFreehand(false);
    m_view->setRoiColor(QColor(0, 200, 255));
    m_view->setRoiEditable(true, RoiType::Circle);
    m_hint->setText(QStringLiteral("按下定圆心、拖出半径；右键取消。"));
}

void TemplateConfigDialog::onPickFreehand()
{
    m_wandMode = false;
    m_view->setRoiColor(QColor(0, 200, 255));
    m_view->setRoiFreehand(true);
    m_hint->setText(QStringLiteral("按下后沿轮廓拖动、松手自动闭合；右键取消。"));
}

void TemplateConfigDialog::onPickWand()
{
    m_view->setRoiFreehand(false);
    m_view->setRoiEditable(false);
    m_wandMode = true;
    m_view->setCursor(Qt::PointingHandCursor);
    m_hint->setText(m_wandAppend
                        ? QStringLiteral("点要追加提取的区域（容差 %1）；取到的区域会加到现有形状上。")
                              .arg(m_wandTol->value())
                        : QStringLiteral("点要提取的区域（容差 %1）；取形后会替换当前形状。")
                              .arg(m_wandTol->value()));
}

QVector<QPointF> TemplateConfigDialog::wandPolygon(const cv::Mat &gray, const QPoint &seed,
                                                  int tolerance)
{
    QVector<QPointF> out;
    if (gray.empty() || gray.channels() != 1)
        return out;
    const int x = qBound(0, seed.x(), gray.cols - 1);
    const int y = qBound(0, seed.y(), gray.rows - 1);
    const int tol = qBound(1, tolerance, 255);

    // 只取掩膜、不改图（FLOODFILL_MASK_ONLY）；掩膜比图大 2 像素，取出内层
    cv::Mat mask = cv::Mat::zeros(gray.rows + 2, gray.cols + 2, CV_8UC1);
    cv::floodFill(gray, mask, cv::Point(x, y), cv::Scalar(),
                  nullptr, cv::Scalar(tol), cv::Scalar(tol),
                  4 | (255 << 8) | cv::FLOODFILL_MASK_ONLY);
    const cv::Mat region = mask(cv::Rect(1, 1, gray.cols, gray.rows));

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(region, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty())
        return out;
    auto largest = std::max_element(
        contours.begin(), contours.end(),
        [](const std::vector<cv::Point> &a, const std::vector<cv::Point> &b) {
            return cv::contourArea(a) < cv::contourArea(b);
        });
    std::vector<cv::Point> approx;
    cv::approxPolyDP(*largest, approx, 1.5, true);
    if (approx.size() < 3)
        return out;
    out.reserve(int(approx.size()));
    for (const cv::Point &p : approx)
        out.append(QPointF(p.x, p.y));
    return out;
}

QVector<RoiShape> TemplateConfigDialog::applyWand(const QVector<RoiShape> &current,
                                                 const cv::Mat &gray, const QPoint &seed,
                                                 int tolerance, bool append)
{
    const QVector<QPointF> pts = wandPolygon(gray, seed, tolerance);
    if (pts.size() < 3)
        return current;                     // 取不到就保持原样，绝不误清空
    RoiShape s;
    s.type = RoiType::Polygon;
    s.points = pts;
    if (!append)
        return QVector<RoiShape>{s};       // 魔术棒：替换当前选择
    QVector<RoiShape> out = current;       // 魔术棒+：追加一块
    out.append(s);
    return out;
}

QVector<RoiShape> TemplateConfigDialog::splitShape(const RoiShape &shape, const QPointF &cutA,
                                                  const QPointF &cutB, bool hasCut)
{
    QVector<RoiShape> out;
    const QRectF box = roiAxisAlignedBounds(shape);
    if (box.width() <= 3.0 || box.height() <= 3.0)
        return out;
    const int ox = int(box.x()) - 2;
    const int oy = int(box.y()) - 2;
    const int w = int(box.width()) + 5;
    const int h = int(box.height()) + 5;
    cv::Mat mask = cv::Mat::zeros(h, w, CV_8UC1);
    auto toLocal = [&](const QPointF &p) {
        return cv::Point(int(p.x()) - ox, int(p.y()) - oy);
    };
    switch (shape.type) {
    case RoiType::Rect: {
        const QRectF r = QRectF(shape.p1, shape.p2).normalized();
        cv::rectangle(mask, cv::Rect(toLocal(r.topLeft()), toLocal(r.bottomRight())),
                      cv::Scalar(255), cv::FILLED);
        break;
    }
    case RoiType::RotatedRect: {
        std::vector<cv::Point> pts;
        for (const QPointF &p : rotatedRectCorners(shape))
            pts.push_back(toLocal(p));
        cv::fillConvexPoly(mask, pts, cv::Scalar(255));
        break;
    }
    case RoiType::Circle: {
        const double r = std::hypot(shape.p2.x() - shape.p1.x(), shape.p2.y() - shape.p1.y());
        cv::circle(mask, toLocal(shape.p1), int(r + 0.5), cv::Scalar(255), cv::FILLED);
        break;
    }
    case RoiType::Polygon: {
        std::vector<cv::Point> pts;
        for (const QPointF &p : shape.points)
            pts.push_back(toLocal(p));
        if (pts.size() >= 3)
            cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{pts}, cv::Scalar(255));
        break;
    }
    default:
        return out;
    }
    if (hasCut) {
        // 剪刀：沿线擦掉一条 3px 带，剩下的连通域就是切开后的块
        cv::line(mask, toLocal(cutA), toLocal(cutB), cv::Scalar(0), 3);
    }

    cv::Mat labels, stats, centroids;
    const int n = cv::connectedComponentsWithStats(mask, labels, stats, centroids, 8, CV_32S);
    for (int i = 1; i < n; ++i) {
        if (stats.at<int>(i, cv::CC_STAT_AREA) < 20)
            continue;
        const cv::Mat comp = (labels == i);
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(comp, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        if (contours.empty())
            continue;
        auto largest = std::max_element(
            contours.begin(), contours.end(),
            [](const std::vector<cv::Point> &a, const std::vector<cv::Point> &b) {
                return cv::contourArea(a) < cv::contourArea(b);
            });
        std::vector<cv::Point> approx;
        cv::approxPolyDP(*largest, approx, 1.5, true);
        if (approx.size() < 3)
            continue;
        RoiShape s;
        s.type = RoiType::Polygon;
        for (const cv::Point &p : approx)
            s.points.append(QPointF(p.x + ox, p.y + oy));
        out.append(s);
    }
    return out;
}

void TemplateConfigDialog::onSplitShape()
{
    if (m_shapes.isEmpty())
        return;
    const int row = m_list->currentRow();
    QVector<RoiShape> next;
    for (int i = 0; i < m_shapes.size(); ++i) {
        if (row >= 0 && i != row) {
            next.append(m_shapes.at(i));          // 只拆选中的那块
            continue;
        }
        const QVector<RoiShape> parts = splitShape(m_shapes.at(i));
        if (parts.size() >= 2)
            next += parts;
        else
            next.append(m_shapes.at(i));          // 拆不开（本来就是一块）就原样保留
    }
    const int before = m_shapes.size();
    m_shapes = next;
    m_shape = m_shapes.isEmpty() ? RoiShape() : m_shapes.first();
    refreshShapeList();
    refreshFeaturePreview();
    m_hint->setText(QStringLiteral("分割完成：%1 块 → %2 块。").arg(before).arg(m_shapes.size()));
}

void TemplateConfigDialog::onScissors()
{
    if (m_shapes.isEmpty()) {
        m_hint->setText(QStringLiteral("先画一块形状，再用剪刀沿线切开。"));
        return;
    }
    m_wandMode = false;
    m_cutMode = true;
    m_view->setRoiFreehand(false);
    m_view->setRoiColor(QColor(255, 180, 60));
    m_view->setRoiEditable(true, RoiType::Line);
    m_hint->setText(QStringLiteral("沿要切开的位置拖一条线（穿过形状），松手即切。"));
}

void TemplateConfigDialog::onImageClicked(const QPointF &imagePos)
{
    // 匹配中心取点：图上单击设锚点（VM「选择匹配中心」）
    if (m_anchorPickMode) {
        m_anchorPickMode = false;
        m_view->setCursor(Qt::ArrowCursor);
        // 锚点 = 相对模板中心的偏移；先算模板中心
        const QRectF box = OpencvTemplateMatchNode::shapesBounds(
            m_shapes.isEmpty() ? QVector<RoiShape>() : m_shapes);
        const double cx = box.isValid() ? box.center().x() : 0.0;
        const double cy = box.isValid() ? box.center().y() : 0.0;
        m_anchorX->setValue(imagePos.x() - cx);
        m_anchorY->setValue(imagePos.y() - cy);
        m_hint->setText(QStringLiteral("匹配中心已设为 (%1, %2)（相对模板中心 %3, %4）")
                            .arg(imagePos.x(), 0, 'f', 1)
                            .arg(imagePos.y(), 0, 'f', 1)
                            .arg(cx, 0, 'f', 1)
                            .arg(cy, 0, 'f', 1));
        return;
    }
    if (!m_wandMode || m_trainGray.empty())
        return;
    const QPoint seed(int(imagePos.x()), int(imagePos.y()));
    const QVector<QPointF> pts = wandPolygon(m_trainGray, seed, m_wandTol->value());
    if (pts.size() < 3) {
        m_hint->setText(QStringLiteral("魔术棒没取到区域：换个点或调大容差再试。"));
        return;
    }

    if (m_maskMode) {
        m_excludes = applyWand(m_excludes, m_trainGray, seed, m_wandTol->value(), m_wandAppend);
    } else {
        m_shapes = applyWand(m_shapes, m_trainGray, seed, m_wandTol->value(), m_wandAppend);
    }
    m_shape = m_shapes.isEmpty() ? RoiShape() : m_shapes.last();
    if (m_shape.type == RoiType::None && !m_excludes.isEmpty())
        m_shape = m_excludes.last();
    m_wandMode = false;
    m_view->setRoiEditable(false);
    m_view->setRoiColor(QColor(0, 200, 255));
    if (m_shape.type != RoiType::None)
        m_view->setRoiShape(m_shape);
    m_view->setRoiHandleEditEnabled(true);
    refreshShapeList();
    syncAutoParamsToUi();
    refreshFeaturePreview();
    if (m_shapes.size() <= 1)
        applySuggestedParams();
    m_hint->setText(QStringLiteral("魔术棒已取形：%1 个顶点（共 %2 块形状）。")
                        .arg(pts.size())
                        .arg(m_shapes.size()));
}

void TemplateConfigDialog::onClearShape()
{
    m_shape = RoiShape();
    m_shapes.clear();
    m_excludes.clear();
    m_view->clearRoi();
    m_view->setFeaturePoints({});
    refreshShapeList();
}

void TemplateConfigDialog::onRoiEdited(const RoiShape &shape)
{
    if (shape.type == RoiType::None)
        return;
    // 屏蔽模式：这条形状进屏蔽列表（挖掉该区域），不进保留列表
    if (m_maskMode && shape.type != RoiType::Line) {
        m_excludes.append(shape);
        m_view->setRoiEditable(false);
        m_view->setRoiHandleEditEnabled(true);
        refreshShapeList();
        refreshFeaturePreview();
        m_hint->setText(QStringLiteral("已添加屏蔽形状（屏蔽 %1 块 / 保留 %2 块）。")
                            .arg(m_excludes.size())
                            .arg(m_shapes.size()));
        return;
    }
    // 剪刀：画完一条线就沿它切开（只切选中的那块，未选中则全部）
    if (m_cutMode && shape.type == RoiType::Line) {
        m_cutMode = false;
        const int row = m_list->currentRow();
        QVector<RoiShape> next;
        for (int i = 0; i < m_shapes.size(); ++i) {
            if (row >= 0 && i != row) {
                next.append(m_shapes.at(i));
                continue;
            }
            const QVector<RoiShape> parts =
                splitShape(m_shapes.at(i), shape.p1, shape.p2, true);
            if (parts.size() >= 2)
                next += parts;
            else
                next.append(m_shapes.at(i));   // 线没穿过，形状不变
        }
        m_shapes = next;
        m_shape = m_shapes.isEmpty() ? RoiShape() : m_shapes.first();
        m_view->setRoiEditable(false);
        m_view->setRoiColor(QColor(0, 200, 255));
        if (!m_shapes.isEmpty())
            m_view->setRoiShape(m_shape);
        m_view->setRoiHandleEditEnabled(true);
        refreshShapeList();
        refreshFeaturePreview();
        m_hint->setText(QStringLiteral("已沿线切开：现在共 %1 块。").arg(m_shapes.size()));
        return;
    }
    m_shapes.append(shape);          // 多形状：每次画完追加一块，不清掉之前的
    m_shape = m_shapes.first();
    m_view->setRoiEditable(false);
    m_view->setRoiHandleEditEnabled(true);
    refreshShapeList();
    syncAutoParamsToUi();
    refreshFeaturePreview();
    if (m_shapes.size() == 1)
        applySuggestedParams();          // 第一块形状落下时给一次建议值
    m_hint->setText(QStringLiteral("已添加形状（共 %1 块）；可继续画下一块，或直接确定。")
                        .arg(m_shapes.size()));
}

void TemplateConfigDialog::refreshShapeList()
{
    m_list->clear();
    auto describe = [](const RoiShape &s) -> QString {
        if (s.type == RoiType::Rect) {
            const QRectF r = QRectF(s.p1, s.p2).normalized();
            return QStringLiteral("矩形  %1×%2 @ (%3, %4)")
                .arg(int(r.width()))
                .arg(int(r.height()))
                .arg(int(r.x()))
                .arg(int(r.y()));
        }
        if (s.type == RoiType::RotatedRect)
            return QStringLiteral("旋转矩形  %1×%2  %3°")
                .arg(int(s.width))
                .arg(int(s.height))
                .arg(s.angleDeg, 0, 'f', 1);
        if (s.type == RoiType::Polygon)
            return QStringLiteral("多边形  %1 点").arg(s.points.size());
        if (s.type == RoiType::Circle) {
            const double r = std::hypot(s.p2.x() - s.p1.x(), s.p2.y() - s.p1.y());
            return QStringLiteral("圆  半径 %1 @ (%2, %3)")
                .arg(r, 0, 'f', 1)
                .arg(s.p1.x(), 0, 'f', 1)
                .arg(s.p1.y(), 0, 'f', 1);
        }
        return QString();
    };
    for (int i = 0; i < m_shapes.size(); ++i) {
        const QString t = describe(m_shapes.at(i));
        if (!t.isEmpty())
            m_list->addItem(QStringLiteral("%1. %2").arg(i + 1).arg(t));
    }
    for (int i = 0; i < m_excludes.size(); ++i) {
        const QString t = describe(m_excludes.at(i));
        if (!t.isEmpty())
            m_list->addItem(QStringLiteral("屏蔽%1. %2").arg(i + 1).arg(t));
    }
}

cv::Mat TemplateConfigDialog::cropTemplate() const
{
    if (m_trainGray.empty() || (m_shapes.isEmpty() && m_excludes.isEmpty()))
        return {};
    // 裁剪口径 = 保留 + 屏蔽的并集包围盒（与打分掩膜同一口径）
    QVector<RoiShape> all = m_shapes;
    all += m_excludes;
    const QRectF box = OpencvTemplateMatchNode::shapesBounds(all);
    if (box.width() <= 1.0 || box.height() <= 1.0)
        return {};
    RoiShape rect;
    rect.type = RoiType::Rect;
    rect.p1 = box.topLeft();
    rect.p2 = box.bottomRight();
    return OpencvTemplateMatchNode::extractTemplatePatch(m_trainGray, rect);
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

void TemplateConfigDialog::applySuggestedParams()
{
    if (m_editIndex >= 0)          // 编辑已有模板：不覆盖用户/历史设置
        return;
    const cv::Mat patch = cropTemplate();
    if (patch.empty())
        return;
    const OpencvTemplateMatchNode::ParamSuggestion s =
        OpencvTemplateMatchNode::suggestParams(patch);
    m_apertureCombo->setCurrentText(QString::number(s.featureScale));
    m_coarseSpin->setValue(s.speedScale);
    if (!m_autoChk->isChecked())
        m_edgeSpin->setValue(s.contrastThreshold);
    m_hint->setText(QStringLiteral(
        "已按模板自动给出建议参数：对比度阈值 %1 / 特征尺度 %2 / 速度尺度 %3（可再调）。")
                        .arg(s.contrastThreshold)
                        .arg(s.featureScale)
                        .arg(s.speedScale));
}

void TemplateConfigDialog::refreshFeaturePreview()
{
    const cv::Mat patch = cropTemplate();
    if (patch.empty() || (m_shapes.isEmpty() && m_excludes.isEmpty())) {
        m_view->setFeaturePoints({});
        return;
    }
    QVector<RoiShape> all = m_shapes;
    all += m_excludes;
    const QRectF box = OpencvTemplateMatchNode::shapesBounds(all);
    RoiShape rect;
    rect.type = RoiType::Rect;
    rect.p1 = box.topLeft();
    rect.p2 = box.bottomRight();
    const int aperture = m_apertureCombo->currentText().toInt();
    const double threshold = OpencvTemplateMatchNode::effectiveEdgeThresholdFor(
        patch, aperture, m_edgeSpin->value(), m_autoChk->isChecked());
    cv::Mat mask;
    bool needMask = m_shapes.size() > 1 || !m_excludes.isEmpty();
    for (const RoiShape &s : m_shapes) {
        if (s.type == RoiType::Polygon || s.type == RoiType::Circle)
            needMask = true;
    }
    if (needMask)
        mask = OpencvTemplateMatchNode::shapesMask(m_shapes, box, m_excludes);
    m_view->setFeaturePoints(
        OpencvTemplateMatchNode::featurePointsForPatch(patch, rect, threshold, aperture, mask));
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
    if (m_shapes.isEmpty() && m_excludes.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("模板配置"),
                             QStringLiteral("还没有形状：先用矩形/多边形/圆/套索/魔术棒画一块。"));
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
    entry.shapes = m_shapes;             // 多形状模板
    entry.excludes = m_excludes;         // 屏蔽形状：最终掩膜 = 保留 − 屏蔽
    entry.shape = m_shapes.isEmpty() ? RoiShape() : m_shapes.first();   // 老字段 = 第一块
    entry.contrastThreshold = int(m_edgeSpin->value());
    entry.autoThreshold = m_autoChk->isChecked();
    entry.featureScale = m_apertureCombo->currentText().toInt();
    entry.speedScale = m_coarseSpin->value();
    entry.anchorDx = m_anchorX->value();
    entry.anchorDy = m_anchorY->value();
    entry.anchorLocked = m_anchorLock->isChecked();

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
    m_node->setParam(QStringLiteral("contrastThreshold"), entry.contrastThreshold);
    m_node->setParam(QStringLiteral("autoEdgeThreshold"), entry.autoThreshold);
    m_node->setParam(QStringLiteral("featureScale"), entry.featureScale);
    m_node->setParam(QStringLiteral("speedScale"), entry.speedScale);

    QMessageBox::information(this, QStringLiteral("模板配置"),
                             QStringLiteral("创建模板成功：%1").arg(entry.name));
    accept();
}
