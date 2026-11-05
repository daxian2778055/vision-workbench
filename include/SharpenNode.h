#pragma once

#include "HalconNode.h"

/// 图像锐化算子（Laplace 增强）
class SharpenNode : public HalconNode
{
    Q_OBJECT
public:
    explicit SharpenNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;

    bool supportsMaskEdit() const override { return true; }
    /// VM 图像增强「锐化」：矩形 ROI（无 ROI 时全图；ROI 外保留原图）
    RoiType geometryRoiType() const override { return RoiType::Rect; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;
};
