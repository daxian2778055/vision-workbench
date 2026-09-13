#include "VisionWorkbenchStyle.h"
#include <QApplication>
#include <QPalette>
#include <QGraphicsView>
#include <QPainter>
#include <QFrame>
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>

void VisionWorkbenchStyle::applyFusionDarkPalette(QApplication *app)
{
    if (!app)
        return;
    app->setStyle(QStringLiteral("Fusion"));

    QPalette p;
    p.setColor(QPalette::Window, QColor(0x30, 0x30, 0x34));
    p.setColor(QPalette::WindowText, QColor(0xe8, 0xe8, 0xec));
    p.setColor(QPalette::Base, QColor(0x25, 0x25, 0x28));
    p.setColor(QPalette::AlternateBase, QColor(0x2e, 0x2e, 0x33));
    p.setColor(QPalette::ToolTipBase, QColor(0x2d, 0x2d, 0x32));
    p.setColor(QPalette::ToolTipText, QColor(0xee, 0xee, 0xf0));
    p.setColor(QPalette::Text, QColor(0xe8, 0xe8, 0xec));
    p.setColor(QPalette::Button, QColor(0x3e, 0x3e, 0x45));
    p.setColor(QPalette::ButtonText, QColor(0xee, 0xee, 0xf0));
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Link, QColor(0x40, 0xa9, 0xff));
    p.setColor(QPalette::Highlight, QColor(0x09, 0x52, 0x7a));
    p.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    p.setColor(QPalette::PlaceholderText, QColor(0x88, 0x88, 0x90));
    app->setPalette(p);
}

QString VisionWorkbenchStyle::globalWidgetsStylesheet()
{
    // 统一暗色主题，覆盖所有常见控件
    return QStringLiteral(
        "QMainWindow { background: #303034; color: #e8e8ec; }"
        "QMenuBar { background: #2d2d32; color: #e8e8ec; border-bottom: 1px solid #1a1a1f; spacing: 4px; }"
        "QMenuBar::item:selected { background: #405060; }"
        "QMenu { background: #2d2d32; color: #e8e8ec; border: 1px solid #505058; padding: 4px; }"
        "QMenu::item:selected { background: #305070; }"
        "QSplitter::handle { background: #38383f; }"
        "QSplitter::handle:horizontal { width: 6px; }"
        "QSplitter::handle:vertical { height: 6px; }"
        "QTabWidget::pane { border: 1px solid #3c3c42; background: #252528; margin-top: -1px; }"
        "QTabBar::tab { background: #2d2d32; color: #c8c8d0; padding: 8px 16px;"
        "  border: 1px solid #3c3c42; margin-right: 2px; }"
        "QTabBar::tab:selected { background: #1e4a62; color: #ffffff; border-bottom-color: #1e4a62; }"
        "QTabBar::tab:hover { background: #3a3a42; }"
        "QTreeWidget { background: #252528; color: #d8d8e0;"
        "  border: 1px solid #3c3c42; alternate-background-color: #2a2a2f; outline: none; }"
        "QTreeWidget::item:hover { background: #3a4860; }"
        "QTreeWidget::item:selected { background: #275070; color: #fff; }"
        "QHeaderView::section { background: #34343a; color: #c0c0c8;"
        "  padding: 4px; border: none; border-bottom: 1px solid #484850; font-weight: 600; }"
        "QToolBar { background: #34343c; border: none; spacing: 4px; padding: 4px; }"
        "QToolBar QToolButton { background: transparent; padding: 4px 10px;"
        "  border-radius: 3px; color: #e8e8ec; }"
        "QToolBar QToolButton:hover { background: #404050; }"
        "QToolBar QToolButton:pressed { background: #305070; }"

        // 日志框
        "QPlainTextEdit#logTextEdit { background: #1a1a1f; color: #b8b8c4;"
        "  font-family: \"Consolas\", \"Courier New\", monospace;"
        "  border: 1px solid #3c3c42; selection-background-color: #305070; }"

        // 状态栏
        "QStatusBar { background: #34343c; color: #aaaab0; border-top: 1px solid #1a1a1f; }"

        // ===== 参数面板公共控件 =====
        "QGroupBox { background: #2a2a30; border: 1px solid #3c3c42; border-radius: 6px;"
        "  margin-top: 14px; padding: 16px 10px 10px 10px; font-weight: 500; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left;"
        "  padding: 2px 8px; color: #c0c8d8; background: transparent; }"

        "QLabel { color: #c8c8d0; background: transparent; }"

        "QLineEdit { background: #252528; color: #e8e8ec; border: 1px solid #4a4a52;"
        "  border-radius: 4px; padding: 4px 8px; min-height: 22px;"
        "  selection-background-color: #305070; }"
        "QLineEdit:focus { border-color: #40a9ff; }"
        "QLineEdit:disabled { background: #2a2a30; color: #808088; }"

        "QComboBox { background: #252528; color: #e8e8ec; border: 1px solid #4a4a52;"
        "  border-radius: 4px; padding: 4px 8px; min-height: 22px; }"
        "QComboBox:focus { border-color: #40a9ff; }"
        "QComboBox:disabled { background: #2a2a30; color: #808088; }"
        "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right;"
        "  width: 20px; border-left: 1px solid #4a4a52; background: #3a3a42;"
        "  border-top-right-radius: 4px; border-bottom-right-radius: 4px; }"
        "QComboBox::down-arrow { width: 10px; height: 10px; }"
        "QComboBox QAbstractItemView { background: #2d2d32; color: #e8e8ec;"
        "  border: 1px solid #505058; selection-background-color: #305070; outline: none; }"

        "QSpinBox, QDoubleSpinBox { background: #252528; color: #e8e8ec;"
        "  border: 1px solid #4a4a52; border-radius: 4px; padding: 4px 8px;"
        "  min-height: 22px; }"
        "QSpinBox:focus, QDoubleSpinBox:focus { border-color: #40a9ff; }"
        "QSpinBox::up-button, QDoubleSpinBox::up-button {"
        "  subcontrol-origin: border; subcontrol-position: top right;"
        "  width: 18px; background: #3a3a42; border-left: 1px solid #4a4a52;"
        "  border-top-right-radius: 4px; }"
        "QSpinBox::down-button, QDoubleSpinBox::down-button {"
        "  subcontrol-origin: border; subcontrol-position: bottom right;"
        "  width: 18px; background: #3a3a42; border-left: 1px solid #4a4a52;"
        "  border-bottom-right-radius: 4px; }"

        "QCheckBox { color: #c8c8d0; spacing: 6px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border-radius: 3px;"
        "  border: 1px solid #5a5a62; background: #252528; }"
        "QCheckBox::indicator:checked { background: #1e4a62; border-color: #40a9ff; }"
        "QCheckBox::indicator:hover { border-color: #40a9ff; }"

        "QRadioButton { color: #c8c8d0; spacing: 6px; }"
        "QRadioButton::indicator { width: 16px; height: 16px; border-radius: 8px;"
        "  border: 1px solid #5a5a62; background: #252528; }"
        "QRadioButton::indicator:checked { background: #1e4a62; border-color: #40a9ff; }"
        "QRadioButton::indicator:hover { border-color: #40a9ff; }"

        "QPushButton { background: #3e3e45; color: #e8e8ec;"
        "  border: 1px solid #55555d; border-radius: 4px; padding: 6px 14px;"
        "  min-height: 24px; font-weight: 500; }"
        "QPushButton:hover { background: #4a4a55; border-color: #66666e; }"
        "QPushButton:pressed { background: #305070; }"
        "QPushButton:disabled { background: #2a2a30; color: #707078; }"

        // 滚动条
        "QScrollBar:vertical { background: #2a2a30; width: 10px;"
        "  margin: 0; border: none; }"
        "QScrollBar::handle:vertical { background: #4a4a54; min-height: 30px;"
        "  border-radius: 5px; margin: 2px; }"
        "QScrollBar::handle:vertical:hover { background: #5a5a64; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar:horizontal { background: #2a2a30; height: 10px;"
        "  margin: 0; border: none; }"
        "QScrollBar::handle:horizontal { background: #4a4a54; min-width: 30px;"
        "  border-radius: 5px; margin: 2px; }"
        "QScrollBar::handle:horizontal:hover { background: #5a5a64; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
        );
}

