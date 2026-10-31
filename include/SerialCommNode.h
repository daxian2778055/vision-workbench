#pragma once

#include "CommunicationNodeBase.h"
#include <QSerialPort>
#include <QTimer>
#include <QThread>
#include <QMutex>

/// 串口通信节点（数据位/停止位/校验生效；USB 掉线自动重连）
class SerialCommNode : public CommunicationNodeBase
{
    Q_OBJECT
public:
    /// 测试用（r19 P1-12）：端口对象所在的线程。必须 != 节点线程，
    /// 否则说明 invokeMethod 又用 QThread 对象当 context 了。
    QThread *serialIoThreadForTest() const;
    /// 测试用：以端口为 context 跑一次探针，返回它实际执行的线程
    QThread *probeIoThreadForTest();
    explicit SerialCommNode(QObject *parent = nullptr);
    ~SerialCommNode() override;

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
    void applyPortSettings(QSerialPort *port);   // 指定端口（端口在 IO 线程里）
    void scheduleReconnect();

    QSerialPort *m_serial = nullptr;   // 归 m_ioThread（r19 P1-12）
    /// 端口专属事件循环线程：创建/读写/关闭/删除都只在这里做
    QThread *m_ioThread = nullptr;
    /// 保护 m_serial / m_connected（多线程：执行线程 run、界面线程 write、IO 线程回写）
    QMutex m_stateMutex;

    // 断线自动重连（USB 转串口松动/掉线时自愈并报警）
    // S1 残留收口（通信类第 14 类）：autoReconnect / reconnectInterval 的**唯一来源改为参数表**，
    // 不再保留无锁成员镜像——原先 setParam 写成员、重连定时器与 scheduleReconnect 读成员，
    // 与界面线程写同为无保护竞态。
    // 钳制放**写侧**（interval 下限 500ms）：旧实现是成员钳、参数表存原值（成员/参数表/面板三个口径），
    // 现在参数表直接存钳后值，重连排程与面板共用同一份。
    // 以下成员**刻意保留**（非参数镜像）：m_serial / m_reconnectTimer（运行期资源）、
    // m_userClosed（用户主动关闭标志，运行期流程状态）。
    QTimer *m_reconnectTimer = nullptr;
    bool m_userClosed = false;
};
