// 通讯回写链路端到端验证：本机起一个 QTcpServer 充当「模拟 PLC」，让平台按 TCP 客户端连上去，
// 再由流程里的「发送数据」算子把结果写回；并验证"设备不存在 / 未连接"时回写失败是**可见**的
// （此前 process() 恒返回 true：PLC 什么都没收到，流程却显示成功、日志里也没有任何痕迹）。
#include <QtTest>
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

namespace {

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

    // 投递是排队执行（sendRequested → onSendRequested），需要事件循环才会真正 write
    QVERIFY2(cm->sendData(QStringLiteral("SIM_PLC"), QByteArray("OK,1,3.14\r\n")), "sendData 投递失败");
    QTRY_VERIFY_WITH_TIMEOUT(received.contains("OK,1,3.14"), 3000);
    QCOMPARE(received, QByteArray("OK,1,3.14\r\n"));   // 原样字节，无额外包装

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

void CommWritebackTest::testModbusRegisterWritebackAndRawSendGuard()
{
    // 用项目自带的 Modbus 双角色在本进程内回环：服务器（从站）+ 客户端（主站）
    const int port = 15502;   // 高位端口，避开常见 Modbus 502
    auto *cm = CommunicationManager::instance();

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

// 必须用 QTEST_MAIN：流程用例要创建 FlowScene（QGraphicsScene），仅 QCoreApplication 会崩；
// vfp_core 公开链接 Widgets，故这里会得到 QApplication，平台插件由 VFP_TESTS 的
// QT_PLUGIN_PATH 注入（dist/VisionFlowPlatform）。
QTEST_MAIN(CommWritebackTest)

// 源文件内定义 Q_OBJECT 类时，AUTOMOC 要求显式包含生成的 moc
#include "comm_writeback_test.moc"
