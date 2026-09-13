#include "FormatNode.h"
#include "DataObject.h"
#include "Port.h"
#include "PortDataType.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>

FormatNode::FormatNode(QObject *parent)
    : HalconNode(parent)
{
    setName(QStringLiteral("\u683C\u5F0F\u5316"));
    m_type = OUTPUT;
}

void FormatNode::init()
{
    addInputPort(QStringLiteral("\u8F93\u5165\u6570\u636E"), PortDataType::String);
    addOutputPort(QStringLiteral("\u683C\u5F0F\u5316\u7ED3\u679C"), PortDataType::String);

    m_params[QStringLiteral("template")] = QStringLiteral("OK\r\n");
    m_params[QStringLiteral("outputSuffix")] = QString();
    m_template = QStringLiteral("OK\r\n");
    m_outputSuffix = QString();
}

bool FormatNode::process()
{
    run();
    return true;
}

void FormatNode::run(bool /*autoSwitch*/)
{
    // 获取输入数据
    QString inputStr;
    auto inputData = getInputData(0);
    if (inputData) {
        QVariant var = inputData->getData();
        inputStr = var.toString();
    }

    if (m_template.isEmpty()) {
        // 无模板时直接透传
        if (!inputStr.isEmpty()) {
            auto obj = QSharedPointer<DataObject>::create();
            obj->setData(QVariant(inputStr));
            setOutputData(0, obj);
        }
        return;
    }

    QString output = m_template;

    // 如果输入是 JSON，尝试替换 {fieldName} 占位符
    if (!inputStr.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(inputStr.toUtf8());
        if (doc.isObject()) {
            QJsonObject jsonObj = doc.object();
            // 替换 {name} 为对应的 JSON 字段值
            QRegularExpression re(QStringLiteral("\\{([^}]+)\\}"));
            QRegularExpressionMatchIterator it = re.globalMatch(output);
            // 从后往前替换，保持偏移正确
            QList<QPair<int, int>> replacements; // pos, len
            QList<QString> values;
            while (it.hasNext()) {
                QRegularExpressionMatch match = it.next();
                QString fieldName = match.captured(1).trimmed();
                QString replacement;
                if (jsonObj.contains(fieldName)) {
                    QJsonValue val = jsonObj[fieldName];
                    if (val.isDouble()) {
                        double d = val.toDouble();
                        if (d == static_cast<int>(d))
                            replacement = QString::number(static_cast<int>(d));
                        else
                            replacement = QString::number(d, 'f', 6);
                    } else if (val.isString()) {
                        replacement = val.toString();
                    } else {
                        replacement = QString::fromUtf8(QJsonDocument(QJsonObject{{fieldName, val}}).toJson(QJsonDocument::Compact));
                    }
                    // Remove trailing zeros for floats
                    if (replacement.contains('.')) {
                        while (replacement.endsWith('0')) replacement.chop(1);
                        if (replacement.endsWith('.')) replacement.chop(1);
                    }
                } else {
                    replacement = QString();
                }
                output.replace(match.capturedStart(), match.capturedLength(), replacement);
            }
        }
    }

    output += m_outputSuffix;

    auto obj = QSharedPointer<DataObject>::create();
    obj->setData(QVariant(output));
    setOutputData(0, obj);
}

void FormatNode::setParam(const QString &name, const QVariant &value)
{
    if (name == QStringLiteral("template")) {
        m_template = value.toString();
    } else if (name == QStringLiteral("outputSuffix")) {
        m_outputSuffix = value.toString();
    }
    HalconNode::setParam(name, value);
}

QVariant FormatNode::getParam(const QString &name) const
{
    return HalconNode::getParam(name);
}

QWidget *FormatNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>\u683C\u5F0F\u5316</b>")));

    auto *infoLabel = new QLabel(QStringLiteral(
        "\u5C06\u8F93\u5165\u7684 JSON \u6570\u636E\u6309\u6A21\u677F\u683C\u5F0F\u5316\u4E3A\u5B57\u7B26\u4E32\u8F93\u51FA\u3002\n"
        "\u652F\u6301 {fieldName} \u5360\u4F4D\u7B26\u66FF\u6362\uFF0C\u793A\u4F8B\u6A21\u677F:\n"
        "\"OK,{X},{Y},{str}\\r\\n\""
    ));
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color:gray; font-size:11px;");
    layout->addWidget(infoLabel);

    layout->addWidget(new QLabel(QStringLiteral("\u683C\u5F0F\u6A21\u677F:")));
    m_templateEdit = new QTextEdit();
    m_templateEdit->setObjectName(QStringLiteral("formatTemplate"));
    m_templateEdit->setPlainText(m_template);
    m_templateEdit->setMinimumHeight(80);
    m_templateEdit->setPlaceholderText(QStringLiteral("\u4F8B: OK,{X},{Y},{str}\\r\\n"));
    layout->addWidget(m_templateEdit);

    connect(m_templateEdit, &QTextEdit::textChanged, this, [this]() {
        m_template = m_templateEdit->toPlainText();
        setParam(QStringLiteral("template"), m_template);
    });

    layout->addWidget(new QLabel(QStringLiteral("\u8F93\u51FA\u540E\u7F00:")));
    auto *suffixEdit = new QLineEdit();
    suffixEdit->setObjectName(QStringLiteral("formatSuffix"));
    suffixEdit->setText(m_outputSuffix);
    suffixEdit->setPlaceholderText(QStringLiteral("\u7A7A\u5219\u4E0D\u8FFD\u52A0"));
    layout->addWidget(suffixEdit);

    connect(suffixEdit, &QLineEdit::editingFinished, this, [this, suffixEdit]() {
        m_outputSuffix = suffixEdit->text();
        setParam(QStringLiteral("outputSuffix"), m_outputSuffix);
    });

    layout->addStretch();
    return panel;
}

void FormatNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    if (auto *edit = panel->findChild<QTextEdit *>(QStringLiteral("formatTemplate"))) {
        QSignalBlocker b(edit);
        edit->setPlainText(m_template);
    }
    if (auto *suffix = panel->findChild<QLineEdit *>(QStringLiteral("formatSuffix"))) {
        QSignalBlocker b(suffix);
        suffix->setText(m_outputSuffix);
    }
}

QJsonObject FormatNode::toJson() const
{
    QJsonObject obj = HalconNode::toJson();
    obj[QStringLiteral("template")] = m_template;
    obj[QStringLiteral("outputSuffix")] = m_outputSuffix;
    return obj;
}

void FormatNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);
    m_template = json[QStringLiteral("template")].toString();
    m_outputSuffix = json[QStringLiteral("outputSuffix")].toString();
    m_params[QStringLiteral("template")] = m_template;
    m_params[QStringLiteral("outputSuffix")] = m_outputSuffix;
}
