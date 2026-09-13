#pragma once

#include "HalconNode.h"

/// 动态阈值算子
class DynThresholdNode : public HalconNode
{
    Q_OBJECT
public:
    explicit DynThresholdNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
