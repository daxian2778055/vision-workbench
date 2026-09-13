#include "ConditionalNode.h"
#include "DataObject.h"
#include <QVBoxLayout>
#include <QLabel>

ConditionalNode::ConditionalNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("条件判断"));
    m_type = LOGIC;
}

void ConditionalNode::init()
{
    HalconNode::init();
    // 端口结构：0=图像入/出（兼容旧项目），1=条件输入（Bool/Number，可选），
    // 1=TRUE 分支输出，2=FALSE 分支输出
    addInputPort(QStringLiteral("条件"), PortDataType::Any);
    addOutputPort(QStringLiteral("TRUE 分支"), PortDataType::Image);
    addOutputPort(QStringLiteral("FALSE 分支"), PortDataType::Image);

    registerParams({
        makeBoolParam(QStringLiteral("condition"), true,
                      QStringLiteral("条件结果（无输入数据时使用）")),
    });
    m_params[QStringLiteral("conditionResult")] = true;
}

void ConditionalNode::run(bool /*autoSwitch*/)
{
    m_hasEvaluatedFlag = true;

    // 条件判定：优先使用输入端口 1（Number/Bool 数据），否则使用参数
    bool cond = m_params.value(QStringLiteral("condition"), true).toBool();
    QSharedPointer<DataObject> in = getInputData(1);
    if (in) {
        const QVariant v = in->getData();
        if (v.typeId() == QMetaType::Bool) {
            cond = v.toBool();
        } else if (v.canConvert<double>()) {
            cond = v.toDouble() != 0.0;
        }
    }
    m_conditionResult = cond;
    m_params[QStringLiteral("conditionResult")] = cond;
    m_params["moduleStatus"] = true;

    // 图像透传：TRUE/FALSE 两个分支端口均输出当前图像，
    // 执行引擎按 conditionResult 决定激活哪一侧的下游节点
    m_outputImage = m_inputImage;
    if (m_outputImage.IsInitialized()) {
        auto outObj = QSharedPointer<DataObject>::create();
        outObj->setHImage(HalconCpp::HImage(m_outputImage));
        setOutputData(0, outObj);
        setOutputData(1, outObj);
        setOutputData(2, outObj);
    }
}

QWidget *ConditionalNode::createParamPanel()
{
    auto *panel = createAutoParamPanel();
    auto *layout = panel->findChild<QVBoxLayout *>();
    if (!layout) layout = new QVBoxLayout(panel);
    auto *tip = new QLabel(QStringLiteral(
        "<i>说明：条件为真时执行 TRUE 分支下游，为假时执行 FALSE 分支下游；"
        "未选中分支的下游节点将被跳过。条件可来自输入端口数据（非零为真）。</i>"));
    tip->setWordWrap(true);
    layout->addWidget(tip);
    layout->addStretch();
    return panel;
}

void ConditionalNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}
