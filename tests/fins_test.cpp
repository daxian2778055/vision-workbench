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
#include <QHostAddress>
#include <QNetworkDatagram>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>

#include <memory>

#include "CommunicationNodeBase.h"
#include "FinsCommNode.h"
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
                if (!Fins::parseTcpHeader(state->buffer, &length, &command, &errorCode,
                                          &headerBytes))
                    break;
                if (headerBytes > state->buffer.size())
                    break;   // 半包
                const QByteArray data = state->buffer.mid(16, int(length) - 8);
                state->buffer.remove(0, headerBytes);

                if (command == 0x00000000u) {          // 节点地址请求
                    if (state->nodeAddrRequest.isEmpty())
                        state->nodeAddrRequest = Fins::tcpHeader(command, errorCode, data);
                    // 节点地址响应：客户端节点 = 1，服务端（PLC）节点 = 2
                    QByteArray payload;
                    payload.append(char(0x00)).append(char(0x00))
                        .append(char(0x00)).append(char(0x01));
                    payload.append(char(0x00)).append(char(0x00))
                        .append(char(0x00)).append(char(0x02));
                    cli->write(Fins::tcpHeader(0x00000001u, 0u, payload));
                    cli->flush();
                } else if (command == 0x00000002u) {   // 帧发送：FINS 帧本体
                    if (requestMc(data) == 0x0102u) {
                        state->lastWriteFrame = data;
                        ++state->writeFrameCount;
                        cli->write(Fins::frameSend(finsWriteResponse(data)));
                    } else {
                        state->lastFinsFrame = data;
                        ++state->finsFrameCount;
                        cli->write(Fins::frameSend(finsReadResponse(data)));
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
    const QByteArray resp =
        Fins::tcpHeader(0x00000001u, 0u, QByteArray::fromHex("0000000100000002"));
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

    // 过短 / magic 不符 / 长度字段越界：一律拒绝
    QVERIFY(!Fins::parseTcpHeader(QByteArray(19, '\0'), &length, &command, &errorCode,
                                  &headerBytes));
    QByteArray badMagic = resp;
    badMagic[0] = 'X';
    QVERIFY(!Fins::parseTcpHeader(badMagic, &length, &command, &errorCode, &headerBytes));
    QByteArray overLen = resp;
    overLen[7] = char(0x20);   // 声称 length = 32 > 24 - 4
    QVERIFY(!Fins::parseTcpHeader(overLen, &length, &command, &errorCode, &headerBytes));
    QByteArray tinyLen = resp;
    tinyLen[7] = char(0x04);   // length = 4 < 8（连命令+错误码都不够）
    QVERIFY(!Fins::parseTcpHeader(tinyLen, &length, &command, &errorCode, &headerBytes));
    // 短 1 字节仍能拆出帧头，headerBytes 会大于缓冲：调用方须以 headerBytes <= size 判半包
    //（FinsCommNode 就是这么切帧的；口径同任务书 length <= buf.size()-4）。
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
    QCOMPARE(state->nodeAddrRequest, Fins::nodeAddressRequest());
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
    const QByteArray expected = Fins::command(
        0x0102u, 2, 1,
        Fins::writeMemoryParams(Fins::DM, 100, 1, QByteArray::fromHex("1234")));
    QCOMPARE(state->lastWriteFrame, expected);
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

QTEST_GUILESS_MAIN(FinsTest)

#include "fins_test.moc"
