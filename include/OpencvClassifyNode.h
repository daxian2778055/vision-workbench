#pragma once

#include "HalconNode.h"

/// OpenCV 分类推理节点：加载 ANN_MLP 模型，对输入图像分类
class OpencvClassifyNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvClassifyNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
};
