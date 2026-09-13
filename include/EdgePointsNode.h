#pragma once

#include "HalconNode.h"

/// 亚像素边缘点提取算子
class EdgePointsNode : public HalconNode
{
    Q_OBJECT
public:
    explicit EdgePointsNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
