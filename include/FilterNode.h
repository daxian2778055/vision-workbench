#pragma once

#include "HalconNode.h"
#include <QComboBox>
#include <QDoubleSpinBox>

/// 数据筛选算子 — 依据条件判断输入数值是否通过，输出布尔结果
/// 对应 VM 4.4 的「数据筛选」工具
class FilterNode : public HalconNode
{
    Q_OBJECT
public:
    explicit FilterNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    bool process() override;
    void setParam(const QString &name, const QVariant &value) override;
    QVariant getParam(const QString &name) const override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &json) override;

private:
    QString m_operator = QStringLiteral(">="); /// >= <= == > < !=
    double m_threshold = 0.0;

    QComboBox *m_opCombo = nullptr;
    QDoubleSpinBox *m_thresholdSpin = nullptr;
};
