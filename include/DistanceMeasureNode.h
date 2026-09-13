#pragma once

#include "HalconNode.h"

/// 两点距离测量算子
class DistanceMeasureNode : public HalconNode
{
    Q_OBJECT
public:
    explicit DistanceMeasureNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
