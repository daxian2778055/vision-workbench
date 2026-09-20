#include "ReceiveEvent.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QtEndian>   // qFromBigEndian / qFromLittleEndian（字节匹配按配置的字节序解析）

// ==================== ReceiveEvent Base ====================

ReceiveEvent::ReceiveEvent(const QString &id, const QString &deviceName,
                           EventType type, QObject *parent)
    : QObject(parent), m_eventId(id), m_deviceName(deviceName), m_eventType(type)
{
}

QJsonObject ReceiveEvent::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = m_eventId;
    obj[QStringLiteral("deviceName")] = m_deviceName;
    obj[QStringLiteral("type")] = static_cast<int>(m_eventType);
    obj[QStringLiteral("enabled")] = m_enabled;
    return obj;
}

void ReceiveEvent::fromJson(const QJsonObject &json)
{
    m_eventId = json[QStringLiteral("id")].toString();
    m_deviceName = json[QStringLiteral("deviceName")].toString();
    m_eventType = static_cast<EventType>(json[QStringLiteral("type")].toInt());
    m_enabled = json[QStringLiteral("enabled")].toBool(true);
}

// ==================== TextProtocolReceiveEvent ====================

TextProtocolReceiveEvent::TextProtocolReceiveEvent(const QString &id, const QString &deviceName,
                                                     QObject *parent)
    : ReceiveEvent(id, deviceName, TEXT_PROTOCOL, parent)
{
}

bool TextProtocolReceiveEvent::parse(const QByteArray &data, QList<QVariant> &fields)
{
    if (!m_enabled || data.isEmpty()) return false;

    const QString text = QString::fromUtf8(data).trimmed();
    if (text.isEmpty()) return false;

    if (m_parseMode == Regex) {
        // 正则模式：全局匹配；有捕获组时各非空捕获组为一个字段，无捕获组时整体匹配为一个字段
        if (m_regex.isEmpty()) return false;
        const QRegularExpression re(m_regex);
        if (!re.isValid()) return false;
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const int n = m.lastCapturedIndex();
            if (n >= 1) {
                for (int gi = 1; gi <= n; ++gi) {
                    const QString cap = m.captured(gi);
                    if (!cap.isEmpty())
                        fields.append(cap);
                }
            } else {
                fields.append(m.captured(0));
            }
        }
        if (fields.isEmpty()) return false;
        emit eventGenerated(m_eventId, fields);
        return true;
    }

    const QStringList parts = text.split(m_delimiter, Qt::SkipEmptyParts);
    if (parts.isEmpty()) return false;

    for (const QString &part : parts) {
        fields.append(part.trimmed());
    }

    emit eventGenerated(m_eventId, fields);
    return true;
}

QJsonObject TextProtocolReceiveEvent::toJson() const
{
    QJsonObject obj = ReceiveEvent::toJson();
    obj[QStringLiteral("delimiter")] = m_delimiter;
    obj[QStringLiteral("parseMode")] = static_cast<int>(m_parseMode);
    obj[QStringLiteral("regex")] = m_regex;
    return obj;
}

void TextProtocolReceiveEvent::fromJson(const QJsonObject &json)
{
    ReceiveEvent::fromJson(json);
    m_delimiter = json[QStringLiteral("delimiter")].toString(QStringLiteral(","));
    m_parseMode = static_cast<ParseMode>(json[QStringLiteral("parseMode")].toInt(0));
    m_regex = json[QStringLiteral("regex")].toString();
}

// ==================== ByteMatchReceiveEvent ====================

ByteMatchReceiveEvent::ByteMatchReceiveEvent(const QString &id, const QString &deviceName,
                                               QObject *parent)
    : ReceiveEvent(id, deviceName, BYTE_MATCH, parent)
{
}

void ByteMatchReceiveEvent::addRule(const ByteMatchRule &rule)
{
    m_rules.append(rule);
}

void ByteMatchReceiveEvent::clearRules()
{
    m_rules.clear();
}

