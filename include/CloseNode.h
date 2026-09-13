#pragma once

#include "HalconNode.h"

/// 灰度闭运算算子
class CloseNode : public HalconNode
{
    Q_OBJECT
public:
    explicit CloseNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
