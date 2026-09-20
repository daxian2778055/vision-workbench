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

bool ByteMatchReceiveEvent::extractValue(const QByteArray &data, const ByteMatchRule &rule,
                                        double &out) const
{
    // 负偏移 / 零长度 / 越界都必须显式判无效：历史实现直接把配置值交给 mid()，
    // 负偏移在 Qt 中会断言（调试）或裁剪（发布），而"取不到值"被 return 0 当成真实值
    // 写进基线 → 下一帧真值到来被误判成上升沿，凭空触发一次流程。
    if (rule.byteOffset < 0 || rule.byteLength <= 0) return false;
    if (rule.byteOffset + rule.byteLength > data.size()) return false;

    QByteArray chunk = data.mid(rule.byteOffset, rule.byteLength);
    const QString bo = rule.byteOrder;

    // 32 位值：先按"字节序"把 chunk 规范到 ABCD（A=MSB 在前），再按大端解释。
    // 16 位值：按首字母定字节序（A… = 高字节在前；B…/D… = 低字节在前）。
    // 历史缺陷：只有 int16/int32/float 三个分支，且用 reinterpret_cast 按宿主序
    // （小端）读取——uint16 直接落到 return 0（永远不触发），大小端也与配置相反；
    // BADC（字内字节互换）完全没有分支，配了 BADC 的 32 位规则一律算成 ABCD 的错值。
    if (chunk.size() >= 4 && (rule.dataType == QStringLiteral("int32")
                              || rule.dataType == QStringLiteral("uint32")
                              || rule.dataType == QStringLiteral("float"))) {
        if (bo == QStringLiteral("CDAB")) {
            char tmp = chunk[0]; chunk[0] = chunk[2]; chunk[2] = tmp;
            tmp = chunk[1]; chunk[1] = chunk[3]; chunk[3] = tmp;
        } else if (bo == QStringLiteral("BADC")) {
            // 字内字节互换：[B,A,D,C] → [A,B,C,D]
            char tmp = chunk[0]; chunk[0] = chunk[1]; chunk[1] = tmp;
            tmp = chunk[2]; chunk[2] = chunk[3]; chunk[3] = tmp;
        } else if (bo == QStringLiteral("DCBA")) {
            for (int i = 0; i < chunk.size() / 2; ++i) {
                const char tmp = chunk[i];
                chunk[i] = chunk[chunk.size() - 1 - i];
                chunk[chunk.size() - 1 - i] = tmp;
            }
        }
        const uchar *p = reinterpret_cast<const uchar *>(chunk.constData());
        if (rule.dataType == QStringLiteral("int32"))
            out = static_cast<double>(static_cast<qint32>(qFromBigEndian<quint32>(p)));
        else if (rule.dataType == QStringLiteral("uint32"))
            out = static_cast<double>(qFromBigEndian<quint32>(p));
        else
            out = static_cast<double>(qFromBigEndian<float>(p));
        return true;
    }

    const bool littleEndianByteOrder =
        bo.startsWith(QLatin1Char('B')) || bo.startsWith(QLatin1Char('D'));
    const uchar *p = reinterpret_cast<const uchar *>(chunk.constData());
    if (rule.dataType == QStringLiteral("uint16") && chunk.size() >= 2)
        out = static_cast<double>(littleEndianByteOrder ? qFromLittleEndian<quint16>(p)
                                                        : qFromBigEndian<quint16>(p));
    else if (rule.dataType == QStringLiteral("int16") && chunk.size() >= 2)
        out = static_cast<double>(littleEndianByteOrder ? qFromLittleEndian<qint16>(p)
                                                        : qFromBigEndian<qint16>(p));
    else if (rule.dataType == QStringLiteral("uint8") && chunk.size() >= 1)
        out = static_cast<double>(*p);
    else if (rule.dataType == QStringLiteral("int8") && chunk.size() >= 1)
        out = static_cast<double>(*reinterpret_cast<const qint8 *>(p));
    else
        return false;   // 类型与可用长度不匹配（如 int32 只给了 2 字节）：按无效帧处理
    return true;
}

bool ByteMatchReceiveEvent::checkCondition(const ByteMatchRule &rule, double currentValue)
{
    // 首个采样只建立基线，不判断边沿：否则流程启动/设备重连后的第一帧
    // 就会被误判为"上升沿"，凭空触发一次流程。
    if (!rule.hasLastValue)
        return false;
    // 边沿以 compareValue 为阈值：上升沿 = 上一帧在阈值线下方(含)、本帧越过阈值线；下降沿反之。
    // 历史实现硬编码 0↔非0，用户填的比较值对边沿完全无效（填 100 的"过阈值触发"永远按 0 判）。
    // 默认 compareValue=0 时，0→1 / 1→0 的开关量行为与旧实现一致（0→1 仍触发、1→0 仍触发）。
    if (rule.useRisingEdge) {
        return (rule.lastValue <= rule.compareValue && currentValue > rule.compareValue);
    }
    if (rule.useFallingEdge) {
        return (rule.lastValue > rule.compareValue && currentValue <= rule.compareValue);
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
        double val = 0;
        const bool valid = extractValue(data, rule, val);

        // 无效帧（越界/负偏移/长度或类型不匹配）既不判断、也不更新基线：
        // 历史实现把"取不到值"当成 0 写进基线，下一帧真值到来即被误判成上升沿凭空触发。
        if (!valid)
            continue;

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
