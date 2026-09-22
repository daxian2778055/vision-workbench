// 通讯回写链路端到端验证：本机起一个 QTcpServer 充当「模拟 PLC」，让平台按 TCP 客户端连上去，
// 再由流程里的「发送数据」算子把结果写回；并验证"设备不存在 / 未连接"时回写失败是**可见**的
// （此前 process() 恒返回 true：PLC 什么都没收到，流程却显示成功、日志里也没有任何痕迹）。
#include <QtTest>
#include <QPushButton>
#include <QTableWidget>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QJsonObject>
#include <QTemporaryDir>

#include "CommunicationManager.h"
#include "FlowExecutor.h"
#include "FlowScene.h"
#include "GlobalTriggerManager.h"
#include "HeartbeatManager.h"
#include "ModbusNode.h"
#include "PlcCommNode.h"
#include "NodeBase.h"
#include "NodeTemplateStore.h"
#include "RecipeManager.h"
#include "NodeRegistry.h"
#include "FindCircleNode.h"
#include "FindLineNode.h"
#include "CaliperMeasureNode.h"
#include "EdgePointsNode.h"
#include "ThresholdNode.h"
#include "BlobAnalysisNode.h"
#include "OtsuThresholdNode.h"
#include "DynThresholdNode.h"
#include "ReceiveEvent.h"
#include "SendEvent.h"
#include "CommunicationNodeBase.h"
#include "CommunicationManagerDialog.h"
#include "CommDeviceConfigDialog.h"
#include "ModbusConfigDialog.h"
#include "PlcConfigDialog.h"
#include "registerbyteorder.h"   // S4：回写校验比对判定

namespace {

/// 取一个"此刻可用"的临时端口：让 OS 分配（ephemeral）后立即释放，只把端口号带出去给
/// Modbus / PLC 的**服务端节点**用（那几个节点必须收到一个具体端口号，不能像 QTcpServer 那样 listen(0)）。
/// 为什么必须替掉原先的固定字面量（15502 / 15504–15511）：
///   固定端口在"上一个用例刚用完、其连接处于 TIME_WAIT"时会 bind 失败——本仓曾因此在全量 ctest 里
///   偶发红一次（CommWritebackTest::testModbusUserCloseDoesNotResurrect，报"Modbus 服务器启动失败"），
///   而单跑该套件却是绿的（序列相关）。OS 分配的临时端口不会撞上这种残留状态。
/// 说明：仍存在"取到端口 → 真正 listen"之间的微小窗口；因此各用例的"服务端已监听"断言都保留有界等待，
///       失败时报错信息明确（不会静默空转）。
quint16 pickFreePort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    const quint16 p = probe.serverPort();
    probe.close();
    return p;
}

/// 起一个只收不回的模拟 PLC；返回后可用 received 取回收到的字节
void startSimulatedPlc(QTcpServer &plc, QByteArray &received)
{
    plc.listen(QHostAddress::LocalHost, 0);
    QObject::connect(&plc, &QTcpServer::newConnection, &plc, [&plc, &received]() {
        QTcpSocket *cli = plc.nextPendingConnection();
        QObject::connect(cli, &QTcpSocket::readyRead, cli,
                         [cli, &received]() { received += cli->readAll(); });
    });
}

QJsonObject tcpClientConfig(quint16 port)
{
    QJsonObject cfg;
    cfg[QStringLiteral("serverIp")] = QStringLiteral("127.0.0.1");
    cfg[QStringLiteral("port")] = int(port);
    cfg[QStringLiteral("mode")] = QStringLiteral("Client");
    return cfg;
}

}   // namespace

class CommWritebackTest : public QObject
{
    Q_OBJECT

private slots:
    void testSendDataReachesSimulatedPlc();
    void testPipelineWritebackWithSuffix();
    void testWritebackFailureIsReported();
    void testModbusRegisterWritebackAndRawSendGuard();
    void testModbusCoilWriteback();   // P0-4 切片①：线圈（数字 IO 协议基础）端到端回环
    void testPlcDataTriggersFlowAndWritesBack();
    void testHeartbeatActuallyGoesOnTheWire();
    void testSendEventsActuallySend();
    void testHeartbeatFailureRaisesAlarm();
    void testStringTriggerStartsFlow();
    void testEnabledSendEventsFireOnRoundEnd();
    void testNodeTemplateRoundTrip();
    void testRecipeSaveAndLoad();
    void testRoiParamRoundTrip();
    void testRecipeTypeMismatchSkipped();
    // 对标 VM 4.4 通讯增强
    void testMultiFieldTemplateWithInjectedData();
    void testTextReceiveEventRegexParse();
    void testUdpRoundTrip();
    void testTcpAutoReconnect();
    void testFrameAssemblerTerminatorAndTimeout();

    // 通讯健壮性回归（点连接开关 UAF / 组帧重入 / 断线脏半帧 / 配置键保留 / 发送事件往返）
    void testDialogToggleConnection();
    void testFrameAssemblerReentrancy();
    void testDisconnectClearsFrameBuffer();
    void testConfigDialogPreservesUnmanagedKeys();
    void testSendEventsSurviveSaveLoad();
    // C 类回归：接收事件按设备过滤（A 设备数据不得触发 B 设备的事件）
    void testReceiveEventFiltersByDevice();

    // 数据正确性回归：边沿阈值语义 / 无效帧不污染基线 / BADC 字节序 /
    // 重复关闭不发假断开 / 断开时发送必须可见
    void testByteMatchEdgeUsesCompareValue();
    void testByteMatchInvalidFrameKeepsBaseline();
    void testByteMatchBadcByteOrder();
    void testCloseConnectionNoSpuriousSignal();
    void testSendWhileDisconnectedIsReported();
    // N 轮评审补强：重复关闭不假断开 / 按身份注销 / 单寄存器字节序全矩阵（含 CDAB 回归）
    void testModbusCloseConnectionNoSpuriousSignal();
    void testPlcCloseConnectionNoSpuriousSignal();   // S-2：PLC 关闭守卫与 Modbus 同款（m_everReallyConnected 判据 + 不复活）
    void testModbusCloseNoDoubleReportOnTransientDrop();   // L1：瞬断已报一次后主动 close 不得二次上报 connectionClosed
    void testPlcCloseNoDoubleReportOnTransientDrop();      // L1（PLC 同款）：判别性覆盖 PlcCommNode 的 ever 标志复位
    void testUnregisterExecutorByIdentity();
    void testModbusSingleRegisterByteOrder();
    void testModbusWriteReadByteOrderRoundTrip();   // S3/S4：写入逆变换 + 宽类型拆寄存器往返
    void testModbusServerWideRegisterInit();         // S7：服务器寄存器表初始化须按宽类型拆字
    void testPlcWriteReadByteOrderRoundTrip();       // S3（PLC 半边）：PLC 写入逆变换往返
    void testModbusUserCloseDoesNotResurrect();      // S-1：真连后用户关闭不复活 + 重开后自愈仍有效
    void testPlcUserCloseDoesNotResurrect();         // S-1（PLC 同款）：并钉住 openConnection 两行复位
    void testAllocFlowNameAvoidsCollision();
};

void CommWritebackTest::testSendDataReachesSimulatedPlc()
{
    QTcpServer plc;
    QByteArray received;
    startSimulatedPlc(plc, received);
    QVERIFY2(plc.isListening(), qPrintable(plc.errorString()));

    auto *cm = CommunicationManager::instance();
    QVERIFY2(cm->addDevice(QStringLiteral("SIM_PLC"), QStringLiteral("TCP"),
                           tcpClientConfig(plc.serverPort())),
             "addDevice 失败");
    QVERIFY2(cm->openDevice(QStringLiteral("SIM_PLC")), "连接模拟 PLC 失败（同步 waitForConnected）");

    // 成功发送不得误报错误：flush() 返回 false 只代表写缓冲已空（数据早已交给内核），
    // 历史实现把它当失败 → 每次成功回写都刷一条"TCP发送失败"且 lastSent 不更新
    auto *simNode = cm->deviceNode(QStringLiteral("SIM_PLC"));
    QVERIFY(simNode != nullptr);
    QSignalSpy errSpy(simNode, &CommunicationNodeBase::communicationError);

    // 投递是排队执行（sendRequested → onSendRequested），需要事件循环才会真正 write
    QVERIFY2(cm->sendData(QStringLiteral("SIM_PLC"), QByteArray("OK,1,3.14\r\n")), "sendData 投递失败");
    QTRY_VERIFY_WITH_TIMEOUT(received.contains("OK,1,3.14"), 3000);
    QCOMPARE(received, QByteArray("OK,1,3.14\r\n"));   // 原样字节，无额外包装
    QCOMPARE(errSpy.count(), 0);   // 成功路径不得发通信错误
    QCOMPARE(simNode->getParam(QStringLiteral("lastSent")).toString(),
             QStringLiteral("OK,1,3.14\r\n"));   // 误判失败时这里不会更新

    QVERIFY(cm->closeDevice(QStringLiteral("SIM_PLC")));
    QVERIFY(cm->removeDevice(QStringLiteral("SIM_PLC")));
}

void CommWritebackTest::testPipelineWritebackWithSuffix()
{
    QTcpServer plc;
    QByteArray received;
    startSimulatedPlc(plc, received);
    QVERIFY2(plc.isListening(), qPrintable(plc.errorString()));

    auto *cm = CommunicationManager::instance();
    QVERIFY(cm->addDevice(QStringLiteral("SIM_PLC_FLOW"), QStringLiteral("TCP"),
                          tcpClientConfig(plc.serverPort())));
    QVERIFY(cm->openDevice(QStringLiteral("SIM_PLC_FLOW")));

    // 流程：单个「发送数据」算子，绑定模拟 PLC；无上游输入时用 sendText 参数回写
    FlowScene scene;
    FlowExecutor exec;
    exec.setFlowName(QStringLiteral("CommWriteback"));
    exec.setFlowMode(FlowMode::SoftwareTrigger);

    NodeBase *send = scene.createNode(NodeBase::OUTPUT, QPointF(120, 120),
                                      QStringLiteral("发送数据"));
    QVERIFY2(send != nullptr, "无法创建「发送数据」算子");
    send->setParam(QStringLiteral("deviceName"), QStringLiteral("SIM_PLC_FLOW"));
    send->setParam(QStringLiteral("suffix"), QStringLiteral("\r\n"));
    send->setParam(QStringLiteral("sendText"), QStringLiteral("OK,3600"));

    exec.setFlowScene(&scene);

    exec.startExecution();
    bool finished = exec.wait(10000);
    if (!finished) {
        exec.stopExecution();
        finished = exec.wait(3000);
    }
    exec.setFlowScene(nullptr);
    QVERIFY2(finished, "流程未在 10 秒内结束");

    QTRY_VERIFY_WITH_TIMEOUT(received.contains("OK,3600"), 3000);
    QVERIFY2(received.endsWith("\r\n"), "未按 suffix 参数追加行尾缓冲");
    QVERIFY2(send->executionSuccess(), "回写成功时节点应报告成功");

    QVERIFY(cm->closeDevice(QStringLiteral("SIM_PLC_FLOW")));
    QVERIFY(cm->removeDevice(QStringLiteral("SIM_PLC_FLOW")));
}

void CommWritebackTest::testWritebackFailureIsReported()
{
    // 设备名不存在：以前 doSend 静默返回、process() 恒为 true，
    // 现场表现是「流程全过、PLC 什么都没收到、日志无痕迹」——这条断言把它钉住。
    FlowScene scene;
    FlowExecutor exec;
    exec.setFlowName(QStringLiteral("CommWritebackFail"));
    exec.setFlowMode(FlowMode::SoftwareTrigger);

    NodeBase *send = scene.createNode(NodeBase::OUTPUT, QPointF(120, 120),
                                      QStringLiteral("发送数据"));
    QVERIFY(send != nullptr);
    send->setParam(QStringLiteral("deviceName"), QStringLiteral("NO_SUCH_DEVICE"));
    send->setParam(QStringLiteral("sendText"), QStringLiteral("OK,1"));

    exec.setFlowScene(&scene);

    exec.startExecution();
    bool finished = exec.wait(10000);
    if (!finished) {
        exec.stopExecution();
        finished = exec.wait(3000);
    }
    exec.setFlowScene(nullptr);
    QVERIFY2(finished, "流程未在 10 秒内结束");

    QVERIFY2(!send->executionSuccess(), "设备不存在时回写必须报告失败，不能静默算成功");
}

namespace {
/// 用例内设备清理守卫：断言失败会提前 return，若不清理则残留设备（监听端口/轮询客户端）
/// 会串扰后续用例——已实测：本用例失败后 testDialogToggleConnection 会跟着失败。
struct DeviceCleanup {
    QStringList names;
    ~DeviceCleanup()
    {
        auto *cm = CommunicationManager::instance();
        for (const QString &n : names) {
            if (cm->deviceNode(n)) {
                cm->closeDevice(n);
                cm->removeDevice(n);
            }
        }
    }
};
}   // namespace

void CommWritebackTest::testModbusRegisterWritebackAndRawSendGuard()
{
    // 用项目自带的 Modbus 双角色在本进程内回环：服务器（从站）+ 客户端（主站）
    const quint16 port = pickFreePort();   // 临时端口（替代固定字面量，避免 TIME_WAIT 撞端口）
    QVERIFY2(port != 0, "无法取得空闲端口（pickFreePort）");
    auto *cm = CommunicationManager::instance();
    // 断言失败提前 return 时也要清理，避免残留服务器/客户端串扰后续用例
    DeviceCleanup cleanup{ { QStringLiteral("MB_SRV"), QStringLiteral("MB_CLI") } };

    QJsonObject srvCfg;
    srvCfg[QStringLiteral("role")] = QStringLiteral("服务器");
    srvCfg[QStringLiteral("connectionType")] = QStringLiteral("TCP");
    srvCfg[QStringLiteral("port")] = port;
    srvCfg[QStringLiteral("slaveAddress")] = 1;
    QVERIFY2(cm->addDevice(QStringLiteral("MB_SRV"), QStringLiteral("Modbus"), srvCfg),
             "addDevice(服务器) 失败");
    auto *srv = qobject_cast<ModbusNode *>(cm->deviceNode(QStringLiteral("MB_SRV")));
    QVERIFY2(srv != nullptr, "服务器节点类型不符");

    // 先声明寄存器表再 open：openConnection() 里的 syncServerRegisters() 才会把它映射进数据单元，
    // 否则客户端写到未映射地址会被服务器拒绝（本用例必须覆盖"真的写进去"）
    QList<ModbusRegisterItem> regs;
    ModbusRegisterItem reg;
    reg.address = 0;
    reg.dataType = QStringLiteral("uint16");
    reg.byteOrder = QStringLiteral("ABCD");
    reg.accessMode = QStringLiteral("ReadWrite");
    reg.enabled = true;
    regs.append(reg);
    srv->setRegisters(regs);

    QVERIFY2(cm->openDevice(QStringLiteral("MB_SRV")), "Modbus 服务器启动失败");
    QTRY_VERIFY_WITH_TIMEOUT(srv->isServerListening(), 3000);

    QJsonObject cliCfg;
    cliCfg[QStringLiteral("role")] = QStringLiteral("客户端");
    cliCfg[QStringLiteral("connectionType")] = QStringLiteral("TCP");
    cliCfg[QStringLiteral("host")] = QStringLiteral("127.0.0.1");
    cliCfg[QStringLiteral("port")] = port;
    cliCfg[QStringLiteral("slaveAddress")] = 1;
    QVERIFY2(cm->addDevice(QStringLiteral("MB_CLI"), QStringLiteral("Modbus"), cliCfg),
             "addDevice(客户端) 失败");
    QVERIFY2(cm->openDevice(QStringLiteral("MB_CLI")), "Modbus 客户端连接请求失败");
    auto *cli = qobject_cast<ModbusNode *>(cm->deviceNode(QStringLiteral("MB_CLI")));
    QVERIFY2(cli != nullptr, "客户端节点类型不符");
    // 注意：ModbusNode::isConnected() 只反映 m_connected，而它在 connectDevice() 返回 true
    // 时就置位了——那只代表"连接请求已发出"，链路可能尚未建立（QModbus 是异步的）。
    // 因此这里不停留在标志位，而是重试到真的写成功为止。
    QSignalSpy writtenSpy(srv, &ModbusNode::registerWrittenByClient);
    // 服务器模式接收事件链：客户端写服务器必须发出 registerValueChanged。
    // 历史缺陷：onServerDataWritten 只发 UI 信号（registerCurrentValueChanged /
    // registerWrittenByClient），从不发 registerValueChanged → 接收事件/触发链路
    // 对"外部写服务器"完全失聪（客户端写成功但流程永不触发）。
    QSignalSpy changedSpy(srv, &CommunicationNodeBase::registerValueChanged);
    bool wrote = false;
    for (int i = 0; i < 50 && !wrote; ++i) {   // 最多等约 5 秒
        wrote = cli->writeRegister(0, 42);
        if (!wrote)
            QTest::qWait(100);
    }
    QVERIFY2(wrote, "写寄存器请求一直失败（Modbus 客户端未真正建立链路）");
    QTRY_COMPARE_WITH_TIMEOUT(writtenSpy.count(), 1, 5000);
    QCOMPARE(writtenSpy.at(0).at(0).toInt(), 0);
    QCOMPARE(writtenSpy.at(0).at(1).toUInt(), quint16(42));

    // 原始字节必须是 Modbus 大端的 0x002A，类型取该行配置（uint16）
    QTRY_COMPARE_WITH_TIMEOUT(changedSpy.count(), 1, 3000);
    QCOMPARE(changedSpy.at(0).at(0).toInt(), 0);
    QCOMPARE(changedSpy.at(0).at(1).toByteArray(), QByteArray::fromHex("002A"));
    QCOMPARE(changedSpy.at(0).at(2).toString(), QStringLiteral("uint16"));

    // 本次修复点：对 Modbus 设备调 sendData 没有"裸字节"语义，必须**立即失败**，
    // 而不是像以前那样一路投递到基类空实现变成静默 no-op
    QVERIFY2(!cm->sendData(QStringLiteral("MB_CLI"), QByteArray("OK,1")),
             "Modbus 设备不支持原始字节发送，sendData 必须返回失败");

    // 用户可见后果：把「发送数据」算子绑到 Modbus 设备时，流程必须报失败（而不是显示成功）
    {
        FlowScene scene;
        FlowExecutor exec;
        exec.setFlowName(QStringLiteral("CommWritebackModbus"));
        exec.setFlowMode(FlowMode::SoftwareTrigger);
        NodeBase *send = scene.createNode(NodeBase::OUTPUT, QPointF(120, 120),
                                          QStringLiteral("发送数据"));
        QVERIFY2(send != nullptr, "无法创建「发送数据」算子");
        send->setParam(QStringLiteral("deviceName"), QStringLiteral("MB_CLI"));
        send->setParam(QStringLiteral("sendText"), QStringLiteral("OK,1"));
        exec.setFlowScene(&scene);

        exec.startExecution();
        bool finished = exec.wait(10000);
        if (!finished) {
            exec.stopExecution();
            finished = exec.wait(3000);
        }
        exec.setFlowScene(nullptr);
        QVERIFY2(finished, "流程未在 10 秒内结束");
        QVERIFY2(!send->executionSuccess(),
                 "对寄存器设备做裸发送时必须报失败（历史行为是静默 no-op，PLC 什么都没收到却显示成功）");
    }

    QVERIFY(cm->closeDevice(QStringLiteral("MB_CLI")));
    QVERIFY(cm->removeDevice(QStringLiteral("MB_CLI")));
    QVERIFY(cm->closeDevice(QStringLiteral("MB_SRV")));
    QVERIFY(cm->removeDevice(QStringLiteral("MB_SRV")));
}

