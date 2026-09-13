#pragma once

#include "HalconNode.h"

/// 手眼标定算子（像素点 → 机器人坐标点 2D 标定）
class HandEyeCalibNode : public HalconNode
{
    Q_OBJECT
public:
    explicit HandEyeCalibNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
