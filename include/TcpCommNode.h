#pragma once

#include "CommunicationNodeBase.h"
#include <QTcpSocket>
#include <QTcpServer>
#include <QTimer>

/// TCP 通信节点（客户端/服务端；客户端支持断线自动重连，对齐 Modbus/PLC 的重连能力）
class TcpCommNode : public CommunicationNodeBase
{
    Q_OBJECT
public:
    explicit TcpCommNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    bool openConnection() override;
    void closeConnection() override;
    bool isConnected() const override;
    void setParam(const QString &name, const QVariant &value) override;
    void fromJson(const QJsonObject &json) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

protected slots:
    void onSendRequested(const QByteArray &data) override;

private slots:
    void onReadyRead();
    void onSocketDisconnected();

private:
    /// 为当前 socket 挂接接收/断开处理
    void hookSocket(QTcpSocket *socket);
    /// 断线后按配置安排重连（服务端模式 / 用户主动断开 / 关闭了自动重连时不排）
    void scheduleReconnect();

    QTcpSocket *m_socket = nullptr;
    QTcpServer *m_server = nullptr;
    bool m_isServer = false;

    // 自动重连（历史缺陷：TCP 断线后只报警不重连、永久失联，必须手动点连接）
    // S1 残留收口（通信类第 13 类）：autoReconnect / reconnectInterval 的**唯一来源改为参数表**，
    // 不再保留无锁成员镜像——原先 setParam 写成员、定时器与 scheduleReconnect 读成员，
    // 与界面线程写同为无保护竞态。
    // 钳制放**写侧**（interval 下限 500ms）：旧实现是成员钳、参数表存原值（成员/参数表/面板三个口径），
    // 现在参数表直接存钳后值，读取方（重连排程/面板）共用同一份。
    // 以下成员**刻意保留**（非参数镜像）：
    //  · m_reconnectTimer / m_socket / m_server —— 运行期资源；
    //  · m_isServer —— 连接时从 `mode` 参数**派生**的运行期状态（openConnection 内刷新）；
    //  · m_userClosed / m_asyncConnect —— 运行期流程标志（用户主动关闭 / 异步重连路径）。
    QTimer *m_reconnectTimer = nullptr;
    bool m_userClosed = false;   /// 用户/上层主动关闭：不触发自动重连
    /// 异步连接模式（自动重连路径使用）：connectToHost 发起后立即返回，绝不阻塞 UI 线程。
    /// 此前重连在 UI 线程同步 waitForConnected(3000)：设备离线时会周期性卡死界面。
    bool m_asyncConnect = false;
};
