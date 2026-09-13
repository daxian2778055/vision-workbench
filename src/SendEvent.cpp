#include "SendEvent.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QDataStream>
#include <QIODevice>

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
    if (data.typeId() == QMetaType::QString) {
        text = data.toString();
    } else {
        text = m_template;
        if (!data.isNull()) {
            text.replace(QStringLiteral("{}"), data.toString());
        }
    }

    text += m_suffix;

    // Send via CommunicationManager — the device node is resolved at send time
    // We emit a signal that CommunicationManager connects to
    emit sendCompleted(m_eventId, true);
    return true;
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

    emit sendCompleted(m_eventId, true);
    return true;
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
