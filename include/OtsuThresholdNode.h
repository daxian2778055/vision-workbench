#pragma once

#include "HalconNode.h"

/// Otsu 自适应二值化算子
class OtsuThresholdNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OtsuThresholdNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;

    /// 只在矩形内做 Otsu 二值化（清除几何 = 回到全图）。
    /// 用 reduce_domain：只改"域"、不动图像矩阵，区域坐标仍是整图坐标；
    /// 输出图像按**原图**尺寸重建，下游坐标系不变。
    RoiType geometryRoiType() const override { return RoiType::Rect; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;
};
