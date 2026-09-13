#pragma once

#include "HalconNode.h"

/// 灰度开运算算子
class OpenNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpenNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
