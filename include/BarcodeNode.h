#pragma once

#include "HalconNode.h"

/// 条形码/二维码读取算子
class BarcodeNode : public HalconNode
{
    Q_OBJECT
public:
    explicit BarcodeNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    QString decodedText() const { return m_params.value(QStringLiteral("barcodeText"), QString()).toString(); }
};
