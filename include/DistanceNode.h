#pragma once

#include "HalconNode.h"

/// \u4E24\u70B9\u8DDD\u79BB\u6D4B\u91CF\u7B97\u5B50
class DistanceNode : public HalconNode
{
    Q_OBJECT
public:
    explicit DistanceNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    double distancePixels() const { return m_params.value(QStringLiteral("distancePixel"), 0.0).toDouble(); }
};
