#pragma once

#include "HalconNode.h"

/// 图像减法算子（图像 - 图像 / 常数）
class ImageSubNode : public HalconNode
{
    Q_OBJECT
public:
    explicit ImageSubNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
