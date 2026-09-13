#pragma once

#include "HalconNode.h"

/// 图像求反算子
class ImageInvertNode : public HalconNode
{
    Q_OBJECT
public:
    explicit ImageInvertNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
