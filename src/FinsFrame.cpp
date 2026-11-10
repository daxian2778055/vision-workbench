#include "FinsFrame.h"

#include <cstring>

namespace Fins {

namespace {

inline void appendU16(QByteArray &out, quint16 v)
{
    out.append(char((v >> 8) & 0xFF));
    out.append(char(v & 0xFF));
}

inline void appendU32(QByteArray &out, quint32 v)
{
    out.append(char((v >> 24) & 0xFF));
    out.append(char((v >> 16) & 0xFF));
    out.append(char((v >> 8) & 0xFF));
    out.append(char(v & 0xFF));
}

inline quint16 readU16(const QByteArray &buf, int pos)
{
    return quint16((quint8(buf.at(pos)) << 8) | quint8(buf.at(pos + 1)));
}

inline quint32 readU32(const QByteArray &buf, int pos)
{
    return (quint32(quint8(buf.at(pos))) << 24)
           | (quint32(quint8(buf.at(pos + 1))) << 16)
           | (quint32(quint8(buf.at(pos + 2))) << 8)
           | quint32(quint8(buf.at(pos + 3)));
}

}   // namespace

quint16 areaCode(const QString &area)
{
    const QString a = area.trimmed().toUpper();
    if (a == QStringLiteral("DM"))  return DM;
    if (a == QStringLiteral("CIO")) return CIO;
    if (a == QStringLiteral("WR"))  return WR;
    if (a == QStringLiteral("HR"))  return HR;
    if (a == QStringLiteral("AR"))  return AR;
    return 0;
}

QByteArray tcpHeader(quint32 command, quint32 errorCode, const QByteArray &data)
{
    QByteArray out;
    out.reserve(16 + data.size());
    out.append("FINS", 4);
    appendU32(out, quint32(8 + data.size()));   // 长度 = 命令(4) + 错误码(4) + 数据
    appendU32(out, command);
    appendU32(out, errorCode);
    out.append(data);
    return out;
}

bool parseTcpHeader(const QByteArray &buf, quint32 *length, quint32 *command,
                    quint32 *errorCode, int *headerBytes)
{
    // 16 字节（magic4 + 长度4 + 命令4 + 错误码4）就能拆出帧头。
    // 旧实现要求 20 字节、且要求「长度 ≤ 当前缓冲 - 4」，把**正常分片**也判成
    // 解析失败；调用方随即清空整段缓冲，真机 TCP 分片下会永久失步（2026-11-10 审核）。
    if (buf.size() < 16)
        return false;
    if (buf.at(0) != 'F' || buf.at(1) != 'I' || buf.at(2) != 'N' || buf.at(3) != 'S')
        return false;
    const quint32 len = readU32(buf, 4);
    if (len < 8)
        return false;
    // 只校验长度上界（防损坏/恶意长度让接收端无限等或吃满内存）；
    // 「数据是否已收齐」交给调用方用 headerBytes 与缓冲长度比较。
    if (len > 1024u * 1024u)
        return false;
    if (length)
        *length = len;
    if (command)
        *command = readU32(buf, 8);
    if (errorCode)
        *errorCode = readU32(buf, 12);
    // 整帧 = 4('FINS') + 4(长度) + 4(命令) + 4(错误码) + (length-8) 数据 = length + 8
    if (headerBytes)
        *headerBytes = int(len) + 8;
    return true;
}

QByteArray nodeAddressRequest()
{
    // 长度字段 0x0C = 命令(4) + 错误码(4) + 客户端节点地址(4)，整帧 20 字节。
    // 客户端节点地址填 0 = 请 PLC 分配（W342 节点地址请求）。
    return tcpHeader(0x00000000u, 0u, QByteArray(4, '\0'));
}

bool parseNodeAddressResponse(const QByteArray &payload, quint32 *clientNode, quint32 *serverNode)
{
    if (payload.size() < 8)
        return false;
    if (clientNode)
        *clientNode = readU32(payload, 0);
    if (serverNode)
        *serverNode = readU32(payload, 4);
    return true;
}

QByteArray frameSend(const QByteArray &finsFrame)
{
    return tcpHeader(0x00000002u, 0u, finsFrame);
}

QByteArray command(quint16 commandCode, quint8 da1, quint8 sa1, const QByteArray &params)
{
    QByteArray f;
    f.reserve(12 + params.size());
    f.append(char(0x80));   // ICF：bit7=1 命令，bit6=0 需要响应
    f.append(char(0x00));   // RSV
    f.append(char(0x02));   // GCT（网关计数）
    f.append(char(0x00));   // DNA（目标网络号）
    f.append(char(da1));    // DA1（目标节点号）
    f.append(char(0x00));   // DA2（目标单元号）
    f.append(char(0x00));   // SNA（源网络号）
    f.append(char(sa1));    // SA1（源节点号）
    f.append(char(0x00));   // SA2（源单元号）
    f.append(char(0x00));   // SID（服务 ID）
    appendU16(f, commandCode);
    f.append(params);
    return f;
}

QByteArray readMemoryParams(quint16 area, quint16 address, quint16 count)
{
    QByteArray p;
    p.reserve(6);
    // Omron W342：存储区码字段是「区域码字节 + 0x00」（字访问），例如 DM -> 82 00。
    // 不能按 BE 的 quint16 直接写 0x0082——真机报文不认（2026-11-09 对照 W342/公开帧例修正）。
    p.append(char(area & 0xFF));
    p.append(char(0x00));
    appendU16(p, address);
    appendU16(p, count);
    return p;
}

QByteArray writeMemoryParams(quint16 area, quint16 address, quint16 count,
                             const QByteArray &valuesBigEndian)
{
    QByteArray p = readMemoryParams(area, address, count);
    p.append(valuesBigEndian);
    return p;
}

bool parseResponse(const QByteArray &finsFrame, quint16 expectedCommand,
                   quint16 *endCode, QByteArray *data)
{
    // 12 字节头部（ICF..SID + 命令码）+ 至少 2 字节 endCode
    if (finsFrame.size() < 14)
        return false;
    if (readU16(finsFrame, 10) != expectedCommand)
        return false;
    if (endCode)
        *endCode = readU16(finsFrame, 12);
    if (data)
        *data = finsFrame.mid(14);
    return true;
}

QByteArray encodeWords(const QVector<quint16> &words)
{
    QByteArray out;
    out.reserve(words.size() * 2);
    for (quint16 w : words)
        appendU16(out, w);
    return out;
}

QVector<quint16> decodeWords(const QByteArray &data)
{
    QVector<quint16> words;
    words.reserve(data.size() / 2);
    for (int i = 0; i + 1 < data.size(); i += 2)
        words.append(readU16(data, i));
    return words;
}

int wordCount(const QString &dataType)
{
    const QString t = dataType.trimmed().toLower();
    if (t == QStringLiteral("int16") || t == QStringLiteral("uint16"))
        return 1;
    if (t == QStringLiteral("int32") || t == QStringLiteral("uint32")
        || t == QStringLiteral("float"))
        return 2;
    return 0;
}

QByteArray encodeValue(const QString &dataType, double value)
{
    const QString t = dataType.trimmed().toLower();
    QByteArray out;
    if (t == QStringLiteral("int16") || t == QStringLiteral("uint16")) {
        // 统一经 qint64 收窄：负数按二补码、越界按 16 位回绕（与 PLC 侧字口径一致）
        appendU16(out, quint16(quint32(qint64(value)) & 0xFFFF));
        return out;
    }
    if (t == QStringLiteral("int32") || t == QStringLiteral("uint32")) {
        appendU32(out, quint32(qint64(value)));
        return out;
    }
    if (t == QStringLiteral("float")) {
        const float f = float(value);
        quint32 bits = 0;
        std::memcpy(&bits, &f, sizeof(bits));
        appendU32(out, bits);
        return out;
    }
    return QByteArray();
}

bool decodeValue(const QString &dataType, const QByteArray &words, double *out)
{
    if (!out)
        return false;
    const QString t = dataType.trimmed().toLower();
    const int wc = wordCount(t);
    if (wc == 0 || words.size() < wc * 2)
        return false;

    if (wc == 1) {
        const quint16 u = readU16(words, 0);
        *out = (t == QStringLiteral("int16")) ? double(qint16(u)) : double(u);
        return true;
    }

    const quint32 u = readU32(words, 0);
    if (t == QStringLiteral("int32")) {
        *out = double(qint32(u));
        return true;
    }
    if (t == QStringLiteral("uint32")) {
        *out = double(u);
        return true;
    }
    float f = 0.0f;
    std::memcpy(&f, &u, sizeof(f));
    *out = double(f);
    return true;
}

}   // namespace Fins