void CommWritebackTest::testPlcDataTriggersFlowAndWritesBack()
{
    // 完整闭环：模拟 PLC 主动下发 → 接收事件解析 → 全局事件触发 → 流程执行 → 回写 ACK 给 PLC。
    // 这条链此前是断的：GlobalTriggerManager::onDataReceived 解析成功后什么都不做，
    // onEventTriggered 无人调用 ⇒ 配了"接收事件触发"也不会有流程被启动。
    QTcpServer plc;
    plc.listen(QHostAddress::LocalHost, 0);
    QVERIFY2(plc.isListening(), qPrintable(plc.errorString()));

    QByteArray fromPlatform;      // 平台回写的字节
    QTcpSocket *peer = nullptr;   // PLC 侧的连接套接字（平台是 TCP 客户端，所以由服务端往连接上写）
    QObject::connect(&plc, &QTcpServer::newConnection, &plc, [&]() {
        peer = plc.nextPendingConnection();
        QObject::connect(peer, &QTcpSocket::readyRead, peer,
                         [&]() { fromPlatform += peer->readAll(); });
    });

    auto *cm = CommunicationManager::instance();
    QVERIFY(cm->addDevice(QStringLiteral("SIM_TRIG"), QStringLiteral("TCP"),
                          tcpClientConfig(plc.serverPort())));
    QVERIFY(cm->openDevice(QStringLiteral("SIM_TRIG")));

    // 接收事件：文本按分隔符解析（"OK,1" → 两个字段）
    auto *ev = new TextProtocolReceiveEvent(QStringLiteral("EV_OK"), QStringLiteral("SIM_TRIG"));
    ev->setDelimiter(QStringLiteral(","));
    QVERIFY2(cm->addReceiveEvent(ev), "注册接收事件失败");

    // 目标流程：单个「发送数据」算子，回写 ACK（流程是否执行，用"PLC 有没有收到 ACK"来证明）
    FlowScene scene;
    FlowExecutor exec;                       // 其构造函数把通讯数据源接到 GlobalTriggerManager
    const QString flowName = QStringLiteral("CommTriggerFlow");
    exec.setFlowName(flowName);
    exec.setFlowMode(FlowMode::SoftwareTrigger);
    NodeBase *send = scene.createNode(NodeBase::OUTPUT, QPointF(120, 120),
                                      QStringLiteral("发送数据"));
    QVERIFY2(send != nullptr, "无法创建「发送数据」算子");
    send->setParam(QStringLiteral("deviceName"), QStringLiteral("SIM_TRIG"));
    send->setParam(QStringLiteral("suffix"), QString());
    send->setParam(QStringLiteral("sendText"), QStringLiteral("ACK"));
    exec.setFlowScene(&scene);

    auto *gtm = GlobalTriggerManager::instance();
    QVERIFY2(gtm->setEventTrigger(QStringLiteral("EV_OK"), flowName), "配置事件触发失败");
    gtm->registerFlow(flowName, &scene, &exec);
    QSignalSpy firedSpy(gtm, &GlobalTriggerManager::triggerFired);

    // 模拟 PLC 主动下发
    QTRY_VERIFY_WITH_TIMEOUT(peer != nullptr, 3000);
    peer->write("OK,1\r\n");
    peer->flush();

    // 触发确实发生（且指向目标流程）
    QTRY_VERIFY_WITH_TIMEOUT(firedSpy.count() >= 1, 5000);
    QCOMPARE(firedSpy.at(0).at(0).toString(), flowName);
    // 流程确实执行了：ACK 被回写给 PLC
    QTRY_VERIFY_WITH_TIMEOUT(fromPlatform.contains("ACK"), 5000);
    QVERIFY2(send->executionSuccess(), "回写 ACK 应成功");

    gtm->removeEventTrigger(QStringLiteral("EV_OK"));
    gtm->unregisterFlow(flowName);
    cm->removeReceiveEvent(QStringLiteral("EV_OK"));
    exec.setFlowScene(nullptr);
    QVERIFY(cm->closeDevice(QStringLiteral("SIM_TRIG")));
    QVERIFY(cm->removeDevice(QStringLiteral("SIM_TRIG")));
}

void CommWritebackTest::testHeartbeatActuallyGoesOnTheWire()
{
    // 心跳此前只 emit 一个无人接收的 heartBeatSent，设备侧永远收不到任何报文；
    // 而且 intervalMs 从未被使用（基定时器固定 1s）。这里两个都验证。
    QTcpServer plc;
    QByteArray received;
    startSimulatedPlc(plc, received);
    QVERIFY2(plc.isListening(), qPrintable(plc.errorString()));

    auto *cm = CommunicationManager::instance();
    QVERIFY(cm->addDevice(QStringLiteral("SIM_HB"), QStringLiteral("TCP"),
                          tcpClientConfig(plc.serverPort())));
    QVERIFY(cm->openDevice(QStringLiteral("SIM_HB")));

    auto *hb = HeartbeatManager::instance();
    // 300ms 间隔：若 intervalMs 仍被忽略（固定 1s），"≥3 次"就来不及出现
    QVERIFY2(hb->registerHeartbeat(QStringLiteral("SIM_HB"), 300,
                                   QStringLiteral("HB0"), QStringLiteral("HB1")),
             "注册心跳失败");
    QVERIFY(hb->startHeartbeat(QStringLiteral("SIM_HB")));

    // 2500ms 内至少 4 次：300ms 间隔约 1s 就到，而固定 1s 节拍需要 4s —— 因此这条断言
    // 同时钉住"真的发包"和"intervalMs 被真正使用"
    QTRY_VERIFY_WITH_TIMEOUT(received.count("HB") >= 4, 2500);
    QVERIFY2(received.contains("HB0"), "未发出 pattern0");
    QVERIFY2(received.contains("HB1"), "未发出 pattern1（交替逻辑或间隔未生效）");

    // 停止后必须在线上安静（把"配了停止却不生效"也挡掉）
    QVERIFY(hb->stopHeartbeat(QStringLiteral("SIM_HB")));
    QTest::qWait(200);   // 让在途的那一次先落下来
    const int afterStop = received.size();
    QTest::qWait(800);   // 远超 300ms 间隔
    QCOMPARE(received.size(), afterStop);

    hb->unregisterHeartbeat(QStringLiteral("SIM_HB"));
    QVERIFY(cm->closeDevice(QStringLiteral("SIM_HB")));
    QVERIFY(cm->removeDevice(QStringLiteral("SIM_HB")));
}

void CommWritebackTest::testSendEventsActuallySend()
{
    // 发送事件此前只 emit 一个无人接收的 sendCompleted：模板替换、字节组包都写好了，
    // 唯独"发出去"这一步没写（且全仓库无人调用 send()）。这里两条路径都验证到字节级。
    QTcpServer plc;
    QByteArray received;
    startSimulatedPlc(plc, received);
    QVERIFY2(plc.isListening(), qPrintable(plc.errorString()));

    auto *cm = CommunicationManager::instance();
    QVERIFY(cm->addDevice(QStringLiteral("SIM_SE"), QStringLiteral("TCP"),
                          tcpClientConfig(plc.serverPort())));
    QVERIFY(cm->openDevice(QStringLiteral("SIM_SE")));

    // ① 文本-直接输出：模板 {} 替换 + 行尾后缀，且必须真的落线
    auto *txt = new TextDirectSendEvent(QStringLiteral("SE_TXT"), QStringLiteral("SIM_SE"), cm);
    txt->setTemplate(QStringLiteral("OK,{},END"));
    txt->setSuffix(QStringLiteral("\r\n"));
    QVERIFY(cm->addSendEvent(txt));
    QVERIFY2(cm->fireSendEvent(QStringLiteral("SE_TXT"), 42), "文本发送事件应发送成功");
    QTRY_VERIFY_WITH_TIMEOUT(received.contains("OK,42,END\r\n"), 3000);

    // ② 字节组包：小端 int16（固定值 100）+ int16（数据值 7）→ 64 00 07 00
    auto *bin = new BytePackSendEvent(QStringLiteral("SE_BIN"), QStringLiteral("SIM_SE"), cm);
    BytePackField fixedField;
    fixedField.dataType = QStringLiteral("int16");
    fixedField.fixedValue = 100;
    BytePackField dataField;
    dataField.dataType = QStringLiteral("int16");   // 无固定值 → 取 send(data) 的值
    bin->addField(fixedField);
    bin->addField(dataField);
    QVERIFY(cm->addSendEvent(bin));

    received.clear();
    QVERIFY2(cm->fireSendEvent(QStringLiteral("SE_BIN"), 7), "字节组包事件应发送成功");
    QTRY_VERIFY_WITH_TIMEOUT(received.size() >= 4, 3000);
    QByteArray expected;
    expected.append(char(100)).append(char(0)).append(char(7)).append(char(0));
    QCOMPARE(received.left(4), expected);

    // ③ 失败必须可见：设备不存在 → false，且 sendCompleted 报 false（不再"静默成功"）
    auto *bad = new TextDirectSendEvent(QStringLiteral("SE_BAD"), QStringLiteral("NO_SUCH_DEV"), cm);
    bad->setTemplate(QStringLiteral("x"));
    QVERIFY(cm->addSendEvent(bad));
    QSignalSpy doneSpy(bad, &SendEvent::sendCompleted);
    QVERIFY2(!cm->fireSendEvent(QStringLiteral("SE_BAD"), 1), "设备不存在时必须报失败");
    QCOMPARE(doneSpy.count(), 1);
    QCOMPARE(doneSpy.at(0).at(1).toBool(), false);

    // ④ 未注册的事件 ID：入口本身也要能给出"没发"
    QVERIFY(!cm->fireSendEvent(QStringLiteral("NO_SUCH_EVENT"), 1));

    cm->removeSendEvent(QStringLiteral("SE_TXT"));
    cm->removeSendEvent(QStringLiteral("SE_BIN"));
    cm->removeSendEvent(QStringLiteral("SE_BAD"));
    QVERIFY(cm->closeDevice(QStringLiteral("SIM_SE")));
    QVERIFY(cm->removeDevice(QStringLiteral("SIM_SE")));
}

void CommWritebackTest::testHeartbeatFailureRaisesAlarm()
{
    // 心跳失败必须能被上层感知（此前 connectionLost/connectionRestored 无人 emit，
    // 心跳断了也没有任何报警出口）。并验证是**边沿触发**：不会每 300ms 刷一条报警。
    auto *hb = HeartbeatManager::instance();
    QSignalSpy lostSpy(hb, &HeartbeatManager::connectionLost);
    QSignalSpy restoredSpy(hb, &HeartbeatManager::connectionRestored);

    const QString dev = QStringLiteral("SIM_HB_ALARM");
    QVERIFY2(hb->registerHeartbeat(dev, 300, QStringLiteral("P"), QStringLiteral("Q")),
             "注册心跳失败");
    QVERIFY(hb->startHeartbeat(dev));

    // 设备还不存在 → 第一次失败就应报 connectionLost
    QTRY_VERIFY_WITH_TIMEOUT(lostSpy.count() >= 1, 3000);
    QCOMPARE(lostSpy.at(0).at(0).toString(), dev);

    // 仍持续失败，但不应重复刷报警
    const int lostCount = lostSpy.count();
    QTest::qWait(700);
    QCOMPARE(lostSpy.count(), lostCount);

    // 设备上线 → 心跳恢复 → connectionRestored（且心跳真的发到新设备上）
    QTcpServer plc;
    QByteArray received;
    startSimulatedPlc(plc, received);
    auto *cm = CommunicationManager::instance();
    QVERIFY(cm->addDevice(dev, QStringLiteral("TCP"), tcpClientConfig(plc.serverPort())));
    QVERIFY(cm->openDevice(dev));

    QTRY_VERIFY_WITH_TIMEOUT(restoredSpy.count() >= 1, 3000);
    QCOMPARE(restoredSpy.at(0).at(0).toString(), dev);
    QTRY_VERIFY_WITH_TIMEOUT(received.contains("P") || received.contains("Q"), 3000);

    hb->stopHeartbeat(dev);
    hb->unregisterHeartbeat(dev);
    QVERIFY(cm->closeDevice(dev));
    QVERIFY(cm->removeDevice(dev));
}

void CommWritebackTest::testStringTriggerStartsFlow()
{
    // 字符串触发是此前唯一"一直可用"的触发路径，但一直没有用例保护。
    // 与接收事件触发互补：PLC 下发匹配串 → 直接启动目标流程。
    QTcpServer plc;
    QByteArray fromPlatform;
    QTcpSocket *peer = nullptr;
    plc.listen(QHostAddress::LocalHost, 0);
    QVERIFY2(plc.isListening(), qPrintable(plc.errorString()));
    QObject::connect(&plc, &QTcpServer::newConnection, &plc, [&]() {
        peer = plc.nextPendingConnection();
        QObject::connect(peer, &QTcpSocket::readyRead, peer,
                         [&]() { fromPlatform += peer->readAll(); });
    });

    auto *cm = CommunicationManager::instance();
    QVERIFY(cm->addDevice(QStringLiteral("SIM_STR"), QStringLiteral("TCP"),
                          tcpClientConfig(plc.serverPort())));
    QVERIFY(cm->openDevice(QStringLiteral("SIM_STR")));

    FlowScene scene;
    FlowExecutor exec;
    const QString flowName = QStringLiteral("CommStringTriggerFlow");
    exec.setFlowName(flowName);
    exec.setFlowMode(FlowMode::SoftwareTrigger);
    NodeBase *send = scene.createNode(NodeBase::OUTPUT, QPointF(120, 120),
                                      QStringLiteral("发送数据"));
    QVERIFY2(send != nullptr, "无法创建「发送数据」算子");
    send->setParam(QStringLiteral("deviceName"), QStringLiteral("SIM_STR"));
    send->setParam(QStringLiteral("suffix"), QString());
    send->setParam(QStringLiteral("sendText"), QStringLiteral("TRIGGERED"));
    exec.setFlowScene(&scene);

    auto *gtm = GlobalTriggerManager::instance();
    QVERIFY2(gtm->setStringTrigger(QStringLiteral("GO"), flowName), "配置字符串触发失败");
    gtm->registerFlow(flowName, &scene, &exec);
    QSignalSpy firedSpy(gtm, &GlobalTriggerManager::triggerFired);

    QTRY_VERIFY_WITH_TIMEOUT(peer != nullptr, 3000);
    peer->write("GO\r\n");   // 触发按 trim 后匹配
    peer->flush();

    QTRY_VERIFY_WITH_TIMEOUT(firedSpy.count() >= 1, 5000);
    QCOMPARE(firedSpy.at(0).at(0).toString(), flowName);
    QTRY_VERIFY_WITH_TIMEOUT(fromPlatform.contains("TRIGGERED"), 5000);
    QVERIFY2(send->executionSuccess(), "回写应成功");

    gtm->removeStringTrigger(QStringLiteral("GO"));
    gtm->unregisterFlow(flowName);
    exec.setFlowScene(nullptr);
    QVERIFY(cm->closeDevice(QStringLiteral("SIM_STR")));
    QVERIFY(cm->removeDevice(QStringLiteral("SIM_STR")));
}

void CommWritebackTest::testEnabledSendEventsFireOnRoundEnd()
{
    // 「每轮结束自动上报已启用的发送事件」策略的落点在核心层
    // （CommunicationManager::fireEnabledSendEvents），故这里能直接测到语义：
    //   已启用 → 发出；被禁用 → 不发出且不计入；设备不存在 → 不计入。
    QTcpServer plc;
    QByteArray received;
    startSimulatedPlc(plc, received);
    QVERIFY2(plc.isListening(), qPrintable(plc.errorString()));

    auto *cm = CommunicationManager::instance();
    QVERIFY(cm->addDevice(QStringLiteral("SIM_RSE"), QStringLiteral("TCP"),
                          tcpClientConfig(plc.serverPort())));
    QVERIFY(cm->openDevice(QStringLiteral("SIM_RSE")));

    auto *on = new TextDirectSendEvent(QStringLiteral("RSE_ON"), QStringLiteral("SIM_RSE"), cm);
    on->setTemplate(QStringLiteral("ROUND-END"));
    on->setSuffix(QStringLiteral("\r\n"));
    QVERIFY(cm->addSendEvent(on));

    auto *off = new TextDirectSendEvent(QStringLiteral("RSE_OFF"), QStringLiteral("SIM_RSE"), cm);
    off->setTemplate(QStringLiteral("MUST-NOT-SEND"));
    off->setSuffix(QStringLiteral("\r\n"));
    off->setEnabled(false);   // 被禁用的事件不得发出
    QVERIFY(cm->addSendEvent(off));

    auto *dead = new TextDirectSendEvent(QStringLiteral("RSE_DEAD"), QStringLiteral("NO_SUCH_DEV"), cm);
    dead->setTemplate(QStringLiteral("MUST-NOT-SEND-EITHER"));
    QVERIFY(cm->addSendEvent(dead));   // 设备不存在 → 不得计入

    const int sent = cm->fireEnabledSendEvents();
    QCOMPARE(sent, 1);   // 只有 RSE_ON 真的发出
    QTRY_VERIFY_WITH_TIMEOUT(received.contains("ROUND-END"), 3000);
    QVERIFY2(!received.contains("MUST-NOT-SEND"), "被禁用的事件不得发出");
    QVERIFY2(!received.contains("MUST-NOT-SEND-EITHER"), "设备不存在的事件不得发出");

    cm->removeSendEvent(QStringLiteral("RSE_ON"));
    cm->removeSendEvent(QStringLiteral("RSE_OFF"));
    cm->removeSendEvent(QStringLiteral("RSE_DEAD"));
    QVERIFY(cm->closeDevice(QStringLiteral("SIM_RSE")));
    QVERIFY(cm->removeDevice(QStringLiteral("SIM_RSE")));
}

