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

    /// 重命名通信设备（VM 5.2.1：设备列表右键→重命名）。连接与节点参数原样保留；
    /// 连带引用跟随：接收/发送事件的绑定设备、心跳条目、组帧缓冲与定时器、事件触发映射。
    /// 旧名不存在 / 新名为空（去空白后）/ 新名与旧名相同 / 新名已被占用 → false。
    bool renameDevice(const QString &oldName, const QString &newName);

    /// 更新设备配置记录（热更新参数后调用，保证方案保存的是最新配置；不触碰节点）
    bool updateDeviceConfig(const QString &name, const QJsonObject &config);

    /// 获取设备的通信节点实例
    CommunicationNodeBase *deviceNode(const QString &name) const;

    // ---- 接收事件管理 ----
    QStringList receiveEventIds() const;
    ReceiveEvent *receiveEvent(const QString &id) const;
    bool addReceiveEvent(ReceiveEvent *event);
    bool removeReceiveEvent(const QString &id);
    /// 重命名接收事件（VM：事件列表右键→重命名）；全局触发里的事件触发映射跟随。
    /// 校验口径同 renameDevice。
    bool renameReceiveEvent(const QString &oldId, const QString &newId);

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
    /// 重命名发送事件（VM：事件列表右键→重命名）；校验口径同 renameDevice。
    bool renameSendEvent(const QString &oldId, const QString &newId);

    // ---- 序列化 ----
    QJsonObject toJson() const;
    void fromJson(const QJsonObject &json);
    bool loadFromFile(const QString &path);
    bool saveToFile(const QString &path);

    // ---- 响应配置（VM 5.2.5：方案加载 / 流程控制 / 相机响应）----
    /// 配置本体随方案保存（root["responseConfig"]）；运行期由 CommResponseManager 读取组装。
    QJsonObject responseConfig() const;
    void setResponseConfig(const QJsonObject &cfg);

    /// 转义解码（\r \n \t \0 \\ \xHH）：响应配置的结束符与组帧共用同一口径。
    static QByteArray decodeEscapes(const QString &text);

    // ---- 操作 ----
    bool openDevice(const QString &name);
    bool closeDevice(const QString &name);
    bool sendData(const QString &deviceName, const QByteArray &data);

signals:
    void deviceAdded(const QString &name, const QString &type);
    /// 方案载入时某条设备无法建立（未知类型/构造失败）：载入面据此汇总提示，不留无声丢失
    void deviceLoadFailed(const QString &name, const QString &type, const QString &reason);
    void deviceRemoved(const QString &name);
    void deviceRenamed(const QString &oldName, const QString &newName);
    void receiveEventRenamed(const QString &oldId, const QString &newId);
    void sendEventRenamed(const QString &oldId, const QString &newId);
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

    /// 按类型构造并注册通信节点。返回 false = 未知类型/构造失败（调用方应回滚登记，勿留幽灵设备）。
    bool createDeviceNode(const QString &name, const QString &type, const QJsonObject &config);

    /// 帧组装（TCP/串口/UDP 的粘包/半包治理）：
    /// 按设备节点的参数 frameTimeoutMs（静默即一帧）/ frameTerminator（结束符，支持 \r\n 等转义）
    /// 切帧后再转发；两个参数都未配置时行为与历史完全一致（原样转发）。
    void feedFrameAssembler(const QString &deviceName, const QByteArray &data);
    /// 组帧静默定时器（不存在则建，回调按设备名取缓冲）；每次调用重置计时。
    /// 重命名后旧定时器回调仍捕获旧名，须停旧建新——与 feedFrameAssembler 共用此入口。
    void startFrameTimer(const QString &deviceName, int timeoutMs);

    /// 响应配置（仅界面线程读写；命名字段见 Dialog 的 setResponseConfig 组装处）
    QJsonObject m_responseConfig;

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
