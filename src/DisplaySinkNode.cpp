#include "DisplaySinkNode.h"
#include <QVBoxLayout>
#include <QLabel>

DisplaySinkNode::DisplaySinkNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("\u663E\u793A"));
    m_type = OUTPUT;
}

void DisplaySinkNode::init()
{
    HalconNode::init();
    registerParams({
        makeEnumParam(QStringLiteral("windowIndex"), 0, QStringList{"主窗口", "窗口 1", "窗口 2", "窗口 3", "窗口 4"},
                      QStringLiteral("目标显示窗口")),
    });
    m_params[QStringLiteral("windowIndex")] = 0;
}

void DisplaySinkNode::run(bool /*autoSwitch*/)
{
    m_outputImage = m_inputImage;
}

QWidget *DisplaySinkNode::createParamPanel()
{
    auto *panel = createAutoParamPanel();
    auto *layout = panel->findChild<QVBoxLayout *>();
    if (!layout) layout = new QVBoxLayout(panel);
    auto *tip = new QLabel(QStringLiteral(
        "<i>说明：本节点透传上游图像并标记显示意图。"
        "「主窗口」对应编辑器主图像视图；「窗口 1-4」由运行界面设计器中"
        "绑定本节点的图像控件决定实际显示位置。</i>"));
    tip->setWordWrap(true);
    layout->addWidget(tip);
    layout->addStretch();
    return panel;
}
