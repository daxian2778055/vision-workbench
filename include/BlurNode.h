#pragma once

#include "HalconNode.h"

/// \u9AD8\u65AF\u6A21\u7CCA\u7B97\u5B50
class BlurNode : public HalconNode
{
    Q_OBJECT
public:
    explicit BlurNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    bool supportsMaskEdit() const override { return true; }
    /// VM 滤波「ROI区域」：矩形 ROI（无 ROI 时全图处理；ROI 外保留原图）
    RoiType geometryRoiType() const override { return RoiType::Rect; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;
};
