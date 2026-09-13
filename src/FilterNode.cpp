#include "FilterNode.h"
#include "DataObject.h"
#include "Port.h"
#include "PortDataType.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QSignalBlocker>

FilterNode::FilterNode(QObject *parent)
    : HalconNode(parent)
{
    setName(QStringLiteral("\u6570\u636E\u7B5B\u9009"));
    m_type = LOGIC;
}

void FilterNode::init()
{
    addInputPort(QStringLiteral("\u6570\u503C\u8F93\u5165"), PortDataType::Number);
    addOutputPort(QStringLiteral("\u7B5B\u9009\u7ED3\u679C"), PortDataType::Bool);

    m_params[QStringLiteral("operator")] = m_operator;
    m_params[QStringLiteral("threshold")] = m_threshold;
}

bool FilterNode::process()
{
    run();
    return true;
}

void FilterNode::run(bool /*autoSwitch*/)
{
    bool ok = false;
    double v = 0.0;
    auto data = getInputData(0);
    if (data) {
        v = data->getData().toDouble(&ok);
    }

    bool passed = false;
    if (ok) {
        if (m_operator == QStringLiteral(">="))      passed = v >= m_threshold;
        else if (m_operator == QStringLiteral("<=")) passed = v <= m_threshold;
        else if (m_operator == QStringLiteral("==")) passed = qFuzzyCompare(v, m_threshold);
        else if (m_operator == QStringLiteral("!=")) passed = !qFuzzyCompare(v, m_threshold);
        else if (m_operator == QStringLiteral(">"))  passed = v > m_threshold;
        else if (m_operator == QStringLiteral("<"))  passed = v < m_threshold;
    }

    auto obj = QSharedPointer<DataObject>::create(DataObject::DataType::Bool, QVariant(passed));
    setOutputData(0, obj);
}

void FilterNode::setParam(const QString &name, const QVariant &value)
{
    if (name == QStringLiteral("operator")) {
        m_operator = value.toString();
    } else if (name == QStringLiteral("threshold")) {
        m_threshold = value.toDouble();
    }
    HalconNode::setParam(name, value);
}

QVariant FilterNode::getParam(const QString &name) const
{
    return HalconNode::getParam(name);
}

QWidget *FilterNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>\u6570\u636E\u7B5B\u9009</b>")));
    layout->addWidget(new QLabel(QStringLiteral("\u8F93\u5165\u6570\u503C\u4E0E\u9608\u503C\u6BD4\u8F83\uFF0C\u6EE1\u8DB3\u6761\u4EF6\u8F93\u51FA true\u3002")));

    layout->addWidget(new QLabel(QStringLiteral("\u6BD4\u8F83\u7B26:")));
    m_opCombo = new QComboBox();
    m_opCombo->setObjectName(QStringLiteral("filterOp"));
    m_opCombo->addItems({QStringLiteral(">="), QStringLiteral("<="), QStringLiteral("=="),
                         QStringLiteral("!="), QStringLiteral(">"), QStringLiteral("<")});
    m_opCombo->setCurrentText(m_operator);
    layout->addWidget(m_opCombo);

    layout->addWidget(new QLabel(QStringLiteral("\u9608\u503C:")));
    m_thresholdSpin = new QDoubleSpinBox();
    m_thresholdSpin->setObjectName(QStringLiteral("filterThreshold"));
    m_thresholdSpin->setRange(-1e12, 1e12);
    m_thresholdSpin->setDecimals(6);
    m_thresholdSpin->setValue(m_threshold);
    layout->addWidget(m_thresholdSpin);

    connect(m_opCombo, &QComboBox::currentTextChanged, this, [this](const QString &t) {
        setParam(QStringLiteral("operator"), t);
    });
    connect(m_thresholdSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        setParam(QStringLiteral("threshold"), v);
    });

    layout->addStretch();
    return panel;
}

void FilterNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    if (auto *c = panel->findChild<QComboBox *>(QStringLiteral("filterOp"))) {
        QSignalBlocker b(c);
        c->setCurrentText(m_operator);
    }
    if (auto *s = panel->findChild<QDoubleSpinBox *>(QStringLiteral("filterThreshold"))) {
        QSignalBlocker b(s);
        s->setValue(m_threshold);
    }
}

QJsonObject FilterNode::toJson() const
{
    QJsonObject obj = HalconNode::toJson();
    obj[QStringLiteral("operator")] = m_operator;
    obj[QStringLiteral("threshold")] = m_threshold;
    return obj;
}

void FilterNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);
    m_operator = json[QStringLiteral("operator")].toString(m_operator);
    m_threshold = json[QStringLiteral("threshold")].toDouble(m_threshold);
    m_params[QStringLiteral("operator")] = m_operator;
    m_params[QStringLiteral("threshold")] = m_threshold;
}
