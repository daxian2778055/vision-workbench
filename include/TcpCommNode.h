#pragma once

#include "CommunicationNodeBase.h"
#include <QTcpSocket>
#include <QTcpServer>

/// TCP 通信节点
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

    QTcpSocket *m_socket = nullptr;
    QTcpServer *m_server = nullptr;
    bool m_isServer = false;
};
