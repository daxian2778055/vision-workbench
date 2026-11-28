#pragma once

#include "HalconNode.h"
#include "PortDataType.h"
#include <QHostAddress>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QHash>
#include <atomic>

/// 通信节点基类，提供统一的连接状态管理和线程模型
class CommunicationNodeBase : public HalconNode
{
    Q_OBJECT
public:

    /// 轮级发送失败计数（2026-10-30 r19 P1-11）：sendData 入队即返回 true，
    /// 真正的写在稍后执行；写失败原先只报界面日志，流程仍记「通过」。
    /// 累计写失败次数，执行器在轮末据此把本轮判失败。
    /// flow 传 FlowRunContext 指针（流程隔离）；nullptr = 进程级兜底桶（r19 复核 P1-11）
    static int sendWriteFailureCount(const void *flow = nullptr);
    /// 排队中（已入队、尚未写完）的发送条数（r19 P2：轮末排空用）
    static int pendingSendCount(const void *flow = nullptr);
    /// 等本轮入队的发送全部落地（有界）；返回 true=已排空
    static bool waitSendDrain(const void *flow, int timeoutMs);
    static void resetSendWriteFailureCount(const void *flow = nullptr);
    enum CommType {
        SERIAL,
        TCP_CLIENT,
        TCP_SERVER,
        MODBUS_MASTER,
        PLC,
        UDP           /// UDP 收发（对标 VM 4.4 UDP 通信）
    };

    explicit CommunicationNodeBase(QObject *parent = nullptr);
    /// AH1d：节点销毁时释放所有仍在排队的上下文引用（接收方销毁后排队槽不会运行）
    ~CommunicationNodeBase() override;

    void init() override;
    void run(bool autoSwitch = true) override;

    virtual bool openConnection() = 0;
    virtual void closeConnection() = 0;
    virtual bool isConnected() const = 0;

    CommType commType() const { return m_commType; }

    /// 设置/获取设备名称（供 CommunicationManager 路由使用）
    void setDeviceName(const QString &name) { m_deviceName = name; }
    QString deviceName() const { return m_deviceName; }

    /// AH1b：该成员只服务同步寄存器写入的归属回退；异步发送不再读它。
    /// 异步路径的归属由 requestSend 的显式 ctx 参数随请求一起排队。
    void setSendFlowContext(const std::shared_ptr<FlowRunContext> &ctx)
    {
        QMutexLocker locker(&m_sendContextMutex);
        m_sendFlowContext = ctx;
    }

    /// 发送外部数据（由 CommunicationManager::sendData 触发，队列投递到节点线程执行）
    /// AH1b：ctx 必须显式传入，避免"先 set 再 request"两步之间被另一流程覆盖。
    void requestSend(const QByteArray &data, const std::shared_ptr<FlowRunContext> &ctx)
    {
        // AH1d：ctx 缺省时也要保活 ownerContext，不能用空指针去 retain 一个非空 key。
        const std::shared_ptr<FlowRunContext> effective = ctx ? ctx : ownerContext();
        const void *key = effective ? effective.get() : nullptr;
        // AH1c：排队期间持有上下文强引用，流程提前关闭也不会释放该对象；
        // 地址不会被新流程复用，待处理请求结算时再释放。
        retainSendContext(key, effective);
        bumpSendPending(key);
        emit sendRequested(data, key);
    }

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
    /// AH1：第二参数是入队时冻结的流程归属 key，异步结算只认它。
    void sendRequested(const QByteArray &data, const void *flowKey);

protected slots:
    /// 实际发送实现（子类覆盖：socket/串口 write）。默认空实现。
    virtual void onSendRequested(const QByteArray &data) { Q_UNUSED(data); }

private slots:
    /// 队列出口（r19 P2）：先跑子类的发送实现，再销掉一条 pending（用入队时的 key）
    void handleSendRequested(const QByteArray &data, const void *flowKey);

protected:

    /// 写失败的统一收口：计数 + 报错（各节点的发送失败路径都走它）
    static void bumpSendPending(const void *flow);
    static void dropSendPending(const void *flow);
    /// AH1c：排队请求持有上下文强引用，完成时按引用计数释放
    void retainSendContext(const void *flow, const std::shared_ptr<FlowRunContext> &ctx);
    void releaseSendContext(const void *flow);
    /// 发送归属流程上下文的读取：优先用 sendData 传入的发起方上下文，
 /// 否则退回节点自身 ownerContext，都没有则落 nullptr 桶（非流程场景）。
 const void *sendFlowContext() const
 {
     QMutexLocker locker(&m_sendContextMutex);
     if (m_sendFlowContext) return m_sendFlowContext.get();
     if (auto ctx = ownerContext()) return ctx.get();
     return nullptr;
 }
 /// AH1: active key during async send; write failures use it instead of rereading a mutable member.
 const void *activeSendFlow() const { return m_activeSendFlow.load(std::memory_order_acquire); }
 mutable QMutex m_sendContextMutex;
 /// AH1d：本节点仍持有的排队 key（析构时统一释放，防止接收方销毁导致槽不运行）
 QHash<const void *, int> m_heldSendKeys;
 std::shared_ptr<FlowRunContext> m_sendFlowContext;
 std::atomic<const void *> m_activeSendFlow{nullptr};
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
