#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QList>
#include <QVariant>
#include <QJsonObject>

/// 发送事件基类 — 将数据格式化为指定协议并发送到外部设备
class SendEvent : public QObject
{
    Q_OBJECT
public:
    enum SendType {
        TEXT_DIRECT,     /// 文本-直接输出：直接发送字符串
        BYTE_PACK        /// 字节组包：组装二进制数据包发送
    };

    explicit SendEvent(const QString &id, const QString &deviceName,
                       SendType type, QObject *parent = nullptr);
    virtual ~SendEvent() = default;

    QString eventId() const { return m_eventId; }
    QString deviceName() const { return m_deviceName; }
    SendType sendType() const { return m_sendType; }
    bool enabled() const { return m_enabled; }
    void setEnabled(bool e) { m_enabled = e; }

    /// 执行发送操作，返回是否成功
    virtual bool send(const QVariant &data) = 0;

    virtual QJsonObject toJson() const;
    virtual void fromJson(const QJsonObject &json);

signals:
    void sendCompleted(const QString &eventId, bool success);

protected:
    QString m_eventId;
    QString m_deviceName;
    SendType m_sendType;
    bool m_enabled = true;
};

/// 文本-直接输出
class TextDirectSendEvent : public SendEvent
{
    Q_OBJECT
public:
    explicit TextDirectSendEvent(const QString &id, const QString &deviceName,
                                 QObject *parent = nullptr);

    void setTemplate(const QString &tmpl) { m_template = tmpl; }
    QString templateText() const { return m_template; }
    void setSuffix(const QString &suffix) { m_suffix = suffix; }
    QString suffix() const { return m_suffix; }

    bool send(const QVariant &data) override;

    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &json) override;

private:
    QString m_template;
    QString m_suffix = QStringLiteral("\n");
};

/// 字节组包发送
struct BytePackField {
    int offset = 0;
    int length = 2;
    QString dataType = QStringLiteral("int16");
    QVariant fixedValue;
};

class BytePackSendEvent : public SendEvent
{
    Q_OBJECT
public:
    explicit BytePackSendEvent(const QString &id, const QString &deviceName,
                               QObject *parent = nullptr);

    void addField(const BytePackField &field);
    void clearFields();
    QList<BytePackField> fields() const { return m_fields; }

    bool send(const QVariant &data) override;

    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &json) override;

private:
    QByteArray packData(const QVariant &source) const;
    QList<BytePackField> m_fields;
};
