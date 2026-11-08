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
class QPushButton;

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
                                  QWidget *parent = nullptr,
                                  bool startInMaskMode = false);

    /// 魔术棒取形（纯函数，便于无界面测试）：以 seed 为种子按容差泛洪，
    /// 取最大外轮廓并做多边形逼近；返回图像坐标顶点（<3 点表示失败）。
    static QVector<QPointF> wandPolygon(const cv::Mat &gray, const QPoint &seed, int tolerance);

    /// 形状布尔编辑（纯函数，便于无界面测试）：
    /// 把一块形状按连通域拆成多个多边形；hasCut=true 时先沿 cutA→cutB 擦掉一条 3px 带（剪刀）。
    static QVector<RoiShape> splitShape(const RoiShape &shape, const QPointF &cutA = QPointF(),
                                        const QPointF &cutB = QPointF(), bool hasCut = false);

    /// 魔术棒取形并合并到当前形状列表：append=true 追加（魔术棒+），false 替换（魔术棒）。
    /// 取不到区域时原样返回（不允许把已有形状误清空）。
    static QVector<RoiShape> applyWand(const QVector<RoiShape> &current, const cv::Mat &gray,
                                       const QPoint &seed, int tolerance, bool append);

private slots:
    void onPickRect();
    void onPickPolygon();
    void onPickCircle();
    void onPickFreehand();
    void onPickWand();
    /// 分割：把选中形状按连通域拆成多块（未选中 = 全部拆）
    void onSplitShape();
    /// 剪刀：画一条线，沿它把形状切开
    void onScissors();
    /// 魔术棒：在图上点一下，按点击点做泛洪取形（容差可调）
    void onImageClicked(const QPointF &imagePos);
    void onClearShape();
    void onRoiEdited(const RoiShape &shape);
    void onUndo();
    void onRedo();
    void onPickOtherImage();
    void onPickSavePath();
    void onConfirm();

private:
    void refreshShapeList();
    void refreshFeaturePreview();
    void syncAutoParamsToUi();
    /// 建模板时按模板梯度/尺寸给一组建议参数（只在第一块形状落下时填一次，不跟用户抢）
    void applySuggestedParams();
    bool ensureSavePath(QString *path);
    cv::Mat cropTemplate() const;

    /// 撤销/重做：形状、屏蔽、擦除点的整体快照（VM 工具条的撤销/重做按钮）
    struct ShapeState {
        QVector<RoiShape> shapes;
        QVector<RoiShape> excludes;
        QVector<RoiShape> erases;
        RoiShape shape;
        QVector<bool> enhanced;   ///< 与 shapes 同序：VM「增强匹配」勾选
    };
    void pushShapeHistory();
    void applyShapeHistory(const ShapeState &state);
    void updateHistoryButtons();
    /// 拖拽已有形状的手柄：更新列表里对应的那一块，而不是新增一块
    void updateShapeFromHandle(const RoiShape &shape);

    OpencvTemplateMatchNode *m_node = nullptr;
    int m_editIndex = -1;
    HalconWindow *m_view = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QDoubleSpinBox *m_edgeSpin = nullptr;
    QCheckBox *m_autoChk = nullptr;
    QComboBox *m_apertureCombo = nullptr;
    QComboBox *m_scaleModeCombo = nullptr;   /// VM「尺度模式」自动/手动（只作用于速度尺度）
    QSpinBox *m_coarseSpin = nullptr;
    QComboBox *m_chainModeCombo = nullptr;   /// VM「链长模式」自动/手动
    QSpinBox *m_minChainSpin = nullptr;      /// VM「最小链长」
    QListWidget *m_list = nullptr;
    QLabel *m_hint = nullptr;
    QSpinBox *m_wandTol = nullptr;
    QDoubleSpinBox *m_anchorX = nullptr;
    QDoubleSpinBox *m_anchorY = nullptr;
    QCheckBox *m_anchorLock = nullptr;
    bool m_wandMode = false;
    bool m_cutMode = false;        ///< 剪刀：等用户画一条线
    bool m_wandAppend = false;     ///< 魔术棒+：取到的区域追加到现有形状
    bool m_anchorPickMode = false; ///< 匹配中心取点：图上单击设锚点
    bool m_eraseMode = false;      ///< 擦除轮廓点：图上单击擦掉该区域特征点
    int m_eraseRadius = 20;        ///< 橡皮擦半径（VM：工具按钮右下角三角弹出滑块可调）
    QVector<RoiShape> m_erases;    ///< 擦除区域（圆）

    RoiShape m_shape;              ///< 主形状（=m_shapes.first()，老逻辑兼容）
    QVector<RoiShape> m_shapes;    ///< 保留形状（一个模板可由多块组成）
    QVector<bool> m_shapeEnhanced; ///< 与 m_shapes 同序：VM「增强匹配」（仅高精度匹配）
    QVector<RoiShape> m_excludes;  ///< 屏蔽形状（保留区里挖掉的 logo/水印/背景）
    bool m_maskMode = false;       ///< 屏蔽模式：新画的形状进 m_excludes
    /// 「全部屏蔽」开关：开启时临时清空形状/匹配中心，关闭时恢复（VM 同款开关语义）
    QVector<RoiShape> m_blockedShapes;
    QVector<RoiShape> m_blockedExcludes;
    QVector<bool> m_blockedShapeEnhanced;
    bool m_blockAllActive = false;
    cv::Mat m_trainGray;      ///< 训练用灰度图（当前输入图或"选择其他图像"）
    QString m_trainLabel;     ///< 图上角标：输入源名/文件名
    QString m_savePath;       ///< 模板存图目标（空 = 确定时再问）

    QVector<ShapeState> m_undoStates;
    QVector<ShapeState> m_redoStates;
    QPushButton *m_undoBtn = nullptr;
    QPushButton *m_redoBtn = nullptr;
};