void CommWritebackTest::testNodeTemplateRoundTrip()
{
    // 算子模板库：保存 = toJson()（去掉 moduleId/position），插入 = createById + fromJson。
    // 这里要验证的正是模板存在的意义 —— **跨流程**成立，且参数真的跟着走。
    // 注意必须重定向存储路径：否则测试会写进真实用户数据目录（QSettings 上踩过一次这个坑）。
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    NodeTemplateStore::instance().setStorageFilePathOverride(tmp.filePath(QStringLiteral("t.json")));

    // 场景 A：造一个带标志性参数值的算子，存成模板
    FlowScene sourceScene;
    NodeBase *origin = sourceScene.createNode(NodeBase::OUTPUT, QPointF(100, 100),
                                              QStringLiteral("模板源"));
    QVERIFY2(origin != nullptr, "无法创建算子");
    origin->setParam(QStringLiteral("sendText"), QStringLiteral("TMPL-VALUE-42"));
    origin->setParam(QStringLiteral("deviceName"), QStringLiteral("SIM_X"));

    QString error;
    QVERIFY2(NodeTemplateStore::instance().saveFromNode(QStringLiteral("T1"), origin, &error),
             qPrintable(error));
    QVERIFY(NodeTemplateStore::instance().names().contains(QStringLiteral("T1")));

    // 落盘记录里不应带 moduleId / position（实例要保持自己的模块 ID，位置由插入时决定）
    const QJsonObject stored = NodeTemplateStore::instance().nodeJson(QStringLiteral("T1"));
    QVERIFY2(!stored.contains(QStringLiteral("moduleId")), "模板不应携带 moduleId");
    QVERIFY2(!stored.contains(QStringLiteral("position")), "模板不应携带 position");

    // 场景 B（另一个流程）：按模板插入，参数应原样带过来、位置用插入点
    FlowScene targetScene;
    NodeBase *inserted = targetScene.createNodeFromTemplate(QStringLiteral("T1"), QPointF(10, 10));
    QVERIFY2(inserted != nullptr, "模板实例化失败");
    const QJsonObject params = inserted->toJson().value(QStringLiteral("params")).toObject();
    QCOMPARE(params.value(QStringLiteral("sendText")).toString(), QStringLiteral("TMPL-VALUE-42"));
    QCOMPARE(params.value(QStringLiteral("deviceName")).toString(), QStringLiteral("SIM_X"));
    QCOMPARE(inserted->position(), QPointF(10, 10));

    // 不存在的模板：不得崩、返回 nullptr
    QVERIFY(targetScene.createNodeFromTemplate(QStringLiteral("没有这个模板"), QPointF(0, 0))
            == nullptr);

    QVERIFY(NodeTemplateStore::instance().remove(QStringLiteral("T1")));
    QVERIFY(!NodeTemplateStore::instance().contains(QStringLiteral("T1")));
    NodeTemplateStore::instance().setStorageFilePathOverride(QString());
}

void CommWritebackTest::testRecipeSaveAndLoad()
{
    // 配方功能此前是**完全失效**的：对话框拿不到场景（saveRecipe 收到 nullptr 立刻返回 false）
    // 却照样发成功信号；loadRecipe 又无条件 return true。这里把两端都钉住：
    //   保存 → 真的存下；加载 → 真的写回；匹配不上 → 如实返回 false；模块ID对不上 → 也是 false。
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    RecipeManager::setStoragePathOverride(tmp.filePath(QStringLiteral("recipes.json")));

    FlowScene scene;
    NodeBase *node = scene.createNode(NodeBase::OUTPUT, QPointF(20, 20),
                                      QStringLiteral("配方算子"));
    QVERIFY2(node != nullptr, "无法创建算子");
    node->setParam(QStringLiteral("sendText"), QStringLiteral("RECIPE-A"));

    auto *rm = RecipeManager::instance();
    QVERIFY2(rm->saveRecipe(QStringLiteral("VFP_TEST_RECIPE"), QStringLiteral("自动用例"),
                            &scene),
             "保存配方失败");

    // 改掉参数再加载回来 → 应恢复成保存时的值（证明参数真的被存下并写回）
    node->setParam(QStringLiteral("sendText"), QStringLiteral("CHANGED"));
    QVERIFY2(rm->loadRecipe(QStringLiteral("VFP_TEST_RECIPE"), &scene), "加载配方失败");
    const QJsonObject params = node->toJson().value(QStringLiteral("params")).toObject();
    QCOMPARE(params.value(QStringLiteral("sendText")).toString(), QStringLiteral("RECIPE-A"));

    // 不存在的配方：false（不再谎报成功）
    QVERIFY(!rm->loadRecipe(QStringLiteral("VFP_TEST_NOT_EXIST"), &scene));

    // ---- 改按名称匹配后：同一份配方要能加载到**另一个工程**里同名的算子上 ----
    // 这是本次改动的核心：以前按 moduleId 匹配，换个工程必然一个都匹配不上。
    FlowScene otherScene;
    NodeBase *sameName = otherScene.createNode(NodeBase::OUTPUT, QPointF(70, 70),
                                               QStringLiteral("配方算子"));
    QVERIFY2(sameName != nullptr, "无法创建算子");
    QVERIFY2(rm->loadRecipe(QStringLiteral("VFP_TEST_RECIPE"), &otherScene),
             "同名算子跨工程加载失败");
    QCOMPARE(sameName->toJson().value(QStringLiteral("params")).toObject()
                 .value(QStringLiteral("sendText")).toString(),
             QStringLiteral("RECIPE-A"));

    // ---- 同名重复算子：按屏幕顺序（上→下、左→右）编号，各自往返 ----
    FlowScene dupSource;
    NodeBase *dup1 = dupSource.createNode(NodeBase::OUTPUT, QPointF(20, 20), QStringLiteral("同名"));
    NodeBase *dup2 = dupSource.createNode(NodeBase::OUTPUT, QPointF(20, 120), QStringLiteral("同名"));
    QVERIFY(dup1 != nullptr && dup2 != nullptr);
    dup1->setParam(QStringLiteral("sendText"), QStringLiteral("DUP-FIRST"));
    dup2->setParam(QStringLiteral("sendText"), QStringLiteral("DUP-SECOND"));
    QVERIFY(rm->saveRecipe(QStringLiteral("VFP_TEST_DUP"), QString(), &dupSource));

    FlowScene dupTarget;
    NodeBase *t1 = dupTarget.createNode(NodeBase::OUTPUT, QPointF(20, 20), QStringLiteral("同名"));
    NodeBase *t2 = dupTarget.createNode(NodeBase::OUTPUT, QPointF(20, 120), QStringLiteral("同名"));
    QVERIFY(t1 != nullptr && t2 != nullptr);
    t1->setParam(QStringLiteral("sendText"), QStringLiteral("CHANGED-1"));
    t2->setParam(QStringLiteral("sendText"), QStringLiteral("CHANGED-2"));
    QVERIFY2(rm->loadRecipe(QStringLiteral("VFP_TEST_DUP"), &dupTarget), "同名重复算子加载失败");
    QCOMPARE(t1->toJson().value(QStringLiteral("params")).toObject()
                 .value(QStringLiteral("sendText")).toString(),
             QStringLiteral("DUP-FIRST"));
    QCOMPARE(t2->toJson().value(QStringLiteral("params")).toObject()
                 .value(QStringLiteral("sendText")).toString(),
             QStringLiteral("DUP-SECOND"));
    QVERIFY(rm->deleteRecipe(QStringLiteral("VFP_TEST_DUP")));

    QVERIFY(rm->deleteRecipe(QStringLiteral("VFP_TEST_RECIPE")));
    QVERIFY(!rm->recipeNames().contains(QStringLiteral("VFP_TEST_RECIPE")));
    RecipeManager::setStoragePathOverride(QString());
}

void CommWritebackTest::testRoiParamRoundTrip()
{
    // ROI 的"几何 ⇄ 参数"映射：画布上的拖拽测不了，但真正容易出错的就是这一层
    // （拖完框没写回参数、或参数改了框不跟着动）。这里直接构造节点（ctor 是 public，
    // 不需要场景也不需要注册表），验证往返一致，并验证"清除几何"后不再显示。
    FindCircleNode circle;
    circle.init();
    RoiShape c;
    c.type = RoiType::Circle;
    c.p1 = QPointF(300.0, 120.0);   // 圆心（x=列, y=行）
    c.p2 = QPointF(340.0, 120.0);   // 圆上一点 → 半径 40
    circle.applyGeometryRoi(c);

    const RoiShape cBack = circle.geometryRoi();
    QVERIFY(cBack.type == RoiType::Circle);
    QCOMPARE(cBack.p1, QPointF(300.0, 120.0));
    QVERIFY(qAbs((cBack.p2.x() - cBack.p1.x()) - 40.0) < 1e-6);   // 半径经参数往返不变

    // 清除几何 → 半径归零 → 不再显示（搜索区域是必需参数，归零后节点会明确失败，
    // 而不是悄悄沿用上一次的圆）
    circle.applyGeometryRoi(RoiShape());
    QVERIFY(circle.geometryRoi().type == RoiType::None);

    FindLineNode line;
    line.init();
    RoiShape l;
    l.type = RoiType::Line;
    l.p1 = QPointF(50.0, 60.0);     // (列, 行)
    l.p2 = QPointF(250.0, 160.0);
    line.applyGeometryRoi(l);

    const RoiShape lBack = line.geometryRoi();
    QVERIFY(lBack.type == RoiType::Line);
    QCOMPARE(lBack.p1, QPointF(50.0, 60.0));
    QCOMPARE(lBack.p2, QPointF(250.0, 160.0));

    line.applyGeometryRoi(RoiShape());
    QVERIFY(line.geometryRoi().type == RoiType::None);

    // 卡尺测量：与线段查找同为 Metrology 线型（Line），复用上面的线段几何
    CaliperMeasureNode caliper;
    caliper.init();
    caliper.applyGeometryRoi(l);
    const RoiShape calBack = caliper.geometryRoi();
    QVERIFY(calBack.type == RoiType::Line);
    QCOMPARE(calBack.p1, l.p1);
    QCOMPARE(calBack.p2, l.p2);
    caliper.applyGeometryRoi(RoiShape());
    QVERIFY(caliper.geometryRoi().type == RoiType::None);

    // 亚像素边缘点：矩形 ROI（参数与几何的映射同拟合族）
    EdgePointsNode edges;
    edges.init();
    RoiShape r;
    r.type = RoiType::Rect;
    r.p1 = QPointF(30.0, 40.0);     // 左上（列, 行）
    r.p2 = QPointF(130.0, 140.0);   // 右下
    edges.applyGeometryRoi(r);
    const RoiShape rBack = edges.geometryRoi();
    QVERIFY(rBack.type == RoiType::Rect);
    QCOMPARE(rBack.p1, QPointF(30.0, 40.0));
    QCOMPARE(rBack.p2, QPointF(130.0, 140.0));
    edges.applyGeometryRoi(RoiShape());
    QVERIFY(edges.geometryRoi().type == RoiType::None);

    // 阈值分割 / Blob 分析：矩形 ROI（同一套映射；它们的参数面板是手工搭的，
    // 因此 ROI 靠画布拖框设置，不会出现在面板上）
    ThresholdNode threshold;
    threshold.init();
    threshold.applyGeometryRoi(r);
    const RoiShape thBack = threshold.geometryRoi();
    QVERIFY(thBack.type == RoiType::Rect);
    QCOMPARE(thBack.p1, QPointF(30.0, 40.0));
    QCOMPARE(thBack.p2, QPointF(130.0, 140.0));
    threshold.applyGeometryRoi(RoiShape());
    QVERIFY(threshold.geometryRoi().type == RoiType::None);

    BlobAnalysisNode blob;
    blob.init();
    blob.applyGeometryRoi(r);
    const RoiShape blobBack = blob.geometryRoi();
    QVERIFY(blobBack.type == RoiType::Rect);
    QCOMPARE(blobBack.p1, QPointF(30.0, 40.0));
    QCOMPARE(blobBack.p2, QPointF(130.0, 140.0));
    blob.applyGeometryRoi(RoiShape());
    QVERIFY(blob.geometryRoi().type == RoiType::None);

    // Otsu 二值化 / 动态阈值：矩形 ROI（这两个走 registerParams，
    // ROI 参数与其它参数同一条面板/序列化通路）
    OtsuThresholdNode otsu;
    otsu.init();
    otsu.applyGeometryRoi(r);
    const RoiShape otsuBack = otsu.geometryRoi();
    QVERIFY(otsuBack.type == RoiType::Rect);
    QCOMPARE(otsuBack.p1, QPointF(30.0, 40.0));
    QCOMPARE(otsuBack.p2, QPointF(130.0, 140.0));
    otsu.applyGeometryRoi(RoiShape());
    QVERIFY(otsu.geometryRoi().type == RoiType::None);

    DynThresholdNode dyn;
    dyn.init();
    dyn.applyGeometryRoi(r);
    const RoiShape dynBack = dyn.geometryRoi();
    QVERIFY(dynBack.type == RoiType::Rect);
    QCOMPARE(dynBack.p1, QPointF(30.0, 40.0));
    QCOMPARE(dynBack.p2, QPointF(130.0, 140.0));
    dyn.applyGeometryRoi(RoiShape());
    QVERIFY(dyn.geometryRoi().type == RoiType::None);
}

void CommWritebackTest::testRecipeTypeMismatchSkipped()
{
    // ② 配方加载的**类型校验**：名称相同但实现类型不同的算子应被跳过，
    // 而不是把参数错写进去。这一层此前完全没被用例钉住——通用算子 typeId 为空走不进校验分支，
    // 必须用两个**真实注册**算子（各有非空 vfpNodeTypeId）才能触发。
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    RecipeManager::setStoragePathOverride(tmp.filePath(QStringLiteral("recipes.json")));
    registerAllNodes();   // 确保注册表已填充（createById 依赖它）

    // 源场景：真实「圆拟合」算子，起一个易识别的参数值
    FlowScene sourceScene;
    NodeBase *circle = NodeRegistry::instance().createById(
        QStringLiteral("OpencvFitCircleNode"), &sourceScene);
    QVERIFY2(circle != nullptr, "无法创建圆拟合算子");
    QVERIFY2(!circle->property("vfpNodeTypeId").toString().isEmpty(),
             "注册算子应有 vfpNodeTypeId");
    sourceScene.adoptNode(circle, QPointF(10, 10));   // 必须接入场景，否则不在 scene.nodes() 里
    circle->init();                                   // init 后参数才有默认值，再覆盖
    circle->setName(QStringLiteral("配方算子"));
    circle->setParam(QStringLiteral("minPoints"), 999);   // 易识别的值

    auto *rm = RecipeManager::instance();
    QVERIFY2(rm->saveRecipe(QStringLiteral("VFP_TYPE_RECIPE"), QString(), &sourceScene),
             "保存配方失败");

    // 目标场景：真实「直线拟合」算子（同名、但类型不同）
    FlowScene targetScene;
    NodeBase *line = NodeRegistry::instance().createById(
        QStringLiteral("OpencvFitLineNode"), &targetScene);
    QVERIFY2(line != nullptr, "无法创建直线拟合算子");
    targetScene.adoptNode(line, QPointF(10, 10));   // 必须接入场景，否则不在 scene.nodes() 里
    line->init();
    line->setName(QStringLiteral("配方算子"));   // 同名 → 配方会按名称找到它
    line->setParam(QStringLiteral("minPoints"), 5);   // 原值（与圆拟合的 999 不同）

    // 加载应跳过类型不匹配的算子：返回 false，且参数不被改写
    QVERIFY2(!rm->loadRecipe(QStringLiteral("VFP_TYPE_RECIPE"), &targetScene),
             "类型不匹配时应跳过而不是写错参数");
    QCOMPARE(line->toJson().value(QStringLiteral("params")).toObject()
                 .value(QStringLiteral("minPoints")).toInt(), 5);   // 未被 999 覆盖

    QVERIFY(rm->deleteRecipe(QStringLiteral("VFP_TYPE_RECIPE")));
    RecipeManager::setStoragePathOverride(QString());
}

void CommWritebackTest::testMultiFieldTemplateWithInjectedData()
{
    // 对标 VM 的"每轮上报结果"：文本模板的命名占位符 {模块号.参数名} / {global.变量名}
    // 由每轮注入的 QVariantMap 填充（此前模板只能 {} 单个值，无法把多个结果拼成一条报文）
    QTcpServer plc;
    QByteArray received;
    startSimulatedPlc(plc, received);
    QVERIFY2(plc.isListening(), qPrintable(plc.errorString()));

    auto *cm = CommunicationManager::instance();
    QVERIFY(cm->addDevice(QStringLiteral("SIM_TMPL"), QStringLiteral("TCP"),
                          tcpClientConfig(plc.serverPort())));
    QVERIFY(cm->openDevice(QStringLiteral("SIM_TMPL")));

    auto *ev = new TextDirectSendEvent(QStringLiteral("TMPL_MULTI"),
                                       QStringLiteral("SIM_TMPL"), cm);
    ev->setTemplate(QStringLiteral("{1.结果},{global.计数},{2.x}"));
    ev->setSuffix(QStringLiteral("\r\n"));
    QVERIFY(cm->addSendEvent(ev));

    QVariantMap payload;
    payload[QStringLiteral("1.结果")] = QStringLiteral("OK");
    payload[QStringLiteral("global.计数")] = 42;
    payload[QStringLiteral("2.x")] = 3.5;
    QVERIFY2(cm->fireSendEvent(QStringLiteral("TMPL_MULTI"), payload), "多字段模板发送失败");
    QTRY_VERIFY_WITH_TIMEOUT(received.contains("OK,42,3.5"), 3000);

    // 未提供的占位符保留原样：便于现场一眼发现拼写错，而不是静默变空
    received.clear();
    ev->setTemplate(QStringLiteral("A{9.不存在}B"));
    QVERIFY(cm->fireSendEvent(QStringLiteral("TMPL_MULTI"), payload));
    QTRY_VERIFY_WITH_TIMEOUT(received.contains("A{9.不存在}B"), 3000);

    cm->removeSendEvent(QStringLiteral("TMPL_MULTI"));
    QVERIFY(cm->closeDevice(QStringLiteral("SIM_TMPL")));
    QVERIFY(cm->removeDevice(QStringLiteral("SIM_TMPL")));
}

