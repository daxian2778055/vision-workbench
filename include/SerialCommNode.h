#pragma once

#include "CommunicationNodeBase.h"
#include <QSerialPort>
#include <QTimer>

/// 串口通信节点（数据位/停止位/校验生效；USB 掉线自动重连）
class SerialCommNode : public CommunicationNodeBase
{
    Q_OBJECT
public:
    explicit SerialCommNode(QObject *parent = nullptr);

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
    void onDataReceived();
    void onSerialError(QSerialPort::SerialPortError error);

private:
    /// 按 m_params 应用数据位/停止位/校验（历史缺陷：打开时硬编码 8/1/None，配置形同虚设）
    void applyPortSettings();
    void scheduleReconnect();

    QSerialPort *m_serial = nullptr;

    // 断线自动重连（USB 转串口松动/掉线时自愈并报警）
    QTimer *m_reconnectTimer = nullptr;
    bool m_autoReconnect = true;
    int m_reconnectInterval = 3000;
    bool m_userClosed = false;
};
