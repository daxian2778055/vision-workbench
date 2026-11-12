#pragma once

#include "HalconNode.h"

/// 圆圆测量（对齐 VM 4.4「几何测量 → 圆圆测量」）：两圆交点、圆心距与位置关系。
///
/// 圆的两种来源（`sourceMode`）：
///   - 订阅：两个逻辑槽 `圆1` / `圆2`，各取一条 `Measure(type="circle")`（圆心 = point1，半径 = value）；
///   - 绘制：直接填两组的圆心与半径。
///
/// 位置关系按官方五档：Inside（内含）/ Inscribe（内切）/ Intersect（相交）/
/// Circumscribe（外切）/ Outside（外离）；交点 0/1/2 个与关系一一对应。
/// 纯几何节点；绘制模式要求输入图像（与 VM 输入源一致），订阅模式不要求。
/// 未对齐（明确挂账）：官方「角度（最小外接矩形较长边）」与卡尺检测区、按点订阅、特定坐标系。
class CircleCircleMeasureNode : public HalconNode
{
    Q_OBJECT
public:
    static constexpr int kCircle1Slot = 1;
    static constexpr int kCircle2Slot = 2;

    explicit CircleCircleMeasureNode(QObject *parent = nullptr);

    void init() override;
    bool process() override;
    void run(bool autoSwitch = true) override;
    bool requiresInputImage() const override { return false; }
    QVector<NodeResultField> resultDisplayFields() const override;
};
