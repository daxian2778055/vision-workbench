#pragma once

#include "HalconNode.h"

/// Otsu 自适应二值化算子
class OtsuThresholdNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OtsuThresholdNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
