#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "TemplateConfigDialog.h"

#include "HalconWindow.h"
#include "OpencvUtil.h"
#include "ProjectPaths.h"
#include "VisionWorkbenchStyle.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRegularExpression>
#include <QToolButton>
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

namespace {

/// VM 模板配置窗的工具图标：用 QPainter 画单色线稿，避免把第三方图标资源带进仓库。
static QIcon vmToolIcon(const QString &kind)
{
    QPixmap pm(28, 28);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(QStringLiteral("#5A5A5A")));
    pen.setWidthF(1.4);
    p.setPen(pen);

    if (kind == QLatin1String("select")) {
        // VM 工具条第一个图标：箭头指针
        QPainterPath arrow;
        arrow.moveTo(7, 5);
        arrow.lineTo(20, 13);
        arrow.lineTo(14, 15);
        arrow.lineTo(18, 23);
        arrow.lineTo(15, 24);
        arrow.lineTo(11, 16);
        arrow.lineTo(7, 21);
        arrow.closeSubpath();
        p.drawPath(arrow);
    } else if (kind == QLatin1String("rotatedrect")) {
        QPainterPath rr;
        rr.moveTo(8, 12);
        rr.lineTo(18, 6);
        rr.lineTo(23, 15);
        rr.lineTo(11, 22);
        rr.closeSubpath();
        p.drawPath(rr);
    } else if (kind == QLatin1String("rect")) {
        p.drawRect(6, 8, 16, 12);
    } else if (kind == QLatin1String("circle")) {
        p.drawEllipse(6, 6, 16, 16);
    } else if (kind == QLatin1String("polygon")) {
        QPainterPath path;
        path.moveTo(14, 5); path.lineTo(24, 12); path.lineTo(20, 23);
        path.lineTo(8, 23); path.lineTo(4, 12); path.closeSubpath();
        p.drawPath(path);
    } else if (kind == QLatin1String("freehand")) {
        QPainterPath path;
        path.moveTo(4, 20);
        path.cubicTo(8, 8, 13, 24, 18, 12);
        path.cubicTo(21, 6, 23, 12, 25, 9);
        p.drawPath(path);
    } else if (kind == QLatin1String("wand") || kind == QLatin1String("wandplus")) {
        p.drawLine(QPointF(7, 23), QPointF(21, 9));
        QPainterPath star;
        star.moveTo(22, 5); star.lineTo(24, 9); star.lineTo(27, 10);
        star.lineTo(24, 12); star.lineTo(23, 16); star.lineTo(21, 12);
        star.lineTo(18, 11); star.closeSubpath();
        p.drawPath(star);
        if (kind == QLatin1String("wandplus")) {
            p.drawLine(QPointF(23, 18), QPointF(23, 25));
            p.drawLine(QPointF(20, 21.5), QPointF(26, 21.5));
        }
    } else if (kind == QLatin1String("clear")) {
        // 清除形状：圆 + 斜杠（"清空"），不要和垃圾桶（删除选中）撞脸
        p.drawEllipse(6, 6, 16, 16);
        p.drawLine(QPointF(9, 9), QPointF(19, 19));
    } else if (kind == QLatin1String("scissors")) {
        // 分割：一个被竖虚线切开的矩形（左右各一块）
        p.drawRect(5, 8, 8, 12);
        p.drawRect(15, 8, 8, 12);
        QPen dashed = p.pen();
        dashed.setStyle(Qt::DashLine);
        p.setPen(dashed);
        p.drawLine(QPointF(14, 4), QPointF(14, 24));
    } else if (kind == QLatin1String("cut")) {
        // 剪刀：两片刀刃 + 两个指环，和上面的"分割"一眼可分
        p.drawEllipse(5, 17, 6, 6);
        p.drawEllipse(5, 5, 6, 6);
        p.drawLine(QPointF(10, 8), QPointF(23, 21));
        p.drawLine(QPointF(10, 20), QPointF(23, 7));
    } else if (kind == QLatin1String("brush")) {
        p.drawRoundedRect(5, 13, 14, 8, 2, 2);
        p.drawLine(QPointF(19, 13), QPointF(24, 8));
        p.drawLine(QPointF(22, 6), QPointF(26, 10));
        p.drawPoint(QPointF(8, 17));
        p.drawPoint(QPointF(12, 17));
    } else if (kind == QLatin1String("eraserdots")) {
        p.drawRect(6, 12, 13, 7);
        p.drawLine(QPointF(8, 12), QPointF(15, 5));
        p.drawPoint(QPointF(21, 18));
        p.drawPoint(QPointF(24, 15));
        p.drawPoint(QPointF(21, 12));
    } else if (kind == QLatin1String("eraser")) {
        p.drawRect(7, 12, 14, 7);
        p.drawLine(QPointF(9, 12), QPointF(16, 5));
        p.drawLine(QPointF(20, 12), QPointF(23, 7));
    } else if (kind == QLatin1String("delete")) {
        // 删除选中：垃圾桶（桶身竖线 + 桶盖 + 提手），与"清除形状"的圆斜杠区分
        p.drawLine(QPointF(6, 9), QPointF(22, 9));
        p.drawLine(QPointF(11, 6), QPointF(17, 6));
        p.drawLine(QPointF(9, 9), QPointF(10, 23));
        p.drawLine(QPointF(10, 23), QPointF(18, 23));
        p.drawLine(QPointF(18, 23), QPointF(19, 9));
        p.drawLine(QPointF(13, 12), QPointF(13, 20));
        p.drawLine(QPointF(16, 12), QPointF(16, 20));
    } else if (kind == QLatin1String("undo") || kind == QLatin1String("redo")) {
        QPainterPath path;
        path.moveTo(kind == QLatin1String("undo") ? 22 : 6, 8);
        path.cubicTo(12, 4, 6, 10, kind == QLatin1String("undo") ? 7 : 21, 20);
        p.drawPath(path);
        if (kind == QLatin1String("undo")) {
            p.drawLine(QPointF(4, 5), QPointF(7, 9));
            p.drawLine(QPointF(10, 5), QPointF(7, 9));
        } else {
            p.drawLine(QPointF(24, 5), QPointF(21, 9));
            p.drawLine(QPointF(18, 5), QPointF(21, 9));
        }
    } else if (kind == QLatin1String("zoomin") || kind == QLatin1String("zoomout")) {
        p.drawEllipse(6, 6, 11, 11);
        p.drawLine(QPointF(16, 16), QPointF(23, 23));
        p.drawLine(QPointF(8.5, 11.5), QPointF(14.5, 11.5));
        if (kind == QLatin1String("zoomin"))
            p.drawLine(QPointF(11.5, 8.5), QPointF(11.5, 14.5));
    } else if (kind == QLatin1String("fit")) {
        p.drawLine(QPointF(6, 12), QPointF(6, 6));
        p.drawLine(QPointF(6, 6), QPointF(12, 6));
        p.drawLine(QPointF(22, 6), QPointF(22, 12));
        p.drawLine(QPointF(22, 22), QPointF(16, 22));
        p.drawLine(QPointF(6, 16), QPointF(6, 22));
        p.drawRect(10, 10, 8, 8);
    } else if (kind == QLatin1String("fullscreen")) {
        p.drawLine(QPointF(6, 12), QPointF(6, 6));
        p.drawLine(QPointF(6, 6), QPointF(12, 6));
        p.drawLine(QPointF(22, 16), QPointF(22, 22));
        p.drawLine(QPointF(22, 22), QPointF(16, 22));
        p.drawLine(QPointF(16, 6), QPointF(22, 6));
        p.drawLine(QPointF(22, 6), QPointF(22, 12));
        p.drawLine(QPointF(6, 16), QPointF(6, 22));
        p.drawLine(QPointF(6, 22), QPointF(12, 22));
    }
    p.end();
    return QIcon(pm);
}

