#include "SendDataNode.h"
#include "CommunicationManager.h"
#include "DataObject.h"
#include "Port.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QLineEdit>
#include <QCheckBox>

SendDataNode::SendDataNode(QObject *parent)
    : HalconNode(parent)
{
    setName(QStringLiteral("\u53D1\u9001\u6570\u636E"));
    m_type = OUTPUT;
}

void SendDataNode::init()
{
    // 不调用 HalconNode::init() — 发送数据算子不处理图像
    // 添加一个 String 类型的输入端口，用于接收要发送的数据
    addInputPort(QStringLiteral("data"), PortDataType::String);

    m_params[QStringLiteral("deviceName")] = QString();
    m_params[QStringLiteral("suffix")] = QStringLiteral("\r\n");
    m_suffix = QStringLiteral("\r\n");
}

bool SendDataNode::process()
{
    // 发送数据算子：不处理图像。回写是它唯一的工作，因此 process 的返回值必须反映
    // 「是否真的发出去了」——以前无条件返回 true，PLC 没收到也显示成功。
    run();
    return m_lastSendOk;
}

void SendDataNode::run(bool /*autoSwitch*/)
{
    m_lastSendOk = doSend();
    m_executionSuccess = m_lastSendOk;
}

bool SendDataNode::doSend()
{
    if (m_deviceName.isEmpty()) {
        qWarning() << QStringLiteral("SendDataNode: 未绑定通信设备，数据未发送");
        return false;
    }

    // 从输入端口获取数据
    QString dataStr;
    auto inputData = getInputData(0);
    if (inputData) {
        QVariant var = inputData->getData();
        if (var.typeId() == QMetaType::QString) {
            dataStr = var.toString();
        }
    }

    if (dataStr.isEmpty()) {
        // 尝试从 params 中获取缓存的文本
        dataStr = m_params.value(QStringLiteral("sendText"), QString()).toString();
    }

    if (dataStr.isEmpty()) {
        qWarning() << QStringLiteral("SendDataNode: 无待发送数据（输入端口为空且未设置 sendText），数据未发送");
        return false;
    }

    // 追加后缀
    dataStr += m_suffix;

    // 通过 CommunicationManager 发送
    auto *cm = CommunicationManager::instance();
    if (!cm->hasDevice(m_deviceName)) {
        // 设备名写错 / 设备被删除：以前静默返回，现场表现为"流程全过、PLC 什么都没收到"
        qWarning() << QStringLiteral("SendDataNode: 设备不存在，数据未发送：") << m_deviceName;
        return false;
    }
    if (!cm->sendData(m_deviceName, dataStr.toUtf8())) {
        // 设备存在但未连接或投递失败：以前返回值被丢弃，同样没有任何痕迹
        qWarning() << QStringLiteral("SendDataNode: 发送失败（未连接或投递失败）：") << m_deviceName;
        return false;
    }
    return true;
}

void SendDataNode::setParam(const QString &name, const QVariant &value)
{
    if (name == QStringLiteral("deviceName")) {
        m_deviceName = value.toString();
    } else if (name == QStringLiteral("suffix")) {
        m_suffix = value.toString();
    }
    HalconNode::setParam(name, value);
}

QVariant SendDataNode::getParam(const QString &name) const
{
    return HalconNode::getParam(name);
}

QWidget *SendDataNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>\u53D1\u9001\u6570\u636E</b>")));

    layout->addWidget(new QLabel(QStringLiteral("\u7ED1\u5B9A\u8BBE\u5907:")));
    m_deviceCombo = new QComboBox();
    m_deviceCombo->setObjectName(QStringLiteral("sendDataDeviceCombo"));
    layout->addWidget(m_deviceCombo);

    layout->addWidget(new QLabel(QStringLiteral("\u884C\u5C3E\u7F13\u51B2:")));
    auto *suffixEdit = new QLineEdit();
    suffixEdit->setObjectName(QStringLiteral("sendDataSuffix"));
    suffixEdit->setText(m_suffix);
    suffixEdit->setToolTip(QStringLiteral("\u6BCF\u6B21\u53D1\u9001\u65F6\u81EA\u52A8\u8FFD\u52A0\u7684\u540E\u7F00\uFF0C\u9ED8\u8BA4 \r\n"));
    layout->addWidget(suffixEdit);

    layout->addWidget(new QLabel(QStringLiteral("\u8BF4\u660E: \u4ECE\u8F93\u5165\u7AEF\u53E3\u63A5\u6536\u6570\u636E\uFF0C"
        "\u901A\u8FC7\u7ED1\u5B9A\u8BBE\u5907\u53D1\u9001\u5230\u5916\u90E8\u3002")));
    layout->addStretch();

    connect(m_deviceCombo, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        m_deviceName = text;
        setParam(QStringLiteral("deviceName"), text);
    });

    connect(suffixEdit, &QLineEdit::editingFinished, this, [this, suffixEdit]() {
        m_suffix = suffixEdit->text();
        setParam(QStringLiteral("suffix"), m_suffix);
    });

    return panel;
}

void SendDataNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;

    auto *combo = panel->findChild<QComboBox *>(QStringLiteral("sendDataDeviceCombo"));
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

    auto *suffixEdit = panel->findChild<QLineEdit *>(QStringLiteral("sendDataSuffix"));
    if (suffixEdit) {
        QSignalBlocker b(suffixEdit);
        suffixEdit->setText(m_suffix);
    }
}

QJsonObject SendDataNode::toJson() const
{
    QJsonObject obj = HalconNode::toJson();
    obj[QStringLiteral("deviceName")] = m_deviceName;
    obj[QStringLiteral("suffix")] = m_suffix;
    return obj;
}

void SendDataNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);
    m_deviceName = json[QStringLiteral("deviceName")].toString();
    m_suffix = json[QStringLiteral("suffix")].toString(QStringLiteral("\r\n"));
    m_params[QStringLiteral("deviceName")] = m_deviceName;
    m_params[QStringLiteral("suffix")] = m_suffix;
}
