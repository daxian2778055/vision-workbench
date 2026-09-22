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
    // S1 残留收口（第二批补漏）：delayMs 的唯一来源是参数表（默认值在 init() 写入），
    // 不再保留无锁成员镜像——run() 在执行线程读它，界面线程会写。
    // 注意本类的**钳制语义**：旧实现在 setParam 里把成员钳到 ≥0（参数表存原值），
    // 故 toJson/面板/run 读到的都是"钳后值"；收口时把钳制放在**写侧**，语义逐字保持。
    QSpinBox *m_delaySpin = nullptr;
};
