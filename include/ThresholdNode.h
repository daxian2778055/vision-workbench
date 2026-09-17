#pragma once

#include "HalconNode.h"

/// \u9608\u503C\u5206\u5272\u7B97\u5B50
class ThresholdNode : public HalconNode
{
    Q_OBJECT
public:
    explicit ThresholdNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    /// 只在矩形内做阈值分割（清除几何 = 回到全图）。
    /// 实现用 reduce_domain：它只改**域**、不动图像矩阵，区域坐标仍是整图坐标；
    /// 输出图像也仍按原图尺寸重建（尺寸在 reduce 之前取），下游坐标系不变。
    RoiType geometryRoiType() const override { return RoiType::Rect; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;
};