double ByteMatchReceiveEvent::extractValue(const QByteArray &data, const ByteMatchRule &rule) const
{
    if (rule.byteLength <= 0 || data.size() < rule.byteOffset + rule.byteLength) return 0;

    QByteArray chunk = data.mid(rule.byteOffset, rule.byteLength);
    const QString bo = rule.byteOrder;

    // 32 位值：先按"字序"把 chunk 规范到 ABCD（高字在前），再按大端解释。
    // 16 位值：按首字母定字节序（A… = 高字节在前；B…/D… = 低字节在前）。
    // 历史缺陷：只有 int16/int32/float 三个分支，且用 reinterpret_cast 按宿主序
    // （小端）读取——uint16 直接落到 return 0（永远不触发），大小端也与配置相反。
    if (chunk.size() >= 4 && (rule.dataType == QStringLiteral("int32")
                              || rule.dataType == QStringLiteral("uint32")
                              || rule.dataType == QStringLiteral("float"))) {
        if (bo == QStringLiteral("CDAB")) {
            char tmp = chunk[0]; chunk[0] = chunk[2]; chunk[2] = tmp;
            tmp = chunk[1]; chunk[1] = chunk[3]; chunk[3] = tmp;
        } else if (bo == QStringLiteral("DCBA")) {
            for (int i = 0; i < chunk.size() / 2; ++i) {
                const char tmp = chunk[i];
                chunk[i] = chunk[chunk.size() - 1 - i];
                chunk[chunk.size() - 1 - i] = tmp;
            }
        }
        const uchar *p = reinterpret_cast<const uchar *>(chunk.constData());
        if (rule.dataType == QStringLiteral("int32"))
            return static_cast<double>(static_cast<qint32>(qFromBigEndian<quint32>(p)));
        if (rule.dataType == QStringLiteral("uint32"))
            return static_cast<double>(qFromBigEndian<quint32>(p));
        return static_cast<double>(qFromBigEndian<float>(p));
    }

    const bool littleEndianByteOrder =
        bo.startsWith(QLatin1Char('B')) || bo.startsWith(QLatin1Char('D'));
    const uchar *p = reinterpret_cast<const uchar *>(chunk.constData());
    if (rule.dataType == QStringLiteral("uint16") && chunk.size() >= 2)
        return static_cast<double>(littleEndianByteOrder ? qFromLittleEndian<quint16>(p)
                                                         : qFromBigEndian<quint16>(p));
    if (rule.dataType == QStringLiteral("int16") && chunk.size() >= 2)
        return static_cast<double>(littleEndianByteOrder ? qFromLittleEndian<qint16>(p)
                                                         : qFromBigEndian<qint16>(p));
    if (rule.dataType == QStringLiteral("uint8") && chunk.size() >= 1)
        return static_cast<double>(*p);
    if (rule.dataType == QStringLiteral("int8") && chunk.size() >= 1)
        return static_cast<double>(*reinterpret_cast<const qint8 *>(p));
    return 0;
}

bool ByteMatchReceiveEvent::checkCondition(const ByteMatchRule &rule, double currentValue)
{
    // 首个采样只建立基线，不判断边沿：否则流程启动/设备重连后的第一帧
    // 就会被误判为"上升沿"，凭空触发一次流程。
    if (!rule.hasLastValue)
        return false;
    if (rule.useRisingEdge) {
        return (rule.lastValue == 0 && currentValue != 0);
    }
    if (rule.useFallingEdge) {
        return (rule.lastValue != 0 && currentValue == 0);
    }
    if (rule.useEquals) {
        // qFuzzyCompare 与 0 比较不可靠（(0,0) 恒 false）——历史缺陷：比较值填 0 永远不匹配
        return qFuzzyIsNull(currentValue - rule.compareValue);
    }
    return false;
}

bool ByteMatchReceiveEvent::parse(const QByteArray &data, QList<QVariant> &fields)
{
    if (!m_enabled || data.isEmpty() || m_rules.isEmpty()) return false;

    bool anyMatched = false;

    for (auto &rule : m_rules) {
        const double val = extractValue(data, rule);

        if (checkCondition(rule, val)) {
            anyMatched = true;
            fields.append(val);
        }
        // 更新基线（首帧只建基线，不触发边沿）：供下一次边沿判断
        rule.lastValue = val;
        rule.hasLastValue = true;
    }

    if (anyMatched) {
        emit eventGenerated(m_eventId, fields);
        return true;
    }
    return false;
}

QJsonObject ByteMatchReceiveEvent::toJson() const
{
    QJsonObject obj = ReceiveEvent::toJson();
    obj[QStringLiteral("registerAddress")] = m_registerAddress;

    QJsonArray rulesArr;
    for (const auto &r : m_rules) {
        QJsonObject rObj;
        rObj[QStringLiteral("byteOffset")] = r.byteOffset;
        rObj[QStringLiteral("byteLength")] = r.byteLength;
        rObj[QStringLiteral("dataType")] = r.dataType;
        rObj[QStringLiteral("byteOrder")] = r.byteOrder;
        rObj[QStringLiteral("compareValue")] = r.compareValue;
        rObj[QStringLiteral("useRisingEdge")] = r.useRisingEdge;
        rObj[QStringLiteral("useFallingEdge")] = r.useFallingEdge;
        rObj[QStringLiteral("useEquals")] = r.useEquals;
        rObj[QStringLiteral("lastValue")] = r.lastValue;
        rulesArr.append(rObj);
    }
    obj[QStringLiteral("rules")] = rulesArr;
    return obj;
}

void ByteMatchReceiveEvent::fromJson(const QJsonObject &json)
{
    ReceiveEvent::fromJson(json);
    m_registerAddress = json[QStringLiteral("registerAddress")].toInt();

    m_rules.clear();
    QJsonArray rulesArr = json[QStringLiteral("rules")].toArray();
    for (const auto &v : rulesArr) {
        QJsonObject rObj = v.toObject();
        ByteMatchRule r;
        r.byteOffset = rObj[QStringLiteral("byteOffset")].toInt();
        r.byteLength = rObj[QStringLiteral("byteLength")].toInt(2);
        r.dataType = rObj[QStringLiteral("dataType")].toString(QStringLiteral("int16"));
        r.byteOrder = rObj[QStringLiteral("byteOrder")].toString(QStringLiteral("ABCD"));
        r.compareValue = rObj[QStringLiteral("compareValue")].toDouble();
        r.useRisingEdge = rObj[QStringLiteral("useRisingEdge")].toBool();
        r.useFallingEdge = rObj[QStringLiteral("useFallingEdge")].toBool();
        r.useEquals = rObj[QStringLiteral("useEquals")].toBool(true);
        r.lastValue = rObj[QStringLiteral("lastValue")].toDouble();
        m_rules.append(r);
    }
}
