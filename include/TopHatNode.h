#pragma once

#include "HalconNode.h"

/// 顶帽变换算子（提取亮细节）
class TopHatNode : public HalconNode
{
    Q_OBJECT
public:
    explicit TopHatNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
