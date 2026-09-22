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

    m_params[QStringLiteral("operator")] = QStringLiteral(">=");   // >= <= == > < !=
    m_params[QStringLiteral("threshold")] = 0.0;
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

    // 参数唯一来源：本轮开始各取一次（局部快照）——既避免逐次加锁，也保证同一轮内判据一致
    const QString op = getParam(QStringLiteral("operator")).toString();
    const double threshold = getParam(QStringLiteral("threshold")).toDouble();

    bool passed = false;
    if (ok) {
        if (op == QStringLiteral(">="))      passed = v >= threshold;
        else if (op == QStringLiteral("<=")) passed = v <= threshold;
        else if (op == QStringLiteral("==")) passed = qFuzzyCompare(v, threshold);
        else if (op == QStringLiteral("!=")) passed = !qFuzzyCompare(v, threshold);
        else if (op == QStringLiteral(">"))  passed = v > threshold;
        else if (op == QStringLiteral("<"))  passed = v < threshold;
    }

    auto obj = QSharedPointer<DataObject>::create(DataObject::DataType::Bool, QVariant(passed));
    setOutputData(0, obj);
}

void FilterNode::setParam(const QString &name, const QVariant &value)
{
    // 只写参数表（基类负责加锁 + 校验），不再维护成员镜像
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
    m_opCombo->setCurrentText(getParam(QStringLiteral("operator")).toString());
    layout->addWidget(m_opCombo);

    layout->addWidget(new QLabel(QStringLiteral("\u9608\u503C:")));
    m_thresholdSpin = new QDoubleSpinBox();
    m_thresholdSpin->setObjectName(QStringLiteral("filterThreshold"));
    m_thresholdSpin->setRange(-1e12, 1e12);
    m_thresholdSpin->setDecimals(6);
    m_thresholdSpin->setValue(getParam(QStringLiteral("threshold")).toDouble());
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
        c->setCurrentText(getParam(QStringLiteral("operator")).toString());
    }
    if (auto *s = panel->findChild<QDoubleSpinBox *>(QStringLiteral("filterThreshold"))) {
        QSignalBlocker b(s);
        s->setValue(getParam(QStringLiteral("threshold")).toDouble());
    }
}

QJsonObject FilterNode::toJson() const
{
    QJsonObject obj = HalconNode::toJson();
    // 顶层键保留（老读取方兼容），值一律取自参数表（唯一来源）
    obj[QStringLiteral("operator")] = QJsonValue::fromVariant(getParam(QStringLiteral("operator")));
    obj[QStringLiteral("threshold")] = QJsonValue::fromVariant(getParam(QStringLiteral("threshold")));
    return obj;
}

void FilterNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);   // operator/threshold 由基类从 params 恢复（唯一来源）
    // 兼容更早的方案格式：这两个键曾只存在顶层（无 params 段）
    if (!json.contains(QStringLiteral("params"))) {
        if (json.contains(QStringLiteral("operator")))
            setParam(QStringLiteral("operator"),
                     json.value(QStringLiteral("operator")).toVariant());
        if (json.contains(QStringLiteral("threshold")))
            setParam(QStringLiteral("threshold"),
                     json.value(QStringLiteral("threshold")).toVariant());
    }
}
