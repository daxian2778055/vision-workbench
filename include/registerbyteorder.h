#pragma once

#include <QString>
#include <QVector>
#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <cstdint>
#include <cstring>
#include <cmath>

/// 寄存器字节序归一化工具：读、写路径共用同一约定，保证"写回的值等于读回的值"（往返对称）。
///
/// 约定（与 ModbusNode/PlcCommNode 的 parseRawToValue 严格配对）：
///   - 读：把设备寄存器字序列按 byteOrder 组装成"最终字节序列" raw，再由 parseRawToValue 统一按大端解释；
///   - 写：把目标解析值按 dataType/byteOrder 逆变换拆成要写入的寄存器字（地址递增：words[0]=低地址）。
///
/// byteOrder 语义（与 parseRawToValue 一致）：
///   ABCD = 原序；CDAB = 交换两字；BADC = 字内字节互换；DCBA = 字反转 + 字内互换。
/// 单寄存器（16 位）时：ABCD/CDAB 原样；BADC/DCBA 字内互换 = 高低字节颠倒。
namespace RegisterByteOrder {

/// 把寄存器 16 位字序列按字节序组装成最终原始字节（读路径与服务器写入共用）。
inline QByteArray assembleRegisterBytes(const QVector<quint16> &values, const QString &byteOrder)
{
    QByteArray raw;
    QDataStream stream(&raw, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);   // Modbus 是大端

    if (byteOrder == QStringLiteral("ABCD")) {
        for (quint16 v : values)
            stream << v;
    } else if (byteOrder == QStringLiteral("CDAB")) {
        if (values.size() >= 2)
            stream << values[1] << values[0];           // 32 位：交换两字
        else
            for (quint16 v : values) stream << v;       // 16 位单寄存器无"换字"，CDAB=ABCD 原样
    } else if (byteOrder == QStringLiteral("BADC")) {
        for (quint16 v : values)
            stream << quint16(((v & 0xFF) << 8) | ((v >> 8) & 0xFF));
    } else {
        for (int i = values.size() - 1; i >= 0; --i) {
            const quint16 v = values[i];
            stream << quint16(((v & 0xFF) << 8) | ((v >> 8) & 0xFF));
        }
    }
    return raw;
}

/// assembleRegisterBytes 的逆变换：把目标解析值按 dataType/byteOrder 拆成要写入的寄存器字。
/// 返回 false 表示不支持的数据类型。写回的 words 经 assemble 后再被 parseRawToValue 大端解释必得原值。
inline bool disassembleValueToWords(double value, const QString &dataType, const QString &byteOrder,
                                    QVector<quint16> &outWords)
{
    outWords.clear();
    auto bswap = [](quint16 v) -> quint16 {
        return quint16(((v & 0xFF) << 8) | ((v >> 8) & 0xFF));
    };

    if (dataType == QStringLiteral("int16") || dataType == QStringLiteral("uint16")) {
        quint16 word = static_cast<quint16>(static_cast<quint32>(qRound(value)) & 0xFFFFu);
        // 16 位：ABCD/CDAB 原样；BADC/DCBA 字内互换（与 assemble 配对）
        if (byteOrder == QStringLiteral("BADC") || byteOrder == QStringLiteral("DCBA"))
            word = bswap(word);
        outWords.append(word);
        return true;
    }

    if (dataType == QStringLiteral("int32") || dataType == QStringLiteral("uint32")
        || dataType == QStringLiteral("float")) {
        quint32 u = 0;
        if (dataType == QStringLiteral("float")) {
            const float f = static_cast<float>(value);
            std::memcpy(&u, &f, sizeof(u));           // 取 float 位模式
        } else if (dataType == QStringLiteral("int32")) {
            u = static_cast<quint32>(static_cast<qint32>(qRound(value)));   // 取有符号位模式
        } else {
            u = static_cast<quint32>(qRound(value));
        }
        const quint16 hi = static_cast<quint16>((u >> 16) & 0xFFFFu);
        const quint16 lo = static_cast<quint16>(u & 0xFFFFu);

        // 32 位：ABCD=原序(hi,lo)；CDAB=换字(lo,hi)；BADC=字内互换；DCBA=反转+字内互换
        if (byteOrder == QStringLiteral("ABCD")) {
            outWords.append(hi); outWords.append(lo);
        } else if (byteOrder == QStringLiteral("CDAB")) {
            outWords.append(lo); outWords.append(hi);
        } else if (byteOrder == QStringLiteral("BADC")) {
            outWords.append(bswap(hi)); outWords.append(bswap(lo));
        } else { // DCBA
            outWords.append(bswap(lo)); outWords.append(bswap(hi));
        }
        return true;
    }
    return false;
}

} // namespace RegisterByteOrder
