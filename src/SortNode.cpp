#include "SortNode.h"
#include "DataObject.h"
#include "Port.h"
#include "PortDataType.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <algorithm>

SortNode::SortNode(QObject *parent)
    : HalconNode(parent)
{
    setName(QStringLiteral("\u6570\u636E\u6392\u5E8F"));
    m_type = LOGIC;
}

void SortNode::init()
{
    addInputPort(QStringLiteral("\u6570\u7EC4\u8F93\u5165"), PortDataType::Array);
    addOutputPort(QStringLiteral("\u6392\u5E8F\u7ED3\u679C"), PortDataType::Array);

    m_params[QStringLiteral("order")] = QStringLiteral("asc");   // asc / desc
}

bool SortNode::process()
{
    run();
    return true;
}

void SortNode::run(bool /*autoSwitch*/)
{
    QVector<double> values;
    auto data = getInputData(0);
    if (data) {
        QVariant var = data->getData();
        if (var.canConvert<QVector<double>>()) {
            values = var.value<QVector<double>>();
        } else if (var.canConvert<QList<double>>()) {
            QList<double> list = var.value<QList<double>>();
            values = list.toVector();
        } else if (var.canConvert<QStringList>()) {
            QStringList list = var.toStringList();
            for (const QString &s : list) {
                bool ok = false;
                double v = s.toDouble(&ok);
                if (ok) values.append(v);
            }
        }
    }

    // 参数唯一来源：本轮取一次（局部快照）
    const QString order = getParam(QStringLiteral("order")).toString();

    std::sort(values.begin(), values.end());
    if (order == QStringLiteral("desc")) {
        std::reverse(values.begin(), values.end());
    }

    auto obj = QSharedPointer<DataObject>::create(DataObject::DataType::Array, QVariant::fromValue(values));
    setOutputData(0, obj);
}

void SortNode::setParam(const QString &name, const QVariant &value)
{
    // 只写参数表（基类加锁 + 校验），不再维护无锁成员镜像
    HalconNode::setParam(name, value);
}

QVariant SortNode::getParam(const QString &name) const
{
    return HalconNode::getParam(name);
}

QWidget *SortNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>\u6570\u636E\u6392\u5E8F</b>")));
    layout->addWidget(new QLabel(QStringLiteral("\u5C06\u8F93\u5165\u6570\u7EC4\u6392\u5E8F\u540E\u8F93\u51FA\u3002")));

    layout->addWidget(new QLabel(QStringLiteral("\u6392\u5E8F\u65B9\u5F0F:")));
    m_orderCombo = new QComboBox();
    m_orderCombo->setObjectName(QStringLiteral("sortOrder"));
    m_orderCombo->addItem(QStringLiteral("\u5347\u5E8F"), QStringLiteral("asc"));
    m_orderCombo->addItem(QStringLiteral("\u964D\u5E8F"), QStringLiteral("desc"));
    m_orderCombo->setCurrentIndex(
        getParam(QStringLiteral("order")).toString() == QStringLiteral("desc") ? 1 : 0);
    layout->addWidget(m_orderCombo);

    connect(m_orderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        setParam(QStringLiteral("order"), m_orderCombo->itemData(i).toString());
    });

    layout->addStretch();
    return panel;
}

void SortNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    if (auto *c = panel->findChild<QComboBox *>(QStringLiteral("sortOrder"))) {
        QSignalBlocker b(c);
        c->setCurrentIndex(
            getParam(QStringLiteral("order")).toString() == QStringLiteral("desc") ? 1 : 0);
    }
}

QJsonObject SortNode::toJson() const
{
    QJsonObject obj = HalconNode::toJson();
    obj[QStringLiteral("order")] = QJsonValue::fromVariant(getParam(QStringLiteral("order")));
    return obj;
}

void SortNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);   // order 由基类从 params 恢复（唯一来源）
    // 兼容更早方案：该键曾只存在顶层（无 params 段）
    if (!json.contains(QStringLiteral("params")) && json.contains(QStringLiteral("order")))
        setParam(QStringLiteral("order"), json.value(QStringLiteral("order")).toVariant());
}
