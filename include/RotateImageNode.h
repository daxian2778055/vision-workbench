#pragma once

#include "HalconNode.h"

/// 图像旋转算子
class RotateImageNode : public HalconNode
{
    Q_OBJECT
public:
    explicit RotateImageNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
