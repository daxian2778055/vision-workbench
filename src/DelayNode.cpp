#include "DelayNode.h"
#include "DataObject.h"
#include "Port.h"
#include "PortDataType.h"
#include "FlowExecutor.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QThread>

DelayNode::DelayNode(QObject *parent)
    : HalconNode(parent)
{
    setName(QStringLiteral("\u5EF6\u65F6"));
    m_type = LOGIC;
}

void DelayNode::init()
{
    addInputPort(QStringLiteral("\u6570\u636E\u8F93\u5165"), PortDataType::Any);
    addOutputPort(QStringLiteral("\u6570\u636E\u8F93\u51FA"), PortDataType::Any);

    m_params[QStringLiteral("delayMs")] = 100;   // 默认值与旧成员初值一致（原先写在构造函数附近）
}

bool DelayNode::process()
{
    run();
    return true;
}

void DelayNode::run(bool /*autoSwitch*/)
{
    // 参数唯一来源：本轮取一次（局部快照）。写侧已把值钳到 ≥0（见 setParam），此处直接用。
    const int delayMs = getParam(QStringLiteral("delayMs")).toInt();
    if (delayMs > 0) {
        // 可取消等待：若所属执行器已停止/暂停，立即唤醒返回（E4）
        if (FlowExecutor *exec = ownerExecutor()) {
            exec->interruptibleSleep(delayMs);
        } else {
            QThread::msleep(static_cast<unsigned long>(delayMs));
        }
    }
    // 数据透传；无输入时清空输出，避免把上一轮结果留给下游（P1）
    auto input = getInputData(0);
    if (input) {
        auto out = QSharedPointer<DataObject>::create();
        out->setType(input->getType());
        out->setData(input->getData());
        setOutputData(0, out);
    } else {
        setOutputData(0, QSharedPointer<DataObject>());
    }
}

void DelayNode::setParam(const QString &name, const QVariant &value)
{
    // 只写参数表（基类加锁 + 校验）。钳制放**写侧**：旧实现是把**成员**钳到 ≥0、参数表存原值，
    // 于是 toJson / 面板展示的是"钳后值"；现在参数表直接存钳后值 ⇒ run / 面板 / toJson / fromJson 四处自然一致。
    if (name == QStringLiteral("delayMs")) {
        HalconNode::setParam(name, qMax(0, value.toInt()));
        return;
    }
    HalconNode::setParam(name, value);
}

QVariant DelayNode::getParam(const QString &name) const
{
    return HalconNode::getParam(name);
}

QWidget *DelayNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>\u5EF6\u65F6</b>")));
    layout->addWidget(new QLabel(QStringLiteral("\u5EF6\u65F6\u65F6\u95F4 (ms):")));

    m_delaySpin = new QSpinBox();
    m_delaySpin->setObjectName(QStringLiteral("delaySpin"));
    m_delaySpin->setRange(0, 3600000);
    m_delaySpin->setValue(getParam(QStringLiteral("delayMs")).toInt());
    m_delaySpin->setSuffix(QStringLiteral(" ms"));
    layout->addWidget(m_delaySpin);

    connect(m_delaySpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        setParam(QStringLiteral("delayMs"), v);
    });

    layout->addStretch();
    return panel;
}

void DelayNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    if (auto *spin = panel->findChild<QSpinBox *>(QStringLiteral("delaySpin"))) {
        QSignalBlocker b(spin);
        spin->setValue(getParam(QStringLiteral("delayMs")).toInt());
    }
}

QJsonObject DelayNode::toJson() const
{
    QJsonObject obj = HalconNode::toJson();
    // 顶层键保留（老读取方兼容），值取自参数表（唯一来源）
    obj[QStringLiteral("delayMs")] = QJsonValue::fromVariant(getParam(QStringLiteral("delayMs")));
    return obj;
}

void DelayNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);   // delayMs 由基类从 params 恢复（唯一来源）
    // 兼容更早方案：该键曾只存在顶层（无 params 段）。旧实现带默认值
    // （`.toInt(m_delayMs)` = 缺键**保留原值**），故用 contains 守卫。
    if (!json.contains(QStringLiteral("params")) && json.contains(QStringLiteral("delayMs"))) {
        setParam(QStringLiteral("delayMs"), json.value(QStringLiteral("delayMs")).toVariant());
    }
}
