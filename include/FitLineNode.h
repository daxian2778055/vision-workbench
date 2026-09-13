#pragma once

#include "HalconNode.h"

/// 直线拟合算子
class FitLineNode : public HalconNode
{
    Q_OBJECT
public:
    explicit FitLineNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    double lineRow1() const { return m_params.value(QStringLiteral("lineRow1"), 0.0).toDouble(); }
    double lineCol1() const { return m_params.value(QStringLiteral("lineCol1"), 0.0).toDouble(); }
    double lineRow2() const { return m_params.value(QStringLiteral("lineRow2"), 0.0).toDouble(); }
    double lineCol2() const { return m_params.value(QStringLiteral("lineCol2"), 0.0).toDouble(); }
};
