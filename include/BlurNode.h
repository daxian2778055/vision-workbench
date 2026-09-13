#pragma once

#include "HalconNode.h"

/// \u9AD8\u65AF\u6A21\u7CCA\u7B97\u5B50
class BlurNode : public HalconNode
{
    Q_OBJECT
public:
    explicit BlurNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
};
