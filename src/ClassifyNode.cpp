#include "ClassifyNode.h"
#include "DataObject.h"
#include "Port.h"
#include "PortDataType.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QSignalBlocker>

ClassifyNode::ClassifyNode(QObject *parent)
    : HalconNode(parent)
{
    setName(QStringLiteral("\u6570\u636E\u5206\u7C7B"));
    m_type = LOGIC;
}

void ClassifyNode::init()
{
    addInputPort(QStringLiteral("\u6570\u503C\u8F93\u5165"), PortDataType::Number);
    addOutputPort(QStringLiteral("\u5206\u7C7B\u7ED3\u679C"), PortDataType::String);

    m_params[QStringLiteral("thresholdLow")] = m_thresholdLow;
    m_params[QStringLiteral("thresholdHigh")] = m_thresholdHigh;
    m_params[QStringLiteral("nameLow")] = m_nameLow;
    m_params[QStringLiteral("nameMid")] = m_nameMid;
    m_params[QStringLiteral("nameHigh")] = m_nameHigh;
}

bool ClassifyNode::process()
{
    run();
    return true;
}

void ClassifyNode::run(bool /*autoSwitch*/)
{
    bool ok = false;
    double v = 0.0;
    auto data = getInputData(0);
    if (data) {
        v = data->getData().toDouble(&ok);
    }

    QString category;
    if (!ok) {
        category = m_nameMid;
    } else if (v < m_thresholdLow) {
        category = m_nameLow;
    } else if (v > m_thresholdHigh) {
        category = m_nameHigh;
    } else {
        category = m_nameMid;
    }

    auto obj = QSharedPointer<DataObject>::create(DataObject::DataType::String, QVariant(category));
    setOutputData(0, obj);
}

void ClassifyNode::setParam(const QString &name, const QVariant &value)
{
    if (name == QStringLiteral("thresholdLow")) {
        m_thresholdLow = value.toDouble();
    } else if (name == QStringLiteral("thresholdHigh")) {
        m_thresholdHigh = value.toDouble();
    } else if (name == QStringLiteral("nameLow")) {
        m_nameLow = value.toString();
    } else if (name == QStringLiteral("nameMid")) {
        m_nameMid = value.toString();
    } else if (name == QStringLiteral("nameHigh")) {
        m_nameHigh = value.toString();
    }
    HalconNode::setParam(name, value);
}

QVariant ClassifyNode::getParam(const QString &name) const
{
    return HalconNode::getParam(name);
}

