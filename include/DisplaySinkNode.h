#pragma once

#include "HalconNode.h"

/// \u56FE\u50CF\u663E\u793A\u7B97\u5B50\uFF08\u6D41\u7A0B\u672B\u7AEF\u8F93\u51FA\u9884\u89C8\uFF09
class DisplaySinkNode : public HalconNode
{
    Q_OBJECT
public:
    explicit DisplaySinkNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
};