void CommWritebackTest::testTextReceiveEventRegexParse()
{
    // 正则解析模式（对标 VM 文本解析）：捕获组作为字段；无捕获组时整体匹配；不匹配不得虚报
    TextProtocolReceiveEvent ev(QStringLiteral("EV_RE"), QStringLiteral("DEV"));
    QVERIFY(ev.parseMode() == TextProtocolReceiveEvent::Delimiter);   // 默认保持旧行为
    ev.setParseMode(TextProtocolReceiveEvent::Regex);
    ev.setRegex(QStringLiteral("X(-?\\d+),Y(-?\\d+)"));

    QList<QVariant> fields;
    QVERIFY2(ev.parse(QByteArray("X12,Y-7\r\n"), fields), "正则应匹配");
    QCOMPARE(fields.size(), 2);
    QCOMPARE(fields[0].toString(), QStringLiteral("12"));
    QCOMPARE(fields[1].toString(), QStringLiteral("-7"));

    // 无捕获组 → 全局匹配，整体作为字段（一行多个数）
    TextProtocolReceiveEvent ev2(QStringLiteral("EV_RE2"), QStringLiteral("DEV"));
    ev2.setParseMode(TextProtocolReceiveEvent::Regex);
    ev2.setRegex(QStringLiteral("-?\\d+\\.?\\d*"));
    QList<QVariant> f2;
    QVERIFY(ev2.parse(QByteArray("1.5 2.5 3"), f2));
    QCOMPARE(f2.size(), 3);
    QCOMPARE(f2[0].toString(), QStringLiteral("1.5"));

    // 不匹配 → 不得发出事件
    TextProtocolReceiveEvent ev3(QStringLiteral("EV_RE3"), QStringLiteral("DEV"));
    ev3.setParseMode(TextProtocolReceiveEvent::Regex);
    ev3.setRegex(QStringLiteral("NO_MATCH_(\\d+)"));
    QList<QVariant> f3;
    QVERIFY(!ev3.parse(QByteArray("hello"), f3));

    // 非法正则 → false 而不是崩溃
    TextProtocolReceiveEvent ev4(QStringLiteral("EV_RE4"), QStringLiteral("DEV"));
    ev4.setParseMode(TextProtocolReceiveEvent::Regex);
    ev4.setRegex(QStringLiteral("([unclosed"));
    QList<QVariant> f4;
    QVERIFY(!ev4.parse(QByteArray("x"), f4));

    // 序列化往返：模式与表达式随方案保存
    const QJsonObject j = ev.toJson();
    QCOMPARE(j[QStringLiteral("parseMode")].toInt(),
             int(TextProtocolReceiveEvent::Regex));
    TextProtocolReceiveEvent back(QStringLiteral("EV_RE"), QStringLiteral("DEV"));
    back.fromJson(j);
    QVERIFY(back.parseMode() == TextProtocolReceiveEvent::Regex);
    QCOMPARE(back.regex(), QStringLiteral("X(-?\\d+),Y(-?\\d+)"));
}

void CommWritebackTest::testUdpRoundTrip()
{
    // UDP 设备（对标 VM 4.4 UDP 通信）：发送到目标 + 从本地端口接收
    const quint16 kUdpLocalPort = 15601;   // 高位端口，避开常见占用
    QUdpSocket peer;
    QVERIFY2(peer.bind(QHostAddress::LocalHost, 0), qPrintable(peer.errorString()));
    const quint16 peerPort = peer.localPort();

    auto *cm = CommunicationManager::instance();
    QJsonObject cfg;
    cfg[QStringLiteral("localPort")] = int(kUdpLocalPort);
    cfg[QStringLiteral("remoteIp")] = QStringLiteral("127.0.0.1");
    cfg[QStringLiteral("remotePort")] = int(peerPort);
    QVERIFY2(cm->addDevice(QStringLiteral("SIM_UDP"), QStringLiteral("UDP"), cfg),
             "addDevice(UDP) 失败");
    QVERIFY2(cm->openDevice(QStringLiteral("SIM_UDP")), "UDP 绑定失败");

    // ① 发送：平台 → 外部
    QVERIFY2(cm->sendData(QStringLiteral("SIM_UDP"), QByteArray("UDP-HELLO")),
             "UDP 发送投递失败");
    QTRY_VERIFY_WITH_TIMEOUT(peer.hasPendingDatagrams(), 3000);
    QByteArray got;
    got.resize(int(peer.pendingDatagramSize()));
    peer.readDatagram(got.data(), got.size());
    QCOMPARE(got, QByteArray("UDP-HELLO"));

    // ② 接收：外部 → 平台（节点的 dataReceived 必须真的触发）
    auto *node = cm->deviceNode(QStringLiteral("SIM_UDP"));
    QVERIFY(node != nullptr);
    QSignalSpy rxSpy(node, &CommunicationNodeBase::dataReceived);
    peer.writeDatagram(QByteArray("UDP-PING"), QHostAddress::LocalHost, kUdpLocalPort);
    QTRY_VERIFY_WITH_TIMEOUT(rxSpy.count() >= 1, 3000);
    QCOMPARE(rxSpy.at(0).at(0).toByteArray(), QByteArray("UDP-PING"));

    QVERIFY(cm->closeDevice(QStringLiteral("SIM_UDP")));
    QVERIFY(cm->removeDevice(QStringLiteral("SIM_UDP")));
}

void CommWritebackTest::testTcpAutoReconnect()
{
    // 历史缺陷：TCP 断线后只报警不重连、永久失联（Modbus/PLC/串口都有重连，唯独最常用的 TCP 没有）。
    // 闭环：连上 → 服务端主动断开 → 服务端重开 → 客户端应在重连间隔内自动恢复并能再发数据。
    const int kReconnectIntervalMs = 500;   // 用例里用最短间隔（下限 500）

    QTcpServer plc;
    QByteArray received;
    startSimulatedPlc(plc, received);
    QVERIFY2(plc.isListening(), qPrintable(plc.errorString()));
    const quint16 port = plc.serverPort();

    auto *cm = CommunicationManager::instance();
    QJsonObject cfg = tcpClientConfig(port);
    cfg[QStringLiteral("autoReconnect")] = true;
    cfg[QStringLiteral("reconnectInterval")] = kReconnectIntervalMs;
    QVERIFY(cm->addDevice(QStringLiteral("SIM_RC"), QStringLiteral("TCP"), cfg));
    QVERIFY(cm->openDevice(QStringLiteral("SIM_RC")));
    QVERIFY2(cm->sendData(QStringLiteral("SIM_RC"), QByteArray("BEFORE")), "初始发送失败");
    QTRY_VERIFY_WITH_TIMEOUT(received.contains("BEFORE"), 3000);

    // 服务端断开：关闭监听并踢掉现有连接
    received.clear();
    plc.close();
    QTest::qWait(200);   // 让断开事件落地（客户端进入重连等待）

    // 服务端在原端口重开：客户端应在重连间隔内自动连回来
    QVERIFY2(plc.listen(QHostAddress::LocalHost, port), qPrintable(plc.errorString()));
    QObject::connect(&plc, &QTcpServer::newConnection, &plc, [&plc, &received]() {
        QTcpSocket *cli = plc.nextPendingConnection();
        QObject::connect(cli, &QTcpSocket::readyRead, cli,
                         [cli, &received]() { received += cli->readAll(); });
    });

    // 自动重连成功：无需任何手动 openDevice 就能把数据重新发到线路上
    bool resent = false;
    for (int i = 0; i < 40 && !resent; ++i) {   // 最多约 6 秒
        resent = cm->sendData(QStringLiteral("SIM_RC"), QByteArray("AFTER"));
        if (!resent)
            QTest::qWait(150);
    }
    QVERIFY2(resent, "断线后未能自动重连（sendData 一直失败）");
    QTRY_VERIFY_WITH_TIMEOUT(received.contains("AFTER"), 3000);

    auto *node = cm->deviceNode(QStringLiteral("SIM_RC"));
    QVERIFY(node != nullptr);

    // 回归：重连成功后状态必须稳定保持（历史 bug：重连替换 socket 时，旧 socket 的延迟
    // disconnected 信号把新连接误清为"未连接"→ 触发下一轮重连并切断健康连接 →
    // 现场表现为网络调试助手反复 online/offline、界面永远显示"未连接"）
    {
        QSignalSpy closedSpy(node, &CommunicationNodeBase::connectionClosed);
        QTest::qWait(kReconnectIntervalMs * 3);   // 覆盖至少 3 个重连周期
        QVERIFY2(node->isConnected(), "重连成功后状态被误清（反复重连死循环回归）");
        QCOMPARE(closedSpy.count(), 0);
    }

    // 自动重连开关关闭后：断开就真的断开，不再自动重连
    node->setParam(QStringLiteral("autoReconnect"), false);
    QVERIFY(cm->closeDevice(QStringLiteral("SIM_RC")));
    QVERIFY(cm->openDevice(QStringLiteral("SIM_RC")));
    plc.close();
    QTest::qWait(kReconnectIntervalMs * 3);
    QVERIFY2(!node->isConnected(), "关闭自动重连后不得自行重连");

    QVERIFY(cm->closeDevice(QStringLiteral("SIM_RC")));
    QVERIFY(cm->removeDevice(QStringLiteral("SIM_RC")));
}

void CommWritebackTest::testFrameAssemblerTerminatorAndTimeout()
{
    // 组帧治理（粘包/半包）：接收侧不再按"到达块"触发，而是按帧触发。
    // 场景：对端（模拟 PLC）连续发两帧 / 一帧拆两次发 / 无结束符数据静默成帧。
    QTcpServer plc;
    QTcpSocket *peer = nullptr;
    plc.listen(QHostAddress::LocalHost, 0);
    QVERIFY2(plc.isListening(), qPrintable(plc.errorString()));
    QObject::connect(&plc, &QTcpServer::newConnection, &plc,
                     [&plc, &peer]() { peer = plc.nextPendingConnection(); });

    auto *cm = CommunicationManager::instance();
    QJsonObject cfg = tcpClientConfig(plc.serverPort());
    cfg[QStringLiteral("frameTerminator")] = QStringLiteral("\\r\\n");   // 转义 → CRLF
    cfg[QStringLiteral("frameTimeoutMs")] = 600;   // 放宽裕度：避免 80/150ms 级紧时序断言在负载下抖动
    QVERIFY(cm->addDevice(QStringLiteral("SIM_FRAME"), QStringLiteral("TCP"), cfg));
    QVERIFY(cm->openDevice(QStringLiteral("SIM_FRAME")));
    QTRY_VERIFY_WITH_TIMEOUT(peer != nullptr, 3000);

    QSignalSpy rxSpy(cm, &CommunicationManager::dataReceived);
    QVERIFY(rxSpy.isValid());

    // ① 粘包治理：一次写入两帧 → 应产出两个独立帧
    peer->write("A,1\r\nB,2\r\n");
    peer->flush();
    QTRY_VERIFY_WITH_TIMEOUT(rxSpy.count() >= 2, 3000);
    QStringList frames;
    for (int i = 0; i < rxSpy.count(); ++i)
        frames << QString::fromLatin1(rxSpy.at(i).at(1).toByteArray());
    QVERIFY2(frames.contains(QStringLiteral("A,1\r\n")), qPrintable(frames.join(QLatin1Char('|'))));
    QVERIFY2(frames.contains(QStringLiteral("B,2\r\n")), qPrintable(frames.join(QLatin1Char('|'))));

    // ② 半包治理：一帧拆两次写 → 只有凑齐结束符才成帧（中途不得触发）
    rxSpy.clear();
    peer->write("C,");
    peer->flush();
    QTest::qWait(120);   // 远小于帧超时（600ms）
    QCOMPARE(rxSpy.count(), 0);
    peer->write("3\r\n");
    peer->flush();
    QTRY_VERIFY_WITH_TIMEOUT(rxSpy.count() >= 1, 3000);
    QCOMPARE(rxSpy.at(0).at(1).toByteArray(), QByteArray("C,3\r\n"));

    // ③ 超时兜底：对端不发结束符时，静默 600ms 后把剩余数据整体成帧
    rxSpy.clear();
    peer->write("NO-TERM");
    peer->flush();
    QTRY_VERIFY_WITH_TIMEOUT(rxSpy.count() >= 1, 3000);
    QCOMPARE(rxSpy.at(0).at(1).toByteArray(), QByteArray("NO-TERM"));

    QVERIFY(cm->closeDevice(QStringLiteral("SIM_FRAME")));
    QVERIFY(cm->removeDevice(QStringLiteral("SIM_FRAME")));
}

void CommWritebackTest::testDialogToggleConnection()
{
    // A1 回归：用**真实鼠标点击**触发连接开关按钮的 clicked()（复现原 UAF——
    // onToggleConnection 栈内同步重建整表销毁正被点击的按钮）。改异步合并刷新后，
    // 真实点击不得崩溃且真的连上/断开。之前的实现用 invokeMethod(DirectConnection)
    // 绕过真实点击，"PASS 恰恰证明钉不住"——这里改成 QTest::mouseClick。
    QTcpServer plc;
    QByteArray received;
    startSimulatedPlc(plc, received);
    QVERIFY2(plc.isListening(), qPrintable(plc.errorString()));

    auto *cm = CommunicationManager::instance();
    QVERIFY(cm->addDevice(QStringLiteral("SIM_DLG"), QStringLiteral("TCP"),
                          tcpClientConfig(plc.serverPort())));

    CommunicationManagerDialog dlg;
    dlg.show();   // 真实点击需要可见窗口

    auto *table = dlg.findChild<QTableWidget *>(QStringLiteral("deviceTable"));
    QVERIFY2(table, "设备表格应可通过 objectName 定位");

    // 表格异步重建后会换掉第 0 行的按钮，每次点击都重新定位，避免悬垂指针
    auto clickToggle = [&]() {
        auto *btn = qobject_cast<QPushButton *>(table->cellWidget(0, 2));
        QVERIFY2(btn, "第 0 行连接开关按钮应存在");
        QTest::mouseClick(btn, Qt::LeftButton);
    };

    clickToggle();   // 真实点击 → 连上
    QTRY_VERIFY_WITH_TIMEOUT(cm->deviceInfo(QStringLiteral("SIM_DLG")).isConnected, 3000);
    QVERIFY2(received.isEmpty(), "刚连上尚未发送任何数据");

    clickToggle();   // 再点 → 断开
    QTRY_VERIFY_WITH_TIMEOUT(!cm->deviceInfo(QStringLiteral("SIM_DLG")).isConnected, 3000);

    dlg.close();
    QVERIFY(cm->removeDevice(QStringLiteral("SIM_DLG")));
}

void CommWritebackTest::testFrameAssemblerReentrancy()
{
    // A3 回归：feedFrameAssembler 不得跨 emit 持有 m_frameBuffers 的引用——
    // 槽里 addDevice/removeDevice 会让 QHash 重哈希，旧引用悬空 → 写已释放内存。
    // 复现：每收到一帧就在槽里增删一个设备（直接命中"emit 期间容器被改"）。
    QTcpServer plc;
    QTcpSocket *peer = nullptr;
    plc.listen(QHostAddress::LocalHost, 0);
    QVERIFY2(plc.isListening(), qPrintable(plc.errorString()));
    QObject::connect(&plc, &QTcpServer::newConnection, &plc,
                     [&plc, &peer]() { peer = plc.nextPendingConnection(); });

    auto *cm = CommunicationManager::instance();
    QJsonObject cfg = tcpClientConfig(plc.serverPort());
    cfg[QStringLiteral("frameTerminator")] = QStringLiteral("\\r\\n");
    QVERIFY(cm->addDevice(QStringLiteral("SIM_REENT"), QStringLiteral("TCP"), cfg));
    QVERIFY(cm->openDevice(QStringLiteral("SIM_REENT")));
    QTRY_VERIFY_WITH_TIMEOUT(peer != nullptr, 3000);

    QSignalSpy rxSpy(cm, &CommunicationManager::dataReceived);
    // 重入演员：收到任意一帧就增删设备（迫使帧缓冲 QHash 重哈希）
    const QMetaObject::Connection hook = QObject::connect(
        cm, &CommunicationManager::dataReceived, cm, [cm](const QString &) {
            cm->addDevice(QStringLiteral("SIM_REENT_EXTRA"), QStringLiteral("UDP"), QJsonObject());
            cm->removeDevice(QStringLiteral("SIM_REENT_EXTRA"));
        });

    peer->write("X,1\r\nY,2\r\n");   // 一次写入两帧：切帧循环中途触发 emit → 重入
    peer->flush();

    QTRY_VERIFY_WITH_TIMEOUT(rxSpy.count() >= 2, 3000);
    QStringList frames;
    for (int i = 0; i < rxSpy.count(); ++i)
        frames << QString::fromLatin1(rxSpy.at(i).at(1).toByteArray());
    QVERIFY2(frames.contains(QStringLiteral("X,1\r\n")), qPrintable(frames.join(QLatin1Char('|'))));
    QVERIFY2(frames.contains(QStringLiteral("Y,2\r\n")), qPrintable(frames.join(QLatin1Char('|'))));

    QObject::disconnect(hook);
    QVERIFY(cm->closeDevice(QStringLiteral("SIM_REENT")));
    QVERIFY(cm->removeDevice(QStringLiteral("SIM_REENT")));
}

