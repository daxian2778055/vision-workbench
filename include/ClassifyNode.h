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
    // S1 残留收口：5 个参数（thresholdLow/High、nameLow/Mid/High）的唯一来源是参数表，
    // 默认值在 init() 写入；不再保留无锁成员镜像（原先 setParam 写成员、run() 读成员 = 无保护竞态）。
    // 本类 getParam() 是纯转发（直接调基类），不存在 FormulaNode 那种"返回成员"的旁路，故保留不动。

    QDoubleSpinBox *m_lowSpin = nullptr;
    QDoubleSpinBox *m_highSpin = nullptr;
    QLineEdit *m_lowNameEdit = nullptr;
    QLineEdit *m_midNameEdit = nullptr;
    QLineEdit *m_highNameEdit = nullptr;
};
