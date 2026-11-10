// Omron FINS（VM 4.4 B5-1）离线用例：协议编解码逐字节 + 本机回环（QTcpServer / QUdpSocket）
// 模拟 PLC 的握手、读轮询与寄存器值上报。不碰真机——真机验证（DM/CIO 地址口径、节点号分配、
// 端口 9600/7920 的现场差异）留在现场联调。
//
// 报文口径（W342，全部大端）：
//   FINS/TCP 帧头 = 'F','I','N','S' + 长度(4, = 8+data.size()) + 命令(4) + 错误码(4)；
//   命令 0 节点地址请求，1 节点地址响应，2 帧发送（FINS 帧本体）。
//   FINS 帧 = ICF RSV GCT DNA DA1 DA2 SNA SA1 SA2 SID（10 字节）+ 命令码(2) + 参数区。
//   存储区字代码：DM=82 / CIO=B0 / WR=B1 / HR=B2 / AR=B3，报文里写作「区域码字节 + 0x00」（DM -> 82 00）。
#include <QtTest>
#include <QCheckBox>
#include <QComboBox>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QNetworkDatagram>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>

#include <memory>

#include "CommunicationNodeBase.h"
#include "FinsCommNode.h"
#include "FinsConfigDialog.h"
#include "FinsFrame.h"

namespace {

/// 回环服务端观测状态（握手请求原文 / 最近一帧 FINS 请求 / 收发计数）
struct LoopState {
    QByteArray buffer;          ///< TCP 侧累积缓冲
    QByteArray nodeAddrRequest; ///< 收到的节点地址请求整帧
    QByteArray lastFinsFrame;   ///< 最近一帧 FINS 读请求（裸 FINS 帧）
    QByteArray lastWriteFrame;  ///< 最近一帧 FINS 写请求（裸 FINS 帧）
    int finsFrameCount = 0;
    int writeFrameCount = 0;
};

/// 把一条 Memory Area Read 请求变成合法响应：头部照抄后 ICF=0xC0、DA1/SA1 互换，
/// MC=0101、endCode=0000、数据 12 34（即 DM 区读回 0x1234）。
QByteArray finsReadResponse(const QByteArray &requestFinsFrame)
{
    QByteArray resp = requestFinsFrame.left(10);   // ICF..SID
    resp[0] = char(0xC0);                          // 响应帧（bit7=1，bit6=1 无后续）
    resp[4] = requestFinsFrame.at(7);              // DA1 <- 请求的 SA1（源/宿互换）
    resp[7] = requestFinsFrame.at(4);              // SA1 <- 请求的 DA1
    resp.append(char(0x01)).append(char(0x01));    // MC：Memory Area Read 回执
    resp.append(char(0x00)).append(char(0x00));    // endCode：正常
    resp.append(char(0x12)).append(char(0x34));    // 数据：0x1234
    return resp;
}

/// 写请求回执：MC=0102、endCode=0000、无数据
QByteArray finsWriteResponse(const QByteArray &requestFinsFrame)
{
    QByteArray resp = requestFinsFrame.left(10);
    resp[0] = char(0xC0);
    resp[4] = requestFinsFrame.at(7);
    resp[7] = requestFinsFrame.at(4);
    resp.append(char(0x01)).append(char(0x02));    // MC：Memory Area Write 回执
    resp.append(char(0x00)).append(char(0x00));    // endCode：正常
    return resp;
}

/// 请求的 MC（FINS 帧第 10/11 字节）
quint16 requestMc(const QByteArray &finsFrame)
{
    if (finsFrame.size() < 12)
        return 0;
    return quint16((quint8(finsFrame.at(10)) << 8) | quint8(finsFrame.at(11)));
}

// ---- mock 侧独立的 FINS/TCP 帧头解析与构造 ----
// 刻意**不调用 Fins::***（被测函数）：否则组包/拆包口径一旦写错，mock 会跟着一起错，
// 回环腿仍然全绿，形成「实现 + 测试 + 桩」自证闭环（2026-11-10 审核）。
// 这里按公开报文格式逐字节独立实现，只用于模拟 PLC，不参与产品代码。
quint32 readBe32(const QByteArray &b, int off)
{
    return (quint32(quint8(b.at(off))) << 24)
         | (quint32(quint8(b.at(off + 1))) << 16)
         | (quint32(quint8(b.at(off + 2))) << 8)
         | quint32(quint8(b.at(off + 3)));
}

void appendBe32(QByteArray &out, quint32 v)
{
    out.append(char(quint8(v >> 24)));
    out.append(char(quint8(v >> 16)));
    out.append(char(quint8(v >> 8)));
    out.append(char(quint8(v)));
}

/// 返回整帧字节数（length + 8）；帧头不足/magic 错/长度非法返回 false
bool mockParseTcpFrame(const QByteArray &buf, quint32 *length, quint32 *command,
                       quint32 *errorCode, int *totalBytes)
{
    if (buf.size() < 16 || buf.left(4) != QByteArray("FINS", 4))
        return false;
    const quint32 len = readBe32(buf, 4);
    if (len < 8 || len > 1024u * 1024u)
        return false;
    if (length) *length = len;
    if (command) *command = readBe32(buf, 8);
    if (errorCode) *errorCode = readBe32(buf, 12);
    if (totalBytes) *totalBytes = int(len) + 8;
    return true;
}

QByteArray mockTcpFrame(quint32 command, quint32 errorCode, const QByteArray &data)
{
    QByteArray out("FINS", 4);
    appendBe32(out, quint32(8 + data.size()));   // 长度 = 命令+错误码+data
    appendBe32(out, command);
    appendBe32(out, errorCode);
    out.append(data);
    return out;
}

/// 起一个 FINS/TCP 回环服务端（节点地址握手 + Memory Area Read/Write 全应答）。
/// 返回监听端口；listen 失败返回 0。观测值全部写进 state。
quint16 startFinsTcpServer(QTcpServer &server, const std::shared_ptr<LoopState> &state)
{
    if (!server.listen(QHostAddress::LocalHost, 0))
        return 0;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, state]() {
        QTcpSocket *cli = server.nextPendingConnection();
        if (!cli)
            return;
        QObject::connect(cli, &QTcpSocket::readyRead, cli, [cli, state]() {
            state->buffer.append(cli->readAll());
            for (;;) {
                quint32 length = 0;
                quint32 command = 0;
                quint32 errorCode = 0;
                int headerBytes = 0;
                if (!mockParseTcpFrame(state->buffer, &length, &command, &errorCode,
                                       &headerBytes))
                    break;
                if (headerBytes > state->buffer.size())
                    break;   // 半包
                const QByteArray frame = state->buffer.left(headerBytes);
                const QByteArray data = frame.mid(16, int(length) - 8);
                state->buffer.remove(0, headerBytes);

                if (command == 0x00000000u) {          // 节点地址请求
                    // 保存**收到的原文**：旧版用 Fins::tcpHeader 重建观测值，
                    // 等于用被测函数造出「收到的数据」，拆包错了也看不出来。
                    if (state->nodeAddrRequest.isEmpty())
                        state->nodeAddrRequest = frame;
                    // 节点地址响应：客户端节点 = 1，服务端（PLC）节点 = 2
                    QByteArray payload;
                    payload.append(char(0x00)).append(char(0x00))
                        .append(char(0x00)).append(char(0x01));
                    payload.append(char(0x00)).append(char(0x00))
                        .append(char(0x00)).append(char(0x02));
                    cli->write(mockTcpFrame(0x00000001u, 0u, payload));
                    cli->flush();
                } else if (command == 0x00000002u) {   // 帧发送：FINS 帧本体
                    if (requestMc(data) == 0x0102u) {
                        state->lastWriteFrame = data;
                        ++state->writeFrameCount;
                        cli->write(mockTcpFrame(0x00000002u, 0u, finsWriteResponse(data)));
                    } else {
                        state->lastFinsFrame = data;
                        ++state->finsFrameCount;
                        cli->write(mockTcpFrame(0x00000002u, 0u, finsReadResponse(data)));
                    }
                    cli->flush();
                }
            }
        });
    });
    return server.serverPort();
}

}   // namespace

