#pragma once

#include "HalconNode.h"

/// 图像缩放算子
class ResizeNode : public HalconNode
{
    Q_OBJECT
public:
    explicit ResizeNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
