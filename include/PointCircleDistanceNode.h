#pragma once

#include "HalconNode.h"

/// 点圆距离测量算子
class PointCircleDistanceNode : public HalconNode
{
    Q_OBJECT
public:
    explicit PointCircleDistanceNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