class FinsTest : public QObject
{
    Q_OBJECT

private slots:
    void codecNodeAddressRequestBytes();
    void codecReadWriteFrames();
    void codecValueRoundTrip();
    void loopbackFinsTcpRead();
    void loopbackFinsTcpWrite();
    void loopbackFinsUdpRead();
    void finsConfigDialogRoundTrip();
};

void FinsTest::codecNodeAddressRequestBytes()
{
    // 节点地址请求 = 16 字节 FINS/TCP 帧头（'FINS' + 长度 0x0000000C + 命令 0 + 错误码 0）
    // + 4 字节「客户端节点地址 = 0」（请 PLC 分配） = 20 字节。
    // 关于任务书那两处口径不一致（已在实施报告里登记）：
    //   · 「+ 12 字节 0 / 32 bytes total」与它自己给出的长度字段 0x0C 不能同时成立
    //     （length = 8 + data.size() ⇒ data 只能是 4 字节）；16 + 12 也只有 28 字节。
    //   · W342 的节点地址请求就是 20 字节（length 0x0C 计命令+错误码+客户端节点地址），
    //     响应才是 24 字节（length 0x10，多一个服务端节点地址）。此处按协议钉 20 字节。
    const QByteArray expected = QByteArray::fromHex(
        "46494e53"      // 'F','I','N','S'
        "0000000c"      // 长度 = 命令(4) + 错误码(4) + 客户端节点地址(4)
        "00000000"      // 命令 = 节点地址请求
        "00000000"      // 错误码 = 0
        "00000000");    // 客户端节点地址 = 0（请求分配）
    QCOMPARE(expected.size(), 20);

    const QByteArray req = Fins::nodeAddressRequest();
    QCOMPARE(req.size(), 20);
    QCOMPARE(req, expected);
    QCOMPARE(req.left(4), QByteArray("FINS"));
    QCOMPARE(req.mid(4, 4), QByteArray::fromHex("0000000c"));
    QCOMPARE(req.mid(8, 8), QByteArray(8, '\0'));
    QCOMPARE(req.mid(16), QByteArray(4, '\0'));

    // 帧头拆分：length = 8 + data.size()；整帧 = length + 8（= 16 字节头 + data）
    // 输入由 mock 侧独立实现构造，不用被测的 Fins::tcpHeader 自造自拆
    const QByteArray resp =
        mockTcpFrame(0x00000001u, 0u, QByteArray::fromHex("0000000100000002"));
    quint32 length = 0;
    quint32 command = 0xFFFFFFFFu;
    quint32 errorCode = 0xFFFFFFFFu;
    int headerBytes = 0;
    QVERIFY(Fins::parseTcpHeader(resp, &length, &command, &errorCode, &headerBytes));
    QCOMPARE(length, quint32(16));       // 8（命令+错误码）+ 8 字节节点数据
    QCOMPARE(command, quint32(1));
    QCOMPARE(errorCode, quint32(0));
    QCOMPARE(headerBytes, 24);           // length + 8 = 整帧字节数
    QCOMPARE(resp.size(), headerBytes);

    // 节点地址响应：客户端节点 1 + 服务端节点 2
    quint32 clientNode = 0;
    quint32 serverNode = 0;
    QVERIFY(Fins::parseNodeAddressResponse(resp.mid(16), &clientNode, &serverNode));
    QCOMPARE(clientNode, quint32(1));
    QCOMPARE(serverNode, quint32(2));
    QVERIFY(!Fins::parseNodeAddressResponse(resp.mid(16, 7), &clientNode, &serverNode));

    // 不足 16 字节帧头 / magic 不符 / 长度非法：一律拒绝
    QVERIFY(!Fins::parseTcpHeader(QByteArray(10, '\0'), &length, &command, &errorCode,
                                  &headerBytes));
    QByteArray badMagic = resp;
    badMagic[0] = 'X';
    QVERIFY(!Fins::parseTcpHeader(badMagic, &length, &command, &errorCode, &headerBytes));
    QByteArray overLen = resp;
    // 长度超过 1MB 上界才拒绝。仅比当前缓冲大是**正常分片**，不该拒
    overLen[4] = char(0x00); overLen[5] = char(0x10);
    overLen[6] = char(0x00); overLen[7] = char(0x01);   // 0x00100001
    QVERIFY(!Fins::parseTcpHeader(overLen, &length, &command, &errorCode, &headerBytes));
    QByteArray tinyLen = resp;
    tinyLen[7] = char(0x04);   // length = 4 < 8（连命令+错误码都不够）
    QVERIFY(!Fins::parseTcpHeader(tinyLen, &length, &command, &errorCode, &headerBytes));

    // 2026-11-10 审核：只收到 16 字节帧头（数据未到）必须能解析，
    // headerBytes 指明整帧长度，由调用方判断「还没收齐」。
    // 旧实现要求 20 字节且长度 ≤ 缓冲-4，把这种正常分片判成脏帧清空缓冲，
    // 真机 TCP 分片下会永久失步。
    QVERIFY(Fins::parseTcpHeader(resp.left(16), &length, &command, &errorCode,
                                 &headerBytes));
    QCOMPARE(headerBytes, 24);
    QVERIFY2(headerBytes > 16, "headerBytes 必须大于当前缓冲，调用方才知道还要等数据");
    // 短 1 字节（数据未收齐）同样解析成功
    QVERIFY(Fins::parseTcpHeader(resp.left(23), &length, &command, &errorCode, &headerBytes));
    QCOMPARE(headerBytes, 24);
}

