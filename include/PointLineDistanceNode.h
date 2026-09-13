#pragma once

#include "HalconNode.h"

/// 点线距离测量算子
class PointLineDistanceNode : public HalconNode
{
    Q_OBJECT
public:
    explicit PointLineDistanceNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
