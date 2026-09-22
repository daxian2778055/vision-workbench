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

    m_params[QStringLiteral("thresholdLow")] = 0.0;
    m_params[QStringLiteral("thresholdHigh")] = 100.0;
    m_params[QStringLiteral("nameLow")] = QStringLiteral("\u4F4E");
    m_params[QStringLiteral("nameMid")] = QStringLiteral("\u4E2D");
    m_params[QStringLiteral("nameHigh")] = QStringLiteral("\u9AD8");
}

bool ClassifyNode::process()
{
    run();
    return true;
}

void ClassifyNode::run(bool /*autoSwitch*/)
{
    // 参数唯一来源：本轮取一次（局部快照）——避免逐次加锁，并保证同一轮内阈值/名称一致
    const double thresholdLow = getParam(QStringLiteral("thresholdLow")).toDouble();
    const double thresholdHigh = getParam(QStringLiteral("thresholdHigh")).toDouble();
    const QString nameLow = getParam(QStringLiteral("nameLow")).toString();
    const QString nameMid = getParam(QStringLiteral("nameMid")).toString();
    const QString nameHigh = getParam(QStringLiteral("nameHigh")).toString();

    bool ok = false;
    double v = 0.0;
    auto data = getInputData(0);
    if (data) {
        v = data->getData().toDouble(&ok);
    }

    QString category;
    if (!ok) {
        category = nameMid;
    } else if (v < thresholdLow) {
        category = nameLow;
    } else if (v > thresholdHigh) {
        category = nameHigh;
    } else {
        category = nameMid;
    }

    auto obj = QSharedPointer<DataObject>::create(DataObject::DataType::String, QVariant(category));
    setOutputData(0, obj);
}

void ClassifyNode::setParam(const QString &name, const QVariant &value)
{
    // 只写参数表（基类加锁 + 校验），不再维护无锁成员镜像
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
    m_lowSpin->setValue(getParam(QStringLiteral("thresholdLow")).toDouble());
    layout->addWidget(m_lowSpin);

    layout->addWidget(new QLabel(QStringLiteral("\u9AD8\u9650\u9608\u503C:")));
    m_highSpin = new QDoubleSpinBox();
    m_highSpin->setObjectName(QStringLiteral("classifyHigh"));
    m_highSpin->setRange(-1e12, 1e12);
    m_highSpin->setDecimals(4);
    m_highSpin->setValue(getParam(QStringLiteral("thresholdHigh")).toDouble());
    layout->addWidget(m_highSpin);

    auto addNameRow = [layout](const QString &objName, const QString &label, const QString &value) {
        layout->addWidget(new QLabel(label));
        auto *edit = new QLineEdit();
        edit->setObjectName(objName);
        edit->setText(value);
        layout->addWidget(edit);
        return edit;
    };
    m_lowNameEdit  = addNameRow(QStringLiteral("classifyNameLow"),  QStringLiteral("\u4F4E\u5206\u7C7B\u540D:"),
                                getParam(QStringLiteral("nameLow")).toString());
    m_midNameEdit  = addNameRow(QStringLiteral("classifyNameMid"),  QStringLiteral("\u4E2D\u5206\u7C7B\u540D:"),
                                getParam(QStringLiteral("nameMid")).toString());
    m_highNameEdit = addNameRow(QStringLiteral("classifyNameHigh"), QStringLiteral("\u9AD8\u5206\u7C7B\u540D:"),
                                getParam(QStringLiteral("nameHigh")).toString());

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
        s->setValue(getParam(QStringLiteral("thresholdLow")).toDouble());
    }
    if (auto *s = panel->findChild<QDoubleSpinBox *>(QStringLiteral("classifyHigh"))) {
        QSignalBlocker b(s);
        s->setValue(getParam(QStringLiteral("thresholdHigh")).toDouble());
    }
    if (auto *e = panel->findChild<QLineEdit *>(QStringLiteral("classifyNameLow"))) {
        QSignalBlocker b(e);
        e->setText(getParam(QStringLiteral("nameLow")).toString());
    }
    if (auto *e = panel->findChild<QLineEdit *>(QStringLiteral("classifyNameMid"))) {
        QSignalBlocker b(e);
        e->setText(getParam(QStringLiteral("nameMid")).toString());
    }
    if (auto *e = panel->findChild<QLineEdit *>(QStringLiteral("classifyNameHigh"))) {
        QSignalBlocker b(e);
        e->setText(getParam(QStringLiteral("nameHigh")).toString());
    }
}

QJsonObject ClassifyNode::toJson() const
{
    QJsonObject obj = HalconNode::toJson();
    // 顶层键保留（老读取方兼容），值一律取自参数表（唯一来源）
    obj[QStringLiteral("thresholdLow")] = QJsonValue::fromVariant(getParam(QStringLiteral("thresholdLow")));
    obj[QStringLiteral("thresholdHigh")] = QJsonValue::fromVariant(getParam(QStringLiteral("thresholdHigh")));
    obj[QStringLiteral("nameLow")] = QJsonValue::fromVariant(getParam(QStringLiteral("nameLow")));
    obj[QStringLiteral("nameMid")] = QJsonValue::fromVariant(getParam(QStringLiteral("nameMid")));
    obj[QStringLiteral("nameHigh")] = QJsonValue::fromVariant(getParam(QStringLiteral("nameHigh")));
    return obj;
}

void ClassifyNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);   // 5 个参数由基类从 params 恢复（唯一来源）
    // 兼容更早方案：这些键曾只存在顶层（无 params 段）。
    // 旧实现带默认值（`.toDouble(m_thresholdLow)` 等）= 缺键保留原值，故用 contains 守卫。
    if (!json.contains(QStringLiteral("params"))) {
        if (json.contains(QStringLiteral("thresholdLow")))
            setParam(QStringLiteral("thresholdLow"),
                     json.value(QStringLiteral("thresholdLow")).toVariant());
        if (json.contains(QStringLiteral("thresholdHigh")))
            setParam(QStringLiteral("thresholdHigh"),
                     json.value(QStringLiteral("thresholdHigh")).toVariant());
        if (json.contains(QStringLiteral("nameLow")))
            setParam(QStringLiteral("nameLow"), json.value(QStringLiteral("nameLow")).toVariant());
        if (json.contains(QStringLiteral("nameMid")))
            setParam(QStringLiteral("nameMid"), json.value(QStringLiteral("nameMid")).toVariant());
        if (json.contains(QStringLiteral("nameHigh")))
            setParam(QStringLiteral("nameHigh"), json.value(QStringLiteral("nameHigh")).toVariant());
    }
}
