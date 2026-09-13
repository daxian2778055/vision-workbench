#pragma once

#include "HalconNode.h"

/// 位置修正算子（点经仿射变换，用于坐标系修正）
class PositionCorrectNode : public HalconNode
{
    Q_OBJECT
public:
    explicit PositionCorrectNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
