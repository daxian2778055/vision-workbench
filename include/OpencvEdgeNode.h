#pragma once

#include "HalconNode.h"

/// OpenCV 边缘检测节点（Canny）：替代替换版环境下损坏的 HALCON 边缘/区域链路
class OpencvEdgeNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvEdgeNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
};
