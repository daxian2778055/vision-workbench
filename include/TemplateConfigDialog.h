#pragma once

// windows.h 的 min/max 宏会吃掉 std::min/max（HalconCpp 会带进 windows.h），
// 必须在包含 opencv 之前关掉，否则 imgproc.hpp 的模板代码直接编译不过。
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <QDialog>
#include <QString>
#include <opencv2/core.hpp>

#include "OpencvTemplateMatchNode.h"

class HalconWindow;
class QLineEdit;
class QDoubleSpinBox;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QListWidget;
class QLabel;

/// 「模板配置」窗（对齐 VisionMaster 4.4 的 创建/编辑模板 流程）：
///   左侧图像区直接框形状（矩形/多边形）→ 右侧配置参数（对比度阈值/阈值模式/特征尺度/速度尺度）
///   → 图上实时显示该模板的特征点（绿）→ 确定后写入节点的模板列表。
/// 与工具窗的分工：工具窗管"用哪个模板匹配"，这里管"模板怎么建"。
class TemplateConfigDialog : public QDialog
{
    Q_OBJECT
public:
    /// editIndex >= 0 表示编辑已有模板（确定时原地更新），否则新增一条。
    explicit TemplateConfigDialog(OpencvTemplateMatchNode *node, int editIndex = -1,
                                  QWidget *parent = nullptr);

private slots:
    void onPickRect();
    void onPickPolygon();
    void onPickCircle();
    void onClearShape();
    void onRoiEdited(const RoiShape &shape);
    void onPickOtherImage();
    void onPickSavePath();
    void onConfirm();

private:
    void refreshShapeList();
    void refreshFeaturePreview();
    void syncAutoParamsToUi();
    bool ensureSavePath(QString *path);
    cv::Mat cropTemplate() const;

    OpencvTemplateMatchNode *m_node = nullptr;
    int m_editIndex = -1;
    HalconWindow *m_view = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QDoubleSpinBox *m_edgeSpin = nullptr;
    QCheckBox *m_autoChk = nullptr;
    QComboBox *m_apertureCombo = nullptr;
    QSpinBox *m_coarseSpin = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_hint = nullptr;

    RoiShape m_shape;
    cv::Mat m_trainGray;      ///< 训练用灰度图（当前输入图或"选择其他图像"）
    QString m_trainLabel;     ///< 图上角标：输入源名/文件名
    QString m_savePath;       ///< 模板存图目标（空 = 确定时再问）
};
