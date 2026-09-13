#pragma once

#include "HalconNode.h"

/// 底帽变换算子（提取暗细节）
class BottomHatNode : public HalconNode
{
    Q_OBJECT
public:
    explicit BottomHatNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
