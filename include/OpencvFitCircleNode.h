#pragma once

#include "HalconNode.h"

/// OpenCV 圆拟合节点：输入二值/边缘图，最大轮廓 minEnclosingCircle 输出圆心/半径
class OpencvFitCircleNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvFitCircleNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
};
