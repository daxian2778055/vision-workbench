#pragma once

#include "CommunicationNodeBase.h"
#include <QUdpSocket>

/// UDP 通信节点（对标 VM 4.4 的 UDP 通信）：
/// 绑定本地端口接收；向目标 IP:端口 发送。UDP 无连接语义，绑定成功即视为"已连接"。
/// 典型用途：UDP 广播触发、轻量结果上报、与 PLC/机器人做快速数据交换。
class UdpCommNode : public CommunicationNodeBase
{
    Q_OBJECT
public:
    explicit UdpCommNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    bool openConnection() override;
    void closeConnection() override;
    bool isConnected() const override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

protected slots:
    void onSendRequested(const QByteArray &data) override;

private slots:
    void onReadyRead();

private:
    QUdpSocket *m_socket = nullptr;
    /// 最近一次收到数据的来源（供"仅接收"模式下诊断显示）
    QString m_lastSender;
};