void FinsTest::codecReadWriteFrames()
{
    // 存储区代码（大小写不敏感）
    QCOMPARE(Fins::areaCode(QStringLiteral("DM")), quint16(0x82));
    QCOMPARE(Fins::areaCode(QStringLiteral("dm")), quint16(0x82));
    QCOMPARE(Fins::areaCode(QStringLiteral("CIO")), quint16(0xB0));
    QCOMPARE(Fins::areaCode(QStringLiteral("WR")), quint16(0xB1));
    QCOMPARE(Fins::areaCode(QStringLiteral("HR")), quint16(0xB2));
    QCOMPARE(Fins::areaCode(QStringLiteral("AR")), quint16(0xB3));
    QCOMPARE(Fins::areaCode(QStringLiteral("XX")), quint16(0));

    // 读 DM100 两个字的 FINS 帧（18 字节）：
    //   80 00 02 00 00 00 00 00 00 00 | 01 01 | 00 82 | 00 64 | 00 02
    //   ICF RSV GCT DNA DA1 DA2 SNA SA1 SA2 SID, MC=0101, 区=DM(8200), 地址=100, 字数=2
    // 说明：任务书里那条 17 字节序列漏掉了存储区的第二个字节（DM 的 0x82 应写作 00 82），
    //       这里按 W342 的 2 字节区字段钉正确字节。
    const QByteArray readFrame = Fins::command(
        0x0101u, 0x00, 0x00, Fins::readMemoryParams(Fins::DM, 100, 2));
    QCOMPARE(readFrame, QByteArray::fromHex("800002000000000000000101820000640002"));
    QCOMPARE(readFrame.size(), 18);
    QCOMPARE(readFrame.left(10), QByteArray::fromHex("80000200000000000000"));

    // 写 DM100 一个字、值 0x1234（20 字节）：MC=0102，区 8200，地址 0064，字数 0001，值 1234
    const QByteArray writeFrame = Fins::command(
        0x0102u, 0x00, 0x00,
        Fins::writeMemoryParams(Fins::DM, 100, 1, Fins::encodeValue(QStringLiteral("uint16"), 0x1234)));
    QCOMPARE(writeFrame,
             QByteArray::fromHex("8000020000000000000001028200006400011234"));
    QCOMPARE(writeFrame.size(), 20);

    // 读响应：ICF=0xC0，MC=0101，endCode=0000，数据 12 34
    const QByteArray response = QByteArray::fromHex("c0000200000000000000010100001234");
    quint16 endCode = 0xFFFFu;
    QByteArray data;
    QVERIFY(Fins::parseResponse(response, 0x0101u, &endCode, &data));
    QCOMPARE(endCode, quint16(0));
    QCOMPARE(data, QByteArray::fromHex("1234"));

    // 命令码不符 / 帧过短：拒绝
    QVERIFY(!Fins::parseResponse(response, 0x0102u, &endCode, &data));
    QVERIFY(!Fins::parseResponse(response.left(13), 0x0101u, &endCode, &data));

    // 字编解码（大端）
    const QVector<quint16> words{0x1234, 0xABCD};
    QCOMPARE(Fins::encodeWords(words), QByteArray::fromHex("1234abcd"));
    QCOMPARE(Fins::decodeWords(QByteArray::fromHex("1234abcd")), words);
}

