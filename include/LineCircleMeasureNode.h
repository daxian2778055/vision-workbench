#pragma once

#include "HalconNode.h"

/// 线圆测量（对齐 VM 4.4「几何测量 → 线圆测量」）：直线与圆的交点、圆心垂足与圆心到直线距离。
///
/// 直线来源（`sourceMode` + `lineInputMode`）：订阅 · 按线读 `Measure(line)`、按点读两个 Point，
/// 或绘制填端点坐标；圆来源：订阅 `Measure(circle)`（圆心 = point1，半径 = value）或填圆心/半径。
///
/// 只做纯几何；绘制模式要求输入图像（与 VM 输入源一致），订阅模式不要求。
/// 未对齐（明确挂账）：特定坐标系/标定矩阵、卡尺绘制与卡尺检测区、
/// 官方「角度（最小外接矩形较长边）」口径未实现（帮助描述与线圆几何不对应，不臆造）。
class LineCircleMeasureNode : public HalconNode
{
    Q_OBJECT
public:
    static constexpr int kLineSlot = 1;
    static constexpr int kCircleSlot = 2;
    /// 「按点」订阅槽：直线起点 / 终点
    static constexpr int kLineStartSlot = 3;
    static constexpr int kLineEndSlot = 4;

    explicit LineCircleMeasureNode(QObject *parent = nullptr);

    void init() override;
    bool process() override;
    void run(bool autoSwitch = true) override;
    /// 纯几何节点：绘制模式缺图由 run() 给出明确原因
    bool requiresInputImage() const override { return false; }
    QVector<NodeResultField> resultDisplayFields() const override;
};
