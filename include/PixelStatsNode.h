#pragma once

#include "HalconNode.h"

/// 灰度统计算子（均值/偏差/最大/最小）
class PixelStatsNode : public HalconNode
{
    Q_OBJECT
public:
    explicit PixelStatsNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
