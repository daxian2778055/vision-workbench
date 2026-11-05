#pragma once

#include "HalconNode.h"

/// 灰度拉伸算子（指定灰度范围映射）
class GrayStretchNode : public HalconNode
{
    Q_OBJECT
public:
    explicit GrayStretchNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;

    bool supportsMaskEdit() const override { return true; }
    /// VM 归一化「均值标准差归一化」：矩形 ROI（无 ROI 时全图；ROI 外保留原图）
    RoiType geometryRoiType() const override { return RoiType::Rect; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;
};
