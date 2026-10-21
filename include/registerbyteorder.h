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
/// 约定（读路径的装配与值换算同在本面，两节点类不再各写一份）：
///   - 读：把设备寄存器字序列按 byteOrder 组装成"最终字节序列" raw，再由本面的 parseRawToValue
///     统一按大端解释；
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

/// 读路径的值换算（与 assembleRegisterBytes 严格配对）：把**已归一化**的最终字节序列按大端解释成数值。
/// 16 位：归一化已在装配段完成，此处统一大端解释（与 32 位路径一致；此前 16 位用 order 小端——
/// 单寄存器时与「归一化不交换」相互抵消看似正确，但 16/32 位逻辑不统一）。强制大端对单寄存器行为中性：
/// ABCD 不变，BADC/DCBA 与「归一化已交换」配大端结果完全一致。
/// 32 位：同理统一大端解释（与接收事件的原始报文取值规范一致）；强制大端可同时修掉 BADC 双重交换与
/// DCBA 反向（旧实现按 order 小端再换一次）；uint32 分支此前无 → 恒返回 0.0。
/// 长度不足（16 位不足两字节、32 位不足四字节）与空载荷一律按零值处理；未知类型同样按零值处理。
/// byteOrder 形参保留为调用形稳定（原体即不读：归一化已在装配段完成）。
/// 归并记：本函数此前在 ModbusNode 与 PlcCommNode 各有一份**逐行同码**的私有实现（两处口径必须一致，
/// 否则两节点读值不同）——按本面「口径只此一处」的立面缘由并入此处，两类成员与其调用位一并收口。
inline double parseRawToValue(const QByteArray &raw, const QString &dataType, const QString &byteOrder)
{
    if (raw.isEmpty()) return 0.0;

    if (dataType == QStringLiteral("int16")) {
        if (raw.size() < 2) return 0.0;
        qint16 val;
        QDataStream s(raw);
        s.setByteOrder(QDataStream::BigEndian);
        s >> val;
        return static_cast<double>(val);
    } else if (dataType == QStringLiteral("uint16")) {
        if (raw.size() < 2) return 0.0;
        quint16 val;
        QDataStream s(raw);
        s.setByteOrder(QDataStream::BigEndian);
        s >> val;
        return static_cast<double>(val);
    } else if (dataType == QStringLiteral("int32") || dataType == QStringLiteral("uint32")
               || dataType == QStringLiteral("float")) {
        if (raw.size() < 4) return 0.0;
        QDataStream s(raw);
        s.setByteOrder(QDataStream::BigEndian);
        if (dataType == QStringLiteral("int32")) {
            qint32 val;
            s >> val;
            return static_cast<double>(val);
        } else if (dataType == QStringLiteral("uint32")) {
            quint32 val;
            s >> val;
            return static_cast<double>(val);
        } else {
            float val;
            s >> val;
            return static_cast<double>(val);
        }
    }
    return 0.0;
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

/// 回写校验（S4 段③）判定：目标值与"回读后按同一约定解析出的值"是否视为一致。
/// 整数按类型位宽截断后精确比较（写入侧本就按位宽截断，截断后相等即视为成功）；
/// float 必须有容差——100.1 经 float32 精度回转必为 100.099998…，精确比较会天天假报警。
inline bool valueMatches(double expected, double actual, const QString &dataType)
{
    if (dataType == QStringLiteral("float")) {
        const double diff = std::fabs(expected - actual);
        const double scale = qMax(1.0, qMax(std::fabs(expected), std::fabs(actual)));
        return diff <= 1e-6 * scale;   // 相对容差 1e-6：覆盖 float32 的 ~7 位有效数字
    }
    if (dataType == QStringLiteral("int16") || dataType == QStringLiteral("uint16")) {
        const auto wrap16 = [](double v) {
            return static_cast<quint32>(static_cast<qint32>(qRound(v))) & 0xFFFFu;
        };
        return wrap16(expected) == wrap16(actual);
    }
    if (dataType == QStringLiteral("int32") || dataType == QStringLiteral("uint32")) {
        const auto wrap32 = [](double v) {
            return static_cast<quint32>(static_cast<qint64>(qRound(v)));
        };
        return wrap32(expected) == wrap32(actual);
    }
    return std::fabs(expected - actual) < 1e-9;
}

/// 单个值占用的寄存器字数（16 位 1 字；32 位 2 字）。
/// M-4：宽度判定只能有这一处实现——此前"回读字数"在 Modbus/PLC 各写一份 wide 判定，
/// 与 disassembleValueToWords 内部的分支共三份，日后支持 double 必漏改。
inline int wordCountForType(const QString &dataType)
{
    if (dataType == QStringLiteral("int32") || dataType == QStringLiteral("uint32")
        || dataType == QStringLiteral("float"))
        return 2;
    return 1;
}

} // namespace RegisterByteOrder
