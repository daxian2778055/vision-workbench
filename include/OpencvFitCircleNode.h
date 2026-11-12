#pragma once

#include "HalconNode.h"

/// OpenCV 圆拟合节点：输入二值/边缘图，最大轮廓 minEnclosingCircle 输出圆心/半径。
/// 也可在「输入订阅」里订阅点集（XLD / Region / Point / 卡尺边缘点），订阅优先且不回退图像。
class OpencvFitCircleNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvFitCircleNode(QObject *parent = nullptr);

    /// 「点集」订阅槽的逻辑下标：HalconNode::init() 只建 1 个真实输入端口（输入图像）
    static constexpr int kPointSlot = 1;
    /// 「按坐标」订阅的两个数值槽（输入方式 = 按坐标时使用）
    static constexpr int kPointXSlot = 2;
    static constexpr int kPointYSlot = 3;

    void init() override;
    bool process() override;
    void run(bool autoSwitch = true) override;
    /// 订阅了点集就不再要求输入图像；订阅数据无效时 run() 直接判失败，不回退阈值图
    bool requiresInputImage() const override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
    /// VM「模块结果」展示口径：圆心/半径/参与点数/残差
    QVector<NodeResultField> resultDisplayFields() const override;

    /// 模块编辑器：在图上拖一个矩形只在区域内找轮廓（清除几何 = 回到全图）。
    /// 结果坐标会加回 ROI 偏移，因此对下游仍是整图坐标。
    RoiType geometryRoiType() const override { return RoiType::Rect; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;
};
