#pragma once

#include "HalconNode.h"

/// 角度测量算子（两线段夹角）
class AngleMeasureNode : public HalconNode
{
    Q_OBJECT
public:
    explicit AngleMeasureNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
