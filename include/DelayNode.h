#pragma once

#include "HalconNode.h"
#include <QSpinBox>

/// 延时算子 — 数据透传，并在执行时阻塞指定毫秒
/// 对应 VM 4.4 的「延时」工具
class DelayNode : public HalconNode
{
    Q_OBJECT
public:
    explicit DelayNode(QObject *parent = nullptr);

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
    int m_delayMs = 100;

    QSpinBox *m_delaySpin = nullptr;
};
