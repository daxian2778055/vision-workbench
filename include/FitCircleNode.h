#pragma once

#include "HalconNode.h"

/// 圆拟合算子
class FitCircleNode : public HalconNode
{
    Q_OBJECT
public:
    explicit FitCircleNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    /// 模块编辑器：在图上拖一个矩形只在区域内找轮廓（清除几何 = 回到全图）。
    /// 结果坐标会加回 ROI 偏移，因此对下游仍是整图坐标（半径不受平移影响）。
    RoiType geometryRoiType() const override { return RoiType::Rect; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;

    double centerRow() const { return m_params.value(QStringLiteral("circleRow"), 0.0).toDouble(); }
    double centerCol() const { return m_params.value(QStringLiteral("circleCol"), 0.0).toDouble(); }
    double radius() const { return m_params.value(QStringLiteral("circleRadius"), 0.0).toDouble(); }
};
