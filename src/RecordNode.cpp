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

    m_params[QStringLiteral("flowName")] = QStringLiteral("\u6D41\u7A0B 1");
    m_params[QStringLiteral("nodeName")] = QStringLiteral("\u6570\u636E\u8BB0\u5F55");
    m_params[QStringLiteral("passed")] = true;
}

bool RecordNode::process()
{
    // W-2：空载不得绿灯——结果必须由「检测结果」端口算出，统一走基类数据端口契约
    return processDataOutputs();
}

void RecordNode::run(bool /*autoSwitch*/)
{
    // 参数唯一来源：本轮各取一次（局部快照）——避免逐次加锁，并保证"写入数据库的值"与"输出端口的值"一致
    const QString flowName = getParam(QStringLiteral("flowName")).toString();
    const QString nodeName = getParam(QStringLiteral("nodeName")).toString();
    const bool passed = getParam(QStringLiteral("passed")).toBool();

    QString valueStr;
    auto data = getInputData(0);
    if (data) {
        QVariant var = data->getData();
        if (var.canConvert<QString>()) {
            valueStr = var.toString();
        }
    }

    // 写入数据库
    AppDatabase::instance()->saveInspectionResult(flowName, nodeName, passed, valueStr);

    auto obj = QSharedPointer<DataObject>::create(DataObject::DataType::Bool, QVariant(passed));
    setOutputData(0, obj);
}

void RecordNode::setParam(const QString &name, const QVariant &value)
{
    // 只写参数表（基类加锁 + 校验），不再维护无锁成员镜像
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
    m_flowNameEdit->setText(getParam(QStringLiteral("flowName")).toString());
    layout->addWidget(m_flowNameEdit);

    layout->addWidget(new QLabel(QStringLiteral("\u8282\u70B9\u540D:")));
    m_nodeNameEdit = new QLineEdit();
    m_nodeNameEdit->setObjectName(QStringLiteral("recordNode"));
    m_nodeNameEdit->setText(getParam(QStringLiteral("nodeName")).toString());
    layout->addWidget(m_nodeNameEdit);

    m_passedCheck = new QCheckBox(QStringLiteral("\u901A\u8FC7"));
    m_passedCheck->setObjectName(QStringLiteral("recordPassed"));
    m_passedCheck->setChecked(getParam(QStringLiteral("passed")).toBool());
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
        e->setText(getParam(QStringLiteral("flowName")).toString());
    }
    if (auto *e = panel->findChild<QLineEdit *>(QStringLiteral("recordNode"))) {
        QSignalBlocker b(e);
        e->setText(getParam(QStringLiteral("nodeName")).toString());
    }
    if (auto *cb = panel->findChild<QCheckBox *>(QStringLiteral("recordPassed"))) {
        QSignalBlocker b(cb);
        cb->setChecked(getParam(QStringLiteral("passed")).toBool());
    }
}

QJsonObject RecordNode::toJson() const
{
    QJsonObject obj = HalconNode::toJson();
    // 顶层键保留（老读取方兼容），值一律取自参数表（唯一来源）
    obj[QStringLiteral("flowName")] = QJsonValue::fromVariant(getParam(QStringLiteral("flowName")));
    obj[QStringLiteral("nodeName")] = QJsonValue::fromVariant(getParam(QStringLiteral("nodeName")));
    obj[QStringLiteral("passed")] = QJsonValue::fromVariant(getParam(QStringLiteral("passed")));
    return obj;
}

void RecordNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);   // 3 个参数由基类从 params 恢复（唯一来源）
    // 兼容更早方案：三个键曾只存在顶层（无 params 段）。旧实现均带默认值
    // （`.toString(m_flowName)` / `.toBool(m_passed)` = 缺键保留原值），故用 contains 守卫。
    if (!json.contains(QStringLiteral("params"))) {
        if (json.contains(QStringLiteral("flowName")))
            setParam(QStringLiteral("flowName"), json.value(QStringLiteral("flowName")).toVariant());
        if (json.contains(QStringLiteral("nodeName")))
            setParam(QStringLiteral("nodeName"), json.value(QStringLiteral("nodeName")).toVariant());
        if (json.contains(QStringLiteral("passed")))
            setParam(QStringLiteral("passed"), json.value(QStringLiteral("passed")).toVariant());
    }
}
