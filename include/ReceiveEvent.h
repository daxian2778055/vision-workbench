#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QList>
#include <QVariant>
#include <QJsonObject>

/// 接收事件基类 — 将通信原始数据解析为业务事件
class ReceiveEvent : public QObject
{
    Q_OBJECT
public:
    enum EventType {
        TEXT_PROTOCOL,    /// 文本-协议解析：按分隔符拆分字符串
        BYTE_MATCH        /// 字节匹配-协议组装：按字节规则匹配
    };

    explicit ReceiveEvent(const QString &id, const QString &deviceName,
                          EventType type, QObject *parent = nullptr);
    virtual ~ReceiveEvent() = default;

    QString eventId() const { return m_eventId; }
    QString deviceName() const { return m_deviceName; }
    EventType eventType() const { return m_eventType; }
    bool enabled() const { return m_enabled; }
    void setEnabled(bool e) { m_enabled = e; }

    /// 解析原始数据，若匹配规则则返回 true 并填充 fields
    virtual bool parse(const QByteArray &data, QList<QVariant> &fields) = 0;

    virtual QJsonObject toJson() const;
    virtual void fromJson(const QJsonObject &json);

signals:
    void eventGenerated(const QString &eventId, const QList<QVariant> &fields);

protected:
    QString m_eventId;
    QString m_deviceName;
    EventType m_eventType;
    bool m_enabled = true;
};

/// 文本-协议解析：按分隔符拆分 / 按正则提取（对标 VM 4.4 文本解析的两种模式）
class TextProtocolReceiveEvent : public ReceiveEvent
{
    Q_OBJECT
public:
    enum ParseMode {
        Delimiter = 0,   /// 按分隔符拆分（默认，兼容旧配置）
        Regex     = 1    /// 按正则提取：有捕获组取各捕获组，无捕获组取整体匹配
    };

    explicit TextProtocolReceiveEvent(const QString &id, const QString &deviceName,
                                      QObject *parent = nullptr);

    void setDelimiter(const QString &delim) { m_delimiter = delim; }
    QString delimiter() const { return m_delimiter; }

    void setParseMode(ParseMode mode) { m_parseMode = mode; }
    ParseMode parseMode() const { return m_parseMode; }
    void setRegex(const QString &re) { m_regex = re; }
    QString regex() const { return m_regex; }

    bool parse(const QByteArray &data, QList<QVariant> &fields) override;

    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &json) override;

private:
    QString m_delimiter = QStringLiteral(",");
    ParseMode m_parseMode = Delimiter;
    QString m_regex;
};

/// 字节匹配-协议组装：按字节规则匹配值变化
struct ByteMatchRule {
    int byteOffset = 0;        /// 字节起始位置
    int byteLength = 2;        /// 数据长度（字节数）
    QString dataType = QStringLiteral("int16"); /// int16 / int32 / float
    QString byteOrder = QStringLiteral("ABCD"); /// ABCD / CDAB / BADC / DCBA
    double compareValue = 0;    /// 比较目标值
    bool useRisingEdge = false; /// 使用上升沿检测
    bool useFallingEdge = false;/// 使用下降沿检测
    bool useEquals = true;      /// 使用等于比较
    double lastValue = 0;       /// 上一次的值（用于边沿检测）
    /// 是否已采集过基线值：首个采样只建立基线、不判断边沿
    /// （否则流程启动/设备重连后的第一帧就会被误判为"上升沿"，凭空触发一次）。
    /// 不参与序列化。
    bool hasLastValue = false;
};

class ByteMatchReceiveEvent : public ReceiveEvent
{
    Q_OBJECT
public:
    explicit ByteMatchReceiveEvent(const QString &id, const QString &deviceName,
                                   QObject *parent = nullptr);

    void setRegisterAddress(int addr) { m_registerAddress = addr; }
    int registerAddress() const { return m_registerAddress; }

    void addRule(const ByteMatchRule &rule);
    void clearRules();
    QList<ByteMatchRule> rules() const { return m_rules; }

    bool parse(const QByteArray &data, QList<QVariant> &fields) override;

    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &json) override;

private:
    /// 提取规则值；返回 false 表示该帧无法按此规则取值（负偏移 / 长度不符 / 未知类型），
    /// 调用方必须跳过本次判断且**不得**更新基线——否则短帧/空帧的 0 会污染边沿判断。
    bool extractValue(const QByteArray &data, const ByteMatchRule &rule, double &out) const;
    bool checkCondition(const ByteMatchRule &rule, double currentValue);

    int m_registerAddress = 0;
    QList<ByteMatchRule> m_rules;
};
