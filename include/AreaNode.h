#pragma once

#include "HalconNode.h"

/// \u9762\u79EF\u6D4B\u91CF\u7B97\u5B50
class AreaNode : public HalconNode
{
    Q_OBJECT
public:
    explicit AreaNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    double totalArea() const { return m_params.value(QStringLiteral("totalArea"), 0.0).toDouble(); }
    double centroidRow() const { return m_params.value(QStringLiteral("regionCentroidRow"), 0.0).toDouble(); }
    double centroidCol() const { return m_params.value(QStringLiteral("regionCentroidCol"), 0.0).toDouble(); }
};