/// VM 的开关样式（模板存图/全部屏蔽/输出掩膜）：灰底 → 橙底。
static QCheckBox *makeSwitch(QWidget *parent)
{
    auto *sw = new QCheckBox(parent);
    sw->setFixedSize(36, 18);
    sw->setStyleSheet(QStringLiteral(
        "QCheckBox::indicator { width:34px; height:18px; border-radius:9px; background:#C9C9C9; }"
        "QCheckBox::indicator:checked { background:#E76900; }"));
    return sw;
}

/// 把工具按钮变成 VM 工具栏那样的"图标 + 悬浮提示"形态（文字只留在 tooltip）。
static void makeToolButton(QPushButton *btn, const QString &glyph, const QString &name)
{
    btn->setText(QString());
    btn->setIcon(vmToolIcon(glyph));
    btn->setIconSize(QSize(20, 20));
    btn->setFixedSize(30, 26);
    btn->setFlat(true);
    btn->setToolTip(name);
    btn->setAccessibleName(name);
}

}   // namespace

TemplateConfigDialog::TemplateConfigDialog(OpencvTemplateMatchNode *node, int editIndex,
                                           QWidget *parent, bool startInMaskMode)
    : QDialog(parent)
    , m_node(node)
    , m_editIndex(editIndex)
{
    setWindowTitle(QStringLiteral("模板配置"));
    setStyleSheet(VisionWorkbenchStyle::moduleDialogStylesheet());
    resize(1100, 720);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(4);

    // ---- 顶栏：选择当前图像 / 选择其他图像 / 模板存图 ----
    auto *top = new QHBoxLayout();
    auto *useCurrentBtn = new QPushButton(QStringLiteral("选择当前图像"));
    auto *useOtherBtn = new QPushButton(QStringLiteral("选择其他图像"));
    auto *saveSwitch = makeSwitch(this);
    saveSwitch->setToolTip(QStringLiteral("开启后可指定模板图片保存位置；关闭则静默存到方案 templates/"));
    auto *saveFileBtn = new QPushButton(QStringLiteral("↑"));
    saveFileBtn->setFixedSize(28, 26);
    saveFileBtn->setToolTip(QStringLiteral("选择模板图片保存位置"));
    top->addWidget(useCurrentBtn);
    top->addWidget(useOtherBtn);
    top->addWidget(new QLabel(QStringLiteral("模板存图")));
    top->addWidget(saveSwitch);
    top->addWidget(saveFileBtn);
    top->addStretch();
    top->addWidget(new QLabel(QStringLiteral("全部屏蔽")));
    auto *blockSwitch = makeSwitch(this);
    top->addWidget(blockSwitch);
    root->addLayout(top);

    // VM 第二行：图标工具条 + 容差 + 缩放/适应/全屏
    auto *shapeRectBtn = new QPushButton();
    auto *shapePolyBtn = new QPushButton();
    auto *shapeCircleBtn = new QPushButton();
    auto *shapeFreeBtn = new QPushButton();
    auto *shapeWandBtn = new QPushButton();
    auto *shapeWandPlusBtn = new QPushButton();
    auto *clearShapeBtn = new QPushButton();
    auto *delShapeBtn = new QPushButton();
    auto *splitBtn = new QPushButton();
    auto *scissorsBtn = new QPushButton();
    auto *maskModeBtn = new QPushButton();
    maskModeBtn->setCheckable(true);
    auto *eraseBtn = new QPushButton();
    makeToolButton(shapeRectBtn, QStringLiteral("rect"), QStringLiteral("矩形"));
    makeToolButton(shapePolyBtn, QStringLiteral("polygon"), QStringLiteral("多边形"));
    makeToolButton(shapeCircleBtn, QStringLiteral("circle"), QStringLiteral("圆形"));
    makeToolButton(shapeFreeBtn, QStringLiteral("freehand"), QStringLiteral("自由绘制"));
    makeToolButton(shapeWandBtn, QStringLiteral("wand"), QStringLiteral("魔术棒"));
    makeToolButton(shapeWandPlusBtn, QStringLiteral("wandplus"), QStringLiteral("魔术棒+"));
    makeToolButton(clearShapeBtn, QStringLiteral("clear"), QStringLiteral("清除形状"));
    makeToolButton(delShapeBtn, QStringLiteral("delete"), QStringLiteral("删除选中"));
    makeToolButton(splitBtn, QStringLiteral("scissors"), QStringLiteral("分割"));
    makeToolButton(scissorsBtn, QStringLiteral("cut"), QStringLiteral("剪刀"));
    makeToolButton(maskModeBtn, QStringLiteral("brush"), QStringLiteral("屏蔽模式"));
    makeToolButton(eraseBtn, QStringLiteral("eraserdots"), QStringLiteral("擦除轮廓点"));
    maskModeBtn->setToolTip(QStringLiteral(
        "勾上后画/取到的形状进「屏蔽」列表：从保留区里挖掉（logo/水印/背景）。"
        "屏蔽=保留 − 屏蔽；不画保留区时默认整块保留再挖。"));

    m_wandTol = new QSpinBox();
    m_wandTol->setRange(1, 120);
    m_wandTol->setValue(30);
    m_wandTol->setPrefix(QStringLiteral("容差 "));

    auto *selectBtn = new QPushButton();
    auto *rotatedBtn = new QPushButton();
    makeToolButton(selectBtn, QStringLiteral("select"), QStringLiteral("选择"));
    makeToolButton(rotatedBtn, QStringLiteral("rotatedrect"), QStringLiteral("旋转矩形"));

    m_undoBtn = new QPushButton();
    m_redoBtn = new QPushButton();
    auto *zoomInBtn = new QPushButton();
    auto *zoomOutBtn = new QPushButton();
    auto *fitBtn = new QPushButton();
    auto *fullBtn = new QPushButton();
    makeToolButton(m_undoBtn, QStringLiteral("undo"), QStringLiteral("撤销"));
    makeToolButton(m_redoBtn, QStringLiteral("redo"), QStringLiteral("重做"));
    makeToolButton(zoomInBtn, QStringLiteral("zoomin"), QStringLiteral("放大"));
    makeToolButton(zoomOutBtn, QStringLiteral("zoomout"), QStringLiteral("缩小"));
    makeToolButton(fitBtn, QStringLiteral("fit"), QStringLiteral("图像自适应"));
    makeToolButton(fullBtn, QStringLiteral("fullscreen"), QStringLiteral("全屏显示"));
    m_undoBtn->setEnabled(false);
    m_redoBtn->setEnabled(false);
    connect(m_undoBtn, &QPushButton::clicked, this, &TemplateConfigDialog::onUndo);
    connect(m_redoBtn, &QPushButton::clicked, this, &TemplateConfigDialog::onRedo);

    auto *tools = new QHBoxLayout();
    tools->addWidget(selectBtn);
    tools->addWidget(rotatedBtn);
    for (QPushButton *b : {shapeRectBtn, shapePolyBtn, shapeCircleBtn, shapeFreeBtn,
                           shapeWandBtn, shapeWandPlusBtn}) {
        tools->addWidget(b);
    }
    tools->addWidget(m_wandTol);
    for (QPushButton *b : {clearShapeBtn, delShapeBtn, splitBtn, scissorsBtn,
                           maskModeBtn, eraseBtn, m_undoBtn, m_redoBtn}) {
        tools->addWidget(b);
    }
    tools->addStretch();
    for (QPushButton *b : {zoomInBtn, zoomOutBtn, fitBtn, fullBtn}) {
        tools->addWidget(b);
    }
    root->addLayout(tools);

    // ---- 左：图像区；右：配置参数 + 形状列表 ----
    auto *split = new QHBoxLayout();
    m_view = new HalconWindow(this);
    m_view->setMinimumWidth(640);
    split->addWidget(m_view, 1);

    auto *right = new QWidget(this);
    auto *rightLay = new QVBoxLayout(right);
    rightLay->setContentsMargins(6, 0, 0, 0);
    rightLay->setSpacing(4);

    // VM 模板配置窗没有单独的「模板名称」行：名称自动取模板序号/文件名。
    // 控件保留但隐藏，onConfirm 仍按它生成模板名。
    m_nameEdit = new QLineEdit(right);
    m_nameEdit->setVisible(false);

    // VM 模板配置窗右栏：列表在上（占满纵向），配置参数在下，匹配中心在底。
    rightLay->addWidget(new QLabel(QStringLiteral("列表")));
    m_list = new QListWidget();
    m_list->setMinimumHeight(160);
    rightLay->addWidget(m_list, 1);

    rightLay->addWidget(new QLabel(QStringLiteral("配置参数")));
    auto *form = new QVBoxLayout();
    form->setSpacing(3);
    auto makeRow = [&](const QString &label, QWidget *w) {
        auto *row = new QHBoxLayout();
        row->addWidget(new QLabel(label));
        row->addStretch();
        row->addWidget(w);
        form->addLayout(row);
    };
    m_coarseSpin = new QSpinBox();
    m_coarseSpin->setRange(1, 20);         // VM：速度尺度 1~20（金字塔顶层降采样倍率）
    m_coarseSpin->setValue(3);
    makeRow(QStringLiteral("速度尺度"), m_coarseSpin);
    m_apertureCombo = new QComboBox();
    for (int i = 1; i <= 20; ++i)          // VM：特征尺度 1~20，1 最精细
        m_apertureCombo->addItem(QString::number(i));
    m_apertureCombo->setCurrentText(QStringLiteral("3"));
    makeRow(QStringLiteral("特征尺度"), m_apertureCombo);
    m_autoChk = new QCheckBox(QStringLiteral("自动"));
    m_autoChk->setChecked(true);
    makeRow(QStringLiteral("阈值模式"), m_autoChk);
    m_edgeSpin = new QDoubleSpinBox();
    m_edgeSpin->setRange(1.0, 255.0);      // VM：对比度阈值 = 灰度差 1~255
    m_edgeSpin->setDecimals(0);
    m_edgeSpin->setValue(20.0);
    makeRow(QStringLiteral("对比度阈值"), m_edgeSpin);
    m_chainModeCombo = new QComboBox();
    m_chainModeCombo->addItems({QStringLiteral("自动"), QStringLiteral("手动")});
    makeRow(QStringLiteral("链长模式"), m_chainModeCombo);
    m_minChainSpin = new QSpinBox();
    m_minChainSpin->setRange(1, 200);
    m_minChainSpin->setValue(3);
    m_minChainSpin->setEnabled(false);   // 链长模式=自动时不可改（VM 同款）
    makeRow(QStringLiteral("最小链长"), m_minChainSpin);
    connect(m_chainModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) { m_minChainSpin->setEnabled(index == 1); });
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

    split->addWidget(right, 0);
    right->setMinimumWidth(320);
    root->addLayout(split, 1);

    m_hint = new QLabel();
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet(QStringLiteral("color:#666;"));
    root->addWidget(m_hint);

    // VM 模板配置窗底部只有「确定」；放弃编辑直接关右上角 X。
    auto *bottom = new QHBoxLayout();
    bottom->addStretch();
    auto *cancelBtn = new QPushButton(QStringLiteral("取消"), this);
    cancelBtn->setVisible(false);   // 保留对象给既有调用查找，不进入 VM 布局
    auto *okBtn = new QPushButton(QStringLiteral("确定"));
    okBtn->setDefault(true);
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
    connect(saveFileBtn, &QPushButton::clicked, this, &TemplateConfigDialog::onPickSavePath);
    connect(saveSwitch, &QCheckBox::toggled, this, [this](bool on) {
        if (on)
            onPickSavePath();
    });
    connect(zoomInBtn, &QPushButton::clicked, this, [this]() { m_view->zoomIn(); });
    connect(zoomOutBtn, &QPushButton::clicked, this, [this]() { m_view->zoomOut(); });
    connect(fitBtn, &QPushButton::clicked, this, [this]() { m_view->fitToWindow(); });
    connect(fullBtn, &QPushButton::clicked, this, [this]() {
        if (isFullScreen())
            showNormal();
        else
            showFullScreen();
    });
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
    connect(eraseBtn, &QPushButton::clicked, this, [this]() {
        m_eraseMode = true;
        m_anchorPickMode = false;
        m_wandMode = false;
        m_cutMode = false;
        m_view->setRoiFreehand(false);
        m_view->setRoiEditable(false);
        m_view->setCursor(Qt::PointingHandCursor);
        m_hint->setText(QStringLiteral(
            "在图上单击擦掉该区域的特征点（半径 20px）；擦掉的点不参与匹配。"));
    });
    connect(selectBtn, &QPushButton::clicked, this, [this, maskModeBtn]() {
        m_wandMode = false;
        m_cutMode = false;
        m_eraseMode = false;
        m_anchorPickMode = false;
        m_maskMode = false;
        if (maskModeBtn)
            maskModeBtn->setChecked(false);
        m_view->setMaskEditable(false);
        m_view->setRoiFreehand(false);
        m_view->setRoiEditable(true,
                              m_shape.type == RoiType::None ? RoiType::RotatedRect : m_shape.type);
        m_hint->setText(QStringLiteral("选择模式：可拖动/缩放已有形状。"));
    });
    connect(rotatedBtn, &QPushButton::clicked, this, &TemplateConfigDialog::onPickRect);
    connect(blockSwitch, &QCheckBox::toggled, this, [this](bool on) {
        if (on) {
            // VM「全部屏蔽」开关：先暂存当前形状，再清空画面进入刷绘模式
            m_blockedShapes = m_shapes;
            m_blockedExcludes = m_excludes;
            m_blockAllActive = true;
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
        } else if (m_blockAllActive) {
            m_shapes = m_blockedShapes;
            m_excludes = m_blockedExcludes;
            m_blockedShapes.clear();
            m_blockedExcludes.clear();
            m_blockAllActive = false;
            m_shape = m_shapes.isEmpty() ? RoiShape() : m_shapes.first();
            refreshShapeList();
            refreshFeaturePreview();
            m_hint->setText(QStringLiteral("已恢复屏蔽前的形状。"));
        }
        // 屏蔽前后形状集整体换了一份，旧撤销栈不再对应，直接作废避免"撤销到半套形状"。
        m_undoStates.clear();
        m_redoStates.clear();
        updateHistoryButtons();
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
        if (row < m_shapes.size() || row - m_shapes.size() < m_excludes.size())
            pushShapeHistory();
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
    connect(m_view, &HalconWindow::roiHandleEdited, this, [this](const RoiShape &shape) {
        if (shape.type == RoiType::None)
            return;
        pushShapeHistory();
        updateShapeFromHandle(shape);
    });
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
            m_erases = e.erases;
            m_shapes = e.allShapes();
            m_excludes = e.excludes;
            m_shape = m_shapes.isEmpty() ? RoiShape() : m_shapes.first();
            m_savePath = e.imagePath;
            m_edgeSpin->setValue(e.contrastThreshold);
            m_autoChk->setChecked(e.autoThreshold);
            m_apertureCombo->setCurrentText(QString::number(qBound(1, e.featureScale, 20)));
            m_coarseSpin->setValue(qBound(1, e.speedScale, 20));
            m_chainModeCombo->setCurrentIndex(e.chainModeAuto ? 0 : 1);
            m_minChainSpin->setValue(qBound(1, e.minChainLength, 200));
            m_minChainSpin->setEnabled(!e.chainModeAuto);
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
    if (startInMaskMode) {
        // 从模块窗「屏蔽区」/「屏蔽区创建=绘制」进来：直接切到屏蔽模式，
        // 画出的形状进屏蔽列表（挖掉，不参与匹配），而不是模板保留区。
        m_maskMode = true;
        maskModeBtn->setChecked(true);
        m_hint->setText(QStringLiteral(
            "屏蔽模式：画出的形状会从模板里挖掉（不参与匹配）；可继续用魔术棒/橡皮微调。"));
    } else {
        m_hint->setText(QStringLiteral(
            "在图上拖出模板区域（矩形/多边形），右侧改参数；图上绿点 = 该模板参与打分的特征点。"));
    }
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
    pushShapeHistory();
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
    // 擦除轮廓点：图上单击擦掉该区域特征点（VM「擦除轮廓点」）
    if (m_eraseMode) {
        m_eraseMode = false;
        m_view->setCursor(Qt::ArrowCursor);
        pushShapeHistory();
        RoiShape e;
        e.type = RoiType::Circle;
        e.p1 = imagePos;
        e.p2 = QPointF(imagePos.x() + 20.0, imagePos.y());   // 半径 20px
        m_erases.append(e);
        refreshFeaturePreview();
        m_hint->setText(QStringLiteral("已擦除 (%1, %2) 附近特征点（共 %3 个擦除区）")
                            .arg(imagePos.x(), 0, 'f', 1)
                            .arg(imagePos.y(), 0, 'f', 1)
                            .arg(m_erases.size()));
        return;
    }
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
        pushShapeHistory();
        m_excludes = applyWand(m_excludes, m_trainGray, seed, m_wandTol->value(), m_wandAppend);
    } else {
        pushShapeHistory();
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
    if (!m_shapes.isEmpty() || !m_excludes.isEmpty())
        pushShapeHistory();
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
    pushShapeHistory();
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

void TemplateConfigDialog::pushShapeHistory()
{
    if (m_blockAllActive)
        return;   // 「全部屏蔽」是临时显示态，不参与撤销
    if (m_undoStates.size() >= 30)
        m_undoStates.removeFirst();
    m_undoStates.append(ShapeState{m_shapes, m_excludes, m_erases, m_shape});
    m_redoStates.clear();
    updateHistoryButtons();
}

void TemplateConfigDialog::applyShapeHistory(const ShapeState &state)
{
    m_shapes = state.shapes;
    m_excludes = state.excludes;
    m_erases = state.erases;
    m_shape = state.shape;
    m_view->setRoiEditable(false);
    m_view->clearRoi();
    if (m_shape.type != RoiType::None) {
        m_view->setRoiShape(m_shape);
        m_view->setRoiHandleEditEnabled(true);
    }
    refreshShapeList();
    syncAutoParamsToUi();
    refreshFeaturePreview();
    updateHistoryButtons();
}

void TemplateConfigDialog::updateHistoryButtons()
{
    if (m_undoBtn)
        m_undoBtn->setEnabled(!m_blockAllActive && !m_undoStates.isEmpty());
    if (m_redoBtn)
        m_redoBtn->setEnabled(!m_blockAllActive && !m_redoStates.isEmpty());
}

void TemplateConfigDialog::updateShapeFromHandle(const RoiShape &shape)
{
    const int row = m_list->currentRow();
    if (row >= 0 && row < m_shapes.size()) {
        m_shapes[row] = shape;
    } else if (row >= m_shapes.size() && row - m_shapes.size() < m_excludes.size()) {
        m_excludes[row - m_shapes.size()] = shape;
    } else if (!m_shapes.isEmpty()) {
        m_shapes[0] = shape;
    } else if (!m_excludes.isEmpty()) {
        m_excludes[0] = shape;
    } else {
        return;
    }
    m_shape = m_shapes.isEmpty() ? RoiShape() : m_shapes.first();
    refreshShapeList();
    refreshFeaturePreview();
    m_hint->setText(QStringLiteral("已更新形状的位置/大小（不会新增副本）。"));
}

void TemplateConfigDialog::onUndo()
{
    if (m_blockAllActive || m_undoStates.isEmpty())
        return;
    m_redoStates.append(ShapeState{m_shapes, m_excludes, m_erases, m_shape});
    applyShapeHistory(m_undoStates.takeLast());
    m_hint->setText(QStringLiteral("已撤销上一步。"));
}

void TemplateConfigDialog::onRedo()
{
    if (m_blockAllActive || m_redoStates.isEmpty())
        return;
    m_undoStates.append(ShapeState{m_shapes, m_excludes, m_erases, m_shape});
    applyShapeHistory(m_redoStates.takeLast());
    m_hint->setText(QStringLiteral("已恢复到撤销前的状态。"));
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
    // 预览必须用当前「特征尺度」和自动链长，否则界面上永远是 1/1 的最密点集
    // （现场观感：暗部纹理密密麻麻，和 VM 的轮廓点集完全不同）。
    const int featureScale = qMax(1, m_apertureCombo->currentText().toInt());
    const int minChain = qMax(2, featureScale * 2);
    m_view->setFeaturePoints(
        OpencvTemplateMatchNode::featurePointsForPatch(patch, rect, threshold, aperture, mask,
                                                       featureScale, minChain, m_erases));
}

bool TemplateConfigDialog::ensureSavePath(QString *path)
{
    // 用户显式点过「模板存图…」：用它给的路径。
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

    // VM 的「确定」不会再弹保存对话框：模板属于方案，内部静默落盘到方案目录的
    // templates/ 下（「模板存图」只决定是否额外保留一张可浏览的模板图）。
    QString base = ProjectPaths::schemeBaseDir(ProjectPaths::currentSchemeFilePath());
    QDir dir(base + QStringLiteral("/templates"));
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        // 方案未落盘 / 程序目录不可写：退回用户临时目录，仍不打断创建流程
        dir = QDir(QDir::tempPath() + QStringLiteral("/VisionFlow_templates"));
        if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
            QMessageBox::warning(this, QStringLiteral("模板配置"),
                                 QStringLiteral("模板目录创建失败：%1").arg(dir.absolutePath()));
            return false;
        }
    }
    QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty())
        name = QStringLiteral("template");
    name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_\\-\\x4e00-\\x9fa5]")),
                 QStringLiteral("_"));
    const int moduleId = m_node ? m_node->moduleId() : 0;
    QString file = dir.filePath(QStringLiteral("%1_%2.png").arg(moduleId).arg(name));
    int suffix = 2;
    while (QFileInfo::exists(file)) {
        file = dir.filePath(QStringLiteral("%1_%2_%3.png").arg(moduleId).arg(name).arg(suffix));
        ++suffix;
        if (suffix > 99)
            break;
    }
    m_savePath = file;
    *path = file;
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
    entry.chainModeAuto = (m_chainModeCombo->currentIndex() == 0);
    entry.minChainLength = m_minChainSpin->value();
    entry.anchorDx = m_anchorX->value();
    entry.anchorDy = m_anchorY->value();
    entry.anchorLocked = m_anchorLock->isChecked();
    entry.erases = m_erases;

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
    m_node->setParam(QStringLiteral("autoContrastThreshold"), entry.autoThreshold);
    m_node->setParam(QStringLiteral("featureScale"), entry.featureScale);
    m_node->setParam(QStringLiteral("speedScale"), entry.speedScale);
    m_node->setParam(QStringLiteral("minChainLength"), entry.minChainLength);
    m_node->setParam(QStringLiteral("chainMode"), entry.chainModeAuto);

    // VM 的「确定」直接关闭窗口，不再弹成功提示（结果在特征模板页的列表里可见）。
    m_hint->setText(QStringLiteral("已创建模板：%1").arg(entry.name));
    accept();
}
