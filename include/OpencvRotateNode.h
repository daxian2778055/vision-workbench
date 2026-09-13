#pragma once

#include "HalconNode.h"

/// OpenCV 图像旋转节点：替代替换版环境下损坏的 HALCON RotateImage
class OpencvRotateNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvRotateNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
};