void FinsTest::codecValueRoundTrip()
{
    // big-endian 钉死：uint16 0x1234 -> 12 34
    QCOMPARE(Fins::encodeValue(QStringLiteral("uint16"), 0x1234),
             QByteArray::fromHex("1234"));
    QCOMPARE(Fins::encodeValue(QStringLiteral("int16"), -1),
             QByteArray::fromHex("ffff"));
    QCOMPARE(Fins::encodeValue(QStringLiteral("int32"), 0x12345678),
             QByteArray::fromHex("12345678"));
    QCOMPARE(Fins::encodeValue(QStringLiteral("uint32"), 0xABCDEF01u),
             QByteArray::fromHex("abcdef01"));
    // 1.0f 的 IEEE-754 位模式 = 3F800000
    QCOMPARE(Fins::encodeValue(QStringLiteral("float"), 1.0),
             QByteArray::fromHex("3f800000"));

    QCOMPARE(Fins::wordCount(QStringLiteral("int16")), 1);
    QCOMPARE(Fins::wordCount(QStringLiteral("uint16")), 1);
    QCOMPARE(Fins::wordCount(QStringLiteral("int32")), 2);
    QCOMPARE(Fins::wordCount(QStringLiteral("uint32")), 2);
    QCOMPARE(Fins::wordCount(QStringLiteral("float")), 2);
    QCOMPARE(Fins::wordCount(QStringLiteral("double")), 0);
    QVERIFY(Fins::encodeValue(QStringLiteral("double"), 1.0).isEmpty());

    struct Case {
        const char *type;
        double value;
    };
    const Case cases[] = {
        {"int16", -1234.0},
        {"uint16", 4660.0},
        {"int32", -123456789.0},
        {"uint32", 4000000000.0},
        {"float", 3.5},
        {"float", -12.25},
    };
    for (const Case &c : cases) {
        const QString type = QString::fromLatin1(c.type);
        const QByteArray raw = Fins::encodeValue(type, c.value);
        QVERIFY2(!raw.isEmpty(), c.type);
        QCOMPARE(raw.size(), Fins::wordCount(type) * 2);
        double decoded = 0.0;
        QVERIFY2(Fins::decodeValue(type, raw, &decoded), c.type);
        if (type == QStringLiteral("float"))
            QVERIFY2(qFuzzyCompare(decoded, c.value), c.type);
        else
            QCOMPARE(decoded, c.value);
        // 字节数不足 / 未知类型：拒绝
        QVERIFY(!Fins::decodeValue(type, raw.left(raw.size() - 1), &decoded));
        QVERIFY(!Fins::decodeValue(QStringLiteral("double"), raw, &decoded));
    }
}

