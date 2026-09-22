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
    // S1 残留试点（影子成员收口）：比较符/阈值的**唯一来源是参数表 m_params**（ThreadSafeParams 加锁），
    // 不再保留无锁成员镜像——此前 setParam 写成员、run() 读成员，与界面线程写参数形成无保护竞态
    // （QString 影子还可能破坏堆结构）。默认值在 init() 写入参数表。

    QComboBox *m_opCombo = nullptr;
    QDoubleSpinBox *m_thresholdSpin = nullptr;
};
