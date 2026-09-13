#pragma once

#include "HalconNode.h"

/// 二维码/DataMatrix 识别算子
class DataCode2DNode : public HalconNode
{
    Q_OBJECT
public:
    explicit DataCode2DNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
