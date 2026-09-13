#pragma once

#include "HalconNode.h"

/// 中值滤波算子
class MedianFilterNode : public HalconNode
{
    Q_OBJECT
public:
    explicit MedianFilterNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