void FinsTest::loopbackFinsTcpRead()
{
    QTcpServer server;
    auto state = std::make_shared<LoopState>();
    const quint16 serverPort = startFinsTcpServer(server, state);
    QVERIFY2(serverPort != 0, qPrintable(server.errorString()));

    FinsCommNode node(FinsCommNode::FinsTcp);
    node.init();
    node.setParam(QStringLiteral("targetIp"), QStringLiteral("127.0.0.1"));
    node.setParam(QStringLiteral("targetPort"), int(serverPort));
    node.setParam(QStringLiteral("pollInterval"), 50);   // 用例内加速（生产默认 500）
    node.setParam(QStringLiteral("timeoutMs"), 500);
    node.setParam(QStringLiteral("autoReconnect"), false);   // 用例内确定性：不排重连
    FinsRegisterItem reg;
    reg.area = QStringLiteral("DM");
    reg.address = 100;
    reg.dataType = QStringLiteral("uint16");
    node.setRegisters(QList<FinsRegisterItem>{reg});

    QSignalSpy valueSpy(&node, &CommunicationNodeBase::registerValueChanged);
    QVERIFY(valueSpy.isValid());

    QVERIFY(node.openConnection());
    QTRY_VERIFY_WITH_TIMEOUT(node.isConnected(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(valueSpy.count() >= 1, 3000);

    // 握手请求逐字节 = codec 里的 28 字节节点地址请求
    // 观测值是收到的原文、期望按 W342 独立硬编码（20 字节），两边都不调用被测函数
    QCOMPARE(state->nodeAddrRequest,
             QByteArray::fromHex("46494e530000000c000000000000000000000000"));
    // 轮询已发出，且节点号取自握手：DA1 = 服务端 2，SA1 = 本机 1
    QVERIFY(state->finsFrameCount >= 1);
    QCOMPARE(state->lastFinsFrame.size(), 18);
    QCOMPARE(int(quint8(state->lastFinsFrame.at(4))), 2);
    QCOMPARE(int(quint8(state->lastFinsFrame.at(7))), 1);
    QCOMPARE(int(quint8(state->lastFinsFrame.at(10))), 0x01);   // MC 高字节
    QCOMPARE(int(quint8(state->lastFinsFrame.at(11))), 0x01);   // MC 低字节
    QCOMPARE(int(quint8(state->lastFinsFrame.at(12))), 0x82);   // 区码 DM 高字节（82 00）
    QCOMPARE(int(quint8(state->lastFinsFrame.at(13))), 0x00);   // 区码 DM 低字节
    QCOMPARE(int(quint8(state->lastFinsFrame.at(15))), 100);    // 地址 100（00 64）
    QCOMPARE(int(quint8(state->lastFinsFrame.at(17))), 1);      // 字数 1（00 01）

    // 上报内容：地址 100、值字节 12 34、类型 uint16、当前值 0x1234
    const QList<QVariant> args = valueSpy.at(0);
    QCOMPARE(args.at(0).toInt(), 100);
    QCOMPARE(args.at(1).toByteArray(), QByteArray::fromHex("1234"));
    QCOMPARE(args.at(2).toString(), QStringLiteral("uint16"));
    QCOMPARE(node.registerCurrentValue(100), 4660.0);
    QCOMPARE(node.registerDisplayValue(100), QStringLiteral("4660"));

    node.closeConnection();
    QVERIFY(!node.isConnected());
}

void FinsTest::loopbackFinsTcpWrite()
{
    QTcpServer server;
    auto state = std::make_shared<LoopState>();
    const quint16 serverPort = startFinsTcpServer(server, state);
    QVERIFY2(serverPort != 0, qPrintable(server.errorString()));

    FinsCommNode node(FinsCommNode::FinsTcp);
    node.init();
    node.setParam(QStringLiteral("targetIp"), QStringLiteral("127.0.0.1"));
    node.setParam(QStringLiteral("targetPort"), int(serverPort));
    node.setParam(QStringLiteral("pollInterval"), 50);
    node.setParam(QStringLiteral("timeoutMs"), 500);
    node.setParam(QStringLiteral("autoReconnect"), false);
    FinsRegisterItem reg;
    reg.area = QStringLiteral("DM");
    reg.address = 100;
    reg.dataType = QStringLiteral("uint16");
    node.setRegisters(QList<FinsRegisterItem>{reg});

    QSignalSpy errSpy(&node, &CommunicationNodeBase::communicationError);
    QVERIFY(node.openConnection());
    QTRY_VERIFY_WITH_TIMEOUT(node.isConnected(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(state->finsFrameCount >= 1, 3000);   // 握手完成且轮询已发

    // 写 DM100 = 0x1234（20 字节）：MC=0102，节点号取自握手（DA1=2 / SA1=1）
    QVERIFY(node.writeRegister(100, 0x1234));
    QTRY_VERIFY_WITH_TIMEOUT(state->writeFrameCount >= 1, 3000);
    // 期望按 W342 逐字节独立硬编码，刻意不调用 Fins::command / writeMemoryParams：
    //   80 00 02 00 02 00 00 01 00 00  (ICF..SID；DA1=2/SA1=1 来自握手响应)
    //   01 02                          (MC：Memory Area Write)
    //   82 00 00 64 00 01 12 34       (DM100 / 1 字 / 值 0x1234，全部大端)
    const QByteArray expectedWrite = QByteArray::fromHex(
        "80000200020000010000" "0102" "8200006400011234");
    QCOMPARE(expectedWrite.size(), 20);
    QCOMPARE(state->lastWriteFrame, expectedWrite);
    QCOMPARE(state->lastWriteFrame.size(), 20);

    // 未登记地址：请求不发出，且原因经 communicationError 上报（不静默返回 false）
    const int errorsBefore = errSpy.count();
    QVERIFY(!node.writeRegister(999, 1.0));
    QTRY_VERIFY_WITH_TIMEOUT(errSpy.count() > errorsBefore, 1000);

    node.closeConnection();
    QVERIFY(!node.isConnected());
}

void FinsTest::loopbackFinsUdpRead()
{
    QUdpSocket server;
    QVERIFY2(server.bind(QHostAddress::LocalHost, 0), qPrintable(server.errorString()));
    const quint16 serverPort = server.localPort();
    QVERIFY(serverPort != 0);

    auto state = std::make_shared<LoopState>();
    QObject::connect(&server, &QUdpSocket::readyRead, &server, [&server, state]() {
        while (server.hasPendingDatagrams()) {
            // Qt6 的 QUdpSocket 没有 senderAddress()/senderPort()：数据报与来源一起用
            // receiveDatagram() 取回
            const QNetworkDatagram dg = server.receiveDatagram(65535);
            if (!dg.isValid())
                break;
            // FINS/UDP：数据报就是裸 FINS 帧（无 FINS/TCP 帧头）
            state->lastFinsFrame = dg.data();
            ++state->finsFrameCount;
            server.writeDatagram(finsReadResponse(dg.data()), dg.senderAddress(),
                                 dg.senderPort());
        }
    });

    FinsCommNode node(FinsCommNode::FinsUdp);
    node.init();
    node.setParam(QStringLiteral("targetIp"), QStringLiteral("127.0.0.1"));
    node.setParam(QStringLiteral("targetPort"), int(serverPort));
    node.setParam(QStringLiteral("pollInterval"), 50);
    node.setParam(QStringLiteral("timeoutMs"), 500);
    node.setParam(QStringLiteral("autoReconnect"), false);
    FinsRegisterItem reg;
    reg.area = QStringLiteral("DM");
    reg.address = 100;
    reg.dataType = QStringLiteral("uint16");
    node.setRegisters(QList<FinsRegisterItem>{reg});

    QSignalSpy valueSpy(&node, &CommunicationNodeBase::registerValueChanged);
    QVERIFY(valueSpy.isValid());

    QVERIFY(node.openConnection());
    QVERIFY(node.isConnected());   // UDP 绑定成功即就绪（无连接握手）
    QTRY_VERIFY_WITH_TIMEOUT(valueSpy.count() >= 1, 3000);

    QVERIFY(state->finsFrameCount >= 1);
    QCOMPARE(state->lastFinsFrame.size(), 18);
    QCOMPARE(int(quint8(state->lastFinsFrame.at(12))), 0x82);   // 区码 DM 高字节（82 00）
    QCOMPARE(int(quint8(state->lastFinsFrame.at(13))), 0x00);   // 区码 DM 低字节
    QCOMPARE(int(quint8(state->lastFinsFrame.at(15))), 100);    // 地址 100（00 64）
    QCOMPARE(int(quint8(state->lastFinsFrame.at(17))), 1);      // 字数 1（00 01）

    const QList<QVariant> args = valueSpy.at(0);
    QCOMPARE(args.at(0).toInt(), 100);
    QCOMPARE(args.at(1).toByteArray(), QByteArray::fromHex("1234"));
    QCOMPARE(args.at(2).toString(), QStringLiteral("uint16"));
    QCOMPARE(node.registerCurrentValue(100), 4660.0);

    node.closeConnection();
    QVERIFY(!node.isConnected());
}

void FinsTest::finsConfigDialogRoundTrip()
{
    // B5-2：配置对话框字段往返。initial 里故意用与默认值全不相同的取值，
    // 一旦某个控件没接上 setConfig/config 就能从读数直接看出来。
    QJsonObject regA;
    regA[QStringLiteral("area")] = QStringLiteral("DM");
    regA[QStringLiteral("address")] = 100;
    regA[QStringLiteral("dataType")] = QStringLiteral("uint16");
    regA[QStringLiteral("accessMode")] = QStringLiteral("Read");
    regA[QStringLiteral("enabled")] = true;
    regA[QStringLiteral("description")] = QStringLiteral("描述A");

    QJsonObject regB;
    regB[QStringLiteral("area")] = QStringLiteral("WR");
    regB[QStringLiteral("address")] = 200;
    regB[QStringLiteral("dataType")] = QStringLiteral("float");
    regB[QStringLiteral("accessMode")] = QStringLiteral("ReadWrite");
    regB[QStringLiteral("enabled")] = false;
    regB[QStringLiteral("description")] = QStringLiteral("描述B");

    QJsonObject initial;
    initial[QStringLiteral("targetIp")] = QStringLiteral("192.168.0.10");
    initial[QStringLiteral("targetPort")] = 9600;
    initial[QStringLiteral("pollInterval")] = 100;
    initial[QStringLiteral("timeoutMs")] = 800;
    initial[QStringLiteral("autoReconnect")] = false;
    initial[QStringLiteral("reconnectInterval")] = 2000;
    initial[QStringLiteral("localNode")] = 3;
    initial[QStringLiteral("remoteNode")] = 7;
    initial[QStringLiteral("registers")] = QJsonArray{regA, regB};

    FinsConfigDialog dlg(QStringLiteral("FINS TCP"), initial);
    QCOMPARE(dlg.windowTitle(), QStringLiteral("FINS TCP 配置"));

    auto *ipEdit = dlg.findChild<QLineEdit *>(QStringLiteral("finsTargetIpEdit"));
    QVERIFY(ipEdit);
    QCOMPARE(ipEdit->text(), QStringLiteral("192.168.0.10"));
    auto *portSpin = dlg.findChild<QSpinBox *>(QStringLiteral("finsTargetPortSpin"));
    QVERIFY(portSpin);
    QCOMPARE(portSpin->value(), 9600);
    auto *pollSpin = dlg.findChild<QSpinBox *>(QStringLiteral("finsPollSpin"));
    QVERIFY(pollSpin);
    QCOMPARE(pollSpin->value(), 100);
    auto *timeoutSpin = dlg.findChild<QSpinBox *>(QStringLiteral("finsTimeoutSpin"));
    QVERIFY(timeoutSpin);
    QCOMPARE(timeoutSpin->value(), 800);
    auto *autoCheck = dlg.findChild<QCheckBox *>(QStringLiteral("finsAutoReconnectCheck"));
    QVERIFY(autoCheck);
    QVERIFY(!autoCheck->isChecked());
    auto *reconnectSpin = dlg.findChild<QSpinBox *>(QStringLiteral("finsReconnectSpin"));
    QVERIFY(reconnectSpin);
    QCOMPARE(reconnectSpin->value(), 2000);
    auto *localSpin = dlg.findChild<QSpinBox *>(QStringLiteral("finsLocalNodeSpin"));
    QVERIFY(localSpin);
    QCOMPARE(localSpin->value(), 3);
    auto *remoteSpin = dlg.findChild<QSpinBox *>(QStringLiteral("finsRemoteNodeSpin"));
    QVERIFY(remoteSpin);
    QCOMPARE(remoteSpin->value(), 7);

    auto *table = dlg.findChild<QTableWidget *>(QStringLiteral("finsRegisterTable"));
    QVERIFY(table);
    QCOMPARE(table->rowCount(), 2);

    // config() 回读：标量同名同值，寄存器逐项对照（键名必须与工厂读取的一致）
    const QJsonObject back = dlg.config();
    QCOMPARE(back.value(QStringLiteral("targetIp")).toString(),
             QStringLiteral("192.168.0.10"));
    QCOMPARE(back.value(QStringLiteral("targetPort")).toInt(), 9600);
    QCOMPARE(back.value(QStringLiteral("pollInterval")).toInt(), 100);
    QCOMPARE(back.value(QStringLiteral("timeoutMs")).toInt(), 800);
    QCOMPARE(back.value(QStringLiteral("autoReconnect")).toBool(true), false);
    QCOMPARE(back.value(QStringLiteral("reconnectInterval")).toInt(), 2000);
    QCOMPARE(back.value(QStringLiteral("localNode")).toInt(), 3);
    QCOMPARE(back.value(QStringLiteral("remoteNode")).toInt(), 7);
    QJsonArray regs = back.value(QStringLiteral("registers")).toArray();
    QCOMPARE(int(regs.size()), 2);
    const QJsonObject r0 = regs.at(0).toObject();
    QCOMPARE(r0.value(QStringLiteral("area")).toString(), QStringLiteral("DM"));
    QCOMPARE(r0.value(QStringLiteral("address")).toInt(), 100);
    QCOMPARE(r0.value(QStringLiteral("dataType")).toString(), QStringLiteral("uint16"));
    QCOMPARE(r0.value(QStringLiteral("accessMode")).toString(), QStringLiteral("Read"));
    QCOMPARE(r0.value(QStringLiteral("enabled")).toBool(false), true);
    QCOMPARE(r0.value(QStringLiteral("description")).toString(), QStringLiteral("描述A"));
    const QJsonObject r1 = regs.at(1).toObject();
    QCOMPARE(r1.value(QStringLiteral("area")).toString(), QStringLiteral("WR"));
    QCOMPARE(r1.value(QStringLiteral("address")).toInt(), 200);
    QCOMPARE(r1.value(QStringLiteral("dataType")).toString(), QStringLiteral("float"));
    QCOMPARE(r1.value(QStringLiteral("accessMode")).toString(), QStringLiteral("ReadWrite"));
    QCOMPARE(r1.value(QStringLiteral("enabled")).toBool(true), false);

    // 添加一行 → 3 项；选中末行删除 → 回到 2 项
    auto *addBtn = dlg.findChild<QPushButton *>(QStringLiteral("finsAddRegisterBtn"));
    QVERIFY(addBtn);
    addBtn->click();
    QCOMPARE(table->rowCount(), 3);
    QCOMPARE(int(dlg.config().value(QStringLiteral("registers")).toArray().size()), 3);
    table->setCurrentCell(table->rowCount() - 1, 0);
    auto *removeBtn = dlg.findChild<QPushButton *>(QStringLiteral("finsRemoveRegisterBtn"));
    QVERIFY(removeBtn);
    removeBtn->click();
    QCOMPARE(table->rowCount(), 2);
    QCOMPARE(int(dlg.config().value(QStringLiteral("registers")).toArray().size()), 2);

    // 空 initial：全部落到与 FinsCommNode::init() 相同的默认值，寄存器表为空
    FinsConfigDialog defaults(QStringLiteral("FINS UDP"), QJsonObject());
    QCOMPARE(defaults.windowTitle(), QStringLiteral("FINS UDP 配置"));
    auto *defIp = defaults.findChild<QLineEdit *>(QStringLiteral("finsTargetIpEdit"));
    QVERIFY(defIp);
    QCOMPARE(defIp->text(), QStringLiteral("127.0.0.1"));
    auto *defPort = defaults.findChild<QSpinBox *>(QStringLiteral("finsTargetPortSpin"));
    QVERIFY(defPort);
    QCOMPARE(defPort->value(), 7920);
    auto *defPoll = defaults.findChild<QSpinBox *>(QStringLiteral("finsPollSpin"));
    QVERIFY(defPoll);
    QCOMPARE(defPoll->value(), 500);
    auto *defTimeout = defaults.findChild<QSpinBox *>(QStringLiteral("finsTimeoutSpin"));
    QVERIFY(defTimeout);
    QCOMPARE(defTimeout->value(), 1000);
    auto *defAuto = defaults.findChild<QCheckBox *>(QStringLiteral("finsAutoReconnectCheck"));
    QVERIFY(defAuto);
    QVERIFY(defAuto->isChecked());
    auto *defReconnect = defaults.findChild<QSpinBox *>(QStringLiteral("finsReconnectSpin"));
    QVERIFY(defReconnect);
    QCOMPARE(defReconnect->value(), 3000);
    auto *defLocal = defaults.findChild<QSpinBox *>(QStringLiteral("finsLocalNodeSpin"));
    QVERIFY(defLocal);
    QCOMPARE(defLocal->value(), 0);
    auto *defRemote = defaults.findChild<QSpinBox *>(QStringLiteral("finsRemoteNodeSpin"));
    QVERIFY(defRemote);
    QCOMPARE(defRemote->value(), 0);
    auto *defTable = defaults.findChild<QTableWidget *>(QStringLiteral("finsRegisterTable"));
    QVERIFY(defTable);
    QCOMPARE(defTable->rowCount(), 0);
    QCOMPARE(int(defaults.config().value(QStringLiteral("registers")).toArray().size()), 0);
}

QTEST_MAIN(FinsTest)

#include "fins_test.moc"
