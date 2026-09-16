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
    bool writeRegister(int address, quint16 value);

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
    void readRegister(int slaveAddr, int regAddr, int count);
    double parseRawToValue(const QByteArray &raw, const QString &dataType, const QString &byteOrder) const;

    QModbusClient *m_modbus = nullptr;

    // 自动重连
    QTimer *m_reconnectTimer = nullptr;
    bool m_autoReconnect = true;
    int m_reconnectInterval = 3000;

    // 轮询
    QTimer *m_pollTimer = nullptr;
    int m_pollInterval = 100;
    int m_slaveAddress = 1;

    // 寄存器表格
    QList<ModbusRegisterItem> m_registers;
    int m_currentRegIdx = 0;

    struct PendingRead {
        int slaveAddr;
        int regAddr;
        int count;
        int regIndex;
    };
    QList<PendingRead> m_pendingQueue;
};
