#pragma once

#include "HalconNode.h"
#include <QWidget>

/// ZXing 条码解码节点：一维条码（EAN/UPC/Code128/Code39 等）+ QR/DataMatrix，
/// 替代替换版环境下不可用的 HALCON 条码链路（Apache 2.0 开源库）
class ZxingBarcodeNode : public HalconNode
{
    Q_OBJECT
public:
    explicit ZxingBarcodeNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
};
