#pragma once

#include "HalconNode.h"

/// 直方图均衡化算子
class HistogramEqualizeNode : public HalconNode
{
    Q_OBJECT
public:
    explicit HistogramEqualizeNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
