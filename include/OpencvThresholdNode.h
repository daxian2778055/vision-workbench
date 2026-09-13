#pragma once

#include "HalconNode.h"

/// 二值化（OpenCV）
class OpencvThresholdNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvThresholdNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    bool supportsMaskEdit() const override { return true; }
};
