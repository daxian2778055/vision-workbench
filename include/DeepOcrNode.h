#pragma once

#include "HalconNode.h"

/// HALCON DeepOCR 节点：加载官方预训练 OCR 模型，识别图像文字（开箱即用）
class DeepOcrNode : public HalconNode
{
    Q_OBJECT
public:
    explicit DeepOcrNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
};
