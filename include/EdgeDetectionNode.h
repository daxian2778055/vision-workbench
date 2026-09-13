#pragma once

#include "HalconNode.h"

/// Sobel \u8FB9\u7F18\u68C0\u6D4B\u7B97\u5B50
class EdgeDetectionNode : public HalconNode
{
    Q_OBJECT
public:
    explicit EdgeDetectionNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
};
