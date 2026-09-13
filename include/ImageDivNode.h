#pragma once

#include "HalconNode.h"

/// 图像除法算子（图像 ÷ 图像 / 常数）
class ImageDivNode : public HalconNode
{
    Q_OBJECT
public:
    explicit ImageDivNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
