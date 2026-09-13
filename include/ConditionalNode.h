#pragma once

#include "HalconNode.h"

/// 条件判断算子：根据条件（参数或输入端口数据）选择 TRUE/FALSE 分支。
/// 执行引擎按分支激活下游节点：选中分支的下游执行，未选中分支的下游跳过。
class ConditionalNode : public HalconNode
{
    Q_OBJECT
public:
    explicit ConditionalNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    /// 最近一次运行的条件结果（供执行引擎决定激活哪个分支）
    bool conditionResult() const { return m_conditionResult; }
    /// 是否已执行过（未执行时分支不激活）
    bool hasEvaluated() const { return m_hasEvaluatedFlag; }

private:
    bool m_conditionResult = true;
    bool m_hasEvaluatedFlag = false;
};
