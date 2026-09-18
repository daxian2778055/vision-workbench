#pragma once

#include "HalconNode.h"
#include "PortDataType.h"
#include <QThread>
#include <QMutex>

/// 通信节点基类，提供统一的连接状态管理和线程模型
class CommunicationNodeBase : public HalconNode
{
    Q_OBJECT
public:
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
    CommType m_commType;
    bool m_connected = false;
    QString m_deviceName;
};