void CommWritebackTest::testDisconnectClearsFrameBuffer()
{
    // A4 回归：断线必须丢弃旧链路的半帧缓冲——否则重连后新链路首帧与旧残帧
    // 拼在一起（"C," + "3\r\n" → 脏帧），解析全错却不报错。
    QTcpServer plc;
    QTcpSocket *peer = nullptr;
    int connCount = 0;
    plc.listen(QHostAddress::LocalHost, 0);
    QVERIFY2(plc.isListening(), qPrintable(plc.errorString()));
    QObject::connect(&plc, &QTcpServer::newConnection, &plc,
                     [&plc, &peer, &connCount]() { ++connCount; peer = plc.nextPendingConnection(); });

    auto *cm = CommunicationManager::instance();
    QJsonObject cfg = tcpClientConfig(plc.serverPort());
    cfg[QStringLiteral("frameTerminator")] = QStringLiteral("\\r\\n");
    QVERIFY(cm->addDevice(QStringLiteral("SIM_CLEAN"), QStringLiteral("TCP"), cfg));
    QVERIFY(cm->openDevice(QStringLiteral("SIM_CLEAN")));
    QTRY_VERIFY_WITH_TIMEOUT(peer != nullptr, 3000);

    QSignalSpy rxSpy(cm, &CommunicationManager::dataReceived);

    // ① 只来半帧（无结束符）：绝不能成帧
    peer->write("C,");
    peer->flush();
    QTest::qWait(100);
    QCOMPARE(rxSpy.count(), 0);

    // ② 断开 → 重连（模拟掉线自愈）
    QVERIFY(cm->closeDevice(QStringLiteral("SIM_CLEAN")));
    QVERIFY(cm->openDevice(QStringLiteral("SIM_CLEAN")));
    QTRY_VERIFY_WITH_TIMEOUT(connCount >= 2, 3000);

    // ③ 新链路首帧必须干净：只有 "3\r\n"，不得出现 "C,3\r\n"
    rxSpy.clear();
    peer->write("3\r\n");
    peer->flush();
    QTRY_VERIFY_WITH_TIMEOUT(rxSpy.count() >= 1, 3000);
    QCOMPARE(rxSpy.at(0).at(1).toByteArray(), QByteArray("3\r\n"));

    QVERIFY(cm->closeDevice(QStringLiteral("SIM_CLEAN")));
    QVERIFY(cm->removeDevice(QStringLiteral("SIM_CLEAN")));
}

void CommWritebackTest::testConfigDialogPreservesUnmanagedKeys()
{
    // 警告级回归：热更新用 CommDeviceConfigDialog 时，不在表单上的键不得被丢弃。
    // 历史缺陷：不播种 initial + onAccept 从零构造 → autoReconnect/reconnectInterval
    // 等键在"配置→确定"后静默消失（存盘再打开，"断线自动重连"勾选就没了）。
    QJsonObject initial = tcpClientConfig(15503);
    initial[QStringLiteral("autoReconnect")] = true;
    initial[QStringLiteral("reconnectInterval")] = 2500;
    initial[QStringLiteral("frameTimeoutMs")] = 300;

    CommDeviceConfigDialog dlg(QStringLiteral("TCP"), initial, nullptr);
    const QJsonObject out = dlg.config();
    QCOMPARE(out.value(QStringLiteral("autoReconnect")).toBool(), true);
    QCOMPARE(out.value(QStringLiteral("reconnectInterval")).toInt(), 2500);
    QCOMPARE(out.value(QStringLiteral("frameTimeoutMs")).toInt(), 300);
    QCOMPARE(out.value(QStringLiteral("port")).toInt(), 15503);   // 表单管理的键原样在

    // ── 专用配置对话框（Modbus / PLC）：S4 的"回写校验"必须能设、也能读回 ──
    // 此前 writeVerify 只有节点参数、没有任何表单入口（"配了看不见"）。这里钉住往返，
    // 防止再出现"配置里写了却设不到 / 热更新后丢键"。
    {
        QJsonObject initialModbus;
        initialModbus[QStringLiteral("writeVerify")] = true;
        initialModbus[QStringLiteral("slaveAddress")] = 7;
        initialModbus[QStringLiteral("pollInterval")] = 250;
        ModbusConfigDialog modbusDlg(QStringLiteral("测试"), nullptr, nullptr);
        modbusDlg.setConfig(initialModbus);
        const QJsonObject produced = modbusDlg.config();
        QVERIFY2(produced.value(QStringLiteral("writeVerify")).toBool(),
                 "Modbus 配置对话框未把 writeVerify 播种到表单或未回写（改造前该键根本不存在）");
        QCOMPARE(produced.value(QStringLiteral("slaveAddress")).toInt(), 7);
        QCOMPARE(produced.value(QStringLiteral("pollInterval")).toInt(), 250);
    }
    {
        QJsonObject initialPlc;
        initialPlc[QStringLiteral("writeVerify")] = true;
        initialPlc[QStringLiteral("slaveAddress")] = 7;
        initialPlc[QStringLiteral("pollInterval")] = 250;
        PlcConfigDialog plcDlg(QStringLiteral("测试"), nullptr, nullptr);
        plcDlg.setConfig(initialPlc);
        const QJsonObject produced = plcDlg.config();
        QVERIFY2(produced.value(QStringLiteral("writeVerify")).toBool(),
                 "PLC 配置对话框未把 writeVerify 播种到表单或未回写（改造前该键根本不存在）");
        QCOMPARE(produced.value(QStringLiteral("slaveAddress")).toInt(), 7);
        QCOMPARE(produced.value(QStringLiteral("pollInterval")).toInt(), 250);
    }
    // 缺键 ⇒ 默认关闭（与节点侧 writeVerify 默认 false 一致）
    {
        ModbusConfigDialog modbusDefault(QStringLiteral("测试"), nullptr, nullptr);
        modbusDefault.setConfig(QJsonObject());
        QVERIFY2(!modbusDefault.config().value(QStringLiteral("writeVerify")).toBool(),
                 "配置里没有 writeVerify 时必须默认关闭");
    }
}

void CommWritebackTest::testSendEventsSurviveSaveLoad()
{
    // C 类回归：发送事件必须随方案序列化往返。
    // 历史缺陷：toJson 写了 sendEvents、fromJson 不读 → 保存/重启后
    // PLC 回写配置（模板/字段表/启停）全部"消失"。
    auto *cm = CommunicationManager::instance();

    auto *ev = new TextDirectSendEvent(QStringLiteral("SE_RT"), QStringLiteral("DEV_X"), cm);
    ev->setTemplate(QStringLiteral("RT-{}.END"));
    ev->setSuffix(QStringLiteral("\r\n"));
    QVERIFY(cm->addSendEvent(ev));

    cm->fromJson(cm->toJson());   // 模拟"保存方案 → 重启加载"

    auto *back = cm->sendEvent(QStringLiteral("SE_RT"));
    QVERIFY2(back != nullptr, "文本发送事件未随方案往返（fromJson 漏读 sendEvents）");
    QVERIFY(back->sendType() == SendEvent::TEXT_DIRECT);
    QCOMPARE(back->toJson().value(QStringLiteral("template")).toString(),
             QStringLiteral("RT-{}.END"));
    QCOMPARE(back->toJson().value(QStringLiteral("suffix")).toString(),
             QStringLiteral("\r\n"));

    // 字节组包类型的子类也必须还原正确（否则重启后变文本事件）
    auto *bin = new BytePackSendEvent(QStringLiteral("SE_RT_BIN"), QStringLiteral("DEV_X"), cm);
    BytePackField f;
    f.dataType = QStringLiteral("int16");
    f.fixedValue = 7;
    bin->addField(f);
    QVERIFY(cm->addSendEvent(bin));

    cm->fromJson(cm->toJson());

    auto *binBack = cm->sendEvent(QStringLiteral("SE_RT_BIN"));
    QVERIFY2(binBack != nullptr, "字节组包事件未随方案往返");
    QVERIFY(binBack->sendType() == SendEvent::BYTE_PACK);
    QCOMPARE(binBack->toJson().value(QStringLiteral("fields")).toArray().size(), 1);

    cm->removeSendEvent(QStringLiteral("SE_RT"));
    cm->removeSendEvent(QStringLiteral("SE_RT_BIN"));
}

void CommWritebackTest::testReceiveEventFiltersByDevice()
{
    // C 类回归：接收事件必须按**绑定设备**过滤。
    // 历史缺陷：GlobalTriggerManager::onDataReceived 把 deviceName 直接忽略（Q_UNUSED），
    // 任何设备的一帧数据都会跑遍全部接收事件——A 设备的数据能触发 B 设备的流程
    // （安全事故级）。本用例：先给"别的设备"绑一个事件，发本设备数据必须不触发；
    // 再把同一事件绑到本设备，发同样格式的数据必须触发（证明是过滤生效而非链路坏了）。
    QTcpServer plc;
    QTcpSocket *peer = nullptr;
    plc.listen(QHostAddress::LocalHost, 0);
    QVERIFY2(plc.isListening(), qPrintable(plc.errorString()));
    QObject::connect(&plc, &QTcpServer::newConnection, &plc,
                     [&plc, &peer]() { peer = plc.nextPendingConnection(); });

    auto *cm = CommunicationManager::instance();
    QVERIFY(cm->addDevice(QStringLiteral("SIM_FILTER"), QStringLiteral("TCP"),
                          tcpClientConfig(plc.serverPort())));
    QVERIFY(cm->openDevice(QStringLiteral("SIM_FILTER")));
    QTRY_VERIFY_WITH_TIMEOUT(peer != nullptr, 3000);

    auto *gtm = GlobalTriggerManager::instance();
    QSignalSpy firedSpy(gtm, &GlobalTriggerManager::triggerFired);
    QSignalSpy rxSpy(cm, &CommunicationManager::dataReceived);

    // ① 事件绑定到 OTHER_DEV：本设备（SIM_FILTER）的数据不得触发它
    auto *other = new TextProtocolReceiveEvent(QStringLiteral("EV_OTHER"),
                                               QStringLiteral("OTHER_DEV"));
    other->setDelimiter(QStringLiteral(","));
    QVERIFY(cm->addReceiveEvent(other));
    QVERIFY2(gtm->setEventTrigger(QStringLiteral("EV_OTHER"), QStringLiteral("FlowFilter")),
             "配置事件触发失败");

    peer->write("1,2\r\n");
    peer->flush();
    QTRY_VERIFY_WITH_TIMEOUT(rxSpy.count() >= 1, 3000);   // 数据确实到达平台
    QTest::qWait(150);                                     // 给（不应发生的）触发留出时间窗口
    QCOMPARE(firedSpy.count(), 0);                         // 但绝不能触发

    // ② 对照：同一批数据，事件绑到本设备时必须触发
    QVERIFY(gtm->removeEventTrigger(QStringLiteral("EV_OTHER")));
    cm->removeReceiveEvent(QStringLiteral("EV_OTHER"));

    auto *self = new TextProtocolReceiveEvent(QStringLiteral("EV_SELF"),
                                              QStringLiteral("SIM_FILTER"));
    self->setDelimiter(QStringLiteral(","));
    QVERIFY(cm->addReceiveEvent(self));
    QVERIFY(gtm->setEventTrigger(QStringLiteral("EV_SELF"), QStringLiteral("FlowFilter")));

    peer->write("3,4\r\n");
    peer->flush();
    QTRY_VERIFY_WITH_TIMEOUT(firedSpy.count() >= 1, 3000);
    QCOMPARE(firedSpy.at(0).at(0).toString(), QStringLiteral("FlowFilter"));

    gtm->removeEventTrigger(QStringLiteral("EV_SELF"));
    cm->removeReceiveEvent(QStringLiteral("EV_SELF"));
    QVERIFY(cm->closeDevice(QStringLiteral("SIM_FILTER")));
    QVERIFY(cm->removeDevice(QStringLiteral("SIM_FILTER")));
}

// 必须用 QTEST_MAIN：流程用例要创建 FlowScene（QGraphicsScene），仅 QCoreApplication 会崩；
// vfp_core 公开链接 Widgets，故这里会得到 QApplication，平台插件由 VFP_TESTS 的
// QT_PLUGIN_PATH 注入（dist/VisionFlowPlatform）。

// ==================== 数据正确性回归（边沿阈值 / 无效帧 / BADC / 抖动 / 静默丢包） ====================

namespace {
// 16 位大端帧
QByteArray i16Frame(qint16 v)
{
    const quint16 raw = static_cast<quint16>(v);
    QByteArray b;
    b.append(static_cast<char>((raw >> 8) & 0xFF));
    b.append(static_cast<char>(raw & 0xFF));
    return b;
}
}   // namespace

// 边沿检测必须以 compareValue 为阈值，而不是硬编码的 0↔非0
void CommWritebackTest::testByteMatchEdgeUsesCompareValue()
{
    ByteMatchReceiveEvent ev(QStringLiteral("EDGE"), QStringLiteral("DEV"));
    QSignalSpy spy(&ev, &ReceiveEvent::eventGenerated);

    ByteMatchRule rule;
    rule.byteOffset = 0;
    rule.byteLength = 2;
    rule.dataType = QStringLiteral("int16");
    rule.byteOrder = QStringLiteral("ABCD");
    rule.compareValue = 100;
    rule.useRisingEdge = true;
    rule.useEquals = false;
    ev.addRule(rule);

    QList<QVariant> fields;
    QVERIFY(!ev.parse(i16Frame(50), fields));   // 首帧只建立基线
    QCOMPARE(spy.count(), 0);
    // 50 → 150 越过阈值 100：必须触发。旧实现按 0 判（lastValue=50 非 0 → 永不触发）
    QVERIFY2(ev.parse(i16Frame(150), fields),
             "边沿未使用 compareValue：50→150 越过阈值 100 却未触发");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(fields.value(0).toDouble(), 150.0);
    // 阈值上方继续增大：不得重复触发
    QVERIFY(!ev.parse(i16Frame(160), fields));
    QCOMPARE(spy.count(), 1);
    // 100 == compareValue 不算"穿到阈值线下方"；99 已跌破但规则是上升沿
    QVERIFY(!ev.parse(i16Frame(100), fields));
    QVERIFY(!ev.parse(i16Frame(99), fields));
    QCOMPARE(spy.count(), 1);

    // 下降沿同样按阈值
    ByteMatchRule down = rule;
    down.useRisingEdge = false;
    down.useFallingEdge = true;
    ev.clearRules();
    ev.addRule(down);
    QVERIFY(!ev.parse(i16Frame(50), fields));   // 重建基线
    QVERIFY(!ev.parse(i16Frame(150), fields));  // 上穿阈值：下降沿不触发
    QCOMPARE(spy.count(), 1);
    QVERIFY(ev.parse(i16Frame(50), fields));    // 150 → 50 跌破阈值 100：触发
    QCOMPARE(spy.count(), 2);
}

// 短帧/越界/负偏移取不到值时，不得把 0 当真实值写进基线（否则下一真帧被误判成上升沿）
void CommWritebackTest::testByteMatchInvalidFrameKeepsBaseline()
{
    ByteMatchReceiveEvent ev(QStringLiteral("BADF"), QStringLiteral("DEV"));
    QSignalSpy spy(&ev, &ReceiveEvent::eventGenerated);

    ByteMatchRule rule;
    rule.byteOffset = 0;
    rule.byteLength = 2;
    rule.dataType = QStringLiteral("int16");
    rule.byteOrder = QStringLiteral("ABCD");
    rule.compareValue = 0;
    rule.useRisingEdge = true;
    rule.useEquals = false;
    ev.addRule(rule);

    QList<QVariant> fields;
    // 1 字节短帧（整帧非空，但按规则长度不足）：旧实现 return 0 并写进基线 →
    // 紧随其后的真实 2 会被当成 0→2 上升沿凭空触发一次
    QVERIFY(!ev.parse(QByteArray(1, '\0'), fields));
    QVERIFY2(!ev.parse(i16Frame(2), fields),
             "短帧污染了基线：真值 2 被当成 0→2 上升沿多触发了一次");
    QCOMPARE(spy.count(), 0);

    QVERIFY(!ev.parse(i16Frame(0), fields));    // 建立真实基线 0
    QVERIFY(ev.parse(i16Frame(1), fields));     // 0 → 1：真上升沿
    QCOMPARE(spy.count(), 1);

    // 再次短帧：不得把基线清成 0
    QVERIFY(!ev.parse(QByteArray(1, '\0'), fields));
    QVERIFY(!ev.parse(i16Frame(1), fields));    // 基线仍是 1 → 1→1 不触发
    QCOMPARE(spy.count(), 1);
    QVERIFY(!ev.parse(i16Frame(0), fields));    // 下降，规则是上升沿
    QVERIFY(ev.parse(i16Frame(1), fields));     // 0 → 1 再次触发
    QCOMPARE(spy.count(), 2);

    // 负偏移：整体判无效，不崩溃、不触发、不污染基线
    ByteMatchReceiveEvent ev2(QStringLiteral("NEGOFF"), QStringLiteral("DEV"));
    QSignalSpy spy2(&ev2, &ReceiveEvent::eventGenerated);
    ByteMatchRule bad;
    bad.byteOffset = -4;
    bad.byteLength = 2;
    bad.dataType = QStringLiteral("int16");
    bad.compareValue = 0;
    bad.useEquals = true;
    ev2.addRule(bad);
    QVERIFY(!ev2.parse(i16Frame(7), fields));
    QVERIFY(!ev2.parse(i16Frame(7), fields));
    QCOMPARE(spy2.count(), 0);
}

// 32 位 BADC（字内字节互换）：旧实现没有该分支，会按 ABCD 算成错值
void CommWritebackTest::testByteMatchBadcByteOrder()
{
    // 同一 4 字节：ABCD = 0x00000100 = 256；BADC = 0x00000001 = 1
    const QByteArray raw = QByteArray::fromHex("00000100");
    QList<QVariant> fields;

    ByteMatchReceiveEvent evAbcd(QStringLiteral("ABCD"), QStringLiteral("DEV"));
    ByteMatchRule r1;
    r1.byteOffset = 0;
    r1.byteLength = 4;
    r1.dataType = QStringLiteral("int32");
    r1.byteOrder = QStringLiteral("ABCD");
    r1.compareValue = 256;
    r1.useEquals = true;
    evAbcd.addRule(r1);
    QVERIFY(!evAbcd.parse(raw, fields));    // 首帧只建立基线
    QVERIFY(evAbcd.parse(raw, fields));

    ByteMatchReceiveEvent evBadc(QStringLiteral("BADC"), QStringLiteral("DEV"));
    ByteMatchRule r2 = r1;
    r2.byteOrder = QStringLiteral("BADC");
    r2.compareValue = 1;
    evBadc.addRule(r2);
    QVERIFY(!evBadc.parse(raw, fields));
    QVERIFY2(evBadc.parse(raw, fields),
             "BADC 字内字节互换未生效（旧实现无该分支，按 ABCD 算成 256 ≠ 1）");
}

