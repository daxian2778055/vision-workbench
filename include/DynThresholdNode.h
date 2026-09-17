#pragma once

#include "HalconNode.h"

/// 动态阈值算子
class DynThresholdNode : public HalconNode
{
    Q_OBJECT
public:
    explicit DynThresholdNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;

    /// 只在矩形内做动态阈值（清除几何 = 回到全图）。
    /// 用 reduce_domain：只改"域"、不动图像矩阵，区域坐标仍是整图坐标；
    /// 均值图也基于 reduce 后的域计算，输出图像按**原图**尺寸重建。
    RoiType geometryRoiType() const override { return RoiType::Rect; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;
};
