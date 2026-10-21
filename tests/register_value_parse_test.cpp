// RegisterValueParseTest（词条切片 B 寄存器族首抽）：寄存器值换算口径的直接断言座。
// 纯数据断言：只喂字节序列与类型名，断言换算出的数值与长度守卫与未知类型处置；
// 不触节点、不触通信栈（座不需 SDK 与 GUI）。
// 红臂破坏位 = uint32 分支退化为恒零（历史缺陷形态）——无符号腿当场红，余腿臂内原样绿。
#include <QtTest>
#include <QByteArray>
#include <QString>
#include <QVector>

#include "registerbyteorder.h"

namespace {

QByteArray bytes(std::initializer_list<int> values)
{
    QByteArray b;
    for (int v : values)
        b.append(static_cast<char>(static_cast<quint8>(v)));
    return b;
}

}   // namespace

class RegisterValueParseTest : public QObject
{
    Q_OBJECT

private slots:
    void testInt16SignedAndUnsigned();  // 16 位有符号与无符号
    void testInt32Signed();             // 32 位有符号
    void testUint32Unsigned();          // 32 位无符号（红臂靶腿）
    void testFloat32();                 // 单精度浮点
    void testLengthGuards();            // 长度守卫与空载荷
    void testUnknownTypeZero();         // 未知类型按零值
    void testPairingWithAssemble();     // 与装配段严格配对（往返口径）
};

void RegisterValueParseTest::testInt16SignedAndUnsigned()
{
    const QByteArray raw = bytes({0x12, 0x34});
    QCOMPARE(RegisterByteOrder::parseRawToValue(raw, QStringLiteral("int16"),
                                                 QStringLiteral("ABCD")), 4660.0);
    QCOMPARE(RegisterByteOrder::parseRawToValue(raw, QStringLiteral("uint16"),
                                                 QStringLiteral("ABCD")), 4660.0);
    // 负值按大端补码解释
    QCOMPARE(RegisterByteOrder::parseRawToValue(bytes({0xFF, 0xFE}), QStringLiteral("int16"),
                                                 QStringLiteral("ABCD")), -2.0);
}

void RegisterValueParseTest::testInt32Signed()
{
    QCOMPARE(RegisterByteOrder::parseRawToValue(bytes({0x00, 0x00, 0x01, 0x00}),
                                                 QStringLiteral("int32"),
                                                 QStringLiteral("ABCD")), 256.0);
    QCOMPARE(RegisterByteOrder::parseRawToValue(bytes({0xFF, 0xFF, 0xFF, 0x00}),
                                                 QStringLiteral("int32"),
                                                 QStringLiteral("ABCD")), -256.0);
}

void RegisterValueParseTest::testUint32Unsigned()
{
    // uint32 分支此前缺失 → 恒返回零；本腿把该历史缺陷钉死
    QCOMPARE(RegisterByteOrder::parseRawToValue(bytes({0xFF, 0xFF, 0xFF, 0xFF}),
                                                 QStringLiteral("uint32"),
                                                 QStringLiteral("ABCD")),
             4294967295.0);
    QCOMPARE(RegisterByteOrder::parseRawToValue(bytes({0x00, 0x00, 0x01, 0x00}),
                                                 QStringLiteral("uint32"),
                                                 QStringLiteral("ABCD")), 256.0);
}

void RegisterValueParseTest::testFloat32()
{
    // 1.5 的 float32 位模式 0x3FC00000，按大端**四字节**读出
    // （U-187 行为变更轮：原实现用 QDataStream 的浮点读取，而该读取按八字节取，
    //   四字节载荷读不满即留零——故 float 寄存器读值恒零，本腿即该缺陷的自证位）
    QCOMPARE(RegisterByteOrder::parseRawToValue(bytes({0x3F, 0xC0, 0x00, 0x00}),
                                                 QStringLiteral("float"),
                                                 QStringLiteral("ABCD")), 1.5);
    // 负值：-1.5 的 float32 位模式 0xBFC00000
    QCOMPARE(RegisterByteOrder::parseRawToValue(bytes({0xBF, 0xC0, 0x00, 0x00}),
                                                 QStringLiteral("float"),
                                                 QStringLiteral("ABCD")), -1.5);
}

void RegisterValueParseTest::testLengthGuards()
{
    // 空载荷
    QCOMPARE(RegisterByteOrder::parseRawToValue(QByteArray(), QStringLiteral("int16"),
                                                 QStringLiteral("ABCD")), 0.0);
    // 16 位不足两字节
    QCOMPARE(RegisterByteOrder::parseRawToValue(bytes({0x12}), QStringLiteral("int16"),
                                                 QStringLiteral("ABCD")), 0.0);
    // 32 位不足四字节
    QCOMPARE(RegisterByteOrder::parseRawToValue(bytes({0x12, 0x34, 0x56}), QStringLiteral("int32"),
                                                 QStringLiteral("ABCD")), 0.0);
}

void RegisterValueParseTest::testUnknownTypeZero()
{
    // 节点侧口径只受理 16/32 位四种类型；8 位类型属接收事件的原始报文口径（另一套），此处按零值
    QCOMPARE(RegisterByteOrder::parseRawToValue(bytes({0x7F}), QStringLiteral("uint8"),
                                                 QStringLiteral("ABCD")), 0.0);
    QCOMPARE(RegisterByteOrder::parseRawToValue(bytes({0x7F}), QStringLiteral("int8"),
                                                 QStringLiteral("ABCD")), 0.0);
    QCOMPARE(RegisterByteOrder::parseRawToValue(bytes({0x12, 0x34}), QStringLiteral("double"),
                                                 QStringLiteral("ABCD")), 0.0);
}

void RegisterValueParseTest::testPairingWithAssemble()
{
    // 与装配段严格配对：16 位 ABCD 原样往返
    const QByteArray abcd = RegisterByteOrder::assembleRegisterBytes({0x1234},
                                                                      QStringLiteral("ABCD"));
    QCOMPARE(RegisterByteOrder::parseRawToValue(abcd, QStringLiteral("int16"),
                                                 QStringLiteral("ABCD")), 4660.0);
    // 16 位 BADC：字内字节互换（高低字节颠倒）后按大端解释
    const QByteArray badc = RegisterByteOrder::assembleRegisterBytes({0x1234},
                                                                      QStringLiteral("BADC"));
    QCOMPARE(RegisterByteOrder::parseRawToValue(badc, QStringLiteral("int16"),
                                                 QStringLiteral("ABCD")), 13330.0);   // 0x3412
    // 32 位：两字原序装成四字节后按大端解释
    const QByteArray wide = RegisterByteOrder::assembleRegisterBytes({0x0001, 0x0000},
                                                                     QStringLiteral("ABCD"));
    QCOMPARE(RegisterByteOrder::parseRawToValue(wide, QStringLiteral("int32"),
                                                 QStringLiteral("ABCD")), 65536.0);
}

QTEST_MAIN(RegisterValueParseTest)
#include "register_value_parse_test.moc"
