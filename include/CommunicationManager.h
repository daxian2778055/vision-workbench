#pragma once

#include <QObject>
#include <QMap>
#include <QHash>
#include <QString>
#include <QList>
#include <QJsonObject>
#include <QMutex>

class ReceiveEvent;
class TextProtocolReceiveEvent;
class ByteMatchReceiveEvent;
class SendEvent;
class TextDirectSendEvent;
class BytePackSendEvent;
class CommunicationNodeBase;
class QTimer;

/// 通信设备描述
struct CommDeviceInfo {
    QString name;
    QString type;           /// "TCP", "Serial", "Modbus", "PLC"
    bool isConnected = false;
    QJsonObject config;     /// 设备特定配置参数
};

/// 通信管理器全局单例 — 管理所有通信设备、接收事件、发送事件
class CommunicationManager : public QObject
{
    Q_OBJECT
public:
    static CommunicationManager *instance();

    // ---- 设备管理 ----
    QStringList deviceNames() const;
    CommDeviceInfo deviceInfo(const QString &name) const;
    bool hasDevice(const QString &name) const;

    /// 添加通信设备（type: "TCP", "Serial", "Modbus", "PLC"）
    bool addDevice(const QString &name, const QString &type, const QJsonObject &config);

    /// 移除通信设备
    bool removeDevice(const QString &name);

    /// 更新设备配置记录（热更新参数后调用，保证方案保存的是最新配置；不触碰节点）
    bool updateDeviceConfig(const QString &name, const QJsonObject &config);

    /// 获取设备的通信节点实例
    CommunicationNodeBase *deviceNode(const QString &name) const;

    // ---- 接收事件管理 ----
    QStringList receiveEventIds() const;
    ReceiveEvent *receiveEvent(const QString &id) const;
    bool addReceiveEvent(ReceiveEvent *event);
    bool removeReceiveEvent(const QString &id);

    // ---- 发送事件管理 ----
    QStringList sendEventIds() const;
    SendEvent *sendEvent(const QString &id) const;
    bool addSendEvent(SendEvent *event);
    /// 触发一个已配置的发送事件（按其模板/字段组装后真正发出）。
    /// 返回是否真的发出：事件不存在、被禁用、或设备不存在/未连接都返回 false。
    bool fireSendEvent(const QString &id, const QVariant &data);
    /// 触发**全部已启用**的发送事件（「每轮结束自动上报」策略的落点）。
    /// 返回真正发出的条数：被禁用、设备未连接、事件不存在的都不计入
    /// （语义与 fireSendEvent 一致，"已启用"这一过滤天然成立，无需重复判断）。
    /// 线程约束同 fireSendEvent：先取 ID 快照，再在锁外逐个调用（不持锁碰节点方法）。
    int fireEnabledSendEvents(const QVariant &data = QVariant());
    bool removeSendEvent(const QString &id);

    // ---- 序列化 ----
    QJsonObject toJson() const;
    void fromJson(const QJsonObject &json);
    bool loadFromFile(const QString &path);
    bool saveToFile(const QString &path);

    // ---- 操作 ----
    bool openDevice(const QString &name);
    bool closeDevice(const QString &name);
    bool sendData(const QString &deviceName, const QByteArray &data);

signals:
    void deviceAdded(const QString &name, const QString &type);
    void deviceRemoved(const QString &name);
    void deviceConnected(const QString &name);
    void deviceDisconnected(const QString &name);
    void dataReceived(const QString &deviceName, const QByteArray &data);
    void dataSent(const QString &deviceName, const QByteArray &data);
    void receiveEventAdded(const QString &id);
    void receiveEventRemoved(const QString &id);
    void sendEventAdded(const QString &id);
    void sendEventRemoved(const QString &id);

private:
    CommunicationManager(QObject *parent = nullptr);
    ~CommunicationManager() override;
    CommunicationManager(const CommunicationManager &) = delete;
    CommunicationManager &operator=(const CommunicationManager &) = delete;

    void createDeviceNode(const QString &name, const QString &type, const QJsonObject &config);

    /// 帧组装（TCP/串口/UDP 的粘包/半包治理）：
    /// 按设备节点的参数 frameTimeoutMs（静默即一帧）/ frameTerminator（结束符，支持 \r\n 等转义）
    /// 切帧后再转发；两个参数都未配置时行为与历史完全一致（原样转发）。
    void feedFrameAssembler(const QString &deviceName, const QByteArray &data);
    /// "\r\n" → CRLF 字节（支持 \r \n \t \0 \\ \xHH）
    static QByteArray decodeEscapes(const QString &text);

    /// 帧组装缓冲（仅 UI 线程访问：数据从节点 dataReceived 信号进入）
    QHash<QString, QByteArray> m_frameBuffers;
    QHash<QString, QTimer *> m_frameTimers;

    QMap<QString, CommunicationNodeBase *> m_devices;
    QMap<QString, CommDeviceInfo> m_deviceInfos;
    QMap<QString, ReceiveEvent *> m_receiveEvents;
    QMap<QString, SendEvent *> m_sendEvents;

    /// 保护上述 map 的并发读写（通信线程回调与界面线程共享）。
    /// 约束：严禁在持锁期间调用节点方法（openConnection/closeConnection/setParam 等），
    /// 节点会同步 emit connectionOpened/Closed，其槽函数需要重新进入本锁 → 非递归锁会自死锁。
    /// 正确做法：锁内只取指针 / 改容器，节点调用与 emit 一律放到锁外。
    mutable QMutex m_mutex;
};
