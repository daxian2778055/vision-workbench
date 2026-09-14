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

    m_params[QStringLiteral("delayMs")] = m_delayMs;
}

bool DelayNode::process()
{
    run();
    return true;
}

void DelayNode::run(bool /*autoSwitch*/)
{
    if (m_delayMs > 0) {
        // 可取消等待：若所属执行器已停止/暂停，立即唤醒返回（E4）
        if (FlowExecutor *exec = ownerExecutor()) {
            exec->interruptibleSleep(static_cast<int>(m_delayMs));
        } else {
            QThread::msleep(static_cast<unsigned long>(m_delayMs));
        }
    }
    // 数据透传
    auto input = getInputData(0);
    if (input) {
        auto out = QSharedPointer<DataObject>::create();
        out->setType(input->getType());
        out->setData(input->getData());
        setOutputData(0, out);
    }
}

void DelayNode::setParam(const QString &name, const QVariant &value)
{
    if (name == QStringLiteral("delayMs")) {
        m_delayMs = qMax(0, value.toInt());
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
    m_delaySpin->setValue(m_delayMs);
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
        spin->setValue(m_delayMs);
    }
}

QJsonObject DelayNode::toJson() const
{
    QJsonObject obj = HalconNode::toJson();
    obj[QStringLiteral("delayMs")] = m_delayMs;
    return obj;
}

void DelayNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);
    m_delayMs = json[QStringLiteral("delayMs")].toInt(m_delayMs);
    m_params[QStringLiteral("delayMs")] = m_delayMs;
}
