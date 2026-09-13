#pragma once

#include "HalconNode.h"

/// 灰度拉伸算子（指定灰度范围映射）
class GrayStretchNode : public HalconNode
{
    Q_OBJECT
public:
    explicit GrayStretchNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
