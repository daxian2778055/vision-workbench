#include "CounterNode.h"
#include "DataObject.h"
#include "Port.h"
#include "PortDataType.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QSignalBlocker>

CounterNode::CounterNode(QObject *parent)
    : HalconNode(parent)
{
    setName(QStringLiteral("\u6761\u4EF6\u8BA1\u6570"));
    m_type = LOGIC;
}

void CounterNode::init()
{
    addInputPort(QStringLiteral("\u6761\u4EF6"), PortDataType::Number);
    addOutputPort(QStringLiteral("\u7D2F\u8BA1\u6B21\u6570"), PortDataType::Number);

    m_params[QStringLiteral("conditionMode")] = m_conditionMode;
    m_params[QStringLiteral("threshold")] = m_threshold;
}

bool CounterNode::process()
{
    run();
    return true;
}

void CounterNode::run(bool /*autoSwitch*/)
{
    bool condition = false;
    auto data = getInputData(0);
    if (data) {
        QVariant var = data->getData();
        if (m_conditionMode == QStringLiteral("number")) {
            bool ok = false;
            double v = var.toDouble(&ok);
            condition = ok && (v >= m_threshold);
        } else {
            // bool 模式：输入布尔或非零数值
            condition = var.toBool();
            if (var.canConvert<double>() && var.typeId() != QMetaType::Bool) {
                bool ok = false;
                double v = var.toDouble(&ok);
                if (ok) condition = v != 0.0;
            }
        }
    }

    if (condition) {
        ++m_count;
    }

    auto obj = QSharedPointer<DataObject>::create(DataObject::DataType::Number, QVariant(m_count));
    setOutputData(0, obj);
}

void CounterNode::setParam(const QString &name, const QVariant &value)
{
    if (name == QStringLiteral("conditionMode")) {
        m_conditionMode = value.toString();
    } else if (name == QStringLiteral("threshold")) {
        m_threshold = value.toDouble();
    }
    HalconNode::setParam(name, value);
}

QVariant CounterNode::getParam(const QString &name) const
{
    return HalconNode::getParam(name);
}

QWidget *CounterNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>\u6761\u4EF6\u8BA1\u6570</b>")));
    layout->addWidget(new QLabel(QStringLiteral("\u6761\u4EF6\u6A21\u5F0F:")));

    m_modeCombo = new QComboBox();
    m_modeCombo->setObjectName(QStringLiteral("counterMode"));
    m_modeCombo->addItem(QStringLiteral("bool (\u8F93\u5165\u975E\u96F6\u5373\u8BA1\u6570)"), QStringLiteral("bool"));
    m_modeCombo->addItem(QStringLiteral("number (\u8F93\u5165\u2265\u9608\u503C\u5373\u8BA1\u6570)"), QStringLiteral("number"));
    int idx = m_conditionMode == QStringLiteral("number") ? 1 : 0;
    m_modeCombo->setCurrentIndex(idx);
    layout->addWidget(m_modeCombo);

    layout->addWidget(new QLabel(QStringLiteral("\u9608\u503C:")));
    m_thresholdSpin = new QDoubleSpinBox();
    m_thresholdSpin->setObjectName(QStringLiteral("counterThreshold"));
    m_thresholdSpin->setRange(-1e12, 1e12);
    m_thresholdSpin->setDecimals(6);
    m_thresholdSpin->setValue(m_threshold);
    layout->addWidget(m_thresholdSpin);

    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        m_conditionMode = m_modeCombo->itemData(i).toString();
        setParam(QStringLiteral("conditionMode"), m_conditionMode);
    });
    connect(m_thresholdSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        setParam(QStringLiteral("threshold"), v);
    });

    layout->addStretch();
    return panel;
}

void CounterNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    if (auto *combo = panel->findChild<QComboBox *>(QStringLiteral("counterMode"))) {
        QSignalBlocker b(combo);
        combo->setCurrentIndex(m_conditionMode == QStringLiteral("number") ? 1 : 0);
    }
    if (auto *spin = panel->findChild<QDoubleSpinBox *>(QStringLiteral("counterThreshold"))) {
        QSignalBlocker b(spin);
        spin->setValue(m_threshold);
    }
}

QJsonObject CounterNode::toJson() const
{
    QJsonObject obj = HalconNode::toJson();
    obj[QStringLiteral("conditionMode")] = m_conditionMode;
    obj[QStringLiteral("threshold")] = m_threshold;
    return obj;
}

void CounterNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);
    m_conditionMode = json[QStringLiteral("conditionMode")].toString(m_conditionMode);
    m_threshold = json[QStringLiteral("threshold")].toDouble(m_threshold);
    m_params[QStringLiteral("conditionMode")] = m_conditionMode;
    m_params[QStringLiteral("threshold")] = m_threshold;
}