QString VisionWorkbenchStyle::imageSourceStripStylesheet()
{
    return QStringLiteral(
        "QLabel { font-size: 11px; color: #a8a8b4; background-color: #2a2a30;"
        "  padding: 4px 8px; border-top: 1px solid #404048; max-height: 22px; }"
        );
}

void VisionWorkbenchStyle::applyGraphicsViewWorkbenchDefaults(QGraphicsView *view)
{
    if (!view)
        return;
    view->setRenderHint(QPainter::Antialiasing, true);
    view->setRenderHint(QPainter::SmoothPixmapTransform, true);
    view->setRenderHint(QPainter::TextAntialiasing, true);
    view->setFrameShape(QFrame::NoFrame);
    /// 必须启用，否则工具库拖放的 mime 无法传递到 QGraphicsScene::dropEvent（用户拖算子无反应）
    view->setAcceptDrops(true);
}

// ===== 参数面板样式辅助 =====

QString VisionWorkbenchStyle::paramInputStyle()
{
    return QStringLiteral(
        "QLineEdit { background: #252528; color: #e8e8ec; border: 1px solid #4a4a52;"
        "  border-radius: 4px; padding: 4px 8px; min-height: 22px;"
        "  selection-background-color: #305070; }"
        "QLineEdit:focus { border-color: #40a9ff; }"
        );
}

QString VisionWorkbenchStyle::paramInputDirtyStyle()
{
    return QStringLiteral(
        "QLineEdit { background: #3a2020; color: #f0d0d0; border: 1px solid #e05050;"
        "  border-radius: 4px; padding: 4px 8px; min-height: 22px;"
        "  selection-background-color: #305070; }"
        "QLineEdit:focus { border-color: #ff7070; }"
        );
}

