#pragma once

#include "HalconNode.h"

/// 图像锐化算子（Laplace 增强）
class SharpenNode : public HalconNode
{
    Q_OBJECT
public:
    explicit SharpenNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
