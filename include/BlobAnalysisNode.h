#pragma once

#include "HalconNode.h"

/// Blob \u5206\u6790\u7B97\u5B50
class BlobAnalysisNode : public HalconNode
{
    Q_OBJECT
public:
    explicit BlobAnalysisNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    int regionCount() const { return m_params.value(QStringLiteral("regionCount"), 0).toInt(); }
};
