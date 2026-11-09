#pragma once

// Omron FINS 通信节点（VM 4.4 批次 B5-1）：FINS/TCP 与 FINS/UDP 两个传输面共用一套
// 寄存器表与轮询逻辑。协议编解码在 FinsFrame.h（纯函数、离线可测），本类只管链路与轮询。
//
// 离线可测面：本机回环（QTcpServer / QUdpSocket 模拟 PLC）即可跑通握手、读轮询与写回；
// 真机（DM/CIO 区地址口径、节点号分配）仍需现场验证。

#include "CommunicationNodeBase.h"
#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>

/// FINS 寄存器监控项（与 ModbusRegisterItem 同口径：地址/类型/访问/启用/描述）
struct FinsRegisterItem {
    QString area = QStringLiteral("DM");          ///< DM / CIO / WR / HR / AR
    int address = 0;                              ///< 起始字地址（按区判定的字地址，不是字节地址）
    QString dataType = QStringLiteral("int16");   ///< int16 / uint16 / int32 / uint32 / float
    QString accessMode = QStringLiteral("Read");  ///< Read / ReadWrite / Write
    bool enabled = true;                          ///< 是否轮询此寄存器
    QString description;                          ///< 描述名称

    // 运行时（显示用；不进 toJson）
    double currentValue = 0;
    QString displayValue;
    bool hasValue = false;
};

/// FINS 通信节点 — 客户端（主动连 PLC / 向 PLC 发 UDP），带节点地址握手、轮询、写回、
/// 请求超时与断线自动重连。
class FinsCommNode : public CommunicationNodeBase
{
    Q_OBJECT
public:
    enum Transport {
        FinsTcp = 0,   ///< FINS/TCP：先做节点地址握手，之后每帧带 16 字节 FINS/TCP 帧头
        FinsUdp = 1    ///< FINS/UDP：每个数据报就是一帧裸 FINS 帧（无帧头）
    };

    explicit FinsCommNode(Transport t, QObject *parent = nullptr);

    void init() override;
    bool openConnection() override;
    void closeConnection() override;
    bool isConnected() const override;

    void setParam(const QString &name, const QVariant &value) override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &json) override;

    // ---- 寄存器管理 ----
    void setRegisters(const QList<FinsRegisterItem> &regs);
    QList<FinsRegisterItem> registers() const { return m_registers; }

    /// 写寄存器（0x0102 Memory Area Write）。按该地址登记项的 area/dataType 组帧。
    /// @return 只表示"请求是否已发出"：false = 未发出（未连接/未完成握手/地址未登记/
    ///         类型不支持）；true = 已发出。**不表示写入成功**——PLC 回执里的 endCode
    ///         非 0 或超时一律经 communicationError 上报（与 ModbusNode 同口径）。
    bool writeRegister(int address, double value);

    /// 当前解析值（供 UI 实时显示）
    double registerCurrentValue(int address) const;
    QString registerDisplayValue(int address) const;

    Transport transport() const { return m_transport; }

    /// FINS 是寄存器语义（写寄存器），不支持裸字节发送：上层据此显式失败而非静默 no-op
    bool supportsRawSend() const override { return false; }

private slots:
    void onPollTimeout();
    void onTcpReadyRead();
    void onTcpConnected();
    void onTcpDisconnected();
    void onUdpReadyRead();
    void onRequestTimeout();
    void onReconnectTimeout();

private:
    /// 按传输层封装发送一帧 FINS 帧（TCP 加帧头 / UDP 原样发数据报）；false = 未发出
    bool sendFinsFrame(const QByteArray &finsFrame);
    /// 处理一帧 FINS 响应（命令码 0x0101 读 / 0x0102 写）
    void handleFinsResponse(const QByteArray &finsFrame);
    /// 发起第 index 个寄存器项的读请求
    bool sendReadForIndex(int index);
    /// 链路已坏：清 pending、拆 socket、（按配置）排重连
    void dropLink(const QString &why);

    quint8 destinationNode() const;
    quint8 sourceNode() const;

    Transport m_transport;

    QTcpSocket *m_tcp = nullptr;
    QUdpSocket *m_udp = nullptr;

    QTimer *m_pollTimer = nullptr;      ///< 轮询
    QTimer *m_reconnectTimer = nullptr; ///< 自动重连（单次）
    QTimer *m_requestTimer = nullptr;   ///< 单次请求超时兜底

    QByteArray m_rxBuffer;              ///< TCP 粘包/半包累积缓冲
    bool m_handshakeDone = false;       ///< FINS/TCP 节点地址握手完成
    quint32 m_clientNode = 0;           ///< 握手得到的本机节点号
    quint32 m_serverNode = 0;           ///< 握手得到的 PLC 节点号

    int m_pollIndex = 0;                ///< 轮询游标（轮转，始终只压一个在途请求）
    bool m_requestPending = false;
    int m_pendingIndex = -1;            ///< 在途读请求对应的寄存器项下标

    bool m_userClosed = false;          ///< 用户主动关闭：不触发自动重连

    QList<FinsRegisterItem> m_registers;
};
