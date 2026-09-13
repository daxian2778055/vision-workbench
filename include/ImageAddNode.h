#pragma once

#include "HalconNode.h"

/// 图像加法算子（图像 + 图像 / 常数）
class ImageAddNode : public HalconNode
{
    Q_OBJECT
public:
    explicit ImageAddNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
