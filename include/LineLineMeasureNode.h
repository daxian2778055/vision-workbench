#pragma once

#include "HalconNode.h"

/// 线线测量（对齐 VM 4.4「几何测量 → 线线测量」）：两条直线求交点、夹角与绝对距离。
///
/// 直线来源（`sourceMode` + `lineInputMode`）：
///   - 订阅 · 按线：两个逻辑槽 `直线1` / `直线2`，各接受一个 `Measure(type="line")` 结果
///     （直线拟合、线段查找等节点的输出），按 point1/point2 取端点；
///   - 订阅 · 按点：四个逻辑槽 `直线1起点/终点`、`直线2起点/终点`，各接受一个 Point 数据；
///   - 绘制：直接填 8 个坐标参数（Col/Row 各两组），不依赖订阅。
///
/// 只做纯几何，不需要图像；若接了图像则原样透传。
/// 未对齐（明确挂账）：特定坐标系/标定矩阵换算、卡尺绘制与屏蔽区多边形（官方线线测量有，
/// 本节点没有卡尺链路）。
class LineLineMeasureNode : public HalconNode
{
    Q_OBJECT
public:
    /// 两个「按线」订阅槽的逻辑下标（HalconNode::init 只建 1 个真实输入端口）
    static constexpr int kLine1Slot = 1;
    static constexpr int kLine2Slot = 2;
    /// 「按点」订阅槽：直线1起点/终点、直线2起点/终点
    static constexpr int kLine1StartSlot = 3;
    static constexpr int kLine1EndSlot = 4;
    static constexpr int kLine2StartSlot = 5;
    static constexpr int kLine2EndSlot = 6;

    explicit LineLineMeasureNode(QObject *parent = nullptr);

    void init() override;
    bool process() override;
    void run(bool autoSwitch = true) override;
    /// 纯几何节点：不要求输入图像（绘制模式缺图由 run() 给出明确原因）
    bool requiresInputImage() const override { return false; }
    /// VM「模块结果」口径：两条直线的端点/角度、交点、夹角、带符号角度、绝对距离
    QVector<NodeResultField> resultDisplayFields() const override;
};
