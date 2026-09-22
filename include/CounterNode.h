#pragma once

#include "HalconNode.h"
#include <QSpinBox>
#include <QComboBox>

/// 条件计数算子 — 输入条件为真时计数自增，输出累计次数
/// 对应 VM 4.4 的「条件计数」工具
class CounterNode : public HalconNode
{
    Q_OBJECT
public:
    explicit CounterNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    bool process() override;
    void setParam(const QString &name, const QVariant &value) override;
    QVariant getParam(const QString &name) const override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &json) override;

    int count() const { return m_count; }

private:
    int m_count = 0;              /// 累计次数（运行期状态，非参数镜像）
    // S1 残留收口：conditionMode / threshold 的唯一来源是参数表（默认值在 init() 写入），
    // 不再保留无锁成员镜像——此前 setParam 写成员、run() 读成员，与界面线程写参数构成无保护竞态。

    QComboBox *m_modeCombo = nullptr;
    QDoubleSpinBox *m_thresholdSpin = nullptr;
};
