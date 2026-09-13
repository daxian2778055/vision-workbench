#include "ProtocolParseNode.h"
#include "DataObject.h"
#include "Port.h"
#include "PortDataType.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

ProtocolParseNode::ProtocolParseNode(QObject *parent)
    : HalconNode(parent)
{
    setName(QStringLiteral("\u534F\u8BAE\u89E3\u6790"));
    m_type = IMAGE_PROCESSING;
}

void ProtocolParseNode::init()
{
    addInputPort(QStringLiteral("\u539F\u59CB\u6570\u636E"), PortDataType::String);
    addOutputPort(QStringLiteral("\u89E3\u6790\u7ED3\u679C"), PortDataType::String);

    m_params[QStringLiteral("delimiter")] = QStringLiteral(",");

    // 默认字段
    ParseFieldDef def;
    def.name = QStringLiteral("Field_0");
    def.type = QStringLiteral("string");
    def.index = 0;
    m_fields.append(def);
}

bool ProtocolParseNode::process()
{
    run();
    return true;
}

void ProtocolParseNode::run(bool /*autoSwitch*/)
{
    QString inputStr;
    auto inputData = getInputData(0);
    if (inputData) {
        QVariant var = inputData->getData();
        inputStr = var.toString();
    }
    if (inputStr.isEmpty()) {
        inputStr = m_params.value(QStringLiteral("lastInput")).toString();
    }
    if (inputStr.isEmpty()) return;

    // 按分隔符拆分
    QStringList parts = inputStr.split(m_delimiter, Qt::SkipEmptyParts);

    // 构建 JSON 输出
    QJsonObject jsonOut;
    for (const auto &field : m_fields) {
        if (field.index < 0 || field.index >= parts.size()) {
            // 默认值
            if (field.type == QStringLiteral("int")) jsonOut[field.name] = 0;
            else if (field.type == QStringLiteral("float")) jsonOut[field.name] = 0.0;
            else jsonOut[field.name] = QString();
            continue;
        }

        QString raw = parts[field.index].trimmed();
        if (field.type == QStringLiteral("int")) {
            bool ok = false;
            int val = raw.toInt(&ok);
            jsonOut[field.name] = ok ? val : 0;
        } else if (field.type == QStringLiteral("float")) {
            bool ok = false;
            double val = raw.toDouble(&ok);
            jsonOut[field.name] = ok ? val : 0.0;
        } else {
            jsonOut[field.name] = raw;
        }
    }

    QString jsonStr = QString::fromUtf8(QJsonDocument(jsonOut).toJson(QJsonDocument::Compact));

    auto obj = QSharedPointer<DataObject>::create();
    obj->setData(QVariant(jsonStr));
    setOutputData(0, obj);
}

void ProtocolParseNode::setParam(const QString &name, const QVariant &value)
{
    if (name == QStringLiteral("delimiter")) {
        m_delimiter = value.toString();
    } else if (name == QStringLiteral("lastInput")) {
        // just store
    }
    HalconNode::setParam(name, value);
}

QVariant ProtocolParseNode::getParam(const QString &name) const
{
    return HalconNode::getParam(name);
}

QWidget *ProtocolParseNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>\u534F\u8BAE\u89E3\u6790</b>")));

    // 说明
    auto *infoLabel = new QLabel(QStringLiteral(
        "\u6309\u5206\u9694\u7B26\u62C6\u5206\u5B57\u7B26\u4E32\uFF0C\u5C06\u5404\u5B57\u6BB5\u7EC4\u88C5\u4E3A JSON \u8F93\u51FA\u3002\n"
        "\u793A\u4F8B: \u8F93\u5165 \"100,3.14,OK\" \u2192 \u8F93\u51FA {\"X\":100,\"Y\":3.14,\"str\":\"OK\"}"
    ));
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color:gray; font-size:11px;");
    layout->addWidget(infoLabel);

    // 分隔符
    layout->addWidget(new QLabel(QStringLiteral("\u5206\u9694\u7B26:")));
    m_delimiterEdit = new QLineEdit();
    m_delimiterEdit->setObjectName(QStringLiteral("parseDelimiter"));
    m_delimiterEdit->setText(m_delimiter);
    m_delimiterEdit->setPlaceholderText(QStringLiteral("\u9ED8\u8BA4\u9017\u53F7 ,"));
    layout->addWidget(m_delimiterEdit);

    connect(m_delimiterEdit, &QLineEdit::editingFinished, this, [this]() {
        m_delimiter = m_delimiterEdit->text();
        if (m_delimiter.isEmpty()) m_delimiter = QStringLiteral(",");
        setParam(QStringLiteral("delimiter"), m_delimiter);
    });

    // 字段定义表
    layout->addWidget(new QLabel(QStringLiteral("<b>\u5B57\u6BB5\u5B9A\u4E49</b>")));
    m_fieldTable = new QTableWidget();
    m_fieldTable->setObjectName(QStringLiteral("parseFieldTable"));
    m_fieldTable->setColumnCount(3);
    m_fieldTable->setHorizontalHeaderLabels({
        QStringLiteral("\u5B57\u6BB5\u540D"),
        QStringLiteral("\u7C7B\u578B"),
        QStringLiteral("\u7D22\u5F15")
    });
    m_fieldTable->horizontalHeader()->setStretchLastSection(true);
    m_fieldTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fieldTable->setMinimumHeight(120);
    layout->addWidget(m_fieldTable);

    auto *btnLayout = new QHBoxLayout();
    auto *addFieldBtn = new QPushButton(QStringLiteral("\u6DFB\u52A0\u5B57\u6BB5"));
    auto *removeFieldBtn = new QPushButton(QStringLiteral("\u5220\u9664"));
    auto *applyBtn = new QPushButton(QStringLiteral("\u5E94\u7528"));
    btnLayout->addWidget(addFieldBtn);
    btnLayout->addWidget(removeFieldBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(applyBtn);
    layout->addLayout(btnLayout);

    refreshFieldTable();

    // 添加字段
    connect(addFieldBtn, &QPushButton::clicked, this, [this]() {
        int row = m_fieldTable->rowCount();
        m_fieldTable->insertRow(row);
        m_fieldTable->setItem(row, 0, new QTableWidgetItem(QStringLiteral("Field_%1").arg(row)));
        auto *typeCombo = new QComboBox();
        typeCombo->addItems({
            QStringLiteral("string"),
            QStringLiteral("int"),
            QStringLiteral("float")
        });
        m_fieldTable->setCellWidget(row, 1, typeCombo);
        m_fieldTable->setItem(row, 2, new QTableWidgetItem(QString::number(row)));
    });

    // 删除字段
    connect(removeFieldBtn, &QPushButton::clicked, this, [this]() {
        int row = m_fieldTable->currentRow();
        if (row >= 0) m_fieldTable->removeRow(row);
    });

    // 应用
    connect(applyBtn, &QPushButton::clicked, this, [this]() {
        rebuildFieldsFromTable();
    });

    layout->addStretch();
    return panel;
}

