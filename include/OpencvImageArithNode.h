#pragma once

#include "HalconNode.h"

/// OpenCV 图像运算节点：替代替换版环境下损坏的 HALCON 图像加/乘
class OpencvImageArithNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvImageArithNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
};