// 重复关闭不得反复发假"断开"（openConnection 开头清理/自动重连/removeDevice 都会 close）
void CommWritebackTest::testCloseConnectionNoSpuriousSignal()
{
    auto *cm = CommunicationManager::instance();

    // TCP：从未连接过就 close → 不得发 connectionClosed
    QVERIFY(cm->addDevice(QStringLiteral("JIT_TCP"), QStringLiteral("TCP"), tcpClientConfig(1)));
    auto *tcp = cm->deviceNode(QStringLiteral("JIT_TCP"));
    QVERIFY(tcp != nullptr);
    {
        QSignalSpy closedSpy(tcp, &CommunicationNodeBase::connectionClosed);
        QVERIFY(cm->closeDevice(QStringLiteral("JIT_TCP")));
        QCOMPARE(closedSpy.count(), 0);
    }
    QVERIFY(cm->removeDevice(QStringLiteral("JIT_TCP")));

    // UDP：绑定成功（已连接）后关闭恰好 1 次；已断开再关不得再报
    QJsonObject udpCfg;
    udpCfg[QStringLiteral("localPort")] = 0;   // 系统分配，避免与其它用例抢端口
    QVERIFY(cm->addDevice(QStringLiteral("JIT_UDP"), QStringLiteral("UDP"), udpCfg));
    auto *udp = cm->deviceNode(QStringLiteral("JIT_UDP"));
    QVERIFY(udp != nullptr);
    QVERIFY2(cm->openDevice(QStringLiteral("JIT_UDP")), "UDP 绑定失败");
    QVERIFY(udp->isConnected());
    {
        QSignalSpy closedSpy(udp, &CommunicationNodeBase::connectionClosed);
        QVERIFY(cm->closeDevice(QStringLiteral("JIT_UDP")));
        QCOMPARE(closedSpy.count(), 1);
        QVERIFY(cm->closeDevice(QStringLiteral("JIT_UDP")));   // 已断开再关
        QCOMPARE(closedSpy.count(), 1);
    }
    QVERIFY(cm->removeDevice(QStringLiteral("JIT_UDP")));
}

// 断开状态下投递发送必须可见（旧实现静默 return，调用方以为成功）
void CommWritebackTest::testSendWhileDisconnectedIsReported()
{
    auto *cm = CommunicationManager::instance();
    const QStringList cases = { QStringLiteral("TCP"), QStringLiteral("UDP"), QStringLiteral("Serial") };
    for (const QString &type : cases) {
        const QString name = QStringLiteral("SILENT_%1").arg(type);
        QJsonObject cfg;
        if (type == QStringLiteral("Serial"))
            cfg[QStringLiteral("portName")] = QStringLiteral("COM_DOES_NOT_EXIST");
        QVERIFY2(cm->addDevice(name, type, cfg),
                 qPrintable(QStringLiteral("addDevice 失败: %1").arg(type)));
        auto *node = cm->deviceNode(name);
        QVERIFY(node != nullptr);
        QVERIFY(!node->isConnected());

        QSignalSpy errSpy(node, &CommunicationNodeBase::communicationError);
        // 模拟"调用瞬间 isConnected() 为真、投递到这里链路已断"：直接触发节点的发送槽
        QVERIFY(QMetaObject::invokeMethod(node, "onSendRequested",
                                          Q_ARG(QByteArray, QByteArray("X"))));
        QCOMPARE(errSpy.count(), 1);
        QVERIFY(!errSpy.at(0).at(0).toString().isEmpty());

        QVERIFY(cm->removeDevice(name));
    }
}

// Modbus 客户端以"曾真正连上"为 connectionClosed 唯一判据（与 TCP/串口/PLC 对齐）。
// 连不上（端口 1 无服务）时，异步发起/异步失败/重复 closeDevice 全过程都不得产生任何
// connectionClosed；这里断言"close 后恒 0"（而非"不增量"）：若异步失败落在首个等待窗口内，
// 旧的 afterOpen=1 写法会以"不增量"意外通过，恰好掩盖同一个洞（S1）。
void CommWritebackTest::testModbusCloseConnectionNoSpuriousSignal()
{
    auto *cm = CommunicationManager::instance();
    DeviceCleanup cleanup{ { QStringLiteral("MBJ_TCP") } };

    QJsonObject cliCfg;
    cliCfg[QStringLiteral("role")] = QStringLiteral("客户端");
    cliCfg[QStringLiteral("connectionType")] = QStringLiteral("TCP");
    cliCfg[QStringLiteral("host")] = QStringLiteral("127.0.0.1");
    cliCfg[QStringLiteral("port")] = 1;          // 无服务，连接必失败
    cliCfg[QStringLiteral("slaveAddress")] = 1;
    QVERIFY2(cm->addDevice(QStringLiteral("MBJ_TCP"), QStringLiteral("Modbus"), cliCfg),
             "addDevice(Modbus 客户端) 失败");
    auto *node = qobject_cast<ModbusNode *>(cm->deviceNode(QStringLiteral("MBJ_TCP")));
    QVERIFY2(node != nullptr, "Modbus 节点类型不符");

    QSignalSpy closedSpy(node, &CommunicationNodeBase::connectionClosed);
    QSignalSpy openedSpy(node, &CommunicationNodeBase::connectionOpened);
    cm->openDevice(QStringLiteral("MBJ_TCP"));     // 异步连接，端口 1 必失败
    QTest::qWait(300);                             // 让首轮连接尝试/重连落地
    cm->closeDevice(QStringLiteral("MBJ_TCP"));
    cm->closeDevice(QStringLiteral("MBJ_TCP"));    // 已断再关
    QTest::qWait(1500);                            // 覆盖重连窗口：关闭后不得自行"复活"
    QCOMPARE(closedSpy.count(), 0);                // 从未真正连上 → close 后恒 0
    QCOMPARE(openedSpy.count(), 0);                // 且关闭后不得被重连"复活"发 connectionOpened
    QVERIFY2(!node->isConnected(), "关闭后节点不得仍显示已连接");
}

// S-2：PLC 关闭守卫必须与 Modbus 同款——以 m_everReallyConnected（"曾真正连上"）为唯一判据，
// 而非关闭时的瞬时 m_connected；且用户主动关闭后不得被自动重连"复活"。连不上（端口 1 无服务）时
// open+重复 close 全过程都不得产生任何 connectionClosed / connectionOpened。
void CommWritebackTest::testPlcCloseConnectionNoSpuriousSignal()
{
    auto *cm = CommunicationManager::instance();
    DeviceCleanup cleanup{ { QStringLiteral("PLCJ_TCP") } };

    QJsonObject cliCfg;
    cliCfg[QStringLiteral("host")] = QStringLiteral("127.0.0.1");
    cliCfg[QStringLiteral("port")] = 1;          // 无服务，连接必失败
    cliCfg[QStringLiteral("slaveAddress")] = 1;
    QVERIFY2(cm->addDevice(QStringLiteral("PLCJ_TCP"), QStringLiteral("PLC"), cliCfg),
             "addDevice(PLC 客户端) 失败");
    auto *node = qobject_cast<PlcCommNode *>(cm->deviceNode(QStringLiteral("PLCJ_TCP")));
    QVERIFY2(node != nullptr, "PLC 节点类型不符");

    QSignalSpy closedSpy(node, &CommunicationNodeBase::connectionClosed);
    QSignalSpy openedSpy(node, &CommunicationNodeBase::connectionOpened);
    cm->openDevice(QStringLiteral("PLCJ_TCP"));     // 异步连接，端口 1 必失败
    QTest::qWait(300);                              // 让首轮连接尝试落地
    cm->closeDevice(QStringLiteral("PLCJ_TCP"));
    cm->closeDevice(QStringLiteral("PLCJ_TCP"));    // 已断再关
    QTest::qWait(1500);                             // 覆盖重连窗口（默认 3000ms 未到）：关闭后不得自行"复活"
    QCOMPARE(closedSpy.count(), 0);                 // 从未真正连上 → close 后恒 0（S-2 判据修正点）
    QCOMPARE(openedSpy.count(), 0);                 // 且关闭后不得被重连"复活"发 connectionOpened
    QVERIFY2(!node->isConnected(), "关闭后节点不得仍显示已连接");
}

// L1 回归：一次连接生命周期内 connectionClosed 至多上报一次。真连上后网络瞬断（关服务器）→
// Unconnected 分支已报一次；此时用户再主动 close，判据 m_everReallyConnected 必须已在瞬断时复位，
// 否则 was 仍为真 → 二次上报（修复前 count 会变 2）。客户端关掉 autoReconnect，避免重连的
// openConnection 复位 ever 标志而掩盖该 bug。
void CommWritebackTest::testModbusCloseNoDoubleReportOnTransientDrop()
{
    const quint16 port = pickFreePort();   // 临时端口（替代固定字面量，避免 TIME_WAIT 撞端口）
    QVERIFY2(port != 0, "无法取得空闲端口（pickFreePort）");
    auto *cm = CommunicationManager::instance();
    DeviceCleanup cleanup{ { QStringLiteral("DR_SRV"), QStringLiteral("DR_CLI") } };

    QJsonObject srvCfg;
    srvCfg[QStringLiteral("role")] = QStringLiteral("服务器");
    srvCfg[QStringLiteral("connectionType")] = QStringLiteral("TCP");
    srvCfg[QStringLiteral("port")] = port;
    srvCfg[QStringLiteral("slaveAddress")] = 1;
    QVERIFY2(cm->addDevice(QStringLiteral("DR_SRV"), QStringLiteral("Modbus"), srvCfg),
             "addDevice(服务器) 失败");
    auto *srv = qobject_cast<ModbusNode *>(cm->deviceNode(QStringLiteral("DR_SRV")));
    QVERIFY2(srv != nullptr, "服务器节点类型不符");
    QVERIFY2(cm->openDevice(QStringLiteral("DR_SRV")), "Modbus 服务器启动失败");
    QTRY_VERIFY_WITH_TIMEOUT(srv->isServerListening(), 3000);

    QJsonObject cliCfg;
    cliCfg[QStringLiteral("role")] = QStringLiteral("客户端");
    cliCfg[QStringLiteral("connectionType")] = QStringLiteral("TCP");
    cliCfg[QStringLiteral("host")] = QStringLiteral("127.0.0.1");
    cliCfg[QStringLiteral("port")] = port;
    cliCfg[QStringLiteral("slaveAddress")] = 1;
    cliCfg[QStringLiteral("autoReconnect")] = false;   // 关键：关掉重连，ever 标志不被 openConnection 复位
    QVERIFY2(cm->addDevice(QStringLiteral("DR_CLI"), QStringLiteral("Modbus"), cliCfg),
             "addDevice(客户端) 失败");
    auto *cli = qobject_cast<ModbusNode *>(cm->deviceNode(QStringLiteral("DR_CLI")));
    QVERIFY2(cli != nullptr, "客户端节点类型不符");

    QSignalSpy closedSpy(cli, &CommunicationNodeBase::connectionClosed);
    QVERIFY2(cm->openDevice(QStringLiteral("DR_CLI")), "Modbus 客户端连接失败");
    QTRY_VERIFY_WITH_TIMEOUT(cli->isConnected(), 3000);   // 真正连上 → m_everReallyConnected=true

    // 关服务器 → 客户端瞬断 → Unconnected 分支上报恰好一次（并在 L1 修复里复位 ever 标志）
    QVERIFY(cm->closeDevice(QStringLiteral("DR_SRV")));
    QTRY_COMPARE_WITH_TIMEOUT(closedSpy.count(), 1, 3000);

    // 主动关客户端：ever 标志已复位 → was=false → 不得二次上报
    QVERIFY(cm->closeDevice(QStringLiteral("DR_CLI")));
    QTest::qWait(300);
    QCOMPARE(closedSpy.count(), 1);   // 整个生命周期恰好一次；修复前这里会是 2
}

// L1（PLC 同款）判别性回归：PlcCommNode 的 m_everReallyConnected 必须在瞬断时复位，否则主动 close
// 会二次上报 connectionClosed。PLC 客户端（QModbusTcpClient）连到 Modbus 服务器（QModbusTcpServer），
// 真连上后关服务器制造瞬断→PLC Unconnected 分支报一次；再主动 close PLC→总数仍须为 1（修复前为 2）。
// autoReconnect 关掉，避免重连的 openConnection 复位 ever 标志而掩盖该 bug。
void CommWritebackTest::testPlcCloseNoDoubleReportOnTransientDrop()
{
    const quint16 port = pickFreePort();   // 临时端口（替代固定字面量，避免 TIME_WAIT 撞端口）
    QVERIFY2(port != 0, "无法取得空闲端口（pickFreePort）");
    auto *cm = CommunicationManager::instance();
    DeviceCleanup cleanup{ { QStringLiteral("PLCD_SRV"), QStringLiteral("PLCD_CLI") } };

    QJsonObject srvCfg;
    srvCfg[QStringLiteral("role")] = QStringLiteral("服务器");
    srvCfg[QStringLiteral("connectionType")] = QStringLiteral("TCP");
    srvCfg[QStringLiteral("port")] = port;
    srvCfg[QStringLiteral("slaveAddress")] = 1;
    QVERIFY2(cm->addDevice(QStringLiteral("PLCD_SRV"), QStringLiteral("Modbus"), srvCfg),
             "addDevice(Modbus 服务器) 失败");
    auto *srv = qobject_cast<ModbusNode *>(cm->deviceNode(QStringLiteral("PLCD_SRV")));
    QVERIFY2(srv != nullptr, "服务器节点类型不符");
    QVERIFY2(cm->openDevice(QStringLiteral("PLCD_SRV")), "Modbus 服务器启动失败");
    QTRY_VERIFY_WITH_TIMEOUT(srv->isServerListening(), 3000);

    QJsonObject cliCfg;
    cliCfg[QStringLiteral("host")] = QStringLiteral("127.0.0.1");
    cliCfg[QStringLiteral("port")] = port;
    cliCfg[QStringLiteral("slaveAddress")] = 1;
    cliCfg[QStringLiteral("autoReconnect")] = false;   // 关键：关掉重连，ever 标志不被 openConnection 复位
    QVERIFY2(cm->addDevice(QStringLiteral("PLCD_CLI"), QStringLiteral("PLC"), cliCfg),
             "addDevice(PLC 客户端) 失败");
    auto *cli = qobject_cast<PlcCommNode *>(cm->deviceNode(QStringLiteral("PLCD_CLI")));
    QVERIFY2(cli != nullptr, "PLC 节点类型不符");

    QSignalSpy closedSpy(cli, &CommunicationNodeBase::connectionClosed);
    QVERIFY2(cm->openDevice(QStringLiteral("PLCD_CLI")), "PLC 客户端连接失败");
    QTRY_VERIFY_WITH_TIMEOUT(cli->isConnected(), 3000);   // 真正连上 → m_everReallyConnected=true

    // 关服务器 → PLC 瞬断 → Unconnected 分支上报恰好一次（并在 L1 修复里复位 ever 标志）
    QVERIFY(cm->closeDevice(QStringLiteral("PLCD_SRV")));
    QTRY_COMPARE_WITH_TIMEOUT(closedSpy.count(), 1, 3000);

    // 主动关 PLC：ever 标志已复位 → wasConnected=false → 不得二次上报
    QVERIFY(cm->closeDevice(QStringLiteral("PLCD_CLI")));
    QTest::qWait(300);
    QCOMPARE(closedSpy.count(), 1);   // 整个生命周期恰好一次；修复前这里会是 2
}

// S-1 确定性回归（真连、服务器保持在线，不靠杀服务制造时序赌博），分两段钉住"关闭不复活的守卫 + 复位"：
//  (1) 用户主动 close 后不得被自动重连"复活"：connectionOpened 不新增、isConnected 为假；
//  (2) 重开后断线自愈仍须有效（关服务器 → 立即重启 → 客户端应自行重连回来）。这一段同时钉住
//      openConnection 里 m_userClosed/m_everReallyConnected 两行复位：若这两行缺失，m_userClosed 会
//      从首次 open 起恒为 true，Unconnected 分支永不自愈（8f36746 的 PLC 版正是如此，本用例会红）。
void CommWritebackTest::testModbusCoilWriteback()
{
    // P0-4 切片①：线圈（Coils）端到端回环 —— 客户端写 → **服务器侧观测到同地址同状态**（"IO 真动作了"的判据），
    // 再由客户端读回同值。本用例不依赖寄存器表格（线圈是独立数据区），这也顺带钉住"线圈区确实被映射"。
    const quint16 port = pickFreePort();
    QVERIFY2(port != 0, "无法取得空闲端口（pickFreePort）");
    auto *cm = CommunicationManager::instance();
    DeviceCleanup cleanup{ { QStringLiteral("COIL_SRV"), QStringLiteral("COIL_CLI") } };

    QJsonObject srvCfg;
    srvCfg[QStringLiteral("role")] = QStringLiteral("服务器");
    srvCfg[QStringLiteral("connectionType")] = QStringLiteral("TCP");
    srvCfg[QStringLiteral("port")] = port;
    srvCfg[QStringLiteral("slaveAddress")] = 1;
    QVERIFY2(cm->addDevice(QStringLiteral("COIL_SRV"), QStringLiteral("Modbus"), srvCfg),
             "addDevice(服务器) 失败");
    auto *srv = qobject_cast<ModbusNode *>(cm->deviceNode(QStringLiteral("COIL_SRV")));
    QVERIFY2(srv != nullptr, "服务器节点类型不符");
    QVERIFY2(cm->openDevice(QStringLiteral("COIL_SRV")), "Modbus 服务器启动失败");
    QTRY_VERIFY_WITH_TIMEOUT(srv->isServerListening(), 3000);

    QJsonObject cliCfg;
    cliCfg[QStringLiteral("role")] = QStringLiteral("客户端");
    cliCfg[QStringLiteral("connectionType")] = QStringLiteral("TCP");
    cliCfg[QStringLiteral("host")] = QStringLiteral("127.0.0.1");
    cliCfg[QStringLiteral("port")] = port;
    cliCfg[QStringLiteral("slaveAddress")] = 1;
    cliCfg[QStringLiteral("pollInterval")] = 100;
    QVERIFY2(cm->addDevice(QStringLiteral("COIL_CLI"), QStringLiteral("Modbus"), cliCfg),
             "addDevice(客户端) 失败");
    auto *cli = qobject_cast<ModbusNode *>(cm->deviceNode(QStringLiteral("COIL_CLI")));
    QVERIFY2(cli != nullptr, "客户端节点类型不符");
    QVERIFY2(cm->openDevice(QStringLiteral("COIL_CLI")), "Modbus 客户端连接失败");
    QTRY_VERIFY_WITH_TIMEOUT(cli->isConnected(), 3000);

    QSignalSpy coilSpy(srv, &ModbusNode::coilWrittenByClient);

    // ① 置位：服务器侧必须观测到 (地址 3, true)
    QVERIFY2(cli->writeCoil(3, true), "写线圈请求未发出");
    QTRY_VERIFY_WITH_TIMEOUT(coilSpy.count() >= 1, 3000);
    QCOMPARE(coilSpy.at(0).at(0).toInt(), 3);
    QCOMPARE(coilSpy.at(0).at(1).toBool(), true);

    // ② 复位：同一个地址必须能改回 false（判别性：只写一次成功不算通）
    const int before = coilSpy.count();
    QVERIFY2(cli->writeCoil(3, false), "写线圈请求未发出");
    QTRY_VERIFY_WITH_TIMEOUT(coilSpy.count() > before, 3000);
    QCOMPARE(coilSpy.last().at(0).toInt(), 3);
    QCOMPARE(coilSpy.last().at(1).toBool(), false);

    // ③ 客户端读回：状态须与刚写入的一致（异步回执经 coilStateRead）
    QSignalSpy readSpy(cli, &ModbusNode::coilStateRead);
    QVERIFY2(cli->readCoil(3), "读线圈请求未发出");
    QTRY_VERIFY_WITH_TIMEOUT(readSpy.count() >= 1, 3000);
    QCOMPARE(readSpy.last().at(0).toInt(), 3);
    QCOMPARE(readSpy.last().at(1).toBool(), false);

    // ④ 越界地址必须被拒（不发请求、不崩）
    QVERIFY2(!cli->writeCoil(-1, true), "越界线圈地址必须被拒绝");
    QVERIFY2(!cli->readCoil(70000), "越界线圈地址必须被拒绝");
}

