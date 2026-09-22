#pragma once

#include "CommunicationNodeBase.h"
#include <QModbusClient>
#include <QModbusTcpClient>
#include <QModbusTcpServer>
#include <QModbusRtuSerialMaster>
#include <QModbusDataUnit>
#include <QTimer>
#include <QJsonArray>

/// Modbus 寄存器监控项（对应 VM 4.4 的寄存器表格一行）
struct ModbusRegisterItem {
    int address = 0;          /// 寄存器地址
    QString dataType = QStringLiteral("int16"); /// int16 / int32 / float
    QString byteOrder = QStringLiteral("ABCD"); /// ABCD / CDAB / BADC / DCBA
    QString accessMode = QStringLiteral("Read"); /// Read / ReadWrite / Write
    bool enabled = true;      /// 是否轮询此寄存器
    QString description;      /// 描述名称

    // 运行时
    QByteArray lastRaw;       /// 上次读取的原始值（用于检测变化）
    bool hasLastValue = false;
    double currentValue = 0;  /// 当前解析后的数值（用于显示）
    QString displayValue;     /// 显示的字符串值
};

/// Modbus 通信节点 — 客户端(主站) + 服务器(从站) 双模式，带自动重连、轮询周期、寄存器表格
class ModbusNode : public CommunicationNodeBase
{
    Q_OBJECT
public:
    /// 通信角色
    enum Role {
        MODBUS_CLIENT = 0,  /// 客户端（主站）：主动连接外部服务器并轮询读写
        MODBUS_SERVER = 1   /// 服务器（从站）：监听端口，对外提供寄存器读写
    };

    explicit ModbusNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    bool openConnection() override;
    void closeConnection() override;
    bool isConnected() const override;

    /// 开始/停止轮询（仅客户端模式有效）
    void startPolling();
    void stopPolling();

    void setParam(const QString &name, const QVariant &value) override;
    QVariant getParam(const QString &name) const override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &json) override;

    // ---- 寄存器管理 ----
    void setRegisters(const QList<ModbusRegisterItem> &regs);
    QList<ModbusRegisterItem> registers() const { return m_registers; }

    /// 写入寄存器值（客户端模式：向外部设备写入）。按该地址配置的 dataType/byteOrder
    /// 逆变换拆成 1~2 个寄存器字后写入，保证与读路径字节序往返对称（S3）。
    /// @return 只表示"请求是否已发出"：false = 未发出（宽度/字节序拆分失败、未连接、无设备句柄）；
    ///         true = 已发出。**不表示写入成功**——写入结果（含 S4 回读校验失败）一律经
    ///         communicationError 信号上报（异步回执到达时，或同步完成时立即上报）。
    bool writeRegister(int address, double value);

    /// 服务器模式：更新本地寄存器表，客户端读请求将返回该值（同样按字节序拆字）。
    bool setLocalRegisterValue(int address, double value);

    /// S4 段③：回读同地址（宽类型读 2 字）并按同一 dataType/byteOrder 解析后比对。
    /// 不一致 / 读回失败 → emit communicationError；仅当 writeVerify 开启时被 writeRegister 调用。
    void verifyWrittenValue(int address, double expected, const QString &dataType,
                            const QString &byteOrder);

    /// 获取寄存器当前解析后的值（供 UI 实时显示）
    double registerCurrentValue(int address) const;
    QString registerDisplayValue(int address) const;

    /// 当前角色
    Role role() const { return m_role; }
    void setRole(Role role);

    /// 服务器是否已启动监听
    bool isServerListening() const;

    /// Modbus 为寄存器语义，无「原始字节发送」；返回 false 让上层显式失败而非静默忽略
    bool supportsRawSend() const override { return false; }

signals:
    /// 寄存器值变化时发出（带上设备名、寄存器地址、值字节、数据类型）
    void modbusRegisterChanged(const QString &deviceName, int address,
                              const QByteArray &value, const QString &dataType);
    /// 寄存器当前解析值更新（地址、数值、文本显示）
    void registerCurrentValueChanged(int address, double value, const QString &displayText);
    /// 服务器模式：外部客户端写入了寄存器
    void registerWrittenByClient(int address, quint16 value);

