#pragma once

#include "HalconNode.h"
#include <QWidget>

/// 圆查找算子（亚像素圆拟合）
class FindCircleNode : public HalconNode
{
    Q_OBJECT
public:
    explicit FindCircleNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;

    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    /// 搜索区域 = 圆心 + 半径（HALCON Metrology 圆的参数本身就是搜索区域，
    /// 所以这里只做参数接线，不涉及裁剪或坐标换算）。
    /// 补上这三个虚函数后，模块编辑器里的「绘制几何 / 清除几何」才会出现——
    /// 此前本节点只有面板上的旧按钮走 roiPickRequested 信号，那条路在编辑器里是看不到的。
    RoiType geometryRoiType() const override { return RoiType::Circle; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;

signals:
    /// 请求在画布上绘制 ROI（由主窗口协调进入 ROI 编辑模式）
    void roiPickRequested();
};
