#pragma once

#include "HalconNode.h"

/// 圆拟合算子
class FitCircleNode : public HalconNode
{
    Q_OBJECT
public:
    explicit FitCircleNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    double centerRow() const { return m_params.value(QStringLiteral("circleRow"), 0.0).toDouble(); }
    double centerCol() const { return m_params.value(QStringLiteral("circleCol"), 0.0).toDouble(); }
    double radius() const { return m_params.value(QStringLiteral("circleRadius"), 0.0).toDouble(); }
};
