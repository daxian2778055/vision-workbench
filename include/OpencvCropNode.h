#pragma once

#include "HalconNode.h"

/// OpenCV ROI 裁剪节点：替代替换版环境下损坏的 HALCON 区域裁剪（ReduceDomain）
class OpencvCropNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvCropNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    RoiType geometryRoiType() const override { return RoiType::Rect; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;
};
