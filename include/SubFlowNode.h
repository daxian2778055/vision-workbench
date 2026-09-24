#pragma once

#include "HalconNode.h"

class QLineEdit;

/// 子流程算子（FR15.10 运行期调用，方案 B：方案内命名子图）。
/// 参数 subFlowName 指向当前方案内定义的子流程（FlowScene::defineSubFlowFromSelection）；
/// 执行时由 FlowExecutor::executeSubFlow 同线程内联执行其成员算子
/// （主遍历跳过成员——与循环体 P3 同一套模式），入口喂数据、出口取结果回写本算子输出端口。
class SubFlowNode : public HalconNode
{
    Q_OBJECT
public:
    explicit SubFlowNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    bool process() override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &json) override;

private:
    /// 真实执行体：无执行器上下文（设计期/节点自检）空转成功并清输出（P1）；
    /// 有执行器时转交 FlowExecutor::executeSubFlow，返回值即成功/失败。
    bool invoke();
    QLineEdit *m_nameEdit = nullptr;
};
