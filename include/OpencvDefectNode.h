#pragma once

#include "HalconNode.h"

/// OpenCV 缺陷检测：黄金模板差影 + 形态学残差 + 面积过滤
class OpencvDefectNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvDefectNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    RoiType geometryRoiType() const override { return RoiType::Rect; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;
    bool supportsMaskEdit() const override { return true; }
};
