#pragma once

#include "HalconNode.h"

/// 角点检测算子（Harris）
class CornerNode : public HalconNode
{
    Q_OBJECT
public:
    explicit CornerNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
