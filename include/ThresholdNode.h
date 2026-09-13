#pragma once

#include "HalconNode.h"

/// \u9608\u503C\u5206\u5272\u7B97\u5B50
class ThresholdNode : public HalconNode
{
    Q_OBJECT
public:
    explicit ThresholdNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
};
