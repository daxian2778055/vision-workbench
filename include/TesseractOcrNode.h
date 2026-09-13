#pragma once

#include "HalconNode.h"
#include <QWidget>
#include <QString>

namespace tesseract {
class TessBaseAPI;
}

/// Tesseract OCR 节点（Apache 2.0）：替代替换版环境下不可用的 HALCON OCR
class TesseractOcrNode : public HalconNode
{
    Q_OBJECT
public:
    explicit TesseractOcrNode(QObject *parent = nullptr);
    ~TesseractOcrNode() override;

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

private:
    tesseract::TessBaseAPI *m_api = nullptr;
    QString m_loadedLang;
    QString m_loadedTessdata;
};
