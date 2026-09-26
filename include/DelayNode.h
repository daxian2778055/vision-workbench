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
    /// W-2 的**豁免**节点：延时的职责是时间门控（循环体、暂停、节拍），`数据输入` 只是可选透传。
    /// 与 LoopNode「透传即视为成功（E5）」同族——若把必填数据当成契约，`Loop → 循环体(Delay)`
    /// 与"单独一个延时节点跑节拍"这两类合法流程会全判失败：未豁免时全量门禁实测 4 个套件 /
    /// 14 个测试函数变红（SoakTest 1、NodeGroupTest 2、FlowSnippetTest 1、IntegrationTest 的 Loop/延时系列 10）。
    /// 空载时它不产出任何"参数派生的假结果"（`run()` 明确清空输出），故豁免不掩盖静默绿灯。
    QSet<int> requiredInputDataPorts() const override { return {}; }
    bool requiresDataOutput() const override { return false; }
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
