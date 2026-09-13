#pragma once

#include "HalconNode.h"
#include <QLineEdit>
#include <QDoubleSpinBox>

/// 数据分类算子 — 按区间将输入数值分类为低/中/高并输出字符串
/// 对应 VM 4.4 的「数据分类」工具
class ClassifyNode : public HalconNode
{
    Q_OBJECT
public:
    explicit ClassifyNode(QObject *parent = nullptr);

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
    double m_thresholdLow = 0.0;    /// 低于此值 → 低
    double m_thresholdHigh = 100.0; /// 高于此值 → 高
    QString m_nameLow = QStringLiteral("低");
    QString m_nameMid = QStringLiteral("中");
    QString m_nameHigh = QStringLiteral("高");

    QDoubleSpinBox *m_lowSpin = nullptr;
    QDoubleSpinBox *m_highSpin = nullptr;
    QLineEdit *m_lowNameEdit = nullptr;
    QLineEdit *m_midNameEdit = nullptr;
    QLineEdit *m_highNameEdit = nullptr;
};
