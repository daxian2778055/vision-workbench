#pragma once

#include "HalconNode.h"

/// 对比度拉伸算子（自动最大对比度）
class ContrastStretchNode : public HalconNode
{
    Q_OBJECT
public:
    explicit ContrastStretchNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
