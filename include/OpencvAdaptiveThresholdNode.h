#pragma once

#include "HalconNode.h"

/// OpenCV 自适应阈值节点：替代替换版环境下损坏的 HALCON 动态阈值
class OpencvAdaptiveThresholdNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvAdaptiveThresholdNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
};