void CommWritebackTest::testModbusUserCloseDoesNotResurrect()
{
    const quint16 port = pickFreePort();   // 临时端口（替代固定字面量，避免 TIME_WAIT 撞端口）
    QVERIFY2(port != 0, "无法取得空闲端口（pickFreePort）");
    auto *cm = CommunicationManager::instance();
    DeviceCleanup cleanup{ { QStringLiteral("RS_SRV"), QStringLiteral("RS_CLI") } };

    QJsonObject srvCfg;
    srvCfg[QStringLiteral("role")] = QStringLiteral("服务器");
    srvCfg[QStringLiteral("connectionType")] = QStringLiteral("TCP");
    srvCfg[QStringLiteral("port")] = port;
    srvCfg[QStringLiteral("slaveAddress")] = 1;
    QVERIFY2(cm->addDevice(QStringLiteral("RS_SRV"), QStringLiteral("Modbus"), srvCfg),
             "addDevice(服务器) 失败");
    auto *srv = qobject_cast<ModbusNode *>(cm->deviceNode(QStringLiteral("RS_SRV")));
    QVERIFY2(srv != nullptr, "服务器节点类型不符");
    QVERIFY2(cm->openDevice(QStringLiteral("RS_SRV")), "Modbus 服务器启动失败");
    QTRY_VERIFY_WITH_TIMEOUT(srv->isServerListening(), 3000);

    QJsonObject cliCfg;
    cliCfg[QStringLiteral("role")] = QStringLiteral("客户端");
    cliCfg[QStringLiteral("connectionType")] = QStringLiteral("TCP");
    cliCfg[QStringLiteral("host")] = QStringLiteral("127.0.0.1");
    cliCfg[QStringLiteral("port")] = port;
    cliCfg[QStringLiteral("slaveAddress")] = 1;
    cliCfg[QStringLiteral("autoReconnect")] = true;
    cliCfg[QStringLiteral("reconnectInterval")] = 500;   // 最短间隔（setParam 下限 500）
    QVERIFY2(cm->addDevice(QStringLiteral("RS_CLI"), QStringLiteral("Modbus"), cliCfg),
             "addDevice(客户端) 失败");
    auto *cli = qobject_cast<ModbusNode *>(cm->deviceNode(QStringLiteral("RS_CLI")));
    QVERIFY2(cli != nullptr, "客户端节点类型不符");
    // 断言"最短重连间隔"确实生效：下方等待窗口按 4× 间隔取（2000ms ≥ 4×500ms）。若配置未生效
    // （退回默认 3000ms），本断言直接失败，避免"等待窗口 < 实际间隔"造成假通过。
    QCOMPARE(cli->toJson().value(QStringLiteral("reconnectInterval")).toInt(), 500);

    QSignalSpy openedSpy(cli, &CommunicationNodeBase::connectionOpened);
    QSignalSpy closedSpy(cli, &CommunicationNodeBase::connectionClosed);
    QVERIFY2(cm->openDevice(QStringLiteral("RS_CLI")), "Modbus 客户端连接失败");
    QTRY_VERIFY_WITH_TIMEOUT(cli->isConnected(), 3000);      // 真正连上
    QCOMPARE(openedSpy.count(), 1);

    // (1) 用户主动关闭 → 此后不得被自动重连"复活"。等待 2000ms = 4× 已生效的最短间隔（上一段已断言
    //     reconnectInterval==500）；若守卫缺失，500ms 后即复活 → 必被本窗口捕获。
    QVERIFY2(cm->closeDevice(QStringLiteral("RS_CLI")), "closeDevice 失败");
    QCOMPARE(closedSpy.count(), 1);                          // 曾真正连上 → 恰好一次断开
    QTest::qWait(2000);
    QCOMPARE(openedSpy.count(), 1);                          // 复活会产生第 2 次 connectionOpened
    QVERIFY2(!cli->isConnected(), "用户关闭后节点被自动重连复活");

    // (2) 重开后断线自愈仍须有效：关服务器制造瞬断 → 立即重启服务器 → 客户端应自行重连回来
    QVERIFY2(cm->openDevice(QStringLiteral("RS_CLI")), "重开失败");
    QTRY_VERIFY_WITH_TIMEOUT(cli->isConnected(), 3000);
    QVERIFY2(cm->closeDevice(QStringLiteral("RS_SRV")), "关服务器失败");
    QTRY_VERIFY_WITH_TIMEOUT(!cli->isConnected(), 3000);     // 先确认真瞬断被感知，否则本段空转
    QVERIFY2(cm->openDevice(QStringLiteral("RS_SRV")), "重启服务器失败");
    QTRY_VERIFY_WITH_TIMEOUT(srv->isServerListening(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(cli->isConnected(), 8000);      // 自愈失效（复位缺失）在此变红
    QVERIFY(cm->closeDevice(QStringLiteral("RS_CLI")));
}

// S-1（PLC 同款）：PLC 客户端对 Modbus 服务器真连 → 用户关闭不复活 → 重开后瞬断自愈仍有效。
// 判别性：8f36746 的 PlcCommNode::openConnection 缺 m_userClosed/ever 两行复位，本用例第 (2) 段会红。
void CommWritebackTest::testPlcUserCloseDoesNotResurrect()
{
    const quint16 port = pickFreePort();   // 临时端口（替代固定字面量，避免 TIME_WAIT 撞端口）
    QVERIFY2(port != 0, "无法取得空闲端口（pickFreePort）");
    auto *cm = CommunicationManager::instance();
    DeviceCleanup cleanup{ { QStringLiteral("PRS_SRV"), QStringLiteral("PRS_CLI") } };

    QJsonObject srvCfg;
    srvCfg[QStringLiteral("role")] = QStringLiteral("服务器");
    srvCfg[QStringLiteral("connectionType")] = QStringLiteral("TCP");
    srvCfg[QStringLiteral("port")] = port;
    srvCfg[QStringLiteral("slaveAddress")] = 1;
    QVERIFY2(cm->addDevice(QStringLiteral("PRS_SRV"), QStringLiteral("Modbus"), srvCfg),
             "addDevice(Modbus 服务器) 失败");
    auto *srv = qobject_cast<ModbusNode *>(cm->deviceNode(QStringLiteral("PRS_SRV")));
    QVERIFY2(srv != nullptr, "服务器节点类型不符");
    QVERIFY2(cm->openDevice(QStringLiteral("PRS_SRV")), "Modbus 服务器启动失败");
    QTRY_VERIFY_WITH_TIMEOUT(srv->isServerListening(), 3000);

    QJsonObject cliCfg;
    cliCfg[QStringLiteral("host")] = QStringLiteral("127.0.0.1");
    cliCfg[QStringLiteral("port")] = port;
    cliCfg[QStringLiteral("slaveAddress")] = 1;
    cliCfg[QStringLiteral("autoReconnect")] = true;
    cliCfg[QStringLiteral("reconnectInterval")] = 500;
    QVERIFY2(cm->addDevice(QStringLiteral("PRS_CLI"), QStringLiteral("PLC"), cliCfg),
             "addDevice(PLC 客户端) 失败");
    auto *cli = qobject_cast<PlcCommNode *>(cm->deviceNode(QStringLiteral("PRS_CLI")));
    QVERIFY2(cli != nullptr, "PLC 节点类型不符");
    // 断言"最短重连间隔"确实生效（同上：等待窗口按 4× 间隔取）
    QCOMPARE(cli->toJson().value(QStringLiteral("reconnectInterval")).toInt(), 500);

    QSignalSpy openedSpy(cli, &CommunicationNodeBase::connectionOpened);
    QSignalSpy closedSpy(cli, &CommunicationNodeBase::connectionClosed);
    QVERIFY2(cm->openDevice(QStringLiteral("PRS_CLI")), "PLC 客户端连接失败");
    QTRY_VERIFY_WITH_TIMEOUT(cli->isConnected(), 3000);      // 真正连上
    QCOMPARE(openedSpy.count(), 1);

    // (1) 用户主动关闭 → 不得被自动重连"复活"（2000ms = 4× 已生效间隔）
    QVERIFY2(cm->closeDevice(QStringLiteral("PRS_CLI")), "closeDevice 失败");
    QCOMPARE(closedSpy.count(), 1);
    QTest::qWait(2000);
    QCOMPARE(openedSpy.count(), 1);
    QVERIFY2(!cli->isConnected(), "用户关闭后 PLC 节点被自动重连复活");

    // (2) 重开后瞬断自愈仍须有效（钉住 openConnection 两行复位；缺失则 m_userClosed 恒 true）
    QVERIFY2(cm->openDevice(QStringLiteral("PRS_CLI")), "重开失败");
    QTRY_VERIFY_WITH_TIMEOUT(cli->isConnected(), 3000);
    QVERIFY2(cm->closeDevice(QStringLiteral("PRS_SRV")), "关服务器失败");
    QTRY_VERIFY_WITH_TIMEOUT(!cli->isConnected(), 3000);     // 先确认真瞬断被感知，否则本段空转
    QVERIFY2(cm->openDevice(QStringLiteral("PRS_SRV")), "重启服务器失败");
    QTRY_VERIFY_WITH_TIMEOUT(srv->isServerListening(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(cli->isConnected(), 8000);      // 自愈失效（复位缺失）在此变红
    QVERIFY(cm->closeDevice(QStringLiteral("PRS_CLI")));
}

// "载入后触发静默失效"复活路径的钉：unregisterExecutor 必须按执行器身份注销。
// 同名覆盖（registerFlow 同名静默覆盖）后，旧执行器的迟到析构不得误删新绑定的流程。
void CommWritebackTest::testUnregisterExecutorByIdentity()
{
    auto *gtm = GlobalTriggerManager::instance();
    const QString flow = QStringLiteral("IDENTITY_FLOW");

    FlowScene scene1, scene2;
    FlowExecutor exec1, exec2;
    exec1.setFlowName(flow);
    exec1.setFlowMode(FlowMode::SoftwareTrigger);
    exec2.setFlowName(flow);
    exec2.setFlowMode(FlowMode::SoftwareTrigger);

    gtm->registerFlow(flow, &scene1, &exec1);
    QCOMPARE(gtm->executorForFlow(flow), &exec1);

    // 同名覆盖：新执行器接管同一流程名
    gtm->registerFlow(flow, &scene2, &exec2);
    QCOMPARE(gtm->executorForFlow(flow), &exec2);

    // 旧执行器注销（模拟其迟到析构）：身份不匹配 → 不得移除当前绑定
    gtm->unregisterExecutor(&exec1);
    QCOMPARE(gtm->executorForFlow(flow), &exec2);

    // 新执行器注销才真正解绑
    gtm->unregisterExecutor(&exec2);
    QCOMPARE(gtm->executorForFlow(flow), nullptr);
}

// 单寄存器字节序全矩阵（含 M1 回归探针）：服务器回环，客户端写同一值 0x1234 到三个单寄存器
// （int16+BADC / uint16+DCBA / int16+CDAB），断言服务器收到的原始字节：
//   - BADC 单寄存器：字内字节互换 → "3412"（本就有意修对）
//   - DCBA 单寄存器：单字无换字概念、完全反转退化成字节互换 → "3412"（本就有意修对）
//   - CDAB 单寄存器：单字无"换字"，必须原样 → "1234"（回归点：旧实现落入 else 做了字节互换）
void CommWritebackTest::testModbusSingleRegisterByteOrder()
{
    const quint16 port = pickFreePort();   // 临时端口（替代固定字面量，避免 TIME_WAIT 撞端口）
    QVERIFY2(port != 0, "无法取得空闲端口（pickFreePort）");
    auto *cm = CommunicationManager::instance();
    DeviceCleanup cleanup{ { QStringLiteral("CD_SRV"), QStringLiteral("CD_CLI") } };

    QJsonObject srvCfg;
    srvCfg[QStringLiteral("role")] = QStringLiteral("服务器");
    srvCfg[QStringLiteral("connectionType")] = QStringLiteral("TCP");
    srvCfg[QStringLiteral("port")] = port;
    srvCfg[QStringLiteral("slaveAddress")] = 1;
    QVERIFY2(cm->addDevice(QStringLiteral("CD_SRV"), QStringLiteral("Modbus"), srvCfg),
             "addDevice(服务器) 失败");
    auto *srv = qobject_cast<ModbusNode *>(cm->deviceNode(QStringLiteral("CD_SRV")));
    QVERIFY2(srv != nullptr, "服务器节点类型不符");

    QList<ModbusRegisterItem> regs;
    auto mk = [&](int addr, const QString &dt, const QString &bo) {
        ModbusRegisterItem r;
        r.address = addr;
        r.dataType = dt;
        r.byteOrder = bo;
        r.accessMode = QStringLiteral("ReadWrite");
        r.enabled = true;
        regs.append(r);
    };
    mk(0, QStringLiteral("int16"), QStringLiteral("BADC"));
    mk(1, QStringLiteral("uint16"), QStringLiteral("DCBA"));
    mk(2, QStringLiteral("int16"), QStringLiteral("CDAB"));
    srv->setRegisters(regs);

    QVERIFY2(cm->openDevice(QStringLiteral("CD_SRV")), "Modbus 服务器启动失败");
    QTRY_VERIFY_WITH_TIMEOUT(srv->isServerListening(), 3000);

    QJsonObject cliCfg;
    cliCfg[QStringLiteral("role")] = QStringLiteral("客户端");
    cliCfg[QStringLiteral("connectionType")] = QStringLiteral("TCP");
    cliCfg[QStringLiteral("host")] = QStringLiteral("127.0.0.1");
    cliCfg[QStringLiteral("port")] = port;
    cliCfg[QStringLiteral("slaveAddress")] = 1;
    QVERIFY2(cm->addDevice(QStringLiteral("CD_CLI"), QStringLiteral("Modbus"), cliCfg),
             "addDevice(客户端) 失败");
    QVERIFY2(cm->openDevice(QStringLiteral("CD_CLI")), "Modbus 客户端连接失败");
    auto *cli = qobject_cast<ModbusNode *>(cm->deviceNode(QStringLiteral("CD_CLI")));
    QVERIFY2(cli != nullptr, "客户端节点类型不符");

    QSignalSpy changedSpy(srv, &CommunicationNodeBase::registerValueChanged);
    const quint16 value = 0x1234;   // 4660，落在 int16 正区间
    for (int addr = 0; addr < 3; ++addr) {
        bool wrote = false;
        for (int i = 0; i < 50 && !wrote; ++i) {
            wrote = cli->writeRegister(addr, value);
            if (!wrote) QTest::qWait(100);
        }
        QVERIFY2(wrote, qPrintable(QStringLiteral("写寄存器 %1 失败").arg(addr)));
    }

    QTRY_COMPARE_WITH_TIMEOUT(changedSpy.count(), 3, 5000);
    QMap<int, QByteArray> rawByAddr;
    for (int i = 0; i < changedSpy.count(); ++i) {
        const int a = changedSpy.at(i).at(0).toInt();
        const QByteArray r = changedSpy.at(i).at(1).toByteArray();
        rawByAddr[a] = r;
    }
    QCOMPARE(rawByAddr.value(0), QByteArray::fromHex("3412"));  // BADC：字内字节互换
    QCOMPARE(rawByAddr.value(1), QByteArray::fromHex("3412"));  // DCBA：单字退化成字节互换
    QCOMPARE(rawByAddr.value(2), QByteArray::fromHex("1234"));  // CDAB：单寄存器原样（M1 回归点）
}

// S3/S4 回归：写入必须与读路径字节序/宽类型往返对称（旧缺陷：BADC 写 100 读回 25600；
// int32/uint32 只写单寄存器被截断）。客户端与服务器配置同款 dataType/byteOrder（模拟同一台
// 真实设备）：int16+BADC 写入须先做字内互换；int32/uint32 须拆两寄存器且轮询 byteCount 计 4。
// 硬断言：客户端读回值 == 写入值。
void CommWritebackTest::testModbusWriteReadByteOrderRoundTrip()
{
    const quint16 port = pickFreePort();   // 临时端口（替代固定字面量，避免 TIME_WAIT 撞端口）
    QVERIFY2(port != 0, "无法取得空闲端口（pickFreePort）");
    auto *cm = CommunicationManager::instance();
    DeviceCleanup cleanup{ { QStringLiteral("RTT_SRV"), QStringLiteral("RTT_CLI") } };

    QList<ModbusRegisterItem> regs;
    auto mk = [&](int addr, const QString &dt, const QString &bo) {
        ModbusRegisterItem r;
        r.address = addr;
        r.dataType = dt;
        r.byteOrder = bo;
        r.accessMode = QStringLiteral("ReadWrite");
        r.enabled = true;
        regs.append(r);
    };
    mk(0, QStringLiteral("int16"), QStringLiteral("BADC"));    // 16 位字内互换（占 addr 0）
    mk(2, QStringLiteral("int32"), QStringLiteral("ABCD"));    // 32 位宽类型（拆两寄存器：2/3）
    mk(4, QStringLiteral("uint32"), QStringLiteral("ABCD"));   // S4：uint32 轮询须计 4 字节（4/5）
    mk(6, QStringLiteral("int16"), QStringLiteral("ABCD"));    // 撑开服务器数据区上限，覆盖 4/5

    QJsonObject srvCfg;
    srvCfg[QStringLiteral("role")] = QStringLiteral("服务器");
    srvCfg[QStringLiteral("connectionType")] = QStringLiteral("TCP");
    srvCfg[QStringLiteral("port")] = port;
    srvCfg[QStringLiteral("slaveAddress")] = 1;
    QVERIFY2(cm->addDevice(QStringLiteral("RTT_SRV"), QStringLiteral("Modbus"), srvCfg),
             "addDevice(服务器) 失败");
    auto *srv = qobject_cast<ModbusNode *>(cm->deviceNode(QStringLiteral("RTT_SRV")));
    QVERIFY2(srv != nullptr, "服务器节点类型不符");
    srv->setRegisters(regs);
    QVERIFY2(cm->openDevice(QStringLiteral("RTT_SRV")), "Modbus 服务器启动失败");
    QTRY_VERIFY_WITH_TIMEOUT(srv->isServerListening(), 3000);

    QJsonObject cliCfg;
    cliCfg[QStringLiteral("role")] = QStringLiteral("客户端");
    cliCfg[QStringLiteral("connectionType")] = QStringLiteral("TCP");
    cliCfg[QStringLiteral("host")] = QStringLiteral("127.0.0.1");
    cliCfg[QStringLiteral("port")] = port;
    cliCfg[QStringLiteral("slaveAddress")] = 1;
    cliCfg[QStringLiteral("writeVerify")] = true;   // S4：显式开启回写三段确认（默认关闭）
    // 统一透传回归：**未显式处理**的配置键必须原样到达节点（fail-open）。
    // 改造前 createDeviceNode 是逐键白名单，本键会静默丢失 → 下面 getParam 断言会失败（判别性）。
    cliCfg[QStringLiteral("unmanagedProbeKey")] = 4242;
    // 反向用例：与**节点内部参数**重名的配置键不得被写进参数表（init 已声明 ⇒ 透传排除）。
    // 塞一个特征字符串，随后断言它没有出现在节点的任何参数上——不依赖具体内部键的取值语义。
    cliCfg[QStringLiteral("moduleStatus")] = QStringLiteral("hijacked");
    cliCfg[QStringLiteral("lastSent")] = QStringLiteral("hijacked");
    cliCfg[QStringLiteral("lastError")] = QStringLiteral("hijacked");
    cliCfg[QStringLiteral("registers")] = QStringLiteral("hijacked");
    QVERIFY2(cm->addDevice(QStringLiteral("RTT_CLI"), QStringLiteral("Modbus"), cliCfg),
             "addDevice(客户端) 失败");
    auto *cli = qobject_cast<ModbusNode *>(cm->deviceNode(QStringLiteral("RTT_CLI")));
    QVERIFY2(cli != nullptr, "客户端节点类型不符");
    cli->setRegisters(regs);   // 写路径按客户端自身寄存器配置做逆变换
    // S4：先证明选项真的到达节点——否则"无校验失败"会因校验根本没跑而空转通过
    QCOMPARE(cli->toJson().value(QStringLiteral("writeVerify")).toBool(), true);
    // 统一透传（本轮）：**未显式处理**的键也必须到达节点；改造前它是逐键白名单，会被静默丢弃
    QCOMPARE(cli->getParam(QStringLiteral("unmanagedProbeKey")).toInt(), 4242);
    // 反向：与节点内部参数重名的配置键不得改写内部状态（旧写法会把 "hijacked" 写进 moduleStatus）
    for (const QString &k : cli->getAllParamNames()) {
        QVERIFY2(cli->getParam(k).toString() != QStringLiteral("hijacked"),
                 qPrintable(QStringLiteral("内部参数 %1 被配置里的同名键改写了").arg(k)));
    }

    QSignalSpy srvChangedSpy(srv, &CommunicationNodeBase::registerValueChanged);
    QSignalSpy cliValueSpy(cli, &ModbusNode::registerCurrentValueChanged);
    QSignalSpy cliErrSpy(cli, &CommunicationNodeBase::communicationError);   // S4：校验失败会上报到这里
    QVERIFY2(cm->openDevice(QStringLiteral("RTT_CLI")), "Modbus 客户端连接失败");
    QTest::qWait(200);   // 让轮询线程先跑一轮

    const double writeInt16 = 100.0;        // 0x0064
    const double writeInt32 = 100000.0;     // 0x000186A0
    const double writeUint32 = 70000.0;     // 0x00011170
    QList<QPair<int, double>> writes;
    writes.append(qMakePair(0, writeInt16));
    writes.append(qMakePair(2, writeInt32));
    writes.append(qMakePair(4, writeUint32));
    for (const auto &w : writes) {
        bool wrote = false;
        for (int i = 0; i < 50 && !wrote; ++i) {
            wrote = cli->writeRegister(w.first, w.second);
            if (!wrote) QTest::qWait(100);
        }
        QVERIFY2(wrote, qPrintable(QStringLiteral("写寄存器 %1 失败").arg(w.first)));
    }

    // 服务器收到的 addr0 原始字节：客户端逆变换后写入 0x6400，服务器 BADC 组装 → "0064"
    // （服务器上报是异步的，必须等信号落地，不能写完立刻读 spy）
    auto srvRawFor = [&](int addr) -> QByteArray {
        QByteArray r;
        for (int i = 0; i < srvChangedSpy.count(); ++i) {
            if (srvChangedSpy.at(i).at(0).toInt() == addr)
                r = srvChangedSpy.at(i).at(1).toByteArray();
        }
        return r;
    };
    QTRY_VERIFY_WITH_TIMEOUT(srvRawFor(0) == QByteArray::fromHex("0064"), 5000);

    // 硬断言：客户端读回值 == 写入值（取该地址最后一次读数）
    auto lastReadValue = [&](int addr) -> double {
        double v = -1.0e18;   // 哨兵：未读到
        for (int i = 0; i < cliValueSpy.count(); ++i) {
            if (cliValueSpy.at(i).at(0).toInt() == addr)
                v = cliValueSpy.at(i).at(1).toDouble();
        }
        return v;
    };
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(lastReadValue(0) - writeInt16) < 1e-6, 8000);
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(lastReadValue(2) - writeInt32) < 1e-6, 8000);
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(lastReadValue(4) - writeUint32) < 1e-6, 8000);

    // S4：比对判定的语义（往返一致不得判错；float 必须容差，否则 100.1 经 float32 回转必假报警）
    QVERIFY2(RegisterByteOrder::valueMatches(100.0, 100.0, QStringLiteral("int16")), "值一致应判匹配");
    QVERIFY2(!RegisterByteOrder::valueMatches(100.0, 101.0, QStringLiteral("int16")), "值不一致应判不匹配");
    const double fRound = static_cast<double>(100.1f);   // float32 精度回转后的值
    QVERIFY2(RegisterByteOrder::valueMatches(100.1, fRound, QStringLiteral("float")),
             "float 容差不得对 float32 精度回转假报警");
    QVERIFY2(!RegisterByteOrder::valueMatches(100.1, 100.2, QStringLiteral("float")),
             "float 明显不同应判不匹配");
    // 开启 writeVerify 后，上面三次往返（BADC / int32 / uint32）不得产生任何"回写校验"报错
    int verifyFailures = 0;
    for (int i = 0; i < cliErrSpy.count(); ++i) {
        if (cliErrSpy.at(i).at(0).toString().contains(QStringLiteral("回写校验")))
            ++verifyFailures;
    }
    QCOMPARE(verifyFailures, 0);
}

// S7 回归：服务器寄存器表初始化必须按宽类型拆字写入。旧实现只写单寄存器（4 字节值塞进一个"伪地址"）：
// 高字丢失、值被截断，且数据区上限不覆盖 addr+1 → 客户端按宽类型读 2 个寄存器被服务器判非法地址。
// 场景：服务器侧 int32 item 预置 currentValue=250000（0x0003D090，高字=0x0003 非零可判别），
// 客户端不做任何写入、直接读回，必须等于预置值。
void CommWritebackTest::testModbusServerWideRegisterInit()
{
    const quint16 port = pickFreePort();   // 临时端口（替代固定字面量，避免 TIME_WAIT 撞端口）
    QVERIFY2(port != 0, "无法取得空闲端口（pickFreePort）");
    auto *cm = CommunicationManager::instance();
    DeviceCleanup cleanup{ { QStringLiteral("WI_SRV"), QStringLiteral("WI_CLI") } };

    QList<ModbusRegisterItem> regs;
    ModbusRegisterItem r32;
    r32.address = 0;
    r32.dataType = QStringLiteral("int32");
    r32.byteOrder = QStringLiteral("ABCD");
    r32.accessMode = QStringLiteral("ReadWrite");
    r32.enabled = true;
    r32.currentValue = 250000.0;      // 高字非零：单寄存器写法必然读不回该值
    regs.append(r32);

    QJsonObject srvCfg;
    srvCfg[QStringLiteral("role")] = QStringLiteral("服务器");
    srvCfg[QStringLiteral("connectionType")] = QStringLiteral("TCP");
    srvCfg[QStringLiteral("port")] = port;
    srvCfg[QStringLiteral("slaveAddress")] = 1;
    QVERIFY2(cm->addDevice(QStringLiteral("WI_SRV"), QStringLiteral("Modbus"), srvCfg),
             "addDevice(服务器) 失败");
    auto *srv = qobject_cast<ModbusNode *>(cm->deviceNode(QStringLiteral("WI_SRV")));
    QVERIFY2(srv != nullptr, "服务器节点类型不符");
    srv->setRegisters(regs);          // 触发 syncServerRegisters()
    QVERIFY2(cm->openDevice(QStringLiteral("WI_SRV")), "Modbus 服务器启动失败");
    QTRY_VERIFY_WITH_TIMEOUT(srv->isServerListening(), 3000);

    QJsonObject cliCfg;
    cliCfg[QStringLiteral("role")] = QStringLiteral("客户端");
    cliCfg[QStringLiteral("connectionType")] = QStringLiteral("TCP");
    cliCfg[QStringLiteral("host")] = QStringLiteral("127.0.0.1");
    cliCfg[QStringLiteral("port")] = port;
    cliCfg[QStringLiteral("slaveAddress")] = 1;
    QVERIFY2(cm->addDevice(QStringLiteral("WI_CLI"), QStringLiteral("Modbus"), cliCfg),
             "addDevice(客户端) 失败");
    auto *cli = qobject_cast<ModbusNode *>(cm->deviceNode(QStringLiteral("WI_CLI")));
    QVERIFY2(cli != nullptr, "客户端节点类型不符");
    cli->setRegisters(regs);          // 客户端按 int32 读 2 个寄存器

    QSignalSpy cliValueSpy(cli, &ModbusNode::registerCurrentValueChanged);
    QVERIFY2(cm->openDevice(QStringLiteral("WI_CLI")), "Modbus 客户端连接失败");

    auto lastReadValue = [&](int addr) -> double {
        double v = -1.0e18;   // 哨兵：未读到
        for (int i = 0; i < cliValueSpy.count(); ++i) {
            if (cliValueSpy.at(i).at(0).toInt() == addr)
                v = cliValueSpy.at(i).at(1).toDouble();
        }
        return v;
    };
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(lastReadValue(0) - 250000.0) < 1e-6, 8000);
    QVERIFY(cm->closeDevice(QStringLiteral("WI_CLI")));
}

