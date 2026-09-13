#pragma once

#include "HalconNode.h"

/// \u56FE\u50CF\u5199\u5165\u6587\u4EF6\u7B97\u5B50
class WriteFileNode : public HalconNode
{
    Q_OBJECT
public:
    explicit WriteFileNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
};
