#include "SendEvent.h"
#include "CommunicationManager.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QDataStream>
#include <QIODevice>
#include <QRegularExpression>
#include <QVariantMap>

// ==================== SendEvent Base ====================

SendEvent::SendEvent(const QString &id, const QString &deviceName,
                     SendType type, QObject *parent)
    : QObject(parent), m_eventId(id), m_deviceName(deviceName), m_sendType(type)
{
}

QJsonObject SendEvent::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = m_eventId;
    obj[QStringLiteral("deviceName")] = m_deviceName;
    obj[QStringLiteral("type")] = static_cast<int>(m_sendType);
    obj[QStringLiteral("enabled")] = m_enabled;
    return obj;
}

void SendEvent::fromJson(const QJsonObject &json)
{
    m_eventId = json[QStringLiteral("id")].toString();
    m_deviceName = json[QStringLiteral("deviceName")].toString();
    m_sendType = static_cast<SendType>(json[QStringLiteral("type")].toInt());
    m_enabled = json[QStringLiteral("enabled")].toBool(true);
}

// ==================== TextDirectSendEvent ====================

TextDirectSendEvent::TextDirectSendEvent(const QString &id, const QString &deviceName,
                                           QObject *parent)
    : SendEvent(id, deviceName, TEXT_DIRECT, parent)
{
}

bool TextDirectSendEvent::send(const QVariant &data)
{
    if (!m_enabled) return false;

    QString text;
    if (data.typeId() == QMetaType::QVariantMap) {
        // 多字段命名占位符：{模块号.参数名} / {global.变量名}（与流程"变量引用"同语法）。
        // 每轮结束时由 MainWindow 注入本轮结果，模板如 "{1.结果},{global.计数}".
        const QVariantMap map = data.toMap();
        QString out = m_template;
        static const QRegularExpression re(QStringLiteral("\\{([^{}]+)\\}"));
        QList<QPair<QString, QString>> pairs;
        auto it = re.globalMatch(m_template);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const QString key = m.captured(1);
            if (map.contains(key))
                pairs.append({m.captured(0), map.value(key).toString()});
        }
        for (const auto &p : pairs)
            out.replace(p.first, p.second);
        text = out;
    } else if (data.typeId() == QMetaType::QString) {
        text = data.toString();
    } else {
        text = m_template;
        if (!data.isNull()) {
            text.replace(QStringLiteral("{}"), data.toString());
        }
    }

    text += m_suffix;

    // 真正发送：此前这里只 emit 了一个无人接收的 sendCompleted（注释写着
    // "We emit a signal that CommunicationManager connects to"，但没有任何接收者），
    // 且全仓库无人调用 send() ⇒ 配好的"发送事件"永远不会把数据发出去。
    auto *cm = CommunicationManager::instance();
    const bool ok = cm->sendData(m_deviceName, text.toUtf8());
    if (!ok) {
        qWarning() << QStringLiteral("TextDirectSendEvent: 发送失败（设备不存在或未连接）：")
                   << m_eventId << m_deviceName;
    }
    emit sendCompleted(m_eventId, ok);
    return ok;
}

QJsonObject TextDirectSendEvent::toJson() const
{
    QJsonObject obj = SendEvent::toJson();
    obj[QStringLiteral("template")] = m_template;
    obj[QStringLiteral("suffix")] = m_suffix;
    return obj;
}

void TextDirectSendEvent::fromJson(const QJsonObject &json)
{
    SendEvent::fromJson(json);
    m_template = json[QStringLiteral("template")].toString();
    m_suffix = json[QStringLiteral("suffix")].toString(QStringLiteral("\n"));
}

// ==================== BytePackSendEvent ====================

BytePackSendEvent::BytePackSendEvent(const QString &id, const QString &deviceName,
                                       QObject *parent)
    : SendEvent(id, deviceName, BYTE_PACK, parent)
{
}

void BytePackSendEvent::addField(const BytePackField &field)
{
    m_fields.append(field);
}

void BytePackSendEvent::clearFields()
{
    m_fields.clear();
}

QByteArray BytePackSendEvent::packData(const QVariant &source) const
{
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    for (const auto &field : m_fields) {
        QVariant value = field.fixedValue.isValid() ? field.fixedValue : source;

        if (field.dataType == QStringLiteral("int16")) {
            stream << static_cast<qint16>(value.toInt());
        } else if (field.dataType == QStringLiteral("int32")) {
            stream << static_cast<qint32>(value.toInt());
        } else if (field.dataType == QStringLiteral("float")) {
            stream << static_cast<float>(value.toDouble());
        } else if (field.dataType == QStringLiteral("byte")) {
            result.append(static_cast<char>(value.toInt()));
        }
    }

    return result;
}

bool BytePackSendEvent::send(const QVariant &data)
{
    if (!m_enabled) return false;

    QByteArray packed = packData(data);
    if (packed.isEmpty()) return false;

    // 同 TextDirectSendEvent：此前只 emit 信号，数据从未真正发出
    auto *cm = CommunicationManager::instance();
    const bool ok = cm->sendData(m_deviceName, packed);
    if (!ok) {
        qWarning() << QStringLiteral("BytePackSendEvent: 发送失败（设备不存在或未连接）：")
                   << m_eventId << m_deviceName;
    }
    emit sendCompleted(m_eventId, ok);
    return ok;
}

QJsonObject BytePackSendEvent::toJson() const
{
    QJsonObject obj = SendEvent::toJson();
    QJsonArray fieldsArr;
    for (const auto &f : m_fields) {
        QJsonObject fObj;
        fObj[QStringLiteral("offset")] = f.offset;
        fObj[QStringLiteral("length")] = f.length;
        fObj[QStringLiteral("dataType")] = f.dataType;
        if (f.fixedValue.isValid()) {
            fObj[QStringLiteral("fixedValue")] = QJsonValue::fromVariant(f.fixedValue);
        }
        fieldsArr.append(fObj);
    }
    obj[QStringLiteral("fields")] = fieldsArr;
    return obj;
}

void BytePackSendEvent::fromJson(const QJsonObject &json)
{
    SendEvent::fromJson(json);
    m_fields.clear();
    QJsonArray fieldsArr = json[QStringLiteral("fields")].toArray();
    for (const auto &v : fieldsArr) {
        QJsonObject fObj = v.toObject();
        BytePackField f;
        f.offset = fObj[QStringLiteral("offset")].toInt();
        f.length = fObj[QStringLiteral("length")].toInt(2);
        f.dataType = fObj[QStringLiteral("dataType")].toString(QStringLiteral("int16"));
        if (fObj.contains(QStringLiteral("fixedValue"))) {
            f.fixedValue = fObj[QStringLiteral("fixedValue")].toVariant();
        }
        m_fields.append(f);
    }
}
