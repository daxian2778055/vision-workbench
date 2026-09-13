#pragma once

#include "HalconNode.h"

/// 灰度腐蚀算子
class ErodeNode : public HalconNode
{
    Q_OBJECT
public:
    explicit ErodeNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
