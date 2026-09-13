#pragma once

#include "HalconNode.h"

/// OCR 字符识别算子
class OcrNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OcrNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    QString recognizedText() const { return m_params.value(QStringLiteral("ocrText"), QString()).toString(); }
};
