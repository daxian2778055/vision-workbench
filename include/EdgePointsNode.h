#pragma once

#include "HalconNode.h"

/// 亚像素边缘点提取算子
class EdgePointsNode : public HalconNode
{
    Q_OBJECT
public:
    explicit EdgePointsNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;

    /// 只在矩形内提取边缘（清除几何 = 回到全图）。
    /// 实现用 reduce_domain 而非裁剪：前者保留原图坐标系，输出的 XLD 直接就是整图坐标。
    RoiType geometryRoiType() const override { return RoiType::Rect; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;
};
