#pragma once

#include "HalconNode.h"

/// 直方图均衡化算子
class HistogramEqualizeNode : public HalconNode
{
    Q_OBJECT
public:
    explicit HistogramEqualizeNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;

    bool supportsMaskEdit() const override { return true; }
    /// VM 图像归一化「直方图均衡化」：矩形 ROI（无 ROI 时全图；ROI 外保留原图）
    RoiType geometryRoiType() const override { return RoiType::Rect; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;
};
