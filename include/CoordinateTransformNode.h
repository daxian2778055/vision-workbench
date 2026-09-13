#pragma once

#include "HalconNode.h"

/// 坐标系变换算子（齐次变换矩阵作用于点）
class CoordinateTransformNode : public HalconNode
{
    Q_OBJECT
public:
    explicit CoordinateTransformNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
