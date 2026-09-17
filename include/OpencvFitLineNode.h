#pragma once

#include "HalconNode.h"

/// OpenCV 直线拟合节点：输入二值/边缘图，轮廓点 fitLine 输出直线参数
class OpencvFitLineNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvFitLineNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    /// 模块编辑器：在图上拖一个矩形只在区域内找轮廓（清除几何 = 回到全图）。
    /// 只把拟合点 (x0,y0) 平移回整图坐标——角度由方向向量决定，平移不影响，故无需换算。
    RoiType geometryRoiType() const override { return RoiType::Rect; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;
};
