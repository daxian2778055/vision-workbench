#include "RecordNode.h"
#include "DataObject.h"
#include "Port.h"
#include "PortDataType.h"
#include "AppDatabase.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QSignalBlocker>

RecordNode::RecordNode(QObject *parent)
    : HalconNode(parent)
{
    setName(QStringLiteral("\u6570\u636E\u8BB0\u5F55"));
    m_type = OUTPUT;
}

void RecordNode::init()
{
    addInputPort(QStringLiteral("\u68C0\u6D4B\u7ED3\u679C"), PortDataType::String);
    addOutputPort(QStringLiteral("\u901A\u8FC7\u72B6\u6001"), PortDataType::Bool);

    m_params[QStringLiteral("flowName")] = m_flowName;
    m_params[QStringLiteral("nodeName")] = m_nodeName;
    m_params[QStringLiteral("passed")] = m_passed;
}

bool RecordNode::process()
{
    run();
    return true;
}

void RecordNode::run(bool /*autoSwitch*/)
{
    QString valueStr;
    auto data = getInputData(0);
    if (data) {
        QVariant var = data->getData();
        if (var.canConvert<QString>()) {
            valueStr = var.toString();
        }
    }

    // 写入数据库
    AppDatabase::instance()->saveInspectionResult(m_flowName, m_nodeName, m_passed, valueStr);

    auto obj = QSharedPointer<DataObject>::create(DataObject::DataType::Bool, QVariant(m_passed));
    setOutputData(0, obj);
}

void RecordNode::setParam(const QString &name, const QVariant &value)
{
    if (name == QStringLiteral("flowName")) {
        m_flowName = value.toString();
    } else if (name == QStringLiteral("nodeName")) {
        m_nodeName = value.toString();
    } else if (name == QStringLiteral("passed")) {
        m_passed = value.toBool();
    }
    HalconNode::setParam(name, value);
}

QVariant RecordNode::getParam(const QString &name) const
{
    return HalconNode::getParam(name);
}

QWidget *RecordNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>\u6570\u636E\u8BB0\u5F55</b>")));
    layout->addWidget(new QLabel(QStringLiteral("\u5C06\u8F93\u5165\u7684\u68C0\u6D4B\u7ED3\u679C\u5199\u5165\u6570\u636E\u5E93\u3002")));

    layout->addWidget(new QLabel(QStringLiteral("\u6D41\u7A0B\u540D:")));
    m_flowNameEdit = new QLineEdit();
    m_flowNameEdit->setObjectName(QStringLiteral("recordFlow"));
    m_flowNameEdit->setText(m_flowName);
    layout->addWidget(m_flowNameEdit);

    layout->addWidget(new QLabel(QStringLiteral("\u8282\u70B9\u540D:")));
    m_nodeNameEdit = new QLineEdit();
    m_nodeNameEdit->setObjectName(QStringLiteral("recordNode"));
    m_nodeNameEdit->setText(m_nodeName);
    layout->addWidget(m_nodeNameEdit);

    m_passedCheck = new QCheckBox(QStringLiteral("\u901A\u8FC7"));
    m_passedCheck->setObjectName(QStringLiteral("recordPassed"));
    m_passedCheck->setChecked(m_passed);
    layout->addWidget(m_passedCheck);

    connect(m_flowNameEdit, &QLineEdit::editingFinished, this, [this]() {
        setParam(QStringLiteral("flowName"), m_flowNameEdit->text());
    });
    connect(m_nodeNameEdit, &QLineEdit::editingFinished, this, [this]() {
        setParam(QStringLiteral("nodeName"), m_nodeNameEdit->text());
    });
    connect(m_passedCheck, &QCheckBox::toggled, this, [this](bool checked) {
        setParam(QStringLiteral("passed"), checked);
    });

    layout->addStretch();
    return panel;
}

void RecordNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    if (auto *e = panel->findChild<QLineEdit *>(QStringLiteral("recordFlow"))) {
        QSignalBlocker b(e);
        e->setText(m_flowName);
    }
    if (auto *e = panel->findChild<QLineEdit *>(QStringLiteral("recordNode"))) {
        QSignalBlocker b(e);
        e->setText(m_nodeName);
    }
    if (auto *cb = panel->findChild<QCheckBox *>(QStringLiteral("recordPassed"))) {
        QSignalBlocker b(cb);
        cb->setChecked(m_passed);
    }
}

QJsonObject RecordNode::toJson() const
{
    QJsonObject obj = HalconNode::toJson();
    obj[QStringLiteral("flowName")] = m_flowName;
    obj[QStringLiteral("nodeName")] = m_nodeName;
    obj[QStringLiteral("passed")] = m_passed;
    return obj;
}

void RecordNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);
    m_flowName = json[QStringLiteral("flowName")].toString(m_flowName);
    m_nodeName = json[QStringLiteral("nodeName")].toString(m_nodeName);
    m_passed = json[QStringLiteral("passed")].toBool(m_passed);
    m_params[QStringLiteral("flowName")] = m_flowName;
    m_params[QStringLiteral("nodeName")] = m_nodeName;
    m_params[QStringLiteral("passed")] = m_passed;
}
