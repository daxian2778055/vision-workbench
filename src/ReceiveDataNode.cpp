#include "ReceiveDataNode.h"
#include "CommunicationManager.h"
#include "DataObject.h"
#include "Port.h"
#include "PortDataType.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QLineEdit>

ReceiveDataNode::ReceiveDataNode(QObject *parent)
    : HalconNode(parent)
{
    setName(QStringLiteral("\u63A5\u6536\u6570\u636E"));
    m_type = OUTPUT;
}

void ReceiveDataNode::init()
{
    // 不调用 HalconNode::init() — 接收数据算子不处理图像
    addInputPort(QStringLiteral("触发"), PortDataType::Any);
    addOutputPort(QStringLiteral("数据"), PortDataType::String);

    m_params[QStringLiteral("deviceName")] = QString();
    m_params[QStringLiteral("filterPattern")] = QString();
    m_params[QStringLiteral("lastData")] = QString();

    // 连接 CommunicationManager 的 dataReceived 信号
    if (auto *cm = CommunicationManager::instance()) {
        connect(cm, &CommunicationManager::dataReceived,
                this, &ReceiveDataNode::onDataReceived);
    }
}

bool ReceiveDataNode::process()
{
    // 接收数据算子：不处理图像，只传递缓存数据
    run();
    return true;
}

void ReceiveDataNode::run(bool /*autoSwitch*/)
{
    // run 时不做实际读取 — 数据已在 onDataReceived 中缓存
    // 将缓存数据放到输出
    QString lastData = m_params.value(QStringLiteral("lastData")).toString();
    if (!lastData.isEmpty()) {
        auto dataObj = QSharedPointer<DataObject>::create();
        dataObj->setData(QVariant(lastData));
        setOutputData(0, dataObj);
    }
}

void ReceiveDataNode::setParam(const QString &name, const QVariant &value)
{
    // 只写参数表（基类加锁 + 校验），不再维护无锁成员镜像
    HalconNode::setParam(name, value);
}

QVariant ReceiveDataNode::getParam(const QString &name) const
{
    return HalconNode::getParam(name);
}

void ReceiveDataNode::onDataReceived(const QString &deviceName, const QByteArray &data)
{
    // 参数唯一来源：本次回调开始各取一次（局部快照）。
    // 本函数由 CommunicationManager 的 dataReceived 信号驱动（主线程），而参数可能被执行线程写
    // （如参数引用写回 setParam），原先直接读成员是无保护跨线程读；参数表自带锁。
    // 另外 filterPattern 被用于"判前缀 + 按长度剥离"两处，必须取自同一份快照，
    // 否则中途被改会出现"按新前缀判过、却按旧前缀长度剥离"的错位。
    const QString boundDevice = getParam(QStringLiteral("deviceName")).toString();
    const QString filterPattern = getParam(QStringLiteral("filterPattern")).toString();

    if (deviceName != boundDevice) return;

    QString text = QString::fromUtf8(data).trimmed();
    if (text.isEmpty()) return;

    // 过滤前缀
    if (!filterPattern.isEmpty() && !text.startsWith(filterPattern)) return;

    // 如果设置了过滤前缀，剥去前缀部分
    QString outputData = text;
    if (!filterPattern.isEmpty()) {
        outputData = text.mid(filterPattern.length()).trimmed();
    }

    m_params[QStringLiteral("lastData")] = outputData;
}

QWidget *ReceiveDataNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>\u63A5\u6536\u6570\u636E</b>")));

    layout->addWidget(new QLabel(QStringLiteral("\u7ED1\u5B9A\u8BBE\u5907:")));
    m_deviceCombo = new QComboBox();
    m_deviceCombo->setObjectName(QStringLiteral("receiveDataDeviceCombo"));
    layout->addWidget(m_deviceCombo);

    layout->addWidget(new QLabel(QStringLiteral("\u8FC7\u6EE4\u524D\u7F00(\u53EF\u7A7A):")));
    auto *filterEdit = new QLineEdit();
    filterEdit->setObjectName(QStringLiteral("receiveDataFilter"));
    filterEdit->setPlaceholderText(QStringLiteral("\u7A7A\u5219\u63A5\u6536\u5168\u90E8\u6570\u636E"));
    filterEdit->setText(getParam(QStringLiteral("filterPattern")).toString());
    layout->addWidget(filterEdit);

    layout->addWidget(new QLabel(QStringLiteral("\u8BF4\u660E: \u63A5\u6536\u5230\u7ED1\u5B9A\u8BBE\u5907\u7684\u6570\u636E\u540E\uFF0C"
        "\u5728\u4E0B\u6B21\u6D41\u7A0B\u6267\u884C\u65F6\u5C06\u6570\u636E\u4F20\u9012\u5230\u8F93\u51FA\u7AEF\u53E3\u3002")));
    layout->addStretch();

    connect(m_deviceCombo, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        setParam(QStringLiteral("deviceName"), text);
    });

    connect(filterEdit, &QLineEdit::editingFinished, this, [this, filterEdit]() {
        setParam(QStringLiteral("filterPattern"), filterEdit->text());
    });

    return panel;
}

void ReceiveDataNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;

    auto *combo = panel->findChild<QComboBox *>(QStringLiteral("receiveDataDeviceCombo"));
    if (combo) {
        QSignalBlocker b(combo);
        QString current = combo->currentText();
        combo->clear();
        QStringList devices = CommunicationManager::instance()->deviceNames();
        combo->addItems(devices);
        if (!current.isEmpty()) {
            int idx = combo->findText(current);
            if (idx >= 0) combo->setCurrentIndex(idx);
        } else if (!getParam(QStringLiteral("deviceName")).toString().isEmpty()) {
            int idx = combo->findText(getParam(QStringLiteral("deviceName")).toString());
            if (idx >= 0) combo->setCurrentIndex(idx);
        }
    }

    auto *filterEdit = panel->findChild<QLineEdit *>(QStringLiteral("receiveDataFilter"));
    if (filterEdit) {
        QSignalBlocker b(filterEdit);
        filterEdit->setText(getParam(QStringLiteral("filterPattern")).toString());
    }
}

QJsonObject ReceiveDataNode::toJson() const
{
    QJsonObject obj = HalconNode::toJson();
    // 顶层键保留（老读取方兼容），值一律取自参数表（唯一来源）
    obj[QStringLiteral("deviceName")] = QJsonValue::fromVariant(getParam(QStringLiteral("deviceName")));
    obj[QStringLiteral("filterPattern")] =
        QJsonValue::fromVariant(getParam(QStringLiteral("filterPattern")));
    return obj;
}

void ReceiveDataNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);   // deviceName/filterPattern 由基类从 params 恢复（唯一来源）
    // 兼容更早方案：两个键曾只存在顶层（无 params 段）。旧实现均为**无默认值**的 toString()
    // → 键缺失/非字符串 ⇒ 空串（清掉绑定/前缀），故此处同样无条件重置（isString 保住"显式空串"语义）。
    if (!json.contains(QStringLiteral("params"))) {
        const QJsonValue dv = json.value(QStringLiteral("deviceName"));
        setParam(QStringLiteral("deviceName"), dv.isString() ? dv.toString() : QString());
        const QJsonValue fv = json.value(QStringLiteral("filterPattern"));
        setParam(QStringLiteral("filterPattern"), fv.isString() ? fv.toString() : QString());
    }
}
