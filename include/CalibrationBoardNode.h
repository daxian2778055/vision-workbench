#pragma once

#include "HalconNode.h"

/// 标定板标定算子（相机内参标定）
class CalibrationBoardNode : public HalconNode
{
    Q_OBJECT
public:
    explicit CalibrationBoardNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
