#pragma once

#include "HalconNode.h"

/// OpenCV 形态学节点：腐蚀/膨胀/开/闭，替代替换版环境下损坏的 HALCON 区域形态学
class OpencvMorphNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvMorphNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    bool supportsMaskEdit() const override { return true; }
    /// VM 形态学「ROI区域」：矩形 ROI（无 ROI 时全图处理；ROI 外保留原图）
    RoiType geometryRoiType() const override { return RoiType::Rect; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;
};
