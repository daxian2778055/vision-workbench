#pragma once

#include <QString>

class QApplication;
class QGraphicsView;
class QLineEdit;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;

/// 参考常见工业视觉方案软件（VisionMaster 类 IDE）的视觉与排版习惯：深色工作台、画布网格、高对比连线
namespace VisionWorkbenchStyle {

void applyFusionDarkPalette(QApplication *app);

/// 控件级样式（分割条、日志、选项卡等）
QString globalWidgetsStylesheet();

/// 画布下方「图像来源」信息条样式
QString imageSourceStripStylesheet();

void applyGraphicsViewWorkbenchDefaults(QGraphicsView *view);

/// 画布背景主色（与 FlowScene::drawBackground 一致）
constexpr unsigned canvasBackgroundRgb = 0x1e1e21;

// ===== 参数面板样式辅助 =====

/// 参数输入框默认样式（暗色主题）
QString paramInputStyle();

/// 参数输入框「已修改未应用」样式（红色边框+浅红背景）
QString paramInputDirtyStyle();

/// 参数输入框「只读」样式
QString paramInputReadonlyStyle();

/// 通用 QGroupBox 标题样式
QString groupBoxTitleStyle();

/// 成功（绿色）按钮样式
QString buttonSuccessStyle();

/// 主要按钮样式（蓝色）
QString buttonPrimaryStyle();

/// 危险（删除/关闭）按钮样式
QString buttonDangerStyle();

/// 工具按钮（刷新等）样式
QString buttonToolStyle();

/// 标签标题样式（用于参数面板中的分组标题）
QString labelTitleStyle();

/// 标签副标题/提示文字样式
QString labelHintStyle();

// ===== 快捷函数 =====

/// 将 QLineEdit 标记为「已修改未应用」状态
void markInputDirty(QLineEdit *edit, bool dirty);

/// 将 QComboBox 标记为「已修改未应用」状态
void markComboDirty(QComboBox *combo, bool dirty);

/// 将 QDoubleSpinBox / QSpinBox 标记为「已修改未应用」状态
void markSpinDirty(QDoubleSpinBox *spin, bool dirty);
void markIntSpinDirty(QSpinBox *spin, bool dirty);

} // namespace VisionWorkbenchStyle
