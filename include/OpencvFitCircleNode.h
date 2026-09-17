#pragma once

#include "HalconNode.h"

/// OpenCV 圆拟合节点：输入二值/边缘图，最大轮廓 minEnclosingCircle 输出圆心/半径
class OpencvFitCircleNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvFitCircleNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    /// 模块编辑器：在图上拖一个矩形只在区域内找轮廓（清除几何 = 回到全图）。
    /// 结果坐标会加回 ROI 偏移，因此对下游仍是整图坐标。
    RoiType geometryRoiType() const override { return RoiType::Rect; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;
};
