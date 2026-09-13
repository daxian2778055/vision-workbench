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
    if (name == QStringLiteral("deviceName")) {
        m_deviceName = value.toString();
    } else if (name == QStringLiteral("filterPattern")) {
        m_filterPattern = value.toString();
    }
    HalconNode::setParam(name, value);
}

QVariant ReceiveDataNode::getParam(const QString &name) const
{
    return HalconNode::getParam(name);
}

void ReceiveDataNode::onDataReceived(const QString &deviceName, const QByteArray &data)
{
    if (deviceName != m_deviceName) return;

    QString text = QString::fromUtf8(data).trimmed();
    if (text.isEmpty()) return;

    // 过滤前缀
    if (!m_filterPattern.isEmpty() && !text.startsWith(m_filterPattern)) return;

    // 如果设置了过滤前缀，剥去前缀部分
    QString outputData = text;
    if (!m_filterPattern.isEmpty()) {
        outputData = text.mid(m_filterPattern.length()).trimmed();
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
    filterEdit->setText(m_filterPattern);
    layout->addWidget(filterEdit);

    layout->addWidget(new QLabel(QStringLiteral("\u8BF4\u660E: \u63A5\u6536\u5230\u7ED1\u5B9A\u8BBE\u5907\u7684\u6570\u636E\u540E\uFF0C"
        "\u5728\u4E0B\u6B21\u6D41\u7A0B\u6267\u884C\u65F6\u5C06\u6570\u636E\u4F20\u9012\u5230\u8F93\u51FA\u7AEF\u53E3\u3002")));
    layout->addStretch();

    connect(m_deviceCombo, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        m_deviceName = text;
        setParam(QStringLiteral("deviceName"), text);
    });

    connect(filterEdit, &QLineEdit::editingFinished, this, [this, filterEdit]() {
        m_filterPattern = filterEdit->text();
        setParam(QStringLiteral("filterPattern"), m_filterPattern);
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
        } else if (!m_deviceName.isEmpty()) {
            int idx = combo->findText(m_deviceName);
            if (idx >= 0) combo->setCurrentIndex(idx);
        }
    }

    auto *filterEdit = panel->findChild<QLineEdit *>(QStringLiteral("receiveDataFilter"));
    if (filterEdit) {
        QSignalBlocker b(filterEdit);
        filterEdit->setText(m_filterPattern);
    }
}

QJsonObject ReceiveDataNode::toJson() const
{
    QJsonObject obj = HalconNode::toJson();
    obj[QStringLiteral("deviceName")] = m_deviceName;
    obj[QStringLiteral("filterPattern")] = m_filterPattern;
    return obj;
}

void ReceiveDataNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);
    m_deviceName = json[QStringLiteral("deviceName")].toString();
    m_filterPattern = json[QStringLiteral("filterPattern")].toString();
    m_params[QStringLiteral("deviceName")] = m_deviceName;
    m_params[QStringLiteral("filterPattern")] = m_filterPattern;
}
