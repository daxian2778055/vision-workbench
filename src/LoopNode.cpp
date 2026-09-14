#include "LoopNode.h"
#include <QVBoxLayout>
#include <QLabel>

LoopNode::LoopNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("循环"));
    m_type = LOGIC;
}

void LoopNode::init()
{
    HalconNode::init();
    registerParams({
        makeIntParam(QStringLiteral("loopCount"), 1, 1, 1000,
                     QStringLiteral("循环次数（循环体重复执行次数，1=不循环）")),
    });
    m_params[QStringLiteral("loopStatus")] = QString();
}

void LoopNode::run(bool /*autoSwitch*/)
{
    // 图像透传；循环执行由 FlowExecutor 按 loopCount 重复执行下游循环体
    // 迭代变量：主循环第 1 次执行为 1，循环体重复执行时由 FlowExecutor 更新为 2..loopCount
    m_params[QStringLiteral("iteration")] = 1;
    m_outputImage = m_inputImage;
    m_params[QStringLiteral("moduleStatus")] = true;  // 循环节点透传即视为成功（E5）
}

QWidget *LoopNode::createParamPanel()
{
    auto *panel = createAutoParamPanel();
    auto *layout = qobject_cast<QVBoxLayout *>(panel->layout());
    if (layout) {
        layout->insertWidget(0, new QLabel(QStringLiteral(
            "<b>循环</b>：设置循环次数后，本节点下游（至汇合点前的")
            + QStringLiteral("线性子图）将重复执行。")));
    }
    return panel;
}
