#pragma once

#include "CommunicationNodeBase.h"
#include "ModbusNode.h"  // ModbusRegisterItem
#include <QModbusClient>
#include <QModbusTcpClient>
#include <QTimer>
#include <QJsonArray>

/// PLC 通信节点 — 基于 Modbus TCP 实现（绝大多数 PLC 均支持 Modbus TCP）
/// 支持品牌：Siemens S7-1200/1500、Mitsubishi、Omron、Keyence、Panasonic 等
/// 功能：寄存器表轮询读写、自动重连、可配置轮询周期
class PlcCommNode : public CommunicationNodeBase
{
    Q_OBJECT
public:
    explicit PlcCommNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    bool openConnection() override;
    void closeConnection() override;
    bool isConnected() const override;

    /// PLC（Modbus TCP）为寄存器语义，无「原始字节发送」；返回 false 让上层显式失败而非静默忽略
    bool supportsRawSend() const override { return false; }
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
    void setParam(const QString &name, const QVariant &value) override;
    QVariant getParam(const QString &name) const override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &json) override;

    // ---- 寄存器管理（复用 Modbus 寄存器项结构） ----
    void setRegisters(const QList<ModbusRegisterItem> &regs);
    QList<ModbusRegisterItem> registers() const { return m_registers; }
    /// 写入寄存器值：按该地址配置的 dataType/byteOrder 逆变换拆成 1~2 个寄存器字后写入（S3）。
    /// @return 只表示"请求是否已发出"：false = 未发出（宽度/字节序拆分失败、未连接、无设备句柄）；
    ///         true = 已发出。**不表示写入成功**——写入结果（含 S4 回读校验失败）一律经
    ///         communicationError 信号上报（与 ModbusNode::writeRegister 同口径）。
    bool writeRegister(int address, double value);

    /// S4 段③：回读同地址（宽类型读 2 字）并按同一 dataType/byteOrder 解析后比对。
    /// 不一致 / 读回失败 → emit communicationError；仅当 writeVerify 开启时被 writeRegister 调用。
    void verifyWrittenValue(int address, double expected, const QString &dataType,
                            const QString &byteOrder);

    /// 获取寄存器当前解析后的值（供 UI 实时显示）
    double registerCurrentValue(int address) const;
    QString registerDisplayValue(int address) const;

    /// 开始/停止轮询
    void startPolling();
    void stopPolling();

signals:
    /// 寄存器当前解析值更新（地址、数值、文本显示）
    void registerCurrentValueChanged(int address, double value, const QString &displayText);
    /// 寄存器值变化时发出
    void plcRegisterChanged(const QString &deviceName, int address,
                            const QByteArray &value, const QString &dataType);

private slots:
    void onPollTimeout();
    void onModbusStateChanged(int state);

private:
    /// 发起读请求；返回 false 表示请求未发出（调用方需回滚 pendingQueue 条目）
    bool readRegister(int slaveAddr, int regAddr, int count);
    double parseRawToValue(const QByteArray &raw, const QString &dataType, const QString &byteOrder) const;

    QModbusClient *m_modbus = nullptr;

    // 自动重连
    // S1 残留收口（通信类第 15 类）：autoReconnect / reconnectInterval / writeVerify /
    // pollInterval / slaveAddress 这 5 个**真参数**的**唯一来源改为参数表**，
    // 不再保留无锁成员镜像——原先 setParam 写成员、连接/轮询/写入路径读成员，
    // 与界面线程写同为无保护竞态。
    // 钳制放**写侧**（reconnectInterval ≥500ms、pollInterval ≥10ms，与原成员语义一致）。
    // 以下成员**刻意保留**（非参数镜像）：m_reconnectTimer / m_pollTimer / m_modbus（运行期资源）、
    // m_registers（寄存器表，随方案持久化）、m_everReallyConnected / m_userClosed（连接状态判据）、
    // m_currentRegIdx / m_pendingQueue（轮询运行期状态）。
    QTimer *m_reconnectTimer = nullptr;

    // 轮询
    QTimer *m_pollTimer = nullptr;

    // 寄存器表格
    QList<ModbusRegisterItem> m_registers;
    // 连接状态判据（S1，同 ModbusNode）：m_everReallyConnected 为"曾真正连上"唯一判据；
    // m_userClosed 抑制用户主动关闭后的自动重连"复活"。
    bool m_everReallyConnected = false;
    bool m_userClosed = false;
    // S4：回写三段确认（默认关闭）——段①发出写、段②从站回执 OK、段③回读同地址比对，
    // 不一致才上报。选项名 writeVerify，与 autoReconnect 同级（节点参数，不在表单上）。
    bool m_writeVerify = false;
    int m_currentRegIdx = 0;

    struct PendingRead {
        int slaveAddr;
        int regAddr;
        int count;
        int regIndex;
    };
    QList<PendingRead> m_pendingQueue;
};
