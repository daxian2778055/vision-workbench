#include "SubFlowNode.h"
#include "FlowExecutor.h"
#include "DataObject.h"
#include "Port.h"
#include "PortDataType.h"
#include <QLineEdit>
#include <QVBoxLayout>
#include <QLabel>
#include <QSignalBlocker>

SubFlowNode::SubFlowNode(QObject *parent)
    : HalconNode(parent)
{
    setName(QStringLiteral("\u5B50\u6D41\u7A0B"));   // 子流程
    m_type = LOGIC;
}

void SubFlowNode::init()
{
    addInputPort(QStringLiteral("\u6570\u636E\u8F93\u5165"), PortDataType::Any);
    addOutputPort(QStringLiteral("\u6570\u636E\u8F93\u51FA"), PortDataType::Any);

    m_params[QStringLiteral("subFlowName")] = QString();
}

bool SubFlowNode::invoke()
{
    FlowExecutor *exec = ownerExecutor();
    if (!exec) {
        // 设计期 / 节点自检（runNodeSelfTest 只调 run 且无执行器上下文）：
        // 空转成功并清输出，不留上一轮残留（P1）
        setOutputData(0, QSharedPointer<DataObject>());
        return true;
    }
    // 失败语义在 executeSubFlow 内收口：具体哪个子节点失败、递归/未定义原因都带在错误里
    return exec->executeSubFlow(this);
}

bool SubFlowNode::process()
{
    return invoke();
}

void SubFlowNode::run(bool /*autoSwitch*/)
{
    // 生产执行走 execute() → process()（有布尔返回值）；
    // run() 仅服务节点自检（无执行器空转），不重复触发真实调用。
    invoke();
}

QWidget *SubFlowNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>\u5B50\u6D41\u7A0B</b>")));
    layout->addWidget(new QLabel(QStringLiteral("\u5B50\u6D41\u7A0B\u540D\u79F0\uFF08\u9700\u4E0E\u300C\u5B9A\u4E49\u4E3A\u5B50\u6D41\u7A0B\u300D\u65F6\u7684\u540D\u5B57\u4E00\u81F4\uFF09:")));

    m_nameEdit = new QLineEdit();
    m_nameEdit->setObjectName(QStringLiteral("subFlowNameEdit"));
    m_nameEdit->setText(getParam(QStringLiteral("subFlowName")).toString());
    layout->addWidget(m_nameEdit);

    layout->addWidget(new QLabel(QStringLiteral(
        "\u63D0\u793A\uFF1A\u5148\u9009\u4E2D\u4E00\u6BB5\u6D41\u7A0B\uFF08\u22652 \u4E2A\u7B97\u5B50\uFF0C\u5355\u8FDB\u5355\u51FA\u3001"
        "\u4E0D\u4E0E\u6210\u5458\u5916\u8FDE\u7EBF\uFF09\uFF0C\u518D\u7528\u300C\u7F16\u8F91 \u2192 \u5B9A\u4E49\u4E3A\u5B50\u6D41\u7A0B\u300D"
        "\u8D77\u540D\uFF1B\u540D\u5B57\u5199\u9519\u6267\u884C\u65F6\u4F1A\u62A5\u300C\u672A\u5B9A\u4E49\u300D\u3002")));

    connect(m_nameEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        setParam(QStringLiteral("subFlowName"), text);
    });

    layout->addStretch();
    return panel;
}

void SubFlowNode::updateParamPanel(QWidget *panel)
{
    if (!panel)
        return;
    if (auto *edit = panel->findChild<QLineEdit *>(QStringLiteral("subFlowNameEdit"))) {
        QSignalBlocker b(edit);
        edit->setText(getParam(QStringLiteral("subFlowName")).toString());
    }
}

QJsonObject SubFlowNode::toJson() const
{
    QJsonObject obj = HalconNode::toJson();
    // 顶层键保留（老读取方兼容），值取自参数表（唯一来源）
    obj[QStringLiteral("subFlowName")] = getParam(QStringLiteral("subFlowName")).toString();
    return obj;
}

void SubFlowNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);   // subFlowName 由基类从 params 恢复（唯一来源）
    if (!json.contains(QStringLiteral("params")) && json.contains(QStringLiteral("subFlowName"))) {
        setParam(QStringLiteral("subFlowName"), json.value(QStringLiteral("subFlowName")).toVariant());
    }
}
