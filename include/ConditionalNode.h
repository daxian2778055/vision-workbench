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
    /// 判定不消费图像（继承来的"输入图像"端口只为兼容旧项目），空输入不得判失败
    bool requiresInputImage() const override { return false; }
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    /// 最近一次运行的条件结果（供执行引擎决定激活哪个分支）
    /// **已核实（"运行期状态字段"专项）：读写都在执行线程** —— 写方是本类 run()，
    /// 读方唯一是 FlowExecutor::activateDownstream()（同在执行线程）⇒ 跨线程竞态不成立，故**不加原子/不加锁**。
    /// 若将来出现界面线程读它们的路径（例如面板展示分支结果），再按 CounterNode::m_count 的同款做法
    /// （QAtomicInt/加锁）处理，并同步补并发用例。
    bool conditionResult() const { return m_conditionResult; }
    /// 是否已执行过（未执行时分支不激活）——同上：仅执行线程读写。
    bool hasEvaluated() const { return m_hasEvaluatedFlag; }

private:
    bool m_conditionResult = true;
    bool m_hasEvaluatedFlag = false;
};
