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

namespace {

/// 字段定义列表在参数表里的存放形态：QVariantList<QVariantMap{name,type,index}>。
/// 下面两个转换是它与 QList<ParseFieldDef> 之间的唯一桥（读写两侧都走这里，格式只有一个来源）。
QVariantList fieldsToVariantList(const QList<ParseFieldDef> &fields)
{
    QVariantList list;
    list.reserve(fields.size());
    for (const ParseFieldDef &f : fields) {
        QVariantMap m;
        m.insert(QStringLiteral("name"), f.name);
        m.insert(QStringLiteral("type"), f.type);
        m.insert(QStringLiteral("index"), f.index);
        list.append(m);
    }
    return list;
}

QList<ParseFieldDef> variantListToFields(const QVariantList &list)
{
    QList<ParseFieldDef> fields;
    fields.reserve(list.size());
    for (const QVariant &v : list) {
        const QVariantMap m = v.toMap();
        ParseFieldDef f;
        f.name = m.value(QStringLiteral("name")).toString();
        // 注意：QVariant::toString() 无默认值重载（QJsonValue 才有）。
        // 用 isValid 判定与旧的 `QJsonValue::toString("string")` 语义一致：
        // 键缺失/非字符串 ⇒ "string"；键存在且显式为空串 ⇒ 保持空串。
        const QVariant typeVar = m.value(QStringLiteral("type"));
        f.type = typeVar.isValid() ? typeVar.toString() : QStringLiteral("string");
        f.index = m.value(QStringLiteral("index")).toInt();
        fields.append(f);
    }
    return fields;
}

/// 默认字段定义（与旧 init() 的初值一致）
QVariantList defaultFieldDefs()
{
    ParseFieldDef d;
    d.name = QStringLiteral("Field_0");
    d.type = QStringLiteral("string");
    d.index = 0;
    return fieldsToVariantList({d});
}

} // namespace

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
    m_params[QStringLiteral("fieldDefs")] = defaultFieldDefs();   // 默认一个字段（与旧 init 一致）
}

bool ProtocolParseNode::process()
{
    run();
    return true;
}

void ProtocolParseNode::run(bool /*autoSwitch*/)
{
    // 参数唯一来源：本轮各取一次（局部快照）。列表读到的是 QVariant 值拷贝，
    // 界面线程重建字段表只会替换参数值、不会就地改这个快照 —— 不存在"迭代中被 clear"的崩溃面。
    const QString delimiter = getParam(QStringLiteral("delimiter")).toString();
    const QList<ParseFieldDef> fields =
        variantListToFields(getParam(QStringLiteral("fieldDefs")).toList());

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
    QStringList parts = inputStr.split(delimiter, Qt::SkipEmptyParts);

    // 构建 JSON 输出
    QJsonObject jsonOut;
    for (const auto &field : fields) {
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
    // 只写参数表（基类加锁 + 校验）。原 "lastInput" 分支是空操作（基类本就会存），一并去掉。
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
    m_delimiterEdit->setText(getParam(QStringLiteral("delimiter")).toString());
    m_delimiterEdit->setPlaceholderText(QStringLiteral("\u9ED8\u8BA4\u9017\u53F7 ,"));
    layout->addWidget(m_delimiterEdit);

    connect(m_delimiterEdit, &QLineEdit::editingFinished, this, [this]() {
        QString d = m_delimiterEdit->text();
        if (d.isEmpty()) d = QStringLiteral(",");
        setParam(QStringLiteral("delimiter"), d);
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
    const QList<ParseFieldDef> fields =
        variantListToFields(getParam(QStringLiteral("fieldDefs")).toList());
    for (const auto &f : fields) {
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
    QList<ParseFieldDef> fields;
    if (m_fieldTable) {
        for (int row = 0; row < m_fieldTable->rowCount(); ++row) {
            ParseFieldDef f;
            auto *nameItem = m_fieldTable->item(row, 0);
            f.name = nameItem ? nameItem->text() : QStringLiteral("Field_%1").arg(row);
            auto *typeCombo = qobject_cast<QComboBox *>(m_fieldTable->cellWidget(row, 1));
            f.type = typeCombo ? typeCombo->currentText() : QStringLiteral("string");
            auto *idxItem = m_fieldTable->item(row, 2);
            f.index = idxItem ? idxItem->text().toInt() : row;
            fields.append(f);
        }
    }

    if (fields.isEmpty()) {
        ParseFieldDef d;
        d.name = QStringLiteral("Field_0");
        d.type = QStringLiteral("string");
        d.index = 0;
        fields.append(d);
    }

    // 写回参数表（唯一来源）——执行线程读到的是值拷贝，看不到这里的中间状态
    setParam(QStringLiteral("fieldDefs"), fieldsToVariantList(fields));
}

void ProtocolParseNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    if (auto *edit = panel->findChild<QLineEdit *>(QStringLiteral("parseDelimiter"))) {
        QSignalBlocker b(edit);
        edit->setText(getParam(QStringLiteral("delimiter")).toString());
    }
}

QJsonObject ProtocolParseNode::toJson() const
{
    QJsonObject obj = HalconNode::toJson();
    // 顶层键保留（老读取方兼容），值一律取自参数表（唯一来源）；
    // 列表参数在参数表里就是 QVariantList<QVariantMap>，fromVariant 直接给出同形的 JSON 数组。
    obj[QStringLiteral("delimiter")] = QJsonValue::fromVariant(getParam(QStringLiteral("delimiter")));
    obj[QStringLiteral("fieldDefs")] = QJsonValue::fromVariant(getParam(QStringLiteral("fieldDefs")));
    return obj;
}

void ProtocolParseNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);   // delimiter/fieldDefs 由基类从 params 恢复（唯一来源）
    // 兼容更早方案：两个键曾只存在顶层（无 params 段）。语义与旧实现逐字对齐：
    //  · delimiter：旧代码 `.toString(QStringLiteral(","))` → 键缺失或非字符串 ⇒ ","
    //    （用 isString 判定，以保住"键存在且显式为空串"这一情况的原语义）
    //  · fieldDefs：旧代码无条件"清空后重建" → 缺失或空数组 ⇒ 重置为默认单字段（不是保留原值）
    if (!json.contains(QStringLiteral("params"))) {
        const QJsonValue dv = json.value(QStringLiteral("delimiter"));
        setParam(QStringLiteral("delimiter"), dv.isString() ? dv.toString() : QStringLiteral(","));

        QVariantList list;
        const QJsonArray fieldsArr = json.value(QStringLiteral("fieldDefs")).toArray();
        for (const auto &v : fieldsArr) {
            const QJsonObject fo = v.toObject();
            QVariantMap m;
            m.insert(QStringLiteral("name"), fo[QStringLiteral("name")].toString());
            m.insert(QStringLiteral("type"),
                     fo[QStringLiteral("type")].toString(QStringLiteral("string")));
            m.insert(QStringLiteral("index"), fo[QStringLiteral("index")].toInt());
            list.append(m);
        }
        if (list.isEmpty())
            list = defaultFieldDefs();
        setParam(QStringLiteral("fieldDefs"), list);
    }
}