QWidget *ClassifyNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>\u6570\u636E\u5206\u7C7B</b>")));
    layout->addWidget(new QLabel(QStringLiteral("\u5C06\u8F93\u5165\u6570\u503C\u6309\u95F4\u533A\u5206\u7C7B\u4E3A\u4F4E/\u4E2D/\u9AD8\u3002")));

    layout->addWidget(new QLabel(QStringLiteral("\u4F4E\u9650\u9608\u503C:")));
    m_lowSpin = new QDoubleSpinBox();
    m_lowSpin->setObjectName(QStringLiteral("classifyLow"));
    m_lowSpin->setRange(-1e12, 1e12);
    m_lowSpin->setDecimals(4);
    m_lowSpin->setValue(m_thresholdLow);
    layout->addWidget(m_lowSpin);

    layout->addWidget(new QLabel(QStringLiteral("\u9AD8\u9650\u9608\u503C:")));
    m_highSpin = new QDoubleSpinBox();
    m_highSpin->setObjectName(QStringLiteral("classifyHigh"));
    m_highSpin->setRange(-1e12, 1e12);
    m_highSpin->setDecimals(4);
    m_highSpin->setValue(m_thresholdHigh);
    layout->addWidget(m_highSpin);

    auto addNameRow = [layout](const QString &objName, const QString &label, const QString &value) {
        layout->addWidget(new QLabel(label));
        auto *edit = new QLineEdit();
        edit->setObjectName(objName);
        edit->setText(value);
        layout->addWidget(edit);
        return edit;
    };
    m_lowNameEdit  = addNameRow(QStringLiteral("classifyNameLow"),  QStringLiteral("\u4F4E\u5206\u7C7B\u540D:"), m_nameLow);
    m_midNameEdit  = addNameRow(QStringLiteral("classifyNameMid"),  QStringLiteral("\u4E2D\u5206\u7C7B\u540D:"), m_nameMid);
    m_highNameEdit = addNameRow(QStringLiteral("classifyNameHigh"), QStringLiteral("\u9AD8\u5206\u7C7B\u540D:"), m_nameHigh);

    connect(m_lowSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        setParam(QStringLiteral("thresholdLow"), v);
    });
    connect(m_highSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        setParam(QStringLiteral("thresholdHigh"), v);
    });
    connect(m_lowNameEdit, &QLineEdit::editingFinished, this, [this]() {
        setParam(QStringLiteral("nameLow"), m_lowNameEdit->text());
    });
    connect(m_midNameEdit, &QLineEdit::editingFinished, this, [this]() {
        setParam(QStringLiteral("nameMid"), m_midNameEdit->text());
    });
    connect(m_highNameEdit, &QLineEdit::editingFinished, this, [this]() {
        setParam(QStringLiteral("nameHigh"), m_highNameEdit->text());
    });

    layout->addStretch();
    return panel;
}

void ClassifyNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    if (auto *s = panel->findChild<QDoubleSpinBox *>(QStringLiteral("classifyLow"))) {
        QSignalBlocker b(s);
        s->setValue(m_thresholdLow);
    }
    if (auto *s = panel->findChild<QDoubleSpinBox *>(QStringLiteral("classifyHigh"))) {
        QSignalBlocker b(s);
        s->setValue(m_thresholdHigh);
    }
    if (auto *e = panel->findChild<QLineEdit *>(QStringLiteral("classifyNameLow"))) {
        QSignalBlocker b(e);
        e->setText(m_nameLow);
    }
    if (auto *e = panel->findChild<QLineEdit *>(QStringLiteral("classifyNameMid"))) {
        QSignalBlocker b(e);
        e->setText(m_nameMid);
    }
    if (auto *e = panel->findChild<QLineEdit *>(QStringLiteral("classifyNameHigh"))) {
        QSignalBlocker b(e);
        e->setText(m_nameHigh);
    }
}

QJsonObject ClassifyNode::toJson() const
{
    QJsonObject obj = HalconNode::toJson();
    obj[QStringLiteral("thresholdLow")] = m_thresholdLow;
    obj[QStringLiteral("thresholdHigh")] = m_thresholdHigh;
    obj[QStringLiteral("nameLow")] = m_nameLow;
    obj[QStringLiteral("nameMid")] = m_nameMid;
    obj[QStringLiteral("nameHigh")] = m_nameHigh;
    return obj;
}

void ClassifyNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);
    m_thresholdLow  = json[QStringLiteral("thresholdLow")].toDouble(m_thresholdLow);
    m_thresholdHigh = json[QStringLiteral("thresholdHigh")].toDouble(m_thresholdHigh);
    m_nameLow  = json[QStringLiteral("nameLow")].toString(m_nameLow);
    m_nameMid  = json[QStringLiteral("nameMid")].toString(m_nameMid);
    m_nameHigh = json[QStringLiteral("nameHigh")].toString(m_nameHigh);
    m_params[QStringLiteral("thresholdLow")] = m_thresholdLow;
    m_params[QStringLiteral("thresholdHigh")] = m_thresholdHigh;
    m_params[QStringLiteral("nameLow")] = m_nameLow;
    m_params[QStringLiteral("nameMid")] = m_nameMid;
    m_params[QStringLiteral("nameHigh")] = m_nameHigh;
}
