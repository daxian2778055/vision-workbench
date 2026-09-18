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
    QTimer *m_reconnectTimer = nullptr;
    bool m_autoReconnect = true;
    int m_reconnectInterval = 3000;
    bool m_userClosed = false;   /// 用户/上层主动关闭：不触发自动重连
};