private slots:
    void onPollTimeout();
    void onModbusStateChanged(int state);
    void onServerDataWritten(QModbusDataUnit::RegisterType table, int address, int size);
    void onServerStateChanged(int state);

private:
    /// 发起读请求；返回 false 表示请求未发出（调用方需回滚 pendingQueue 条目）
    bool readRegister(int slaveAddr, int regAddr, int count);
    double parseRawToValue(const QByteArray &raw, const QString &dataType, const QString &byteOrder) const;
    void syncServerRegisters();
    void applyRoleParam(const QString &mode);

    QModbusClient *m_modbus = nullptr;     /// 客户端模式使用的设备
    QModbusTcpServer *m_modbusServer = nullptr; /// 服务器模式使用的监听器
    Role m_role = MODBUS_CLIENT;

    // 关于"单源收口"（S1 残留专项）——**注：原注释记录的"5 个参数刻意不收口"已修订**。
    // 5 个 bool/int 参数（autoReconnect / reconnectInterval / pollInterval / slaveAddress / writeVerify）
    // 已按通信件套**同一口径**收口：唯一来源 = 参数表（默认值在 init() 写入），钳制放写侧。
    // 修订理由（为何推翻当初"不收口"的结论）：
    //  · 原注释称"没有对应的缺陷要修"——这一条不成立：旧实现下同一参数有**三个口径**
    //    （成员 = 钳后值 / 参数表 = 原值 / 读取方各取一份），toJson 写成员（钳后）、
    //    getParam 返回参数表（原值）⇒ 方案文件与运行期行为可能不一致。
    //    TCP / Serial / PLC 三个兄弟类已按同一口径修完，本类保持一致（避免"同族不同规"）。
    //  · 原注释中**仍然成立、本次照旧遵守**的部分：这些键**从不被非主线程写**
    //    （写路径只有 createDeviceNode / 设备配置对话框 / fromJson，均主线程；执行线程的参数写回
    //    只处理 QString 参数）⇒ 这里修的从来不是"竞态"，而是口径一致。
    //  · **已知行为变化（提交说明中已明写）**：fromJson 兼容分支改为走 setParam ⇒
    //    载入老方案时越界值会按 500ms/10ms 下限钳制（旧实现直接赋成员、不钳），
    //    且 pollInterval 会同步一次轮询定时器。属"把已文档化的下限真正执行"，无回归风险面。
    // **仍未收口（刻意保留）**：m_role —— 写路径带**对象重建副作用**
    //（applyRoleParam 会 closeConnection + 重新 openConnection），且 role() 是公开运行期接口；
    // 参数表里的 role 只是它的持久化表示。
    // 自动重连
    QTimer *m_reconnectTimer = nullptr;

    // 轮询
    QTimer *m_pollTimer = nullptr;

    // 寄存器表格
    QList<ModbusRegisterItem> m_registers;

    // 连接状态判据（S1）：m_everReallyConnected 为"曾真正连上"的唯一判据，避免 connectDevice()
    // 仅表示"异步发起"被误判为"曾连接" → 关闭从未建立的连接仍发假断开；m_userClosed 抑制用户
    // 主动关闭后的自动重连"复活"（TcpCommNode 早有同款 m_userClosed）。
    bool m_everReallyConnected = false;
    bool m_userClosed = false;
    // S4：回写三段确认（默认关闭）——段①发出写、段②从站回执 OK、段③回读同地址比对，
    // 不一致才上报。选项名 writeVerify，与 autoReconnect 同级（均属节点参数，不在表单上）。
    bool m_writeVerify = false;

    // 当前正在读取的索引
    int m_currentRegIdx = 0;

    struct PendingRead {
        int slaveAddr;
        int regAddr;
        int count;
        int regIndex;
    };
    QList<PendingRead> m_pendingQueue;
};
