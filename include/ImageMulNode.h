#pragma once

#include "HalconNode.h"

/// 图像乘法算子（图像 × 图像 / 常数）
class ImageMulNode : public HalconNode
{
    Q_OBJECT
public:
    explicit ImageMulNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
