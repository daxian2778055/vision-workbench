#pragma once

#include "HalconNode.h"

/// N 点标定算子（像素坐标 → 世界坐标仿射变换）
class NPointCalibNode : public HalconNode
{
    Q_OBJECT
public:
    explicit NPointCalibNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
