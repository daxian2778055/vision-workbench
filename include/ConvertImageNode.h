#pragma once

#include "HalconNode.h"

/// 图像类型转换算子
class ConvertImageNode : public HalconNode
{
    Q_OBJECT
public:
    explicit ConvertImageNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
