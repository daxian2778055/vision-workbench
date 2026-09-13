#pragma once

#include "HalconNode.h"

/// OpenCV 直线拟合节点：输入二值/边缘图，轮廓点 fitLine 输出直线参数
class OpencvFitLineNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvFitLineNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
};
