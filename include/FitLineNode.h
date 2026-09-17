#pragma once

#include "HalconNode.h"

/// 直线拟合算子
class FitLineNode : public HalconNode
{
    Q_OBJECT
public:
    explicit FitLineNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    /// 模块编辑器：在图上拖一个矩形只在区域内找轮廓（清除几何 = 回到全图）。
    /// 两个端点都加回 ROI 偏移；角度由两点之差决定，平移会同时作用于两点，故角度不变。
    RoiType geometryRoiType() const override { return RoiType::Rect; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;

    double lineRow1() const { return m_params.value(QStringLiteral("lineRow1"), 0.0).toDouble(); }
    double lineCol1() const { return m_params.value(QStringLiteral("lineCol1"), 0.0).toDouble(); }
    double lineRow2() const { return m_params.value(QStringLiteral("lineRow2"), 0.0).toDouble(); }
    double lineCol2() const { return m_params.value(QStringLiteral("lineCol2"), 0.0).toDouble(); }
};
