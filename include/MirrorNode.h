#pragma once

#include "HalconNode.h"

/// 图像镜像算子
class MirrorNode : public HalconNode
{
    Q_OBJECT
public:
    explicit MirrorNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
