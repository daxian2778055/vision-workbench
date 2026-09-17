#pragma once

#include "HalconNode.h"

/// OpenCV 角度测量：两条线段（端点给定）的夹角，替代替换版环境下被禁用的 HALCON AngleMeasureNode。
/// 纯几何计算（无 HALCON 区域链路依赖），可无图运行，头测友好。
class OpencvAngleNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvAngleNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
};
