#pragma once

#include "HalconNode.h"

/// ROI 裁剪算子（按矩形区域裁剪图像域）
class CropNode : public HalconNode
{
    Q_OBJECT
public:
    explicit CropNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
