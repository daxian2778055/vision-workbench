#pragma once

#include "HalconNode.h"

/// 对比度拉伸算子（自动最大对比度）
class ContrastStretchNode : public HalconNode
{
    Q_OBJECT
public:
    explicit ContrastStretchNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;

    bool supportsMaskEdit() const override { return true; }
    /// VM 图像增强「对比度」：矩形 ROI（无 ROI 时全图；ROI 外保留原图）
    RoiType geometryRoiType() const override { return RoiType::Rect; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;
};