QString VisionWorkbenchStyle::paramInputReadonlyStyle()
{
    return QStringLiteral(
        "QLineEdit { background: #2a2a30; color: #909098; border: 1px solid #3c3c42;"
        "  border-radius: 4px; padding: 4px 8px; min-height: 22px; }"
        );
}

QString VisionWorkbenchStyle::groupBoxTitleStyle()
{
    return QStringLiteral(
        "QGroupBox { background: #2a2a30; border: 1px solid #3c3c42; border-radius: 6px;"
        "  margin-top: 14px; padding: 16px 10px 10px 10px; font-weight: 500; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left;"
        "  padding: 2px 8px; color: #c0c8d8; }"
        );
}

QString VisionWorkbenchStyle::buttonSuccessStyle()
{
    return QStringLiteral(
        "QPushButton { background-color: #3a8a40; color: white; font-weight: bold;"
        "  border: 1px solid #4caf50; border-radius: 4px; padding: 6px 14px;"
        "  min-height: 24px; }"
        "QPushButton:hover { background-color: #4caf50; }"
        "QPushButton:pressed { background-color: #2e7d32; }"
        "QPushButton:disabled { background-color: #2a3a2c; color: #808880; }"
        );
}

QString VisionWorkbenchStyle::buttonPrimaryStyle()
{
    return QStringLiteral(
        "QPushButton { background-color: #2a6070; color: white; font-weight: bold;"
        "  border: 1px solid #40a9ff; border-radius: 4px; padding: 6px 14px;"
        "  min-height: 24px; }"
        "QPushButton:hover { background-color: #307080; }"
        "QPushButton:pressed { background-color: #1e4a62; }"
        );
}

QString VisionWorkbenchStyle::buttonDangerStyle()
{
    return QStringLiteral(
        "QPushButton { background-color: #6a3030; color: #f0d0d0;"
        "  border: 1px solid #d05050; border-radius: 4px; padding: 6px 14px;"
        "  min-height: 24px; }"
        "QPushButton:hover { background-color: #8a4040; }"
        "QPushButton:pressed { background-color: #5a2020; }"
        );
}

QString VisionWorkbenchStyle::buttonToolStyle()
{
    return QStringLiteral(
        "QPushButton { background: #3a3a42; color: #c8c8d0;"
        "  border: 1px solid #55555d; border-radius: 4px; padding: 4px 12px;"
        "  min-height: 22px; font-size: 12px; }"
        "QPushButton:hover { background: #4a4a54; }"
        "QPushButton:pressed { background: #305070; }"
        );
}

QString VisionWorkbenchStyle::labelTitleStyle()
{
    return QStringLiteral(
        "QLabel { font-size: 13px; font-weight: bold; color: #d0d8e0; padding: 4px 0; }"
        );
}

QString VisionWorkbenchStyle::labelHintStyle()
{
    return QStringLiteral(
        "QLabel { font-size: 11px; color: #909098; padding: 2px 0; }"
        );
}

void VisionWorkbenchStyle::markInputDirty(QLineEdit *edit, bool dirty)
{
    if (!edit) return;
    edit->setStyleSheet(dirty ? paramInputDirtyStyle() : paramInputStyle());
}

void VisionWorkbenchStyle::markComboDirty(QComboBox *combo, bool dirty)
{
    if (!combo) return;
    if (dirty) {
        combo->setStyleSheet(
            "QComboBox { background: #3a2020; color: #f0d0d0; border: 1px solid #e05050;"
            "  border-radius: 4px; padding: 4px 8px; min-height: 22px; }"
            "QComboBox:focus { border-color: #ff7070; }"
            "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right;"
            "  width: 20px; border-left: 1px solid #e05050; background: #4a2a2a;"
            "  border-top-right-radius: 4px; border-bottom-right-radius: 4px; }"
            "QComboBox QAbstractItemView { background: #2d2d32; color: #e8e8ec;"
            "  border: 1px solid #505058; selection-background-color: #305070; outline: none; }"
            );
    } else {
        combo->setStyleSheet(QString());
    }
}

void VisionWorkbenchStyle::markSpinDirty(QDoubleSpinBox *spin, bool dirty)
{
    if (!spin) return;
    if (dirty) {
        spin->setStyleSheet(
            "QDoubleSpinBox { background: #3a2020; color: #f0d0d0; border: 1px solid #e05050;"
            "  border-radius: 4px; padding: 4px 8px; min-height: 22px; }"
            "QDoubleSpinBox:focus { border-color: #ff7070; }"
            );
    } else {
        spin->setStyleSheet(QString());
    }
}

void VisionWorkbenchStyle::markIntSpinDirty(QSpinBox *spin, bool dirty)
{
    if (!spin) return;
    if (dirty) {
        spin->setStyleSheet(
            "QSpinBox { background: #3a2020; color: #f0d0d0; border: 1px solid #e05050;"
            "  border-radius: 4px; padding: 4px 8px; min-height: 22px; }"
            "QSpinBox:focus { border-color: #ff7070; }"
            );
    } else {
        spin->setStyleSheet(QString());
    }
}