void ProtocolParseNode::refreshFieldTable()
{
    if (!m_fieldTable) return;
    m_fieldTable->setRowCount(0);
    for (const auto &f : m_fields) {
        int row = m_fieldTable->rowCount();
        m_fieldTable->insertRow(row);
        m_fieldTable->setItem(row, 0, new QTableWidgetItem(f.name));
        auto *typeCombo = new QComboBox();
        typeCombo->addItems({QStringLiteral("string"), QStringLiteral("int"), QStringLiteral("float")});
        int typeIdx = 0;
        if (f.type == QStringLiteral("int")) typeIdx = 1;
        else if (f.type == QStringLiteral("float")) typeIdx = 2;
        typeCombo->setCurrentIndex(typeIdx);
        m_fieldTable->setCellWidget(row, 1, typeCombo);
        m_fieldTable->setItem(row, 2, new QTableWidgetItem(QString::number(f.index)));
    }
}

void ProtocolParseNode::rebuildFieldsFromTable()
{
    m_fields.clear();
    if (!m_fieldTable) return;

    for (int row = 0; row < m_fieldTable->rowCount(); ++row) {
        ParseFieldDef f;
        auto *nameItem = m_fieldTable->item(row, 0);
        f.name = nameItem ? nameItem->text() : QStringLiteral("Field_%1").arg(row);
        auto *typeCombo = qobject_cast<QComboBox *>(m_fieldTable->cellWidget(row, 1));
        f.type = typeCombo ? typeCombo->currentText() : QStringLiteral("string");
        auto *idxItem = m_fieldTable->item(row, 2);
        f.index = idxItem ? idxItem->text().toInt() : row;
        m_fields.append(f);
    }

    if (m_fields.isEmpty()) {
        ParseFieldDef d;
        d.name = QStringLiteral("Field_0");
        d.type = QStringLiteral("string");
        d.index = 0;
        m_fields.append(d);
    }
}

void ProtocolParseNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    if (auto *edit = panel->findChild<QLineEdit *>(QStringLiteral("parseDelimiter"))) {
        QSignalBlocker b(edit);
        edit->setText(m_delimiter);
    }
}

QJsonObject ProtocolParseNode::toJson() const
{
    QJsonObject obj = HalconNode::toJson();
    obj[QStringLiteral("delimiter")] = m_delimiter;
    QJsonArray fieldsArr;
    for (const auto &f : m_fields) {
        QJsonObject fo;
        fo[QStringLiteral("name")] = f.name;
        fo[QStringLiteral("type")] = f.type;
        fo[QStringLiteral("index")] = f.index;
        fieldsArr.append(fo);
    }
    obj[QStringLiteral("fieldDefs")] = fieldsArr;
    return obj;
}

void ProtocolParseNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);
    m_delimiter = json[QStringLiteral("delimiter")].toString(QStringLiteral(","));
    m_fields.clear();
    QJsonArray fieldsArr = json[QStringLiteral("fieldDefs")].toArray();
    for (const auto &v : fieldsArr) {
        QJsonObject fo = v.toObject();
        ParseFieldDef f;
        f.name = fo[QStringLiteral("name")].toString();
        f.type = fo[QStringLiteral("type")].toString(QStringLiteral("string"));
        f.index = fo[QStringLiteral("index")].toInt();
        m_fields.append(f);
    }
    if (m_fields.isEmpty()) {
        ParseFieldDef d;
        d.name = QStringLiteral("Field_0");
        d.type = QStringLiteral("string");
        d.index = 0;
        m_fields.append(d);
    }
    m_params[QStringLiteral("delimiter")] = m_delimiter;
}
