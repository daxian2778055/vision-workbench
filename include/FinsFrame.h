#pragma once

// Omron FINS 协议离线编解码（公开规范 W342）——纯函数，不依赖 socket，
// 由 FINS/TCP 与 FINS/UDP 两个传输面共用。所有多字节字段一律**大端**（big-endian）。
//
// 两层结构（与实际报文逐字节对应）：
//   ① FINS/TCP 帧头（16 字节）：'F','I','N','S' + 长度(4) + 命令(4) + 错误码(4)，
//      长度 = 8 + data.size()（命令 + 错误码 + 数据）。FINS/UDP 没有这层，直接发 FINS 帧。
//   ② FINS 帧：ICF RSV GCT DNA DA1 DA2 SNA SA1 SA2 SID（10 字节）+ 命令码(2) + 参数区。
//
// 帧头里的「命令」不是 FINS 命令码：0x00000000 = 节点地址请求，
// 0x00000001 = 节点地址响应，0x00000002 = 帧发送（真正的 FINS 帧塞在 data 里）。

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace Fins {

/// 存储区字代码（W342 Memory Area 表；此处只用字访问的四个区 + CIO）
enum AreaCode : quint16 {
    DM  = 0x82,   ///< 数据存储器 DM
    CIO = 0xB0,   ///< 内部 CIO
    WR  = 0xB1,   ///< 工作区 WR
    HR  = 0xB2,   ///< 保持区 HR
    AR  = 0xB3    ///< 辅助区 AR
};

/// 存储区名 -> 字代码（"DM"/"CIO"/"WR"/"HR"/"AR"，大小写不敏感）；未知返回 0
quint16 areaCode(const QString &area);

// ---- FINS/TCP 帧头 ----
/// 'F','I','N','S' + 4 字节长度（大端 = 8 + data.size()）+ 4 字节命令 + 4 字节错误码 + data
QByteArray tcpHeader(quint32 command, quint32 errorCode, const QByteArray &data);

/// 拆 FINS/TCP 帧头：<20 字节 / magic 不符 / 长度非法（<8 或 > buf.size()-4）返回 false。
/// headerBytes = 4 + 4 + 4 + 4 + (length - 8) = length + 8（= 整帧字节数）。
bool parseTcpHeader(const QByteArray &buf, quint32 *length, quint32 *command,
                    quint32 *errorCode, int *headerBytes);

/// 节点地址请求（命令 0x00000000）：长度字段 0x0C = 命令(4) + 错误码(4) + 客户端节点地址(4)，
/// 整帧 20 字节：46 49 4E 53 00 00 00 0C + 00000000 + 00000000 + 00000000（客户端节点地址 = 0）
QByteArray nodeAddressRequest();

/// 节点地址响应里的 8 字节数据：客户端节点号(4) + 服务端节点号(4)；不足 8 字节返回 false
bool parseNodeAddressResponse(const QByteArray &payload, quint32 *clientNode, quint32 *serverNode);

/// 帧发送：tcpHeader(0x00000002, 0, finsFrame)
QByteArray frameSend(const QByteArray &finsFrame);

// ---- FINS 帧 ----
/// ICF=0x80, RSV=0, GCT=0x02, DNA=0, DA1, DA2=0, SNA=0, SA1, SA2=0, SID=0,
/// 命令码(2 字节大端), 参数区
QByteArray command(quint16 commandCode, quint8 da1, quint8 sa1, const QByteArray &params);

/// 存储区读参数：区码(2) + 起始地址(2) + 字数(2)，均大端
QByteArray readMemoryParams(quint16 area, quint16 address, quint16 count);

/// 存储区写参数：区码(2) + 起始地址(2) + 字数(2) + 写入值（大端）
QByteArray writeMemoryParams(quint16 area, quint16 address, quint16 count,
                             const QByteArray &valuesBigEndian);

/// 解析 FINS 响应：校验 12 字节头部（ICF..SID + 命令码）与期望命令码一致，
/// 返回 endCode（第一个参数，2 字节大端）与其余数据。帧过短 / 命令码不符返回 false。
bool parseResponse(const QByteArray &finsFrame, quint16 expectedCommand,
                   quint16 *endCode, QByteArray *data);

// ---- 值编解码（大端） ----
QByteArray encodeWords(const QVector<quint16> &words);
QVector<quint16> decodeWords(const QByteArray &data);

/// 数据类型占用的字数：int16/uint16 = 1，int32/uint32/float = 2，未知 = 0
int wordCount(const QString &dataType);

/// 按 dataType 编码成 2 或 4 字节大端；未知类型返回空
QByteArray encodeValue(const QString &dataType, double value);

/// 按 dataType 从 2/4 字节大端还原数值；未知类型或字节不足返回 false
bool decodeValue(const QString &dataType, const QByteArray &words, double *out);

}   // namespace Fins