// S3（PLC 半边）：PlcCommNode::writeRegister 同样按该地址 dataType/byteOrder 逆变换拆字。
// 场景：PLC 客户端把 32 位值写到 Modbus 服务器，客户端读回必须等于写入值（往返对称）。
// 判别性：旧实现（quint16 单寄存器）会把 int32 截断成低 16 位 → 本用例必红。
void CommWritebackTest::testPlcWriteReadByteOrderRoundTrip()
{
    const quint16 port = pickFreePort();   // 临时端口（替代固定字面量，避免 TIME_WAIT 撞端口）
    QVERIFY2(port != 0, "无法取得空闲端口（pickFreePort）");
    auto *cm = CommunicationManager::instance();
    DeviceCleanup cleanup{ { QStringLiteral("PW_SRV"), QStringLiteral("PW_CLI") } };

    QList<ModbusRegisterItem> regs;
    ModbusRegisterItem r32;
    r32.address = 0;
    r32.dataType = QStringLiteral("int32");
    r32.byteOrder = QStringLiteral("ABCD");
    r32.accessMode = QStringLiteral("ReadWrite");
    r32.enabled = true;
    regs.append(r32);

    QJsonObject srvCfg;
    srvCfg[QStringLiteral("role")] = QStringLiteral("服务器");
    srvCfg[QStringLiteral("connectionType")] = QStringLiteral("TCP");
    srvCfg[QStringLiteral("port")] = port;
    srvCfg[QStringLiteral("slaveAddress")] = 1;
    QVERIFY2(cm->addDevice(QStringLiteral("PW_SRV"), QStringLiteral("Modbus"), srvCfg),
             "addDevice(服务器) 失败");
    auto *srv = qobject_cast<ModbusNode *>(cm->deviceNode(QStringLiteral("PW_SRV")));
    QVERIFY2(srv != nullptr, "服务器节点类型不符");
    srv->setRegisters(regs);
    QVERIFY2(cm->openDevice(QStringLiteral("PW_SRV")), "Modbus 服务器启动失败");
    QTRY_VERIFY_WITH_TIMEOUT(srv->isServerListening(), 3000);

    QJsonObject cliCfg;
    cliCfg[QStringLiteral("host")] = QStringLiteral("127.0.0.1");
    cliCfg[QStringLiteral("port")] = port;
    cliCfg[QStringLiteral("slaveAddress")] = 1;
    QVERIFY2(cm->addDevice(QStringLiteral("PW_CLI"), QStringLiteral("PLC"), cliCfg),
             "addDevice(PLC 客户端) 失败");
    auto *cli = qobject_cast<PlcCommNode *>(cm->deviceNode(QStringLiteral("PW_CLI")));
    QVERIFY2(cli != nullptr, "PLC 节点类型不符");
    cli->setRegisters(regs);      // 写路径按自身配置逆变换拆字；轮询按 int32 读 2 个字

    QSignalSpy cliValueSpy(cli, &PlcCommNode::registerCurrentValueChanged);
    QVERIFY2(cm->openDevice(QStringLiteral("PW_CLI")), "PLC 客户端连接失败");
    QTRY_VERIFY_WITH_TIMEOUT(cli->isConnected(), 3000);

    const double w = 250000.0;    // 0x0003D090：截断成低 16 位后必不相等，可判别
    bool wrote = false;
    for (int i = 0; i < 50 && !wrote; ++i) {
        wrote = cli->writeRegister(0, w);
        if (!wrote) QTest::qWait(100);
    }
    QVERIFY2(wrote, "PLC 写寄存器失败");

    auto lastReadValue = [&](int addr) -> double {
        double v = -1.0e18;   // 哨兵：未读到
        for (int i = 0; i < cliValueSpy.count(); ++i) {
            if (cliValueSpy.at(i).at(0).toInt() == addr)
                v = cliValueSpy.at(i).at(1).toDouble();
        }
        return v;
    };
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(lastReadValue(0) - w) < 1e-6, 8000);
    QVERIFY(cm->closeDevice(QStringLiteral("PW_CLI")));
}

// L2 回归：新建流程名必须全局唯一。扫描已注册绑定返回首个空闲"流程 N"，
// 验证顺序分配与"删除后回填空档"两种情形，避免 size() 复用序号导致同名覆盖。
void CommWritebackTest::testAllocFlowNameAvoidsCollision()
{
    auto *gtm = GlobalTriggerManager::instance();
    FlowScene s1, s2, s3, s4;
    FlowExecutor e1, e2, e3, e4;

    // L2：进入即自洁净——无条件退订 流程 1~4，消除对 GlobalTriggerManager 单例残留状态的顺序敏感，
    // 保证首条 allocFlowName()=="流程 2" 从确定初态成立。
    for (int n = 1; n <= 4; ++n)
        gtm->unregisterFlow(QStringLiteral("流程 %1").arg(n));

    gtm->registerFlow(QStringLiteral("流程 1"), &s1, &e1);
    QCOMPARE(gtm->allocFlowName(), QStringLiteral("流程 2"));
    gtm->registerFlow(QStringLiteral("流程 2"), &s2, &e2);
    gtm->registerFlow(QStringLiteral("流程 3"), &s3, &e3);
    QCOMPARE(gtm->allocFlowName(), QStringLiteral("流程 4"));

    // 模拟删除"流程 2"：退订后分配应回填空档，而非复用已占用序号
    gtm->unregisterFlow(QStringLiteral("流程 2"));
    QCOMPARE(gtm->allocFlowName(), QStringLiteral("流程 2"));
    gtm->registerFlow(QStringLiteral("流程 2"), &s4, &e4);
    QCOMPARE(gtm->allocFlowName(), QStringLiteral("流程 4"));   // 1/2/3 均占 → 下一个空闲为 4

    // 清理
    gtm->unregisterFlow(QStringLiteral("流程 1"));
    gtm->unregisterFlow(QStringLiteral("流程 2"));
    gtm->unregisterFlow(QStringLiteral("流程 3"));
}

QTEST_MAIN(CommWritebackTest)

// 源文件内定义 Q_OBJECT 类时，AUTOMOC 要求显式包含生成的 moc
#include "comm_writeback_test.moc"
