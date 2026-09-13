#pragma once

#include "HalconNode.h"

/// 灰度膨胀算子
class DilateNode : public HalconNode
{
    Q_OBJECT
public:
    explicit DilateNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
