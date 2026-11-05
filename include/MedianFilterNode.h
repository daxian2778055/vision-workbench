#pragma once

#include "HalconNode.h"

/// 中值滤波算子
class MedianFilterNode : public HalconNode
{
    Q_OBJECT
public:
    explicit MedianFilterNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;

    bool supportsMaskEdit() const override { return true; }
    /// VM 滤波「ROI区域」：矩形 ROI（无 ROI 时全图处理；ROI 外保留原图）
    RoiType geometryRoiType() const override { return RoiType::Rect; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;
};
