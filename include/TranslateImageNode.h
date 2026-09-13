#pragma once

#include "HalconNode.h"

/// 图像平移算子
class TranslateImageNode : public HalconNode
{
    Q_OBJECT
public:
    explicit TranslateImageNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
