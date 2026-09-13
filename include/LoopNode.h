#pragma once

#include "HalconNode.h"

/// \u5FAA\u73AF\u7B97\u5B50\uFF1A\u8BBE\u7F6E loopCount \u540E\uFF0C\u6267\u884C\u5668\u5C06\u91CD\u590D\u6267\u884C\u4E0B\u6E38\u5FAA\u73AF\u4F53
class LoopNode : public HalconNode
{
    Q_OBJECT
public:
    explicit LoopNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
};
