#pragma once

#include "HalconNode.h"
#include "PortDataType.h"
#include <QHostAddress>
#include <QThread>
#include <QMutex>

/// 通信节点基类，提供统一的连接状态管理和线程模型
class CommunicationNodeBase : public HalconNode
{
    Q_OBJECT
public:

    /// 轮级发送失败计数（2026-10-30 r19 P1-11）：sendData 入队即返回 true，
    /// 真正的写在稍后执行；写失败原先只报界面日志，流程仍记「通过」。
    /// 累计写失败次数，执行器在轮末据此把本轮判失败。
    static int sendWriteFailureCount();
    static void resetSendWriteFailureCount();
    enum CommType {
        SERIAL,
        TCP_CLIENT,
        TCP_SERVER,
        MODBUS_MASTER,
        PLC,
        UDP           /// UDP 收发（对标 VM 4.4 UDP 通信）
    };

    explicit CommunicationNodeBase(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;

    virtual bool openConnection() = 0;
    virtual void closeConnection() = 0;
    virtual bool isConnected() const = 0;

    CommType commType() const { return m_commType; }

    /// 设置/获取设备名称（供 CommunicationManager 路由使用）
    void setDeviceName(const QString &name) { m_deviceName = name; }
    QString deviceName() const { return m_deviceName; }

    /// 发送外部数据（由 CommunicationManager::sendData 触发，队列投递到节点线程执行）
    void requestSend(const QByteArray &data) { emit sendRequested(data); }

    /// 本节点是否支持「原始字节发送」。
    /// Modbus/PLC 是寄存器语义（写寄存器），不支持裸字节；上层据此**提前失败**，
    /// 而不是一路投递到 onSendRequested 的空实现变成静默 no-op（历史上就是这样：
    /// 上层以为回写成功、设备其实什么都没收到）。
    virtual bool supportsRawSend() const { return true; }

signals:
    void connectionOpened();
    void connectionClosed();
    void dataReceived(const QByteArray &data);
    void communicationError(const QString &error);
    /// 寄存器值变化（供 Modbus 等使用）
    void registerValueChanged(int address, const QByteArray &value, const QString &dataType);
    /// 发送请求（跨线程安全：QueuedConnection 投递到节点所在线程）
    void sendRequested(const QByteArray &data);

protected slots:
    /// 实际发送实现（子类覆盖：socket/串口 write）。默认空实现。
    virtual void onSendRequested(const QByteArray &data) { Q_UNUSED(data); }

protected:

    /// 写失败的统一收口：计数 + 报错（各节点的发送失败路径都走它）
    void noteSendWriteFailure(const QString &why);
    CommType m_commType;
    bool m_connected = false;
    QString m_deviceName;
};

/// 监听地址解析（177 行拍板；U-202 期客户改口径）：通信服务端（TCP listen / UDP bind /
/// Modbus 从站）绑定面统一走这里，禁止静默 Any——**空参不再等价于全网卡**：判 Missing
/// （未配置）由调用方拒启动；显式参数必须解析成功为 IP 字面量（数字形式，主机名不解析）；
/// 无效参数一律 Invalid，由调用方拒绝启动——宁可不起服务，不静默回落扩面。
/// "0.0.0.0"/"::" 属合法字面量 Explicit（显式全网卡，绑定后就位时留日志）：
/// 「确实要听全网卡」写成字面量即可，平台不再替人默认。
/// U-202 口径变更（明示行为变更）：变更前空参 = Default = 全网卡（177 行的产品默认面）；
/// 变更后空参 = Missing = 拒启动并留可执行提示。取舍：空参回落 127.0.0.1 也能收窄监听面，
/// 但那是**静默收窄**——现场症状是「PLC 连不上」，会被误判成网络或 PLC 的问题；
/// 拒启动则当场说清「哪个字段没填、怎么填」，且全网卡仍可显式写 0.0.0.0。
namespace CommBind {
enum class BindKind { Missing, Explicit, Invalid };

struct BindChoice {
    BindKind kind = BindKind::Invalid;
    QHostAddress address; // Explicit 时有效；Missing 时留空（不得携带任何监听地址）
};

inline BindChoice resolveListenAddress(const QString &raw)
{
    BindChoice c;
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) {
        c.kind = BindKind::Missing;   // U-202：未配置 ≠ 全网卡；调用方须拒启动
        return c;
    }
    QHostAddress addr;
    if (!addr.setAddress(trimmed)) {
        c.kind = BindKind::Invalid; // 非法配置：显式拒绝（N4 同形，静默回落 = 变相扩面）
        return c;
    }
    c.kind = BindKind::Explicit;
    c.address = addr;
    return c;
}
} // namespace CommBind
