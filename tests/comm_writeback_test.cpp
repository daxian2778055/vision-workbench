// 通讯回写链路端到端验证：本机起一个 QTcpServer 充当「模拟 PLC」，让平台按 TCP 客户端连上去，
// 再由流程里的「发送数据」算子把结果写回；并验证"设备不存在 / 未连接"时回写失败是**可见**的
// （此前 process() 恒返回 true：PLC 什么都没收到，流程却显示成功、日志里也没有任何痕迹）。
#include <QtTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonObject>

#include "CommunicationManager.h"
#include "FlowExecutor.h"
#include "FlowScene.h"
#include "GlobalTriggerManager.h"
#include "ModbusNode.h"
#include "NodeBase.h"
#include "ReceiveEvent.h"

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

// 必须用 QTEST_MAIN：流程用例要创建 FlowScene（QGraphicsScene），仅 QCoreApplication 会崩；
// vfp_core 公开链接 Widgets，故这里会得到 QApplication，平台插件由 VFP_TESTS 的
// QT_PLUGIN_PATH 注入（dist/VisionFlowPlatform）。
QTEST_MAIN(CommWritebackTest)

// 源文件内定义 Q_OBJECT 类时，AUTOMOC 要求显式包含生成的 moc
#include "comm_writeback_test.moc"
