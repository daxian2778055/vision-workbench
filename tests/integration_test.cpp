// 集成测试 - 测试多算子组合的流程执行
// 测试场景：图像读取 → 阈值分割 → 形态学 → 连通域 → 结果输出
#include <QtTest/QtTest>
#include <QObject>
#include <QSignalSpy>

// 节点头文件
#include "FlowScene.h"
#include "FlowExecutor.h"
#include "NodeBase.h"
#include "OpencvThresholdNode.h"
#include "OpencvMorphNode.h"
#include "OpencvBlobNode.h"
#include "ImageReadNode.h"
#include "DisplaySinkNode.h"
#include "LoopNode.h"
#include "DelayNode.h"
#include "FilterNode.h"   // S1 残留试点：影子成员收口后的单源/并发回归
#include "SortNode.h"
#include "CounterNode.h"
#include "FormatNode.h"
#include "FormulaNode.h"
#include "ClassifyNode.h"
#include "ProtocolParseNode.h"
#include "SendDataNode.h"
#include "RecordNode.h"
#include "ReceiveDataNode.h"
#include <QDir>
#include <QFile>
#include "ScriptSecurityPolicy.h"
#include "ImageDisplayController.h"
#include "ExecutionStatusController.h"
#include "RecentFilesMenu.h"
#include "RuntimeInterface.h"
#include "Port.h"
#include "Connection.h"
#include "DataObject.h"
#include "ThreadSafeParams.h"
#include "GlobalTriggerManager.h"
#include <QElapsedTimer>
#include <QThread>
#include <QPointer>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStatusBar>
#include <QAction>
#include <QToolButton>
#include <thread>
#include <vector>
#include <atomic>

class IntegrationTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // 基本流程测试
    void testSimplePipeline();
    void testBranchingPipeline();

    // 数据流测试
    void testDataPropagation();

    // 错误处理测试
    void testErrorHandling();

    // 性能测试
    void testExecutionTime();

    // 回归测试（复审 P1/P2）
    void testLoopIterationOrder();
    void testDelayStopCancellable();
    void testDelayPauseResumeKeepsRemaining();
    void testContinuousSecondRoundClearsStaleData();
    void testDestroyWhileRunningIsSafe();
    void testTwoExecutorsIsolation();
    void testScriptNodeStopCancellable();
    void testExecutorReuseAcrossScenesWithLoop();
    void testLoopAddedToSameSceneIsDetected();
    void testScriptNodeReportsSuccess();
    void testScriptNodeLowIntegrityContainment();
    void testConditionalBranchSkipClearsStaleOutput();
    void testNestedLoopIterations();
    void testParamRefResolvesEachRound();   // E1：参数引用跨轮必须重新解析上游最新值
    void testRuntimeStatsCounters();
    void testRestrictedTokenLaunch();
    void testAppContainerSandboxLaunch();
    void testEndToEndPipelineSmoke();
    void testGraphSnapshotDefersDeletionDuringRound();    // S1：快照/墓碑契约 + 真跑一轮内删节点
    void testRecomputeDownstreamOnly();

    // 运行界面（多页/结果表格/IO状态）
    void testRuntimeInterfaceMultiPageRoundTrip();
    void testRuntimeInterfaceLegacyCompat();
    void testRuntimeControlTypeNames();

    // 并发/锁纪律回归（把已修的锁用用例钉死）
    void testThreadSafeParamsConcurrentAccess();
    void testDataObjectConcurrentAccess();
    void testStepModeExitRestoresNormalRun();
    void testBusyTriggerQueuedNotDropped();
    void testSnapshotGuardSurvivesSceneDestruction();   // N-2：场景先亡时句柄释放不得回调已亡场景
    void testBusyOtherFlowDoesNotBlockTrigger();
    void testScriptInterpreterResolvesToAbsolutePath();
    void testImageDisplayResolvePriority();
    void testExecutionStatusController();
    void testFlowExtrasRoundTrip();   // 每流程身份（流程名 + 运行模式）随方案持久化
    void testShadowMemberSingleSourceAndConcurrentAccess();   // S1 残留试点：参数唯一来源 + 并发读写
    void testExternalTriggerAcceptedOnlyInSoftwareMode();   // F-1：非软触发模式不得受理外部触发
    void testRecentFilesMenu();

    // S4 回归：模块号必须单调且不复用（删节点后新建节点不得拿到被删节点的号）
    void testModuleIdNotRecycled();

private:
    FlowScene *m_scene = nullptr;
    FlowExecutor *m_executor = nullptr;
};

void IntegrationTest::initTestCase()
{
    m_scene = new FlowScene(this);
    m_executor = new FlowExecutor(this);
}

void IntegrationTest::cleanupTestCase()
{
    delete m_executor;
    delete m_scene;
}

void IntegrationTest::testSimplePipeline()
{
    // 测试简单线性流程：读取图像 → 阈值分割 → 显示
    m_scene->clearScene();

    // 创建节点
    NodeBase *reader = m_scene->createNode(NodeBase::IMAGE_ACQUISITION, QPointF(100, 200), "读取图像");
    NodeBase *threshold = m_scene->createNode(NodeBase::IMAGE_PROCESSING, QPointF(300, 200), "OpenCV二值化");
    NodeBase *display = m_scene->createNode(NodeBase::OUTPUT, QPointF(500, 200), "图像显示");

    QVERIFY(reader != nullptr);
    QVERIFY(threshold != nullptr);
    QVERIFY(display != nullptr);

    // 验证节点创建
    QCOMPARE(m_scene->nodes().size(), 3);
}

void IntegrationTest::testBranchingPipeline()
{
    // 测试分支流程：一个输出连接到多个输入
    m_scene->clearScene();

    NodeBase *reader = m_scene->createNode(NodeBase::IMAGE_ACQUISITION, QPointF(100, 200), "读取图像");
    NodeBase *threshold1 = m_scene->createNode(NodeBase::IMAGE_PROCESSING, QPointF(300, 100), "OpenCV二值化");
    NodeBase *threshold2 = m_scene->createNode(NodeBase::IMAGE_PROCESSING, QPointF(300, 300), "OpenCV二值化");

    QVERIFY(reader != nullptr);
    QVERIFY(threshold1 != nullptr);
    QVERIFY(threshold2 != nullptr);

    QCOMPARE(m_scene->nodes().size(), 3);
}

void IntegrationTest::testDataPropagation()
{
    // 测试数据在节点间的传播
    m_scene->clearScene();

    // 创建一个简单的阈值节点
    OpencvThresholdNode *threshold = new OpencvThresholdNode();
    threshold->init();

    // 验证端口
    QCOMPARE(threshold->inputPorts().size(), 1);
    QVERIFY(threshold->outputPorts().size() >= 2);

    // 验证端口类型
    QCOMPARE(threshold->inputPorts()[0]->dataType(), PortDataType::Image);
    QCOMPARE(threshold->outputPorts()[0]->dataType(), PortDataType::Image);

    delete threshold;
}

void IntegrationTest::testErrorHandling()
{
    // 测试错误处理：空输入
    OpencvThresholdNode threshold;
    threshold.init();

    // 不设置输入图像，直接执行
    threshold.setParam("mode", 0);
    threshold.setParam("minVal", 128);

    // run 方法应该安全处理空输入
    threshold.run(false);

    // 验证模块状态为 false（因为没有输入）
    QCOMPARE(threshold.getParam("moduleStatus").toBool(), false);
}

void IntegrationTest::testExecutionTime()
{
    // 测试执行时间测量
    OpencvThresholdNode threshold;
    threshold.init();

    // 设置参数
    threshold.setParam("mode", 0);
    threshold.setParam("minVal", 128);

    // 多次执行测量时间
    QElapsedTimer timer;
    timer.start();

    for (int i = 0; i < 100; ++i) {
        threshold.run(false);
    }

    qint64 elapsed = timer.elapsed();

    // 验证执行时间合理（100次执行应该在几秒内完成）
    QVERIFY2(elapsed < 10000,
             qPrintable(QString("100次执行耗时 %1ms，超过10秒").arg(elapsed)));

    qDebug() << "100次阈值执行耗时:" << elapsed << "ms，平均:" << elapsed / 100.0 << "ms";
}

// ===== 回归测试（复审 P1/P2）=====

void IntegrationTest::testLoopIterationOrder()
{
    // 循环体应完整执行 loopCount 次，且迭代号为 1,2,3（而非旧实现的 2,3,3）
    // 使用本测试私有的场景与执行器，避免与其它用例共享状态
    FlowScene scene;
    FlowExecutor exec;
    exec.setFlowName(QStringLiteral("RegressionLoopIteration"));

    NodeBase *loop = scene.createNode(NodeBase::LOGIC, QPointF(100, 200), QStringLiteral("Loop"));
    NodeBase *body = scene.createNode(NodeBase::LOGIC, QPointF(320, 200), QStringLiteral("Delay"));
    QVERIFY2(loop != nullptr, "无法创建循环节点");
    QVERIFY2(body != nullptr, "无法创建延时节点（循环体）");
    loop->setParam(QStringLiteral("loopCount"), 3);
    body->setParam(QStringLiteral("delayMs"), 0);

    QVERIFY2(!loop->outputPorts().isEmpty(), "循环节点无输出端口");
    QVERIFY2(!body->inputPorts().isEmpty(), "循环体节点无输入端口");
    MyProject::Connection *conn = scene.createConnection(loop->outputPorts().first(),
                                                         body->inputPorts().first(), true);
    QVERIFY2(conn != nullptr, "无法建立 循环->循环体 连线");

    QList<int> seenIterations;
    QList<NodeBase *> bodyRuns;
    const auto connHandle = QObject::connect(
        &exec, &FlowExecutor::nodeExecuted, &exec,
        [&](NodeBase *n, bool ok) {
            if (ok && n == body) {
                bodyRuns.append(n);
                seenIterations.append(loop->getParam(QStringLiteral("iteration")).toInt());
            }
        }, Qt::DirectConnection);

    exec.setFlowScene(&scene);
    exec.setFlowMode(FlowMode::SoftwareTrigger);
    exec.startExecution();
    bool finished = exec.wait(10000);
    if (!finished) {
        exec.stopExecution();
        finished = exec.wait(3000);
    }
    QObject::disconnect(connHandle);
    // 先解绑并投递挂起信号，确保节点析构前不再被访问
    exec.setFlowScene(nullptr);
    QCoreApplication::processEvents();

    QVERIFY2(finished, "循环流程未在 10 秒内结束");
    QCOMPARE(bodyRuns.size(), qsizetype(3));
    const QList<int> expectedIterations{1, 2, 3};
    QCOMPARE(seenIterations, expectedIterations);
}

void IntegrationTest::testDelayStopCancellable()
{
    // 运行中停止流程：延时等待应被立即取消，而不是等满 delayMs
    FlowScene scene;
    FlowExecutor exec;
    exec.setFlowName(QStringLiteral("RegressionDelayStop"));

    NodeBase *delay = scene.createNode(NodeBase::LOGIC, QPointF(200, 200), QStringLiteral("Delay"));
    QVERIFY(delay != nullptr);
    delay->setParam(QStringLiteral("delayMs"), 5000);

    exec.setFlowScene(&scene);
    exec.setFlowMode(FlowMode::SoftwareTrigger);

    QElapsedTimer t;
    t.start();
    exec.startExecution();
    QTest::qWait(300);
    exec.stopExecution();
    const bool finished = exec.wait(3000);
    const qint64 elapsed = t.elapsed();
    exec.setFlowScene(nullptr);
    QCoreApplication::processEvents();

    QVERIFY2(finished, "停止后执行器线程未在 3 秒内退出");
    QVERIFY2(elapsed < 2500,
             qPrintable(QStringLiteral("延时 5000ms 被停止后仍耗时 %1ms，取消等待未生效").arg(elapsed)));
}

// S1（执行期图快照 + 墓碑延迟析构）：
// (1) 白盒契约：持快照期间 removeNode 只摘除不析构——成员集立即消失（后续快照不再含它）、对象仍存活
//     （执行线程手里的裸指针不悬垂）、快照冻结在捕获时刻（VisionMaster 式语义）；释放后才真正析构。
//     存活判定用 QPointer，无 UB。
// (2) 并发交叉（真场景）：真跑一轮，在该轮进行中（延时节点阻塞时）从 UI 线程删除**正在执行的**
//     延时节点——旧实现是立即 delete，执行线程随后继续用该对象即 UAF；修复后必须活到轮末。
//     前置断言"此刻本轮仍在进行"，避免延时失效时本段退化为空转假通过。
void IntegrationTest::testGraphSnapshotDefersDeletionDuringRound()
{
    // (1) 墓碑契约（确定性）
    {
        FlowScene scene;
        NodeBase *a = scene.createNode(NodeBase::IMAGE_PROCESSING, QPointF(0, 0),
                                       QStringLiteral("OpenCV二值化"));
        NodeBase *b = scene.createNode(NodeBase::IMAGE_PROCESSING, QPointF(200, 0),
                                       QStringLiteral("OpenCV二值化"));
        QVERIFY(a != nullptr);
        QVERIFY(b != nullptr);
        QPointer<NodeBase> aPtr(a);
        const int aId = a->moduleId();
        {
            FlowScene::GraphSnapshotGuard guard = scene.captureGraphSnapshot();
            QCOMPARE(guard.snapshot().nodes.size(), 2);
            scene.removeNode(a);
            QCOMPARE(scene.nodes().size(), 1);            // 成员集立即摘除（后续快照不再包含）
            QCOMPARE(a->moduleId(), aId);                 // 对象仍存活：延迟析构（若已析构此处即 UB）
            QCOMPARE(guard.snapshot().nodes.size(), 2);   // 快照冻结在捕获时刻
            QVERIFY2(!aPtr.isNull(), "持快照期间不得析构被删节点");
        }
        QVERIFY2(aPtr.isNull(), "快照释放后墓碑未被 flush（对象泄漏）");
        QCOMPARE(scene.nodes().size(), 1);
    }

    // (2) 轮内删除正在执行的节点
    {
        FlowScene scene;
        FlowExecutor exec;
        exec.setFlowName(QStringLiteral("RegressionGraphSnapshot"));
        exec.setFlowMode(FlowMode::SoftwareTrigger);

        // 用与 testDelayStopCancellable 相同的建节点方式（该路径已证会真阻塞：slowest=延时[1](308ms)）
        NodeBase *delayLong = scene.createNode(NodeBase::LOGIC, QPointF(0, 0), QStringLiteral("Delay"));
        NodeBase *other = scene.createNode(NodeBase::LOGIC, QPointF(200, 0), QStringLiteral("Delay"));
        QVERIFY(delayLong != nullptr);
        QVERIFY(other != nullptr);
        delayLong->setParam(QStringLiteral("delayMs"), 1500);   // 要删的节点：正在阻塞
        other->setParam(QStringLiteral("delayMs"), 0);          // 陪跑节点：不失败、不阻塞
        QPointer<NodeBase> delayPtr(delayLong);
        QCOMPARE(scene.nodes().size(), 2);

        exec.setFlowScene(&scene);
        QSignalSpy finishedSpy(&exec, &FlowExecutor::executionFinished);

        exec.startExecution();
        QTest::qWait(200);
        // 前置校验：此刻本轮必须仍在进行（延时失效时本段会假通过，必须变红）
        QVERIFY2(finishedSpy.count() == 0, "延时未生效：本轮在删除前已结束（本段会空转）");
        scene.removeNode(delayLong);        // 轮内删除"正在执行"的节点 → 必须走墓碑
        QVERIFY2(!delayPtr.isNull(), "轮内删除被立即析构：执行线程随后使用即为 UAF");

        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 8000);   // 本轮照常跑完、不崩

        exec.stopExecution();
        QVERIFY2(exec.wait(3000), "执行器线程未退出");
        exec.setFlowScene(nullptr);
        // 轮末释放快照 → 墓碑析构被投递回场景线程（本线程）执行
        QTRY_VERIFY_WITH_TIMEOUT(delayPtr.isNull(), 3000);
    }
}

void IntegrationTest::testDelayPauseResumeKeepsRemaining()
{
    // 暂停不应消耗延时预算；恢复后延时应继续剩余时间而不是提前结束
    FlowScene scene;
    FlowExecutor exec;
    exec.setFlowName(QStringLiteral("RegressionDelayPause"));

    NodeBase *delay = scene.createNode(NodeBase::LOGIC, QPointF(200, 200), QStringLiteral("Delay"));
    QVERIFY(delay != nullptr);
    delay->setParam(QStringLiteral("delayMs"), 800);

    exec.setFlowScene(&scene);
    exec.setFlowMode(FlowMode::SoftwareTrigger);

    QElapsedTimer t;
    t.start();
    exec.startExecution();
    QTest::qWait(200);
    exec.pauseExecution();
    QTest::qWait(600);                 // 暂停期间不计入延时
    exec.resumeExecution();
    QTest::qWait(100);
    const ExecutionState afterResume = exec.getState();

    QVERIFY2(afterResume == ExecutionState::Running,
             "恢复后延时被提前结束（剩余等待时间未维护）");

    const bool finished = exec.wait(3000);
    const qint64 elapsed = t.elapsed();
    exec.setFlowScene(nullptr);
    QCoreApplication::processEvents();

    QVERIFY2(finished, "延时流程未在 3 秒内结束");
    QVERIFY2(elapsed >= 1000,
             qPrintable(QStringLiteral("暂停 600ms 后总耗时仅 %1ms，暂停期间仍在消耗延时").arg(elapsed)));
}

void IntegrationTest::testContinuousSecondRoundClearsStaleData()
{
    // 连续两轮：第二轮上游不再产生输出，下游不得读到第一轮残留数据（P2）
    FlowScene scene;
    FlowExecutor exec;
    exec.setFlowName(QStringLiteral("RegressionStaleRound"));

    NodeBase *formula = scene.createNode(NodeBase::LOGIC, QPointF(120, 200), QStringLiteral("Formula"));
    NodeBase *sink = scene.createNode(NodeBase::LOGIC, QPointF(340, 200), QStringLiteral("Delay"));
    QVERIFY(formula != nullptr);
    QVERIFY(sink != nullptr);
    formula->setParam(QStringLiteral("expression"), QStringLiteral("1 + 2"));
    sink->setParam(QStringLiteral("delayMs"), 0);
    QVERIFY2(scene.createConnection(formula->outputPorts().first(),
                                    sink->inputPorts().first(), true) != nullptr,
             "无法建立 公式->下游 连线");

    QList<bool> sinkInputPresent;
    QList<bool> sinkOutputPresent;
    int formulaRounds = 0;
    const auto connHandle = QObject::connect(
        &exec, &FlowExecutor::nodeExecuted, &exec,
        [&](NodeBase *n, bool ok) {
            if (!ok) return;
            if (n == formula) {
                ++formulaRounds;
                if (formulaRounds == 1) {
                    // 第二轮改为引用未连接的 p0 → 求值失败，不再产生输出
                    formula->setParam(QStringLiteral("expression"), QStringLiteral("p0 + 1"));
                }
            } else if (n == sink) {
                sinkInputPresent.append(sink->getInputData(0) != nullptr);
                // 节点自身输出也必须是本轮结果：DelayNode 无输入时应清空输出（P1）
                sinkOutputPresent.append(sink->getOutputData(0) != nullptr);
                if (sinkInputPresent.size() >= 2) {
                    exec.stopExecution();   // 观察满两轮后结束
                }
            }
        }, Qt::DirectConnection);

    exec.setFlowScene(&scene);
    exec.setFlowMode(FlowMode::Continuous);
    exec.startExecution();
    bool finished = exec.wait(5000);
    if (!finished) {
        exec.stopExecution();
        finished = exec.wait(2000);
    }
    QObject::disconnect(connHandle);
    exec.setFlowScene(nullptr);
    QCoreApplication::processEvents();

    QVERIFY2(finished, "连续流程未在 5 秒内结束");
    QVERIFY2(sinkInputPresent.size() >= 2,
             qPrintable(QStringLiteral("未观察到两轮执行，实际 %1 轮").arg(sinkInputPresent.size())));
    QCOMPARE(sinkInputPresent.at(0), true);    // 第一轮：有输入
    QCOMPARE(sinkInputPresent.at(1), false);   // 第二轮：不得残留第一轮输入
    QVERIFY2(sinkOutputPresent.size() >= 2, "未观察到两轮输出状态");
    QCOMPARE(sinkOutputPresent.at(0), true);   // 第一轮：Delay 透传有输出
    QCOMPARE(sinkOutputPresent.at(1), false);  // 第二轮：Delay 自身输出也必须为空
}

void IntegrationTest::testDestroyWhileRunningIsSafe()
{
    // 运行中销毁执行器：析构应唤醒阻塞节点并等线程真正退出，不得挂起（P1 #4）
    FlowScene *scene = new FlowScene();
    FlowExecutor *exec = new FlowExecutor();
    exec->setFlowName(QStringLiteral("RegressionDestroy"));

    NodeBase *delay = scene->createNode(NodeBase::LOGIC, QPointF(200, 200), QStringLiteral("Delay"));
    if (delay == nullptr) {
        delete exec;
        delete scene;
        QFAIL("无法创建延时节点");
    }
    delay->setParam(QStringLiteral("delayMs"), 5000);

    exec->setFlowScene(scene);
    exec->setFlowMode(FlowMode::SoftwareTrigger);
    exec->startExecution();
    QTest::qWait(300);

    QElapsedTimer t;
    t.start();
    delete exec;                 // 销毁：内部 stopExecution + wait
    const qint64 elapsed = t.elapsed();
    delete scene;
    QCoreApplication::processEvents();

    QVERIFY2(elapsed < 2500,
             qPrintable(QStringLiteral("运行中销毁执行器耗时 %1ms，阻塞节点未被唤醒").arg(elapsed)));
}

void IntegrationTest::testTwoExecutorsIsolation()
{
    // 多流程隔离：停止流程 A 不应影响流程 B 的阻塞等待（E3）
    FlowScene sceneA;
    FlowScene sceneB;
    FlowExecutor execA;
    FlowExecutor execB;
    execA.setFlowName(QStringLiteral("RegressionIsolationA"));
    execB.setFlowName(QStringLiteral("RegressionIsolationB"));

    NodeBase *a = sceneA.createNode(NodeBase::LOGIC, QPointF(200, 200), QStringLiteral("Delay"));
    NodeBase *b = sceneB.createNode(NodeBase::LOGIC, QPointF(200, 200), QStringLiteral("Delay"));
    QVERIFY(a != nullptr);
    QVERIFY(b != nullptr);
    a->setParam(QStringLiteral("delayMs"), 5000);
    b->setParam(QStringLiteral("delayMs"), 3000);

    execA.setFlowScene(&sceneA);
    execB.setFlowScene(&sceneB);
    execA.setFlowMode(FlowMode::SoftwareTrigger);
    execB.setFlowMode(FlowMode::SoftwareTrigger);

    execA.startExecution();
    execB.startExecution();
    QTest::qWait(200);

    execA.stopExecution();
    const bool aFinished = execA.wait(2500);
    QVERIFY2(aFinished, "停止后流程 A 未在 2.5 秒内退出（节点未使用所属执行器取消等待）");
    QVERIFY2(execB.getState() == ExecutionState::Running,
             "停止流程 A 影响了流程 B（多流程隔离失效）");
    const bool bFinished = execB.wait(5000);
    QVERIFY2(bFinished, "流程 B 未在预期时间内结束");

    execA.setFlowScene(nullptr);
    execB.setFlowScene(nullptr);
    QCoreApplication::processEvents();
}

void IntegrationTest::testScriptNodeStopCancellable()
{
    // 脚本节点阻塞等待应可被停止取消（P1 #4：默认 30s 超时不应阻塞关闭流程）
    QProcess probe;
    probe.start(QStringLiteral("python"), { QStringLiteral("-c"), QStringLiteral("pass") });
    const bool hasPython = probe.waitForStarted(3000) && probe.waitForFinished(5000)
                           && probe.exitCode() == 0;
    if (!hasPython) {
        QSKIP("本机无可用 Python，跳过脚本节点取消测试");
    }

    // 进程内强制放行（不落盘，避免影响用户配置）
    ScriptSecurityPolicy &policy = ScriptSecurityPolicy::instance();
    policy.setEnabled(true);
    policy.setRequireConfirmation(false);
    policy.setAllowedLanguages({ QStringLiteral("Python") });
    policy.setMaxExecutionMs(30000);
    policy.setAuditLogEnabled(false);
    policy.setBlockWhenElevated(false);
    policy.setSandboxEnabled(true);   // 强制走受限令牌启动路径
    FlowScene scene;
    FlowExecutor exec;
    exec.setFlowName(QStringLiteral("RegressionScriptCancel"));

    NodeBase *script = scene.createNode(NodeBase::OUTPUT, QPointF(200, 200), QStringLiteral("Script"));
    QVERIFY(script != nullptr);
    script->setParam(QStringLiteral("language"), QStringLiteral("Python"));
    script->setParam(QStringLiteral("scriptContent"),
                     QStringLiteral("import time\ntime.sleep(30)\n"));

    exec.setFlowScene(&scene);
    exec.setFlowMode(FlowMode::SoftwareTrigger);
    exec.startExecution();
    QTest::qWait(700);
    const bool runningBeforeStop = (exec.getState() == ExecutionState::Running);

    QElapsedTimer t;
    t.start();
    exec.stopExecution();
    bool finished = exec.wait(3000);
    if (!finished) {
        exec.stopExecution();
        finished = exec.wait(2000);
    }
    const qint64 stopElapsed = t.elapsed();
    exec.setFlowScene(nullptr);
    QCoreApplication::processEvents();

    if (!runningBeforeStop) {
        QSKIP(qPrintable(QStringLiteral("脚本未在本环境启动，跳过取消验证（lastOutput=%1）")
                             .arg(script->getParam(QStringLiteral("lastOutput")).toString())));
    }
    QVERIFY2(finished, "停止后执行器线程未退出");
    QVERIFY2(stopElapsed < 2000,
             qPrintable(QStringLiteral("停止后耗时 %1ms，脚本阻塞等待未被取消").arg(stopElapsed)));
}

void IntegrationTest::testExecutorReuseAcrossScenesWithLoop()
{
    // 执行器复用：先跑场景 A（无循环），再切到含循环的场景 B。
    // 切换后循环体缓存必须重建，否则循环体会被主遍历与 executeLoop 重复执行（P1）
    FlowScene sceneA;
    FlowScene sceneB;
    FlowExecutor exec;
    exec.setFlowName(QStringLiteral("RegressionSceneSwitch"));

    // --- 场景 A：单个延时节点（先让执行器缓存一份“无循环”的图信息） ---
    {
        NodeBase *a = sceneA.createNode(NodeBase::LOGIC, QPointF(200, 200), QStringLiteral("Delay"));
        QVERIFY(a != nullptr);
        a->setParam(QStringLiteral("delayMs"), 0);
        exec.setFlowScene(&sceneA);
        exec.setFlowMode(FlowMode::SoftwareTrigger);
        exec.startExecution();
        bool ok = exec.wait(5000);
        if (!ok) {
            exec.stopExecution();
            ok = exec.wait(2000);
        }
        QVERIFY2(ok, "场景 A 未在 5 秒内结束");
    }

    // --- 场景 B：Loop(3) -> Delay（循环体） ---
    NodeBase *loop = sceneB.createNode(NodeBase::LOGIC, QPointF(100, 200), QStringLiteral("Loop"));
    NodeBase *body = sceneB.createNode(NodeBase::LOGIC, QPointF(320, 200), QStringLiteral("Delay"));
    QVERIFY(loop != nullptr);
    QVERIFY(body != nullptr);
    loop->setParam(QStringLiteral("loopCount"), 3);
    body->setParam(QStringLiteral("delayMs"), 0);
    QVERIFY2(sceneB.createConnection(loop->outputPorts().first(),
                                     body->inputPorts().first(), true) != nullptr,
             "无法建立 循环->循环体 连线");

    QList<int> seenIterations;
    QList<NodeBase *> bodyRuns;
    const auto connHandle = QObject::connect(
        &exec, &FlowExecutor::nodeExecuted, &exec,
        [&](NodeBase *n, bool success) {
            if (success && n == body) {
                bodyRuns.append(n);
                seenIterations.append(loop->getParam(QStringLiteral("iteration")).toInt());
            }
        }, Qt::DirectConnection);

    exec.setFlowScene(&sceneB);     // 复用同一个执行器切换到新场景
    exec.setFlowMode(FlowMode::SoftwareTrigger);
    exec.startExecution();
    bool finished = exec.wait(10000);
    if (!finished) {
        exec.stopExecution();
        finished = exec.wait(3000);
    }
    QObject::disconnect(connHandle);
    exec.setFlowScene(nullptr);
    QCoreApplication::processEvents();

    QVERIFY2(finished, "场景 B 未在 10 秒内结束");
    QCOMPARE(bodyRuns.size(), qsizetype(3));   // 缓存未清会重复执行成 4 次
    const QList<int> expectedIterations{1, 2, 3};
    QCOMPARE(seenIterations, expectedIterations);
}

void IntegrationTest::testLoopAddedToSameSceneIsDetected()
{
    // 同一场景内新增循环节点：图缓存被置脏，但 m_cachedSortedNodes 仍是旧的
    // （节点/连线变更只置脏、不清缓存），循环体识别必须以当前场景节点为准，
    // 否则新增的循环体不会被登记，会被主遍历重复执行（P1）
    FlowScene scene;
    FlowExecutor exec;
    exec.setFlowName(QStringLiteral("RegressionLoopAddedLater"));

    // 首轮：场景内只有延时节点，先缓存一份“不含循环”的图
    NodeBase *first = scene.createNode(NodeBase::LOGIC, QPointF(200, 200), QStringLiteral("Delay"));
    QVERIFY(first != nullptr);
    first->setParam(QStringLiteral("delayMs"), 0);

    exec.setFlowScene(&scene);
    exec.setFlowMode(FlowMode::SoftwareTrigger);
    exec.startExecution();
    bool ok = exec.wait(5000);
    if (!ok) {
        exec.stopExecution();
        ok = exec.wait(2000);
    }
    QVERIFY2(ok, "首轮执行未在 5 秒内结束");

    // 同场景内新增 Loop(3) -> Delay（循环体），不重新 setFlowScene
    NodeBase *loop = scene.createNode(NodeBase::LOGIC, QPointF(420, 200), QStringLiteral("Loop"));
    NodeBase *body = scene.createNode(NodeBase::LOGIC, QPointF(620, 200), QStringLiteral("Delay"));
    QVERIFY(loop != nullptr);
    QVERIFY(body != nullptr);
    loop->setParam(QStringLiteral("loopCount"), 3);
    body->setParam(QStringLiteral("delayMs"), 0);
    QVERIFY2(scene.createConnection(loop->outputPorts().first(),
                                    body->inputPorts().first(), true) != nullptr,
             "无法建立 循环->循环体 连线");

    QList<int> seenIterations;
    QList<NodeBase *> bodyRuns;
    const auto connHandle = QObject::connect(
        &exec, &FlowExecutor::nodeExecuted, &exec,
        [&](NodeBase *n, bool success) {
            if (success && n == body) {
                bodyRuns.append(n);
                seenIterations.append(loop->getParam(QStringLiteral("iteration")).toInt());
            }
        }, Qt::DirectConnection);

    exec.startExecution();
    bool finished = exec.wait(10000);
    if (!finished) {
        exec.stopExecution();
        finished = exec.wait(3000);
    }
    QObject::disconnect(connHandle);
    exec.setFlowScene(nullptr);
    QCoreApplication::processEvents();

    QVERIFY2(finished, "第二轮未在 10 秒内结束");
    QCOMPARE(bodyRuns.size(), qsizetype(3));   // 漏登记会重复执行成 4 次
    const QList<int> expectedIterations{1, 2, 3};
    QCOMPARE(seenIterations, expectedIterations);
}

void IntegrationTest::testScriptNodeReportsSuccess()
{
    // 脚本节点正常跑完应报告成功并返回解释器输出；
    // 否则默认的“失败时停止”会把整条流程误停
    QProcess probe;
    probe.start(QStringLiteral("python"), { QStringLiteral("-c"), QStringLiteral("pass") });
    if (!(probe.waitForStarted(3000) && probe.waitForFinished(5000) && probe.exitCode() == 0)) {
        QSKIP("本机无可用 Python，跳过脚本节点成功状态测试");
    }

    ScriptSecurityPolicy &policy = ScriptSecurityPolicy::instance();
    policy.setEnabled(true);
    policy.setRequireConfirmation(false);
    policy.setAllowedLanguages({ QStringLiteral("Python") });
    policy.setMaxExecutionMs(30000);
    policy.setAuditLogEnabled(false);
    policy.setBlockWhenElevated(false);
    policy.setSandboxEnabled(true);   // 强制走受限令牌启动路径

    FlowScene scene;
    FlowExecutor exec;
    exec.setFlowName(QStringLiteral("RegressionScriptSuccess"));

    NodeBase *script = scene.createNode(NodeBase::OUTPUT, QPointF(200, 200), QStringLiteral("Script"));
    QVERIFY(script != nullptr);
    script->setParam(QStringLiteral("language"), QStringLiteral("Python"));
    script->setParam(QStringLiteral("scriptContent"), QStringLiteral("print('vfp-script-ok')\n"));

    exec.setFlowScene(&scene);
    exec.setFlowMode(FlowMode::SoftwareTrigger);
    exec.startExecution();
    bool finished = exec.wait(15000);
    if (!finished) {
        exec.stopExecution();
        finished = exec.wait(3000);
    }
    const bool nodeOk = script->executionSuccess();
    const QString lastOutput = script->getParam(QStringLiteral("lastOutput")).toString().trimmed();
    exec.setFlowScene(nullptr);
    QCoreApplication::processEvents();

    QVERIFY2(finished, "脚本流程未在 15 秒内结束");
    QVERIFY2(nodeOk, "脚本正常执行完却报告失败（moduleStatus 未置位，会误停流程）");
    QCOMPARE(lastOutput, QStringLiteral("vfp-script-ok"));
}

void IntegrationTest::testScriptNodeLowIntegrityContainment()
{
    // 低完整性隔离契约（数据来源：tests/restricted_token_probe.cpp 的组合矩阵）：
    //   1) 令牌自检必须如实报告 integrity=Low；
    //   2) 脚本不得写入 Medium 完整性的用户目录（否则仍能改动用户文件/持久化）；
    //   3) 依赖 tempfile 的脚本必须能在私有沙箱目录里正常读写（否则隔离会破坏正常用法）；
    //   4) 沙箱目录在执行结束后必须被清理。
    QProcess probe;
    probe.start(QStringLiteral("python"), { QStringLiteral("-c"), QStringLiteral("pass") });
    const bool hasPython = probe.waitForStarted(3000) && probe.waitForFinished(5000)
                           && probe.exitCode() == 0;
    if (!hasPython) {
        QSKIP("本机无可用 Python，跳过低完整性隔离测试");
    }

    ScriptSecurityPolicy &policy = ScriptSecurityPolicy::instance();
    policy.setEnabled(true);
    policy.setRequireConfirmation(false);
    policy.setSandboxEnabled(true);
    policy.setAuditLogEnabled(false);

    QString out;
    QString errOut;
    QString error;
    int code = -1;

    // 1) 自检必须显示低完整性（不夸大也不隐瞒）
    const QString selfCheck = policy.restrictedTokenSelfCheck();
    QVERIFY2(selfCheck.contains(QStringLiteral("integrity=Low")), qPrintable(selfCheck));

    // 2) 写入用户临时目录（Medium）→ 必须被拒（既非 0 退出，也不留下文件）
    const QString outside = QDir::tempPath() + QStringLiteral("/vfp_containment_probe.txt");
    QFile::remove(outside);
    const QString writeOutside = QStringLiteral("open(r'%1','w').write('x')")
                                     .arg(QDir::toNativeSeparators(outside));
    const bool started = policy.runWithRestrictedToken(
        QStringLiteral("python"), { QStringLiteral("-c"), writeOutside },
        20000, []() { return false; }, out, errOut, error, &code);
    QVERIFY2(started, qPrintable(error));
    QVERIFY2(code != 0,
             qPrintable(QStringLiteral("低完整性下仍成功写入用户目录（隔离失效）：out=%1 err=%2")
                            .arg(out, errOut)));
    QVERIFY2(!QFile::exists(outside), "低完整性下仍在用户目录留下了文件（隔离失效）");

    // 3) 私有沙箱临时目录：tempfile 可用，且路径确实指向沙箱
    const QString tempfileProbe = QStringLiteral(
        "import tempfile,os;p=os.path.join(tempfile.gettempdir(),'x.txt');"
        "open(p,'w').write('ok');print('tempfile-ok',tempfile.gettempdir())");
    const bool ok = policy.runWithRestrictedToken(
        QStringLiteral("python"), { QStringLiteral("-c"), tempfileProbe },
        20000, []() { return false; }, out, errOut, error, &code);
    QVERIFY2(ok, qPrintable(QStringLiteral("%1 / %2").arg(error, errOut)));
    QCOMPARE(code, 0);
    QVERIFY2(out.contains(QStringLiteral("tempfile-ok")), qPrintable(out));
    QVERIFY2(out.contains(QStringLiteral("vfp-script-sandbox")),
             qPrintable(QStringLiteral("TEMP 未指向私有沙箱目录：%1").arg(out)));

    // 4) 执行结束后沙箱目录必须被清理
    const QDir sandboxRoot(QDir::tempPath() + QStringLiteral("/vfp-script-sandbox"));
    const QStringList leftovers = sandboxRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QVERIFY2(leftovers.isEmpty(),
             qPrintable(QStringLiteral("沙箱临时目录未清理：%1").arg(leftovers.join(QStringLiteral(", ")))));
}

void IntegrationTest::testConditionalBranchSkipClearsStaleOutput()
{
    // 条件分支：未选中分支的节点必须被跳过，且其上一轮输出要清空（P2）
    FlowScene scene;
    FlowExecutor exec;
    exec.setFlowName(QStringLiteral("RegressionBranchSkip"));

    NodeBase *cond = scene.createNode(NodeBase::LOGIC, QPointF(100, 200), QStringLiteral("If-Else"));
    NodeBase *trueB = scene.createNode(NodeBase::LOGIC, QPointF(340, 120), QStringLiteral("Formula"));
    NodeBase *falseB = scene.createNode(NodeBase::LOGIC, QPointF(340, 300), QStringLiteral("Formula"));
    QVERIFY(cond != nullptr);
    QVERIFY(trueB != nullptr);
    QVERIFY(falseB != nullptr);
    cond->setParam(QStringLiteral("condition"), true);
    trueB->setParam(QStringLiteral("expression"), QStringLiteral("1 + 2"));
    falseB->setParam(QStringLiteral("expression"), QStringLiteral("2 + 3"));

    QVERIFY2(cond->outputPorts().size() >= 3, "条件节点端口不足（需 TRUE/FALSE 分支端口）");
    QVERIFY2(!trueB->inputPorts().isEmpty() && !falseB->inputPorts().isEmpty(), "分支节点无输入端口");
    // 输出端口 1 = TRUE 分支，端口 2 = FALSE 分支
    QVERIFY2(scene.createConnection(cond->outputPorts().value(1), trueB->inputPorts().first(), true) != nullptr,
             "无法建立 TRUE 分支连线");
    QVERIFY2(scene.createConnection(cond->outputPorts().value(2), falseB->inputPorts().first(), true) != nullptr,
             "无法建立 FALSE 分支连线");

    int trueRuns = 0;
    int falseRuns = 0;
    int rounds = 0;
    const auto execHandle = QObject::connect(
        &exec, &FlowExecutor::nodeExecuted, &exec,
        [&](NodeBase *n, bool success) {
            if (!success) return;
            if (n == trueB) ++trueRuns;
            if (n == falseB) ++falseRuns;
            if (n == cond) {
                ++rounds;
                if (rounds == 1) {
                    // 第二轮改走 FALSE 分支，使 TRUE 分支被跳过
                    cond->setParam(QStringLiteral("condition"), false);
                }
            }
        }, Qt::DirectConnection);
    // 跑满两轮后再停止（避免中途停止导致跳过节点未被处理）
    const auto finishHandle = QObject::connect(
        &exec, &FlowExecutor::executionFinished, &exec,
        [&]() {
            if (rounds >= 2) exec.stopExecution();
        }, Qt::DirectConnection);

    // 三态可视化：TRUE 分支第二轮被跳过 → 必须发 nodeSkipped（结果面板据此标"跳过"）
    int trueSkipped = 0;
    const auto skipHandle = QObject::connect(
        &exec, &FlowExecutor::nodeSkipped, &exec,
        [&](NodeBase *n, const QString &reason) {
            if (n == trueB && !reason.isEmpty()) ++trueSkipped;
        }, Qt::DirectConnection);

    exec.setFlowScene(&scene);
    exec.setFlowMode(FlowMode::Continuous);
    exec.startExecution();
    bool finished = exec.wait(5000);
    if (!finished) {
        exec.stopExecution();
        finished = exec.wait(2000);
    }
    QObject::disconnect(execHandle);
    QObject::disconnect(finishHandle);
    QObject::disconnect(skipHandle);
    QVERIFY2(trueSkipped >= 1, "未选中分支的节点必须发 nodeSkipped（结果面板据此标\"跳过\"）");

    const bool trueOutputAfter = (trueB->getOutputData(0) != nullptr);
    const bool falseOutputAfter = (falseB->getOutputData(0) != nullptr);
    exec.setFlowScene(nullptr);
    QCoreApplication::processEvents();

    QVERIFY2(finished, "分支流程未在 5 秒内结束");
    QCOMPARE(trueRuns, 1);              // TRUE 分支只在第一轮执行
    QCOMPARE(falseRuns, 1);             // FALSE 分支只在第二轮执行
    QCOMPARE(trueOutputAfter, false);   // 第二轮被跳过：输出必须清空，不能残留第一轮结果
    QCOMPARE(falseOutputAfter, true);   // 第二轮实际执行的分支有输出
}

void IntegrationTest::testParamRefResolvesEachRound()
{
    // E1：参数引用 {模块号} 跨轮必须重新解析上游最新值；且表达式不得被写回成常量。
    // 若修复缺失：第一轮解析出 7+1=8 后，引用值被 setParam 永久写回成字面量，
    // 第二轮即使上游改成 9，下游仍按 8 跑——且存方案会把引用直接存成常量。
    FlowScene scene;
    FlowExecutor exec;
    exec.setFlowName(QStringLiteral("RegressionParamRefE1"));

    NodeBase *source = scene.createNode(NodeBase::LOGIC, QPointF(120, 200), QStringLiteral("Formula"));
    NodeBase *consumer = scene.createNode(NodeBase::LOGIC, QPointF(340, 200), QStringLiteral("Formula"));
    QVERIFY(source != nullptr);
    QVERIFY(consumer != nullptr);
    source->setParam(QStringLiteral("expression"), QStringLiteral("7"));
    // 下游引用源模块号（缺省取 value）；连一条线保证源先于下游执行
    const QString refExpr = QStringLiteral("{%1} + 1").arg(source->moduleId());
    consumer->setParam(QStringLiteral("expression"), refExpr);
    QVERIFY2(scene.createConnection(source->outputPorts().first(),
                                    consumer->inputPorts().first(), true) != nullptr,
             "无法建立 源->下游 连线（保证源先执行）");

    QList<double> consumerValues;
    int rounds = 0;
    const auto outHandle = QObject::connect(
        &exec, &FlowExecutor::nodeOutputsUpdated, &exec,
        [&](NodeBase *n, bool ok, qint64, const QVariantMap &vars) {
            if (ok && n == consumer) {
                consumerValues.append(vars.value(QStringLiteral("value")).toDouble());
                ++rounds;
                if (rounds == 1) {
                    // 第一轮结束后改源值：引用必须重新解析成 9（而非沿用上一轮解析出的 7）
                    source->setParam(QStringLiteral("expression"), QStringLiteral("9"));
                }
            }
        }, Qt::DirectConnection);
    const auto finishHandle = QObject::connect(
        &exec, &FlowExecutor::executionFinished, &exec,
        [&]() { if (rounds >= 2) exec.stopExecution(); }, Qt::DirectConnection);

    exec.setFlowScene(&scene);
    exec.setFlowMode(FlowMode::Continuous);
    exec.startExecution();
    bool finished = exec.wait(5000);
    if (!finished) {
        exec.stopExecution();
        finished = exec.wait(2000);
    }
    QObject::disconnect(outHandle);
    QObject::disconnect(finishHandle);

    // E1 核心断言 1：跨轮重新解析——第一轮 7+1=8，第二轮 9+1=10（不是沿用 8）
    const bool exprPreserved = consumer->getParam(QStringLiteral("expression"))
                                   .toString().contains(QLatin1Char('{'));
    exec.setFlowScene(nullptr);
    QCoreApplication::processEvents();

    QVERIFY2(finished, "参数引用流程未在 5 秒内结束");
    QVERIFY2(consumerValues.size() >= 2,
             qPrintable(QStringLiteral("未观察到两轮执行：%1").arg(consumerValues.size())));
    QCOMPARE(consumerValues.at(0), 8.0);    // 第一轮：7 + 1
    QCOMPARE(consumerValues.at(1), 10.0);   // 第二轮：9 + 1（重新解析，不是沿用 8）
    QVERIFY2(exprPreserved, "参数引用表达式被写回成常量（E1：应保留 {模块号} 引用）");
}

void IntegrationTest::testNestedLoopIterations()
{
    // 嵌套循环：外层 2 次 × 内层 3 次 → 最内层节点执行 6 次，
    // 内层迭代号 1,2,3 循环两遍，外层迭代号 1,1,1,2,2,2
    FlowScene scene;
    FlowExecutor exec;
    exec.setFlowName(QStringLiteral("RegressionNestedLoop"));

    NodeBase *outer = scene.createNode(NodeBase::LOGIC, QPointF(80, 200), QStringLiteral("Loop"));
    NodeBase *inner = scene.createNode(NodeBase::LOGIC, QPointF(280, 200), QStringLiteral("Loop"));
    NodeBase *body = scene.createNode(NodeBase::LOGIC, QPointF(480, 200), QStringLiteral("Delay"));
    QVERIFY(outer != nullptr);
    QVERIFY(inner != nullptr);
    QVERIFY(body != nullptr);
    outer->setParam(QStringLiteral("loopCount"), 2);
    inner->setParam(QStringLiteral("loopCount"), 3);
    body->setParam(QStringLiteral("delayMs"), 0);

    QVERIFY2(scene.createConnection(outer->outputPorts().first(), inner->inputPorts().first(), true) != nullptr,
             "无法建立 外层->内层 连线");
    QVERIFY2(scene.createConnection(inner->outputPorts().first(), body->inputPorts().first(), true) != nullptr,
             "无法建立 内层->循环体 连线");

    QList<int> innerIters;
    QList<int> outerIters;
    int bodyRuns = 0;
    const auto connHandle = QObject::connect(
        &exec, &FlowExecutor::nodeExecuted, &exec,
        [&](NodeBase *n, bool success) {
            if (success && n == body) {
                ++bodyRuns;
                innerIters.append(inner->getParam(QStringLiteral("iteration")).toInt());
                outerIters.append(outer->getParam(QStringLiteral("iteration")).toInt());
            }
        }, Qt::DirectConnection);

    exec.setFlowScene(&scene);
    exec.setFlowMode(FlowMode::SoftwareTrigger);
    exec.startExecution();
    bool finished = exec.wait(10000);
    if (!finished) {
        exec.stopExecution();
        finished = exec.wait(3000);
    }
    QObject::disconnect(connHandle);
    exec.setFlowScene(nullptr);
    QCoreApplication::processEvents();

    QVERIFY2(finished, "嵌套循环流程未在 10 秒内结束");
    QCOMPARE(bodyRuns, 6);
    const QList<int> expectedInner{1, 2, 3, 1, 2, 3};
    const QList<int> expectedOuter{1, 1, 1, 2, 2, 2};
    QCOMPARE(innerIters, expectedInner);
    QCOMPARE(outerIters, expectedOuter);
}

void IntegrationTest::testRuntimeStatsCounters()
{
    // 运行期统计：轮次 / 节点执行与失败计数 / 进程资源采样（现场长跑观测的基础）
    // --- 阶段 1：正常流程跑两轮，校验执行计数 ---
    {
        FlowScene scene;
        FlowExecutor exec;
        exec.setFlowName(QStringLiteral("RegressionStatsOk"));
        exec.setStatsLogIntervalMs(0);   // 测试内不输出统计日志

        NodeBase *formula = scene.createNode(NodeBase::LOGIC, QPointF(120, 200), QStringLiteral("Formula"));
        NodeBase *sink = scene.createNode(NodeBase::LOGIC, QPointF(340, 200), QStringLiteral("Delay"));
        QVERIFY(formula != nullptr);
        QVERIFY(sink != nullptr);
        formula->setParam(QStringLiteral("expression"), QStringLiteral("1 + 2"));
        sink->setParam(QStringLiteral("delayMs"), 0);
        QVERIFY2(scene.createConnection(formula->outputPorts().first(),
                                        sink->inputPorts().first(), true) != nullptr,
                 "无法建立 公式->下游 连线");

        int rounds = 0;
        const auto h = QObject::connect(
            &exec, &FlowExecutor::executionFinished, &exec,
            [&]() { if (++rounds >= 2) exec.stopExecution(); }, Qt::DirectConnection);

        exec.resetRuntimeStats();
        exec.setFlowScene(&scene);
        exec.setFlowMode(FlowMode::Continuous);
        exec.startExecution();
        bool finished = exec.wait(5000);
        if (!finished) {
            exec.stopExecution();
            finished = exec.wait(2000);
        }
        QObject::disconnect(h);

        const FlowRuntimeStats stats = exec.runtimeStats();
        const QString sinkName = sink->fullName();
        const QString summary = stats.summary();
        exec.setFlowScene(nullptr);
        QCoreApplication::processEvents();

        QVERIFY2(finished, "统计流程未在 5 秒内结束");
        QVERIFY2(stats.rounds >= 2, qPrintable(QStringLiteral("轮次统计不足：%1").arg(stats.rounds)));
        QVERIFY2(stats.nodes.contains(sinkName), "缺少下游节点统计");
        QCOMPARE(stats.nodes.value(sinkName).executions, quint64(2));
        QCOMPARE(stats.nodes.value(sinkName).failures, quint64(0));
        QCOMPARE(stats.failedRounds, quint64(0));
        QVERIFY2(stats.maxRoundMs >= stats.lastRoundMs, "单轮最大耗时应不小于最近一轮耗时");
        QVERIFY2(!summary.isEmpty(), "统计摘要不应为空");
    }

    // --- 阶段 2：失败流程（节点无输入必然失败），校验失败计数与进程资源采样 ---
    {
        FlowScene scene;
        FlowExecutor exec;
        exec.setFlowName(QStringLiteral("RegressionStatsFail"));
        exec.setStatsLogIntervalMs(0);

        NodeBase *bad = scene.createNode(NodeBase::IMAGE_PROCESSING, QPointF(200, 200),
                                         QStringLiteral("OpenCV二值化"));
        QVERIFY(bad != nullptr);

        int rounds = 0;
        const auto h = QObject::connect(
            &exec, &FlowExecutor::executionFinished, &exec,
            [&]() { if (++rounds >= 2) exec.stopExecution(); }, Qt::DirectConnection);

        exec.resetRuntimeStats();
        exec.setStopOnFailure(false);   // 允许连续两轮以便观察失败累计
        exec.setFlowScene(&scene);
        exec.setFlowMode(FlowMode::Continuous);
        exec.startExecution();
        bool finished = exec.wait(5000);
        if (!finished) {
            exec.stopExecution();
            finished = exec.wait(2000);
        }
        QObject::disconnect(h);

        const FlowRuntimeStats stats = exec.runtimeStats();
        const QString badName = bad->fullName();
        const QString summary = stats.summary();
        exec.setFlowScene(nullptr);
        QCoreApplication::processEvents();

        QVERIFY2(finished, "失败统计流程未在 5 秒内结束");
        QVERIFY2(stats.nodes.contains(badName), "缺少失败节点统计");
        QCOMPARE(stats.nodes.value(badName).failures, quint64(2));
        QCOMPARE(stats.failedRounds, quint64(2));
        QVERIFY2(stats.processHandleCount > 0, "进程句柄数采样为 0");
        QVERIFY2(stats.processWorkingSetBytes > 0, "进程内存采样为 0");
        QVERIFY2(summary.contains(QStringLiteral("handles=")), "摘要缺少进程资源字段");
        QVERIFY2(summary.contains(QStringLiteral("failRounds=2")), "摘要缺少失败轮次字段");
    }
}

void IntegrationTest::testRestrictedTokenLaunch()
{
#if !defined(Q_OS_WIN)
    QSKIP("受限令牌启动仅 Windows 支持");
#else
    ScriptSecurityPolicy &policy = ScriptSecurityPolicy::instance();
    policy.setEnabled(true);
    policy.setSandboxEnabled(true);
    policy.setAuditLogEnabled(false);

    // --- 1) 启动 + 输出捕获 + 降权确证（去特权）---
    // 用 whoami /priv 判定：特权名始终是英文，与系统显示语言无关。
    // 受限令牌应已剥离全部特权（DISABLE_MAX_PRIVILEGE），只剩 SeChangeNotifyPrivilege。
    QString out, errOut, error;
    int code = -1;
    const bool ok = policy.runWithRestrictedToken(
        QStringLiteral("cmd"),
        { QStringLiteral("/c"), QStringLiteral("whoami /priv") },
        // 预算放宽到 30s：本用例全部断言都是墙钟时间，机器被后台构建占满时 15s 也可能不够，
        // 会造成"高负载下偶发红"（复审连续数轮在后台构建时观察到 fail，单跑与空闲全量跑均绿）。
        30000,
        []() { return false; },
        out, errOut, error, &code);

    QVERIFY2(policy.hasRestrictedToken(), "受限令牌未能创建");
    // 令牌自检（可作为上线验收凭据）：去特权后应只剩 SeChangeNotifyPrivilege（privileges=1）
    const QString selfCheck = policy.restrictedTokenSelfCheck();
    QVERIFY2(selfCheck.contains(QStringLiteral("privileges=1")), qPrintable(selfCheck));

    QVERIFY2(ok, qPrintable(QStringLiteral("受限令牌启动失败：%1（stderr=%2）").arg(error, errOut)));
    QCOMPARE(code, 0);
    QVERIFY2(out.contains(QStringLiteral("SeChangeNotifyPrivilege")),
             qPrintable(QStringLiteral("子进程未正常执行：%1").arg(out.left(300))));
    QVERIFY2(!out.contains(QStringLiteral("SeDebugPrivilege")),
             qPrintable(QStringLiteral("受限令牌仍保留调试特权（去特权失败）：%1").arg(out.left(400))));
    QVERIFY2(!out.contains(QStringLiteral("SeBackupPrivilege")),
             qPrintable(QStringLiteral("受限令牌仍保留备份特权（去特权失败）：%1").arg(out.left(400))));

    // --- 2) 取消：长命令应被立即终止 ---
    QElapsedTimer cancelTimer;
    cancelTimer.start();
    QString out2, err2, error2;
    int code2 = -1;
    const bool ok2 = policy.runWithRestrictedToken(
        QStringLiteral("cmd"),
        { QStringLiteral("/c"), QStringLiteral("ping -n 30 127.0.0.1 > nul") },
        30000,
        [&cancelTimer]() { return cancelTimer.elapsed() > 400; },
        out2, err2, error2, &code2);
    const qint64 cancelElapsed = cancelTimer.elapsed();
    QVERIFY2(!ok2, "被取消的脚本不应报告成功");
    QVERIFY2(error2.contains(QStringLiteral("取消")),
             qPrintable(QStringLiteral("错误信息未标注取消：%1").arg(error2)));
    QVERIFY2(cancelElapsed < 15000,
             qPrintable(QStringLiteral("取消耗时 %1ms，未及时终止").arg(cancelElapsed)));

    // --- 3) 超时：到点必须终止 ---
    QString out3, err3, error3;
    int code3 = -1;
    QElapsedTimer timeoutTimer;
    timeoutTimer.start();
    const bool ok3 = policy.runWithRestrictedToken(
        QStringLiteral("cmd"),
        { QStringLiteral("/c"), QStringLiteral("ping -n 30 127.0.0.1 > nul") },
        700,
        []() { return false; },
        out3, err3, error3, &code3);
    const qint64 timeoutElapsed = timeoutTimer.elapsed();
    QVERIFY2(!ok3, "超时脚本不应报告成功");
    QVERIFY2(error3.contains(QStringLiteral("超时")),
             qPrintable(QStringLiteral("错误信息未标注超时：%1").arg(error3)));
    QVERIFY2(timeoutElapsed < 15000,
             qPrintable(QStringLiteral("超时耗时 %1ms").arg(timeoutElapsed)));
#endif
}

void IntegrationTest::testEndToEndPipelineSmoke()
{
    // 端到端冒烟：真实图像文件 → 读取图像(HALCON 解码) → OpenCV 二值化 → 独立复核像素结果。
    // 不依赖相机/PLC，可在 CI 上稳定复现；覆盖 HALCON↔OpenCV 图像桥接这一最易出问题的接缝。
    QTemporaryDir tmpDir;
    QVERIFY2(tmpDir.isValid(), "无法创建临时目录");
    const QString imagePath = tmpDir.filePath(QStringLiteral("vfp_smoke.png"));

    // 夹具：200x200 全黑底 + 居中 60x60 白色方块（白像素数应为 3600）
    const int imgW = 200;
    const int imgH = 200;
    const int sqSize = 60;
    const int expectedWhite = sqSize * sqSize;
    {
        HObject blank;
        HObject rect;
        HObject painted;
        GenImageConst(&blank, "byte", imgW, imgH);
        const int r1 = (imgH - sqSize) / 2;
        const int c1 = (imgW - sqSize) / 2;
        GenRectangle1(&rect, r1, c1, r1 + sqSize - 1, c1 + sqSize - 1);
        PaintRegion(rect, blank, &painted, 255, "fill");
        WriteImage(painted, "png", 0, imagePath.toStdString().c_str());
    }
    QVERIFY2(QFile::exists(imagePath), "测试图像未生成");

    FlowScene scene;
    FlowExecutor exec;
    exec.setFlowName(QStringLiteral("RegressionEndToEnd"));

    NodeBase *reader = scene.createNode(NodeBase::IMAGE_ACQUISITION, QPointF(120, 200),
                                        QStringLiteral("读取图像"));
    NodeBase *threshold = scene.createNode(NodeBase::IMAGE_PROCESSING, QPointF(340, 200),
                                           QStringLiteral("OpenCV二值化"));
    QVERIFY(reader != nullptr);
    QVERIFY(threshold != nullptr);
    reader->setParam(QStringLiteral("filePath"), imagePath);
    threshold->setParam(QStringLiteral("mode"), 0);      // 固定阈值
    threshold->setParam(QStringLiteral("minVal"), 128);
    QVERIFY2(scene.createConnection(reader->outputPorts().first(),
                                    threshold->inputPorts().first(), true) != nullptr,
             "无法建立 读取图像->二值化 连线");

    exec.setFlowScene(&scene);
    exec.setFlowMode(FlowMode::SoftwareTrigger);

    QList<bool> readerRuns;
    QList<bool> thresholdRuns;
    const auto connHandle = QObject::connect(
        &exec, &FlowExecutor::nodeExecuted, &exec,
        [&](NodeBase *n, bool ok) {
            if (n == reader) readerRuns.append(ok);
            if (n == threshold) thresholdRuns.append(ok);
        }, Qt::DirectConnection);

    // 结果表数据面：节点执行后应推出该模块的输出变量快照（结果面板即以此更新）
    int outputsEmits = 0;
    bool outputsOk = false;
    QVariantMap outputsVars;
    const auto outputsHandle = QObject::connect(
        &exec, &FlowExecutor::nodeOutputsUpdated, &exec,
        [&](NodeBase *n, bool ok, qint64, const QVariantMap &vars) {
            if (n != threshold) return;
            ++outputsEmits;
            outputsOk = ok;
            outputsVars = vars;
        }, Qt::DirectConnection);

    auto runOnce = [&exec]() -> bool {
        exec.startExecution();
        bool finished = exec.wait(10000);
        if (!finished) {
            exec.stopExecution();
            finished = exec.wait(3000);
        }
        return finished;
    };

    QVERIFY2(runOnce(), "端到端流程第一轮未在 10 秒内结束");
    QVERIFY2(runOnce(), "端到端流程第二轮未在 10 秒内结束");
    QObject::disconnect(connHandle);

    const int readerW = reader->getParam(QStringLiteral("imageWidth")).toInt();
    const int readerH = reader->getParam(QStringLiteral("imageHeight")).toInt();
    const int fgPixels = threshold->getParam(QStringLiteral("foregroundPixels")).toInt();
    const bool thresholdSuccess = threshold->executionSuccess();

    // 从二值图输出独立复核白像素数（不依赖节点内部计数）
    int measuredWhite = -1;
    QSharedPointer<DataObject> binObj = threshold->getOutputData(1);
    if (binObj && binObj->getHImage().IsInitialized()) {
        HObject region;
        HTuple area, row, col;
        Threshold(binObj->getHImage(), &region, 128, 255);
        AreaCenter(region, &area, &row, &col);
        measuredWhite = area.I();
    }

    exec.setFlowScene(nullptr);
    QCoreApplication::processEvents();

    QVERIFY2(readerRuns.size() >= 2 && thresholdRuns.size() >= 2,
             qPrintable(QStringLiteral("未观察到两轮执行：读取=%1 二值化=%2")
                            .arg(readerRuns.size()).arg(thresholdRuns.size())));
    QCOMPARE(readerRuns.at(0), true);
    QCOMPARE(thresholdRuns.at(0), true);
    QCOMPARE(readerW, imgW);                 // HALCON 确实解码了该文件
    QCOMPARE(readerH, imgH);
    QVERIFY2(thresholdSuccess, "二值化节点报告失败");
    QCOMPARE(fgPixels, expectedWhite);       // 节点内部计数
    QCOMPARE(measuredWhite, expectedWhite);  // 独立复核输出图像

    // 结果表数据面：两轮各推一次，且带本轮输出项（面板据此显示数值，不能是空快照）
    QVERIFY2(outputsEmits >= 2, "执行时未推出输出变量快照（nodeOutputsUpdated）");
    QCOMPARE(outputsOk, true);
    QCOMPARE(outputsVars.value(QStringLiteral("foregroundPixels")).toInt(), expectedWhite);
}

void IntegrationTest::testRecomputeDownstreamOnly()
{
    // 「参数改动后只重算下游」：作废本算子及下游缓存 → 只重跑这一段链路，
    // 无关分支不得被重跑（否则大流程里改一个参数会牵动整张图）。
    QTemporaryDir tmpDir;
    QVERIFY2(tmpDir.isValid(), "无法创建临时目录");
    const QString imageA = tmpDir.filePath(QStringLiteral("vfp_downstream_a.png"));
    const QString imageB = tmpDir.filePath(QStringLiteral("vfp_downstream_b.png"));

    const int sizeA = 200;
    const int squareA = 60;   // 期望白像素 3600
    const int sizeB = 120;
    const int squareB = 50;   // 期望白像素 2500
    auto makeImage = [](const QString &path, int size, int square) {
        HObject blank;
        HObject rect;
        HObject painted;
        GenImageConst(&blank, "byte", size, size);
        const int r1 = (size - square) / 2;
        GenRectangle1(&rect, r1, r1, r1 + square - 1, r1 + square - 1);
        PaintRegion(rect, blank, &painted, 255, "fill");
        WriteImage(painted, "png", 0, path.toStdString().c_str());
    };
    makeImage(imageA, sizeA, squareA);
    makeImage(imageB, sizeB, squareB);

    FlowScene scene;
    FlowExecutor exec;
    exec.setFlowName(QStringLiteral("RegressionDownstreamRecompute"));

    // 链路 A：读取图像 → OpenCV二值化；链路 B：孤立读取图像（无关分支，用于证明不被重跑）
    NodeBase *readerA = scene.createNode(NodeBase::IMAGE_ACQUISITION, QPointF(120, 160),
                                         QStringLiteral("读取图像"));
    NodeBase *thresholdA = scene.createNode(NodeBase::IMAGE_PROCESSING, QPointF(340, 160),
                                            QStringLiteral("OpenCV二值化"));
    NodeBase *readerB = scene.createNode(NodeBase::IMAGE_ACQUISITION, QPointF(120, 380),
                                         QStringLiteral("读取图像"));
    QVERIFY(readerA != nullptr);
    QVERIFY(thresholdA != nullptr);
    QVERIFY(readerB != nullptr);
    readerA->setParam(QStringLiteral("filePath"), imageA);
    readerB->setParam(QStringLiteral("filePath"), imageB);
    thresholdA->setParam(QStringLiteral("mode"), 0);      // 固定阈值
    thresholdA->setParam(QStringLiteral("minVal"), 128);
    QVERIFY2(scene.createConnection(readerA->outputPorts().first(),
                                    thresholdA->inputPorts().first(), true) != nullptr,
             "无法建立 读取图像->二值化 连线");

    exec.setFlowScene(&scene);
    exec.setFlowMode(FlowMode::SoftwareTrigger);

    QHash<NodeBase *, int> runs;
    const auto connHandle = QObject::connect(
        &exec, &FlowExecutor::nodeExecuted, &exec,
        [&](NodeBase *n, bool) { runs[n] = runs.value(n) + 1; }, Qt::DirectConnection);

    // 首轮全量执行，建立缓存与基线结果
    exec.startExecution();
    bool finished = exec.wait(10000);
    if (!finished) {
        exec.stopExecution();
        finished = exec.wait(3000);
    }
    QVERIFY2(finished, "首轮未在 10 秒内结束");
    QCOMPARE(thresholdA->getParam(QStringLiteral("foregroundPixels")).toInt(), squareA * squareA);

    // 基线：首轮全量执行时三条链路各执行一次（含无关分支 readerB）——
    // 否则下面 readerB==0 的断言会退化成"它本来就不跑"，测不出任何东西。
    QCOMPARE(runs.value(readerA), 1);
    QCOMPARE(runs.value(thresholdA), 1);
    QCOMPARE(runs.value(readerB), 1);

    // 改上游参数（换成另一张图）→ 只重算本算子及其下游
    runs.clear();
    readerA->setParam(QStringLiteral("filePath"), imageB);
    exec.invalidateDownstreamOf(readerA);
    exec.executeFrom(readerA);
    QObject::disconnect(connHandle);

    const int fg = thresholdA->getParam(QStringLiteral("foregroundPixels")).toInt();
    const int widthA = readerA->getParam(QStringLiteral("imageWidth")).toInt();
    exec.setFlowScene(nullptr);
    QCoreApplication::processEvents();

    QCOMPARE(runs.value(readerA), 1);        // 本算子重跑一次
    QCOMPARE(runs.value(thresholdA), 1);     // 下游重跑一次
    QCOMPARE(runs.value(readerB), 0);        // 无关分支不得被重跑
    QCOMPARE(widthA, sizeB);                 // 上游确实换成了第二张图
    QCOMPARE(fg, squareB * squareB);         // 下游拿到新值 → 缓存确实已作废并重算

    // 【待办·增量执行复用】此处原计划追加一段"只改下游参数 + executeUpTo"的用例，
    // 断言单图「读取图像」被跳过（runs==0）而目标节点照常重算（runs==1）。
    // 首次实现后该断言失败（readerA 仍被重跑），且执行器侧诊断显示未进入复用分支，
    // 与用例内直接调用 reusesCachedOutput() 返回 true 相矛盾，未能定位，
    // 故连同执行器分支一并回退；契约声明（NodeBase/ImageReadNode）保留备查。
}

namespace {
/// 用例结束后恢复沙箱模式与开关，避免影响同进程内其它用例
class SandboxModeGuard
{
public:
    explicit SandboxModeGuard(ScriptSecurityPolicy &policy)
        : m_policy(policy), m_mode(policy.sandboxMode()), m_enabled(policy.isSandboxEnabled())
    {
    }
    ~SandboxModeGuard()
    {
        m_policy.setSandboxMode(m_mode);
        m_policy.setSandboxEnabled(m_enabled);
    }
    SandboxModeGuard(const SandboxModeGuard &) = delete;
    SandboxModeGuard &operator=(const SandboxModeGuard &) = delete;

private:
    ScriptSecurityPolicy &m_policy;
    ScriptSecurityPolicy::SandboxMode m_mode;
    bool m_enabled;
};
} // namespace

void IntegrationTest::testAppContainerSandboxLaunch()
{
    // AppContainer 沙箱（沙箱强度 = AppContainer）：走的是产品启动路径本身
    // （ScriptSecurityPolicy::runWithRestrictedToken——名字沿用历史，两种模式共用同一条
    //  管道/超时/取消/Job 回收实现，只在"令牌 vs 容器能力"处分流）。
    // 期望：① 容器内进程能启动；② 写用户目录被拒（隔离生效）；③ 容器内解释器可运行且能 import。
    // 容器能力/限制的原始实测数据见 tests/restricted_token_probe.cpp -ac。
    auto &policy = ScriptSecurityPolicy::instance();

    // 默认不跑：本用例会创建 AppContainer 配置文件，并**修改解释器目录的 ACL**（追加容器 SID）。
    // 实测该授权与 Low IL 沙箱路径存在尚未完全解释清的相互影响（授权后 Low IL 下解释器会以
    // 0xC0000135 启动失败，`icacls <解释器目录> /reset` 可恢复），因此不能在默认 CI 里
    // 悄悄改动机器状态。需要复核容器能力时显式开启：set VFP_APPCONTAINER_TEST=1
    if (qEnvironmentVariableIsEmpty("VFP_APPCONTAINER_TEST")) {
        QSKIP("默认跳过（会改动解释器目录 ACL）；置 VFP_APPCONTAINER_TEST=1 显式开启");
    }

    QString error;
    if (!ScriptSecurityPolicy::ensureAppContainerProfile(&error)) {
        QSKIP(qPrintable(QStringLiteral("AppContainer 不可用（%1），跳过").arg(error)));
    }
    QVERIFY2(!ScriptSecurityPolicy::appContainerSid().isEmpty(), "配置文件已建立却拿不到 SID");

    // 解释器目录需要一次性 (RX) 授权；拿不到就跳过（而不是误报失败）
    if (!ScriptSecurityPolicy::grantInterpreterAccess(QStringLiteral("python"), &error)) {
        QSKIP(qPrintable(QStringLiteral("解释器目录授权未完成（%1），跳过").arg(error)));
    }

    const SandboxModeGuard guard(policy);   // 用例结束后恢复原模式/开关
    policy.setSandboxMode(ScriptSecurityPolicy::SandboxMode::AppContainer);
    policy.setSandboxEnabled(true);

    QString out;
    QString err;
    QString runError;
    int code = -1;
    const std::function<bool()> notCancelled = []() { return false; };

    // ① 容器内可以启动进程
    const bool started = policy.runWithRestrictedToken(
        QStringLiteral("cmd.exe"),
        { QStringLiteral("/c"), QStringLiteral("echo"), QStringLiteral("ac-probe") },
        8000, notCancelled, out, err, runError, &code);
    QVERIFY2(started, qPrintable(runError));
    QVERIFY2(out.contains(QStringLiteral("ac-probe")), qPrintable(out));

    // ② 写用户临时目录必须被拒：用解释器写（比 cmd 重定向更可控，也顺带验证解释器可用）
    const QString userFile = QDir::tempPath() + QStringLiteral("/vfp_ac_userwrite.txt");
    QFile::remove(userFile);
    out.clear(); err.clear(); runError.clear();
    policy.runWithRestrictedToken(
        QStringLiteral("python"),
        { QStringLiteral("-I"), QStringLiteral("-E"), QStringLiteral("-c"),
          QStringLiteral("open(r'%1','w').write('x');print('WROTE')").arg(userFile) },
        20000, notCancelled, out, err, runError, &code);
    QVERIFY2(!QFile::exists(userFile),
             qPrintable(QStringLiteral("AppContainer 内竟写入了用户目录，隔离失效: out=%1 err=%2")
                            .arg(out, err)));

    // ③ 解释器在容器内可运行且能 import（验证 stdlib 路径在容器内正常）
    out.clear(); err.clear(); runError.clear();
    const bool imported = policy.runWithRestrictedToken(
        QStringLiteral("python"),
        { QStringLiteral("-I"), QStringLiteral("-E"), QStringLiteral("-c"),
          QStringLiteral("import json,os;print('ac-import-ok')") },
        20000, notCancelled, out, err, runError, &code);
    QVERIFY2(imported, qPrintable(runError));
    QVERIFY2(out.contains(QStringLiteral("ac-import-ok")),
             qPrintable(QStringLiteral("容器内解释器/import 失败: out=%1 err=%2").arg(out, err)));
}

void IntegrationTest::testRuntimeInterfaceMultiPageRoundTrip()
{
    // 多页布局 JSON 往返：页名/页序/控件属性/表格列配置/IO绑定 全量保持
    RuntimeInterface layout;
    QCOMPARE(layout.pages.size(), 1);                    // 空布局保底一页

    layout.currentPageIndex = layout.addPage();
    QCOMPARE(layout.pages.size(), 2);

    RuntimeInterfacePage *p0 = layout.page(0);
    p0->pageName = QStringLiteral("检测页");
    RuntimeControl *img = p0->addControl(RuntimeControlType::ImageView, QRect(10, 10, 320, 240));
    img->bindType = QStringLiteral("node");
    img->bindKey = QStringLiteral("图像源1");

    RuntimeInterfacePage *p1 = layout.page(1);
    p1->pageName = QStringLiteral("IO页");
    RuntimeControl *tbl = p1->addControl(RuntimeControlType::ResultTable, QRect(20, 20, 460, 260));
    ResultColumn c1; c1.header = QStringLiteral("结果"); c1.bindType = QStringLiteral("global"); c1.bindKey = QStringLiteral("检测结果");
    ResultColumn c2; c2.header = QStringLiteral("数值"); c2.bindType = QStringLiteral("node");   c2.bindKey = QStringLiteral("测量1");
    tbl->columns << c1 << c2;
    tbl->maxRows = 50;

    RuntimeControl *io = p1->addControl(RuntimeControlType::IoStatus, QRect(20, 300, 260, 80));
    io->bindType = QStringLiteral("node");
    io->bindKey = QStringLiteral("相机IO控制1");

    const QJsonObject json = layout.toJson();
    RuntimeInterface loaded;
    QVERIFY(loaded.fromJson(json));
    QCOMPARE(loaded.pages.size(), 2);
    QCOMPARE(loaded.currentPageIndex, 1);
    QCOMPARE(loaded.page(0)->pageName, QStringLiteral("检测页"));
    QCOMPARE(loaded.page(1)->pageName, QStringLiteral("IO页"));
    QCOMPARE(loaded.page(0)->controls.size(), 1);
    QCOMPARE(loaded.page(1)->controls.size(), 2);

    const RuntimeControl &lt = loaded.page(1)->controls[0];
    QCOMPARE(lt.type, RuntimeControlType::ResultTable);
    QCOMPARE(lt.columns.size(), 2);
    QCOMPARE(lt.columns[0].bindKey, QStringLiteral("检测结果"));
    QCOMPARE(lt.columns[0].bindType, QStringLiteral("global"));
    QCOMPARE(lt.columns[1].bindType, QStringLiteral("node"));
    QCOMPARE(lt.columns[1].bindKey, QStringLiteral("测量1"));
    QCOMPARE(lt.maxRows, 50);

    const RuntimeControl &lio = loaded.page(1)->controls[1];
    QCOMPARE(lio.type, RuntimeControlType::IoStatus);
    QCOMPARE(lio.bindKey, QStringLiteral("相机IO控制1"));

    // 文件往返
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("ri.json"));
    QVERIFY(loaded.saveToFile(path));
    RuntimeInterface fromFile;
    QVERIFY(fromFile.loadFromFile(path));
    QCOMPARE(fromFile.pages.size(), 2);
    QCOMPARE(fromFile.totalControlCount(), 3);
}

void IntegrationTest::testRuntimeInterfaceLegacyCompat()
{
    // 旧单页格式（无 pages 键）必须零迁移加载为一页
    QJsonObject ctrl;
    ctrl["type"] = QStringLiteral("状态灯");
    ctrl["title"] = QStringLiteral("OK灯");
    ctrl["bindType"] = QStringLiteral("global");
    ctrl["bindKey"] = QStringLiteral("判定结果");
    ctrl["color"] = QStringLiteral("#3a6ea5");
    ctrl["fontSize"] = 16;
    ctrl["visible"] = true;
    QJsonObject geo; geo["x"] = 5; geo["y"] = 6; geo["w"] = 200; geo["h"] = 60;
    ctrl["geometry"] = geo;

    QJsonObject legacy;
    legacy["pageName"] = QStringLiteral("运行界面");
    legacy["controls"] = QJsonArray{ ctrl };

    RuntimeInterface loaded;
    QVERIFY(loaded.fromJson(legacy));
    QCOMPARE(loaded.pages.size(), 1);
    QCOMPARE(loaded.page(0)->pageName, QStringLiteral("运行界面"));
    QCOMPARE(loaded.page(0)->controls.size(), 1);
    QCOMPARE(loaded.page(0)->controls[0].type, RuntimeControlType::StatusLight);
    QCOMPARE(loaded.page(0)->controls[0].bindKey, QStringLiteral("判定结果"));
    QCOMPARE(loaded.page(0)->controls[0].geometry, QRect(5, 6, 200, 60));

    // 旧格式加载后再次保存应升级为多页格式且内容不变
    const QJsonObject upgraded = loaded.toJson();
    QVERIFY(upgraded.contains(QStringLiteral("pages")));
    RuntimeInterface again;
    QVERIFY(again.fromJson(upgraded));
    QCOMPARE(again.page(0)->controls.size(), 1);
    QCOMPARE(again.page(0)->controls[0].type, RuntimeControlType::StatusLight);
}

void IntegrationTest::testRuntimeControlTypeNames()
{
    // 新控件类型中文名双向映射（序列化靠它存取，改名即破坏兼容）
    RuntimeControlType t;
    QVERIFY(runtimeControlTypeFromName(QStringLiteral("结果表格"), t));
    QCOMPARE(t, RuntimeControlType::ResultTable);
    QVERIFY(runtimeControlTypeFromName(QStringLiteral("IO状态"), t));
    QCOMPARE(t, RuntimeControlType::IoStatus);
    QCOMPARE(runtimeControlTypeName(RuntimeControlType::ResultTable), QStringLiteral("结果表格"));
    QCOMPARE(runtimeControlTypeName(RuntimeControlType::IoStatus), QStringLiteral("IO状态"));
    QVERIFY(!runtimeControlTypeFromName(QStringLiteral("不存在"), t));
}

void IntegrationTest::testThreadSafeParamsConcurrentAccess()
{
    // 参数表并发压测：把"界面线程写 / 执行线程读"的锁纪律钉死。
    // 旧实现是裸 QMap——并发 insert 会重排结构，读到脏值/崩溃在本强度下必现。
    ThreadSafeParams params;
    std::atomic<int> badReads{0};

    std::vector<std::thread> threads;
    for (int w = 0; w < 2; ++w) {
        threads.emplace_back([&params, w]() {
            for (int i = 0; i < 8000; ++i) {
                params.insert(QStringLiteral("k%1").arg((i + w) % 64), i);
                params[QStringLiteral("counter%1").arg(w)] = i;   // 写代理路径
            }
        });
    }
    for (int r = 0; r < 3; ++r) {
        threads.emplace_back([&params, &badReads]() {
            for (int i = 0; i < 8000; ++i) {
                const QVariant v = params.value(QStringLiteral("k%1").arg(i % 64));
                if (v.isValid() && v.toInt() < 0)
                    ++badReads;   // 写入的值都 >= 0：出现负值即读到损坏数据
                (void)params.contains(QStringLiteral("counter0"));
            }
        });
    }
    for (auto &t : threads)
        t.join();

    QCOMPARE(badReads.load(), 0);
    QCOMPARE(params.size(), 66);   // 64 个 k* + 2 个 counter*
    QCOMPARE(params.value(QStringLiteral("counter1")).toInt(), 7999);
}

void IntegrationTest::testDataObjectConcurrentAccess()
{
    // DataObject 内部锁压测：发布后写（执行线程 propagateData 打来源戳等）
    // 与读（界面线程结果面板/预览）并发——旧实现无锁即为数据竞争。
    DataObjectPtr obj = QSharedPointer<DataObject>::create();
    std::thread writer([&obj]() {
        for (int i = 0; i < 8000; ++i) {
            MeasureResult r;
            r.valid = true;
            r.value = i;
            obj->setMeasureResult(r);
            obj->setData(QVariant(i));
            obj->setSourceInfo(QStringLiteral("src %1").arg(i));
        }
    });
    std::thread reader([&obj]() {
        for (int i = 0; i < 8000; ++i) {
            const MeasureResult r = obj->getMeasureResult();
            (void)r;
            (void)obj->sourceInfo();
            (void)obj->getData();
        }
    });
    writer.join();
    reader.join();

    // 终态一致、无损坏：最后一轮的写入值必须原样可读回
    QCOMPARE(obj->getData().toInt(), 7999);
    QCOMPARE(obj->sourceInfo(), QStringLiteral("src 7999"));
}

// N-2：场景先于快照句柄析构时，句柄的释放必须安全跳过——旧实现持裸 FlowScene*，
// 析构时会对已亡场景回调 releaseGraphSnapshot()（UAF + 墓碑对象永久泄漏）。
// 现在句柄持 QPointer 弱引用：场景亡后 isHeld() 为假、释放为 no-op。
void IntegrationTest::testSnapshotGuardSurvivesSceneDestruction()
{
    // M-2：本用例刻意让"场景先于快照句柄析构"，需临时关闭场景析构断言——否则 Debug 构建必 abort
    // （仓库只跑 Release 门禁，等于埋雷）。用例结束恢复默认，不影响其它用例与生产语义。
    FlowScene::setDanglingSnapshotAssertEnabled(false);
    FlowScene::GraphSnapshotGuard guard;
    {
        FlowScene scene;
        NodeBase *n = scene.createNode(NodeBase::IMAGE_PROCESSING, QPointF(0, 0),
                                       QStringLiteral("OpenCV二值化"));
        QVERIFY(n != nullptr);
        guard = scene.captureGraphSnapshot();
        QCOMPARE(guard.snapshot().nodes.size(), 1);
        QVERIFY2(guard.isHeld(), "捕获后句柄应为持有态");
        // 注意：场景在此作用域结束时析构，而 guard 仍存活且在快照寄存器里——调用方顺序错误，
        // 场景侧会告警（Release 无断言），句柄侧必须能安全收尾。
    }
    QVERIFY2(!guard.isHeld(), "场景析构后弱引用应自动置空");
    guard = FlowScene::GraphSnapshotGuard();   // 释放/移动赋值：场景已亡 → 必须 no-op，不得 UAF
    QVERIFY(!guard.isHeld());
    FlowScene::setDanglingSnapshotAssertEnabled(true);   // M-2：恢复默认（生产语义）
}

void IntegrationTest::testStepModeExitRestoresNormalRun()
{
    // 单步模式必须能退出：历史缺陷 m_stepMode 只在 stopExecution 里清 →
    // 单步用过一次后，之后每次"开始执行/继续"仍在每个节点后暂停（模式粘住）。
    FlowScene scene;
    FlowExecutor exec;
    exec.setFlowName(QStringLiteral("StepModeFlow"));
    exec.setFlowMode(FlowMode::SoftwareTrigger);

    // 两个互不连接的延时节点：拓扑序即 1→2，单步必在第 1 个节点后暂停
    NodeBase *d1 = scene.createNode(NodeBase::LOGIC, QPointF(100, 100), QStringLiteral("Delay"));
    NodeBase *d2 = scene.createNode(NodeBase::LOGIC, QPointF(300, 100), QStringLiteral("Delay"));
    QVERIFY(d1 != nullptr && d2 != nullptr);
    d1->setParam(QStringLiteral("delayMs"), 50);
    d2->setParam(QStringLiteral("delayMs"), 50);
    exec.setFlowScene(&scene);

    exec.stepExecution();   // 进入单步：第 1 个节点后暂停
    QTRY_VERIFY_WITH_TIMEOUT(exec.getState() == ExecutionState::Paused, 4000);

    // S1 / Phase B 小步：Paused 状态位只表示"暂停请求已受理"（当前节点可能仍在跑），因此
    // 暂停瞬间未必可编辑——必须等 worker 真停进等待点（executionParked）才放行改图。
    // 若某个等待点漏走收口助手 parkWhilePausedLocked()，本断言会超时失败（判别性）。
    QTRY_VERIFY_WITH_TIMEOUT(exec.allowsGraphEditing(), 4000);

    // 以"继续"语义退出单步：剩余节点跑完后必须到 Idle（旧代码会再次 Paused）
    exec.exitStepMode();
    exec.resumeExecution();
    QVERIFY2(!exec.allowsGraphEditing(),
             "恢复执行后必须立刻禁止编辑（否则运行中改图会与执行线程竞争）");
    QTRY_VERIFY_WITH_TIMEOUT(exec.getState() == ExecutionState::Idle, 4000);
    QVERIFY2(exec.allowsGraphEditing(), "空闲时必须允许编辑（连续模式空闲不再一刀切锁死）");

    exec.stopExecution();
    exec.wait(3000);
    exec.setFlowScene(nullptr);
}

void IntegrationTest::testBusyTriggerQueuedNotDropped()
{
    // S9：忙时触发**不再丢弃**，改为有界排队补跑（上限 3 轮）；超界才丢最旧一笔并计数。
    // 旧策略（忙则丢弃且不计入触发计数）已被本用例替换：检测场景下漏检一件比晚检更糟。
    FlowScene scene;
    FlowExecutor exec;
    const QString flowName = QStringLiteral("BusyTriggerFlow");
    exec.setFlowName(flowName);
    exec.setFlowMode(FlowMode::SoftwareTrigger);

    NodeBase *delay = scene.createNode(NodeBase::LOGIC, QPointF(100, 100), QStringLiteral("Delay"));
    QVERIFY(delay != nullptr);
    delay->setParam(QStringLiteral("delayMs"), 400);   // 留出稳定的"忙"窗口
    exec.setFlowScene(&scene);

    auto *gtm = GlobalTriggerManager::instance();
    QVERIFY(gtm->setStringTrigger(QStringLiteral("GO_BUSY"), flowName));
    gtm->registerFlow(flowName, &scene, &exec);

    auto triggerCount = [gtm]() {
        for (const auto &e : gtm->allTriggers()) {
            if (e.triggerSource == QStringLiteral("GO_BUSY"))
                return e.triggerCount;
        }
        return -1;
    };
    QCOMPARE(triggerCount(), 0);

    QSignalSpy firedSpy(gtm, &GlobalTriggerManager::triggerFired);
    QSignalSpy finishedSpy(&exec, &FlowExecutor::executionFinished);

    exec.startExecution();
    QTRY_VERIFY_WITH_TIMEOUT(exec.getState() == ExecutionState::Running, 3000);
    QCOMPARE(exec.pendingExternalRounds(), 0);

    // 忙时连发 4 笔：前 3 笔排队，第 4 笔超界丢弃（丢最旧，保留最新件）
    for (int i = 0; i < 4; ++i)
        gtm->onDataReceived(QStringLiteral("TEST_DEV"), QByteArray("GO_BUSY"));

    QCOMPARE(exec.pendingExternalRounds(), 3);            // 有界队列已满
    QCOMPARE(exec.droppedExternalRounds(), quint64(1));   // 第 4 笔被丢
    QCOMPARE(firedSpy.count(), 3);                        // 仅"会被执行"的才通知
    QCOMPARE(triggerCount(), 3);                          // 计数口径：排队也算（不虚报、不漏报）

    // 每轮末消费一笔待补跑：1(首轮) + 3(补跑) = 4 轮
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 4, 20000);
    QCOMPARE(exec.pendingExternalRounds(), 0);

    // 对照组：空闲时触发 → 立即起一轮（不经队列）
    gtm->onDataReceived(QStringLiteral("TEST_DEV"), QByteArray("GO_BUSY"));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 5, 8000);
    QCOMPARE(exec.pendingExternalRounds(), 0);
    QCOMPARE(triggerCount(), 4);

    exec.stopExecution();
    exec.wait(3000);
    gtm->removeStringTrigger(QStringLiteral("GO_BUSY"));
    gtm->unregisterFlow(flowName);
    exec.setFlowScene(nullptr);
}

void IntegrationTest::testBusyOtherFlowDoesNotBlockTrigger()
{
    // 需求语义：只有"目标流程自己忙"才丢弃触发；**其他流程忙不得影响**本流程被触发。
    // 实现口径：busy 判定按目标流程的执行器查（每流程独立执行器），不查任何全局状态。
    FlowScene sceneTarget;
    FlowExecutor execTarget;
    const QString targetFlow = QStringLiteral("IsolationTargetFlow");
    execTarget.setFlowName(targetFlow);
    execTarget.setFlowMode(FlowMode::SoftwareTrigger);
    NodeBase *t = sceneTarget.createNode(NodeBase::LOGIC, QPointF(100, 100), QStringLiteral("Delay"));
    QVERIFY(t != nullptr);
    t->setParam(QStringLiteral("delayMs"), 0);   // 目标流程：秒完成
    execTarget.setFlowScene(&sceneTarget);

    FlowScene sceneOther;
    FlowExecutor execOther;
    const QString otherFlow = QStringLiteral("IsolationOtherFlow");
    execOther.setFlowName(otherFlow);
    execOther.setFlowMode(FlowMode::SoftwareTrigger);
    NodeBase *o = sceneOther.createNode(NodeBase::LOGIC, QPointF(100, 100), QStringLiteral("Delay"));
    QVERIFY(o != nullptr);
    o->setParam(QStringLiteral("delayMs"), 600);   // 其他流程：长时间忙
    execOther.setFlowScene(&sceneOther);

    auto *gtm = GlobalTriggerManager::instance();
    QVERIFY(gtm->setStringTrigger(QStringLiteral("GO_TGT"), targetFlow));
    gtm->registerFlow(targetFlow, &sceneTarget, &execTarget);
    gtm->registerFlow(otherFlow, &sceneOther, &execOther);

    QSignalSpy firedSpy(gtm, &GlobalTriggerManager::triggerFired);
    QSignalSpy targetStartedSpy(&execTarget, &FlowExecutor::executionStarted);

    // 先让"其他流程"处于忙碌
    execOther.startExecution();
    QTRY_VERIFY_WITH_TIMEOUT(execOther.getState() == ExecutionState::Running, 3000);

    // 触发目标流程：其他流程忙不影响它——必须触发、必须真的跑起来
    gtm->onDataReceived(QStringLiteral("TEST_DEV"), QByteArray("GO_TGT"));
    QCOMPARE(firedSpy.count(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(targetStartedSpy.count() >= 1, 3000);

    int targetCount = -1;
    for (const auto &e : gtm->allTriggers()) {
        if (e.triggerSource == QStringLiteral("GO_TGT"))
            targetCount = e.triggerCount;
    }
    QCOMPARE(targetCount, 1);   // 目标流程空闲：计数 +1（其他流程忙不参与判定）

    execOther.stopExecution();
    execOther.wait(3000);
    execTarget.stopExecution();
    execTarget.wait(3000);
    gtm->removeStringTrigger(QStringLiteral("GO_TGT"));
    gtm->unregisterFlow(targetFlow);
    gtm->unregisterFlow(otherFlow);
    execTarget.setFlowScene(nullptr);
    execOther.setFlowScene(nullptr);
}

void IntegrationTest::testScriptInterpreterResolvesToAbsolutePath()
{
    // 沙箱洞回归：解释器必须解析成**绝对路径**（或返回空串走 fail-closed），
    // 绝不能返回裸名 "python"——Windows 的 CreateProcess 搜索序会先看应用目录/当前目录，
    // 攻击者放一个同名 exe 即可顶替"确认过的可信脚本"实际执行的解释器。
    auto &policy = ScriptSecurityPolicy::instance();
    const QString py = policy.interpreterPath(QStringLiteral("Python"));
    QVERIFY2(!py.isEmpty(),
             "PATH/程序目录中应有 Python（现有脚本执行用例依赖它可运行）");
    QVERIFY2(QFileInfo(py).isAbsolute(),
             qPrintable(QStringLiteral("解释器必须是绝对路径，实际返回: %1").arg(py)));
    QVERIFY2(QFileInfo::exists(py),
             qPrintable(QStringLiteral("解析出的解释器不存在: %1").arg(py)));
}

void IntegrationTest::testImageDisplayResolvePriority()
{
    // Step 2 回归：显示决策优先级 = 下拉框选择 > 画布选中 > 兜底；
    // 且"显式显示源"按场景独立记忆（切换流程互不污染）。
    FlowScene sceneA;
    FlowScene sceneB;
    NodeBase *n1 = sceneA.createNode(NodeBase::LOGIC, QPointF(100, 200), QStringLiteral("Formula"));
    NodeBase *n2 = sceneA.createNode(NodeBase::LOGIC, QPointF(300, 200), QStringLiteral("Delay"));
    QVERIFY2(n1 && n2, "无法创建测试节点");

    // 无画布实例：只验证决策逻辑（provider 注入替代 MainWindow 的场景/选中节点）
    ImageDisplayController ctrl(nullptr, nullptr, nullptr);
    FlowScene *currentScene = &sceneA;
    NodeBase *canvasSelected = nullptr;
    ctrl.setSceneProvider([&currentScene]() { return currentScene; });
    ctrl.setCanvasSelectionProvider([&canvasSelected]() { return canvasSelected; });

    // ① 全空 → 兜底
    QCOMPARE(ctrl.resolveDisplayNode(n1), n1);

    // ② 画布选中 → 覆盖兜底
    canvasSelected = n2;
    QCOMPARE(ctrl.resolveDisplayNode(n1), n2);

    // ③ 下拉框选择 → 最高优先级（覆盖画布选中）
    ctrl.setSelectedOutputNode(&sceneA, n1);
    QCOMPARE(ctrl.resolveDisplayNode(nullptr), n1);
    QCOMPARE(ctrl.selectedOutputNode(&sceneA), n1);

    // ④ 切到场景 B：A 的显式选择不污染 B（回落到画布选中）
    currentScene = &sceneB;
    QCOMPARE(ctrl.resolveDisplayNode(nullptr), n2);
    QVERIFY(ctrl.selectedOutputNode(&sceneB) == nullptr);

    // ⑤ 清空选择（对应「新建方案」）→ 回到画布选中/兜底
    ctrl.clearSelection();
    QVERIFY(ctrl.selectedOutputNode(&sceneA) == nullptr);
    QCOMPARE(ctrl.resolveDisplayNode(nullptr), n2);
    canvasSelected = nullptr;
    QCOMPARE(ctrl.resolveDisplayNode(n1), n1);
}

void IntegrationTest::testExternalTriggerAcceptedOnlyInSoftwareMode()
{
    // M-1/F-1：只有软触发模式会在轮末消费补跑（run() 轮末分支）；其余模式"受理"外部触发=排队永不执行，
    // 且 GTM 会照常计数并发 triggerFired → 幻影计数（现场已按 B 方案"载入即自动连续跑"，不收口就是
    // 全天候虚报）。判别点取 requestExternalRound() 的返回值：false=未受理 → GTM 不计数、不发信号。
    FlowScene scene;
    FlowExecutor exec;
    exec.setFlowName(QStringLiteral("RegressionExtTriggerMode"));

    NodeBase *delay = scene.createNode(NodeBase::LOGIC, QPointF(200, 200), QStringLiteral("Delay"));
    QVERIFY2(delay != nullptr, "无法创建延时节点");
    delay->setParam(QStringLiteral("delayMs"), 50);
    exec.setFlowScene(&scene);

    // 空闲：连续/硬触发都不得受理（连续流程由"载入自启 / 人工点开始"驱动，不由触发唤起）
    exec.setFlowMode(FlowMode::Continuous);
    QVERIFY2(!exec.requestExternalRound(), "连续模式不得受理外部触发（幻影计数回归点）");
    QVERIFY2(!exec.requestExternalRound(), "连续模式重复触发同样不得受理");
    exec.setFlowMode(FlowMode::HardwareTrigger);
    QVERIFY2(!exec.requestExternalRound(), "硬触发模式不得受理外部触发");

    // 软触发：语义不变——空闲时受理并能被唤起一轮
    exec.setFlowMode(FlowMode::SoftwareTrigger);
    QVERIFY2(exec.requestExternalRound(), "软触发模式下外部触发必须受理（原语义）");
    exec.stopExecution();
    QVERIFY(exec.wait(3000));
    QVERIFY2(exec.requestExternalRound(), "软触发+停止后仍应受理（可再被触发唤起）");
    exec.stopExecution();
    QVERIFY(exec.wait(3000));
    exec.setFlowScene(nullptr);
}

void IntegrationTest::testShadowMemberSingleSourceAndConcurrentAccess()
{
    // S1 残留试点（FilterNode 影子成员收口）：参数表成为唯一来源。
    // ① 单源：setParam → getParam / toJson / fromJson 必须处处一致（旧实现另有成员镜像，两者可不同步
    //    → 表现是"面板改了、算子还按旧值跑"）；
    // ② 旧方案兼容：只有顶层键、没有 params 段的早期文件仍必须能载入；
    // ③ 并发：执行线程反复 run() × 界面线程反复 setParam —— 收口前这是影子成员（QString/double）上的
    //    无保护竞态，QString 堆引用计数竞争可随机崩溃；收口后由 ThreadSafeParams 串行化。
    FilterNode filter;
    filter.init();

    filter.setParam(QStringLiteral("threshold"), 250.0);
    filter.setParam(QStringLiteral("operator"), QStringLiteral("<"));
    QCOMPARE(filter.getParam(QStringLiteral("threshold")).toDouble(), 250.0);
    QCOMPARE(filter.getParam(QStringLiteral("operator")).toString(), QStringLiteral("<"));

    const QJsonObject json = filter.toJson();
    QCOMPARE(json.value(QStringLiteral("threshold")).toDouble(), 250.0);
    QCOMPARE(json.value(QStringLiteral("operator")).toString(), QStringLiteral("<"));

    FilterNode restored;
    restored.init();
    restored.fromJson(json);
    QCOMPARE(restored.getParam(QStringLiteral("threshold")).toDouble(), 250.0);
    QCOMPARE(restored.getParam(QStringLiteral("operator")).toString(), QStringLiteral("<"));

    // 旧格式：顶层键、无 params 段（兼容分支）
    QJsonObject legacy;
    legacy[QStringLiteral("operator")] = QStringLiteral(">=");
    legacy[QStringLiteral("threshold")] = 42.0;
    FilterNode legacyLoaded;
    legacyLoaded.init();
    legacyLoaded.fromJson(legacy);
    QCOMPARE(legacyLoaded.getParam(QStringLiteral("threshold")).toDouble(), 42.0);
    QCOMPARE(legacyLoaded.getParam(QStringLiteral("operator")).toString(), QStringLiteral(">="));

    // 并发压测：写侧模拟界面线程，读侧模拟执行线程（run 内先取参数快照）
    QAtomicInt stop(0);
    std::thread uiWriter([&filter, &stop]() {
        for (int i = 0; i < 20000; ++i) {
            filter.setParam(QStringLiteral("threshold"), double(i % 1000));
            filter.setParam(QStringLiteral("operator"),
                            (i % 2) ? QStringLiteral(">=") : QStringLiteral("<="));
        }
        stop.storeRelease(1);
    });
    while (!stop.loadAcquire())
        filter.run(false);
    uiWriter.join();

    // 终态自洽：最后一次写入必须可原样读回（无损坏、无丢写）
    filter.setParam(QStringLiteral("threshold"), 7.0);
    QCOMPARE(filter.getParam(QStringLiteral("threshold")).toDouble(), 7.0);

    // 同批第二类（SortNode）：单源（setParam → getParam/toJson/fromJson）+ 旧格式兼容
    SortNode sorter;
    sorter.init();
    sorter.setParam(QStringLiteral("order"), QStringLiteral("desc"));
    QCOMPARE(sorter.getParam(QStringLiteral("order")).toString(), QStringLiteral("desc"));
    QCOMPARE(sorter.toJson().value(QStringLiteral("order")).toString(), QStringLiteral("desc"));

    SortNode sorterRoundTrip;
    sorterRoundTrip.init();
    sorterRoundTrip.fromJson(sorter.toJson());
    QCOMPARE(sorterRoundTrip.getParam(QStringLiteral("order")).toString(), QStringLiteral("desc"));

    QJsonObject legacySorter;
    legacySorter[QStringLiteral("order")] = QStringLiteral("desc");
    SortNode sorterLegacy;
    sorterLegacy.init();
    sorterLegacy.fromJson(legacySorter);
    QCOMPARE(sorterLegacy.getParam(QStringLiteral("order")).toString(), QStringLiteral("desc"));

    // 同批第三类（CounterNode）：单源（setParam → getParam/toJson/fromJson）+ 旧格式兼容
    CounterNode counter;
    counter.init();
    counter.setParam(QStringLiteral("conditionMode"), QStringLiteral("number"));
    counter.setParam(QStringLiteral("threshold"), 12.5);
    QCOMPARE(counter.getParam(QStringLiteral("conditionMode")).toString(), QStringLiteral("number"));
    QCOMPARE(counter.getParam(QStringLiteral("threshold")).toDouble(), 12.5);

    const QJsonObject counterJson = counter.toJson();
    QCOMPARE(counterJson.value(QStringLiteral("conditionMode")).toString(), QStringLiteral("number"));
    QCOMPARE(counterJson.value(QStringLiteral("threshold")).toDouble(), 12.5);

    CounterNode counterRoundTrip;
    counterRoundTrip.init();
    counterRoundTrip.fromJson(counterJson);
    QCOMPARE(counterRoundTrip.getParam(QStringLiteral("conditionMode")).toString(), QStringLiteral("number"));
    QCOMPARE(counterRoundTrip.getParam(QStringLiteral("threshold")).toDouble(), 12.5);

    QJsonObject legacyCounter;
    legacyCounter[QStringLiteral("conditionMode")] = QStringLiteral("number");
    legacyCounter[QStringLiteral("threshold")] = 33.0;
    CounterNode counterLegacy;
    counterLegacy.init();
    counterLegacy.fromJson(legacyCounter);
    QCOMPARE(counterLegacy.getParam(QStringLiteral("conditionMode")).toString(), QStringLiteral("number"));
    QCOMPARE(counterLegacy.getParam(QStringLiteral("threshold")).toDouble(), 33.0);

    // 同批第四类（FormatNode）：单源 + 旧格式兼容，且"键缺失"语义必须与旧实现逐字一致
    FormatNode fmt;
    fmt.init();
    fmt.setParam(QStringLiteral("template"), QStringLiteral("{X},{Y}"));
    fmt.setParam(QStringLiteral("outputSuffix"), QStringLiteral("\r\n"));
    QCOMPARE(fmt.getParam(QStringLiteral("template")).toString(), QStringLiteral("{X},{Y}"));
    QCOMPARE(fmt.getParam(QStringLiteral("outputSuffix")).toString(), QStringLiteral("\r\n"));

    const QJsonObject fmtJson = fmt.toJson();
    QCOMPARE(fmtJson.value(QStringLiteral("template")).toString(), QStringLiteral("{X},{Y}"));
    QCOMPARE(fmtJson.value(QStringLiteral("outputSuffix")).toString(), QStringLiteral("\r\n"));

    FormatNode fmtRoundTrip;
    fmtRoundTrip.init();
    fmtRoundTrip.fromJson(fmtJson);
    QCOMPARE(fmtRoundTrip.getParam(QStringLiteral("template")).toString(), QStringLiteral("{X},{Y}"));
    QCOMPARE(fmtRoundTrip.getParam(QStringLiteral("outputSuffix")).toString(), QStringLiteral("\r\n"));

    QJsonObject legacyFormat;
    legacyFormat[QStringLiteral("template")] = QStringLiteral("A={A}");
    FormatNode fmtLegacy;
    fmtLegacy.init();
    fmtLegacy.fromJson(legacyFormat);
    QCOMPARE(fmtLegacy.getParam(QStringLiteral("template")).toString(), QStringLiteral("A={A}"));
    // 旧实现是无条件 `m_outputSuffix = json["outputSuffix"].toString()` → 键缺失时取空串（不追加后缀）。
    // 若哪天有人把兼容分支改成"缺失则保留默认值"，本断言会失败（这就是它存在的意义）。
    QCOMPARE(fmtLegacy.getParam(QStringLiteral("outputSuffix")).toString(), QString());

    // 同批第五类（FormulaNode）：单源 + 旧格式兼容；本类还删掉了一处"重写 getParam 直接返回成员"的旁路
    FormulaNode formula;
    formula.init();
    QCOMPARE(formula.getParam(QStringLiteral("expression")).toString(), QStringLiteral("p0 + p1"));
    formula.setParam(QStringLiteral("expression"), QStringLiteral("(p0 + p1) * 2"));
    QCOMPARE(formula.getParam(QStringLiteral("expression")).toString(), QStringLiteral("(p0 + p1) * 2"));
    QCOMPARE(formula.toJson().value(QStringLiteral("expression")).toString(),
             QStringLiteral("(p0 + p1) * 2"));

    FormulaNode formulaRoundTrip;
    formulaRoundTrip.init();
    formulaRoundTrip.fromJson(formula.toJson());
    QCOMPARE(formulaRoundTrip.getParam(QStringLiteral("expression")).toString(),
             QStringLiteral("(p0 + p1) * 2"));

    // 缺键语义与 FormatNode **相反**：旧代码 `toString(m_expression)` → 缺键保留原值（不是清空）
    QJsonObject legacyFormula;
    FormulaNode formulaLegacy;
    formulaLegacy.init();
    formulaLegacy.setParam(QStringLiteral("expression"), QStringLiteral("p2 * 3"));
    formulaLegacy.fromJson(legacyFormula);
    QCOMPARE(formulaLegacy.getParam(QStringLiteral("expression")).toString(), QStringLiteral("p2 * 3"));

    // 同批第六类（ClassifyNode）：5 个参数（2 double + 3 QString）单源 + 旧格式兼容
    ClassifyNode classify;
    classify.init();
    QCOMPARE(classify.getParam(QStringLiteral("thresholdLow")).toDouble(), 0.0);
    QCOMPARE(classify.getParam(QStringLiteral("thresholdHigh")).toDouble(), 100.0);
    classify.setParam(QStringLiteral("thresholdLow"), 10.0);
    classify.setParam(QStringLiteral("thresholdHigh"), 90.0);
    classify.setParam(QStringLiteral("nameMid"), QStringLiteral("MID"));
    QCOMPARE(classify.getParam(QStringLiteral("thresholdLow")).toDouble(), 10.0);
    QCOMPARE(classify.getParam(QStringLiteral("nameMid")).toString(), QStringLiteral("MID"));

    const QJsonObject classifyJson = classify.toJson();
    QCOMPARE(classifyJson.value(QStringLiteral("thresholdHigh")).toDouble(), 90.0);
    QCOMPARE(classifyJson.value(QStringLiteral("nameMid")).toString(), QStringLiteral("MID"));

    ClassifyNode classifyRoundTrip;
    classifyRoundTrip.init();
    classifyRoundTrip.fromJson(classifyJson);
    QCOMPARE(classifyRoundTrip.getParam(QStringLiteral("thresholdLow")).toDouble(), 10.0);
    QCOMPARE(classifyRoundTrip.getParam(QStringLiteral("nameMid")).toString(), QStringLiteral("MID"));

    // 缺键保留原值（本类旧实现 5 个参数全带默认值）：先设 7.0，再载入**不含该键**的旧 JSON，必须仍是 7.0
    QJsonObject legacyClassify;
    legacyClassify[QStringLiteral("nameHigh")] = QStringLiteral("HIGH");
    ClassifyNode classifyLegacy;
    classifyLegacy.init();
    classifyLegacy.setParam(QStringLiteral("thresholdLow"), 7.0);
    classifyLegacy.fromJson(legacyClassify);
    QCOMPARE(classifyLegacy.getParam(QStringLiteral("nameHigh")).toString(), QStringLiteral("HIGH"));
    QCOMPARE(classifyLegacy.getParam(QStringLiteral("thresholdLow")).toDouble(), 7.0);

    // 同批第七类（ProtocolParseNode）：标量 + **列表型**参数（fieldDefs = QVariantList<QVariantMap>）单源
    ProtocolParseNode proto;
    proto.init();
    QCOMPARE(proto.getParam(QStringLiteral("delimiter")).toString(), QStringLiteral(","));
    const QVariantList defFields = proto.getParam(QStringLiteral("fieldDefs")).toList();
    QCOMPARE(defFields.size(), 1);
    QCOMPARE(defFields.first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Field_0"));

    QVariantList twoFields;
    twoFields.append(QVariantMap{{QStringLiteral("name"), QStringLiteral("X")},
                                 {QStringLiteral("type"), QStringLiteral("int")},
                                 {QStringLiteral("index"), 0}});
    twoFields.append(QVariantMap{{QStringLiteral("name"), QStringLiteral("Y")},
                                 {QStringLiteral("type"), QStringLiteral("float")},
                                 {QStringLiteral("index"), 1}});
    proto.setParam(QStringLiteral("delimiter"), QStringLiteral(";"));
    proto.setParam(QStringLiteral("fieldDefs"), twoFields);
    QCOMPARE(proto.getParam(QStringLiteral("delimiter")).toString(), QStringLiteral(";"));
    QCOMPARE(proto.getParam(QStringLiteral("fieldDefs")).toList().size(), 2);

    // toJson 顶层键值与参数表一致（列表参数经 QJsonValue::fromVariant 落成 JSON 数组），且 params 内也带同一份
    const QJsonObject protoJson = proto.toJson();
    QCOMPARE(protoJson.value(QStringLiteral("delimiter")).toString(), QStringLiteral(";"));
    const QJsonArray protoFields = protoJson.value(QStringLiteral("fieldDefs")).toArray();
    QCOMPARE(protoFields.size(), 2);
    QCOMPARE(protoFields.at(1).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Y"));
    QCOMPARE(protoJson.value(QStringLiteral("params")).toObject()
                 .value(QStringLiteral("fieldDefs")).toArray().size(), 2);

    ProtocolParseNode protoRoundTrip;
    protoRoundTrip.init();
    protoRoundTrip.fromJson(protoJson);
    QCOMPARE(protoRoundTrip.getParam(QStringLiteral("delimiter")).toString(), QStringLiteral(";"));
    QCOMPARE(protoRoundTrip.getParam(QStringLiteral("fieldDefs")).toList().size(), 2);

    // 旧格式（只有顶层键）语义与旧实现逐字对齐：delimiter 缺失 ⇒ ","；fieldDefs 缺失 ⇒ 重置为默认单字段
    //（与上一类 ClassifyNode 的"缺键保留原值"**相反** —— 每类都必须按自己的旧语义实现）
    QJsonObject legacyProto;
    ProtocolParseNode protoLegacy;
    protoLegacy.init();
    protoLegacy.setParam(QStringLiteral("delimiter"), QStringLiteral("|"));
    protoLegacy.setParam(QStringLiteral("fieldDefs"), twoFields);
    protoLegacy.fromJson(legacyProto);
    QCOMPARE(protoLegacy.getParam(QStringLiteral("delimiter")).toString(), QStringLiteral(","));
    QCOMPARE(protoLegacy.getParam(QStringLiteral("fieldDefs")).toList().size(), 1);

    // 同批第八类（SendDataNode）：单源 + 旧格式兼容（本类两个键的"缺键语义"还不一样）
    SendDataNode sender;
    sender.init();
    QCOMPARE(sender.getParam(QStringLiteral("deviceName")).toString(), QString());
    QCOMPARE(sender.getParam(QStringLiteral("suffix")).toString(), QStringLiteral("\r\n"));
    sender.setParam(QStringLiteral("deviceName"), QStringLiteral("PLC1"));
    sender.setParam(QStringLiteral("suffix"), QStringLiteral("\n"));
    QCOMPARE(sender.getParam(QStringLiteral("deviceName")).toString(), QStringLiteral("PLC1"));

    const QJsonObject senderJson = sender.toJson();
    QCOMPARE(senderJson.value(QStringLiteral("deviceName")).toString(), QStringLiteral("PLC1"));
    QCOMPARE(senderJson.value(QStringLiteral("suffix")).toString(), QStringLiteral("\n"));

    SendDataNode senderRoundTrip;
    senderRoundTrip.init();
    senderRoundTrip.fromJson(senderJson);
    QCOMPARE(senderRoundTrip.getParam(QStringLiteral("deviceName")).toString(), QStringLiteral("PLC1"));
    QCOMPARE(senderRoundTrip.getParam(QStringLiteral("suffix")).toString(), QStringLiteral("\n"));

    // 旧格式（只有顶层键）：deviceName 缺失 ⇒ 清空（旧代码无默认值）；suffix 缺失 ⇒ 回 "\r\n"。
    // 判别性：先设好值，载入空旧 JSON 后必须被**重置**（不是保留原值）。
    QJsonObject legacySender;
    SendDataNode senderLegacy;
    senderLegacy.init();
    senderLegacy.setParam(QStringLiteral("deviceName"), QStringLiteral("PLC9"));
    senderLegacy.setParam(QStringLiteral("suffix"), QStringLiteral("X"));
    senderLegacy.fromJson(legacySender);
    QCOMPARE(senderLegacy.getParam(QStringLiteral("deviceName")).toString(), QString());
    QCOMPARE(senderLegacy.getParam(QStringLiteral("suffix")).toString(), QStringLiteral("\r\n"));

    // 同批第九类（ReceiveDataNode）：单源 + 旧格式兼容（两键都是"缺键清空"）
    ReceiveDataNode receiver;
    receiver.init();
    QCOMPARE(receiver.getParam(QStringLiteral("deviceName")).toString(), QString());
    QCOMPARE(receiver.getParam(QStringLiteral("filterPattern")).toString(), QString());
    receiver.setParam(QStringLiteral("deviceName"), QStringLiteral("TCP1"));
    receiver.setParam(QStringLiteral("filterPattern"), QStringLiteral("PFX"));
    QCOMPARE(receiver.getParam(QStringLiteral("deviceName")).toString(), QStringLiteral("TCP1"));
    QCOMPARE(receiver.getParam(QStringLiteral("filterPattern")).toString(), QStringLiteral("PFX"));

    const QJsonObject receiverJson = receiver.toJson();
    QCOMPARE(receiverJson.value(QStringLiteral("deviceName")).toString(), QStringLiteral("TCP1"));
    QCOMPARE(receiverJson.value(QStringLiteral("filterPattern")).toString(), QStringLiteral("PFX"));

    ReceiveDataNode receiverRoundTrip;
    receiverRoundTrip.init();
    receiverRoundTrip.fromJson(receiverJson);
    QCOMPARE(receiverRoundTrip.getParam(QStringLiteral("deviceName")).toString(), QStringLiteral("TCP1"));
    QCOMPARE(receiverRoundTrip.getParam(QStringLiteral("filterPattern")).toString(), QStringLiteral("PFX"));

    // 旧格式（只有顶层键）：两键缺键语义都是"重置为空"（旧代码均无默认值）
    QJsonObject legacyReceiver;
    ReceiveDataNode receiverLegacy;
    receiverLegacy.init();
    receiverLegacy.setParam(QStringLiteral("deviceName"), QStringLiteral("TCP9"));
    receiverLegacy.setParam(QStringLiteral("filterPattern"), QStringLiteral("OLD"));
    receiverLegacy.fromJson(legacyReceiver);
    QCOMPARE(receiverLegacy.getParam(QStringLiteral("deviceName")).toString(), QString());
    QCOMPARE(receiverLegacy.getParam(QStringLiteral("filterPattern")).toString(), QString());

    // 行为级断言（不只字段搬运）：直接派发接收回调，验证"绑定匹配 + 前缀剥离"确实按参数表工作
    ReceiveDataNode live;
    live.init();
    live.setParam(QStringLiteral("deviceName"), QStringLiteral("TCP1"));
    live.setParam(QStringLiteral("filterPattern"), QStringLiteral("PFX"));
    QMetaObject::invokeMethod(&live, "onDataReceived", Qt::DirectConnection,
                              Q_ARG(QString, QStringLiteral("TCP2")),
                              Q_ARG(QByteArray, QByteArray("PFXhello")));
    QVERIFY2(live.getParam(QStringLiteral("lastData")).toString().isEmpty(),
             "绑定设备不匹配时不得缓存数据");
    QMetaObject::invokeMethod(&live, "onDataReceived", Qt::DirectConnection,
                              Q_ARG(QString, QStringLiteral("TCP1")),
                              Q_ARG(QByteArray, QByteArray("PFXhello")));
    QCOMPARE(live.getParam(QStringLiteral("lastData")).toString(), QStringLiteral("hello"));
    // 判别性：把前缀改掉后再收一条只匹配**新**前缀的报文 ⇒ 必须按新前缀剥离。
    // 若回调仍读旧快照/成员（PFX），这条报文会被拒收、lastData 仍为 "hello"，断言即失败。
    live.setParam(QStringLiteral("filterPattern"), QStringLiteral("OTHER"));
    QMetaObject::invokeMethod(&live, "onDataReceived", Qt::DirectConnection,
                              Q_ARG(QString, QStringLiteral("TCP1")),
                              Q_ARG(QByteArray, QByteArray("OTHERworld")));
    QCOMPARE(live.getParam(QStringLiteral("lastData")).toString(), QStringLiteral("world"));

    // 同批第十类（ImageReadNode，最后一个 QString 类）：只收口 4 个真参数；
    // 断言分两层：① 参数单源；② 派生缓存（imageFiles/isDirectory）仍随参数变化重建。
    ImageReadNode reader;
    reader.init();
    QCOMPARE(reader.getParam(QStringLiteral("filePath")).toString(), QString());
    QCOMPARE(reader.getParam(QStringLiteral("mono8Mode")).toBool(), false);
    QCOMPARE(reader.getParam(QStringLiteral("autoSwitch")).toBool(), false);
    QCOMPARE(reader.getParam(QStringLiteral("mode")).toInt(),
             static_cast<int>(ImageReadNode::Mode::SingleImage));
    // imagePath 是 filePath 的历史别名，必须继续可用
    QCOMPARE(reader.getParam(QStringLiteral("imagePath")).toString(), QString());

    reader.setParam(QStringLiteral("filePath"), QStringLiteral("C:/tmp/one.bmp"));
    reader.setParam(QStringLiteral("mono8Mode"), true);
    reader.setParam(QStringLiteral("autoSwitch"), true);
    reader.setParam(QStringLiteral("mode"), static_cast<int>(ImageReadNode::Mode::MultiImage));
    QCOMPARE(reader.getParam(QStringLiteral("filePath")).toString(), QStringLiteral("C:/tmp/one.bmp"));
    QCOMPARE(reader.getParam(QStringLiteral("imagePath")).toString(), QStringLiteral("C:/tmp/one.bmp"));
    QCOMPARE(reader.getParam(QStringLiteral("mono8Mode")).toBool(), true);
    QCOMPARE(reader.getParam(QStringLiteral("autoSwitch")).toBool(), true);

    const QJsonObject readerJson = reader.toJson();
    QCOMPARE(readerJson.value(QStringLiteral("filePath")).toString(), QStringLiteral("C:/tmp/one.bmp"));
    QCOMPARE(readerJson.value(QStringLiteral("mono8Mode")).toBool(), true);
    QCOMPARE(readerJson.value(QStringLiteral("autoSwitch")).toBool(), true);
    QCOMPARE(readerJson.value(QStringLiteral("mode")).toInt(),
             static_cast<int>(ImageReadNode::Mode::MultiImage));

    ImageReadNode readerRoundTrip;
    readerRoundTrip.init();
    readerRoundTrip.fromJson(readerJson);
    QCOMPARE(readerRoundTrip.getParam(QStringLiteral("filePath")).toString(),
             QStringLiteral("C:/tmp/one.bmp"));
    QCOMPARE(readerRoundTrip.getParam(QStringLiteral("mono8Mode")).toBool(), true);
    QCOMPARE(readerRoundTrip.getParam(QStringLiteral("autoSwitch")).toBool(), true);
    QCOMPARE(readerRoundTrip.getParam(QStringLiteral("mode")).toInt(),
             static_cast<int>(ImageReadNode::Mode::MultiImage));

    // 旧格式（只有顶层键）：filePath 无条件重置（缺键 ⇒ 空），其余三个 contains 守卫（缺键 ⇒ 保留）
    QJsonObject legacyReader;
    legacyReader[QStringLiteral("mono8Mode")] = true;
    ImageReadNode readerLegacy;
    readerLegacy.init();
    readerLegacy.setParam(QStringLiteral("filePath"), QStringLiteral("C:/tmp/old.bmp"));
    readerLegacy.setParam(QStringLiteral("autoSwitch"), true);
    readerLegacy.fromJson(legacyReader);
    QCOMPARE(readerLegacy.getParam(QStringLiteral("filePath")).toString(), QString());
    QCOMPARE(readerLegacy.getParam(QStringLiteral("mono8Mode")).toBool(), true);
    QVERIFY2(readerLegacy.getParam(QStringLiteral("autoSwitch")).toBool(),
             "旧格式缺 autoSwitch 键时必须保留原值（旧实现是 contains 守卫，不是无条件重置）");

    // 行为级：派生缓存必须仍随 filePath 变化重建（证明 setParam 的副作用没被收口改坏）
    QDir probeDir(QDir::tempPath() + QStringLiteral("/vfp_imageread_probe"));
    probeDir.removeRecursively();
    QVERIFY2(probeDir.mkpath(QStringLiteral(".")), "临时目录创建失败");
    QFile probeA(probeDir.filePath(QStringLiteral("a.bmp")));
    QFile probeB(probeDir.filePath(QStringLiteral("b.png")));
    QVERIFY(probeA.open(QIODevice::WriteOnly));
    probeA.write("x");
    probeA.close();
    QVERIFY(probeB.open(QIODevice::WriteOnly));
    probeB.write("y");
    probeB.close();

    ImageReadNode cacheProbe;
    cacheProbe.init();
    cacheProbe.setParam(QStringLiteral("mode"), static_cast<int>(ImageReadNode::Mode::SingleImage));
    cacheProbe.setParam(QStringLiteral("filePath"), probeDir.absolutePath());
    QVERIFY2(!cacheProbe.reusesCachedOutput(),
             "目录内 2 个文件 ⇒ 会随轮次取不同图，必须判定为不可复用缓存（派生缓存未重建则此处失败）");
    cacheProbe.setParam(QStringLiteral("filePath"), probeDir.filePath(QStringLiteral("a.bmp")));
    QVERIFY2(cacheProbe.reusesCachedOutput(), "单文件 ⇒ 输出是路径的确定性函数，必须判定为可复用");
    cacheProbe.setParam(QStringLiteral("filePath"),
                        probeDir.filePath(QStringLiteral("does_not_exist.bmp")));
    QVERIFY2(cacheProbe.reusesCachedOutput(), "路径不存在 ⇒ 0 个文件，仍按可复用处理");
    probeDir.removeRecursively();

    // 第二批第 1 类（RecordNode）：3 个参数（2 QString + 1 bool）单源 + 旧格式兼容
    RecordNode recorder;
    recorder.init();
    QVERIFY2(!recorder.getParam(QStringLiteral("flowName")).toString().isEmpty(),
             "init 必须写入 flowName 默认值（唯一来源）");
    QVERIFY2(!recorder.getParam(QStringLiteral("nodeName")).toString().isEmpty(),
             "init 必须写入 nodeName 默认值（唯一来源）");
    QCOMPARE(recorder.getParam(QStringLiteral("passed")).toBool(), true);

    recorder.setParam(QStringLiteral("flowName"), QStringLiteral("flow-A"));
    recorder.setParam(QStringLiteral("nodeName"), QStringLiteral("node-1"));
    recorder.setParam(QStringLiteral("passed"), false);
    QCOMPARE(recorder.getParam(QStringLiteral("flowName")).toString(), QStringLiteral("flow-A"));
    QCOMPARE(recorder.getParam(QStringLiteral("nodeName")).toString(), QStringLiteral("node-1"));
    QCOMPARE(recorder.getParam(QStringLiteral("passed")).toBool(), false);

    const QJsonObject recorderJson = recorder.toJson();
    QCOMPARE(recorderJson.value(QStringLiteral("flowName")).toString(), QStringLiteral("flow-A"));
    QCOMPARE(recorderJson.value(QStringLiteral("nodeName")).toString(), QStringLiteral("node-1"));
    QCOMPARE(recorderJson.value(QStringLiteral("passed")).toBool(), false);

    RecordNode recorderRoundTrip;
    recorderRoundTrip.init();
    recorderRoundTrip.fromJson(recorderJson);
    QCOMPARE(recorderRoundTrip.getParam(QStringLiteral("flowName")).toString(), QStringLiteral("flow-A"));
    QCOMPARE(recorderRoundTrip.getParam(QStringLiteral("nodeName")).toString(), QStringLiteral("node-1"));
    QCOMPARE(recorderRoundTrip.getParam(QStringLiteral("passed")).toBool(), false);

    // 旧格式（只有顶层键）：三键旧实现都带默认值 ⇒ 缺键**保留原值**
    //（与上一类 ImageRead 的 filePath"无条件重置"相反——逐类对齐）
    QJsonObject legacyRecorder;
    legacyRecorder[QStringLiteral("nodeName")] = QStringLiteral("legacy-node");
    RecordNode recorderLegacy;
    recorderLegacy.init();
    recorderLegacy.setParam(QStringLiteral("flowName"), QStringLiteral("keep-me"));
    recorderLegacy.setParam(QStringLiteral("passed"), false);
    recorderLegacy.fromJson(legacyRecorder);
    QCOMPARE(recorderLegacy.getParam(QStringLiteral("nodeName")).toString(), QStringLiteral("legacy-node"));
    QCOMPARE(recorderLegacy.getParam(QStringLiteral("flowName")).toString(), QStringLiteral("keep-me"));
    QVERIFY2(!recorderLegacy.getParam(QStringLiteral("passed")).toBool(),
             "旧格式缺 passed 键时必须保留原值（旧实现带默认值，不是重置）");
}

void IntegrationTest::testFlowExtrasRoundTrip()
{
    // 每流程身份必须随方案持久化：
    //  · flowName —— 触发按名路由的键（重排/删除流程后不得漂移）；
    //  · flowMode —— 现场要求"不是所有流程都要连续"，所以模式必须跟着流程走、能存能读。
    FlowScene scene;
    scene.setFlowName(QStringLiteral("F1"));
    scene.setFlowMode(0);   // 0=连续
    const QJsonObject json = scene.extrasToJson();
    QCOMPARE(json.value(QStringLiteral("flowName")).toString(), QStringLiteral("F1"));
    QCOMPARE(json.value(QStringLiteral("flowMode")).toInt(), 0);

    FlowScene restored;
    restored.extrasFromJson(json);
    QCOMPARE(restored.flowName(), QStringLiteral("F1"));
    QCOMPARE(restored.flowMode(), 0);

    // 旧方案（无 flowMode 字段）：模式必须回落"软触发"(1)，绝不能把"连续"当默认——
    // 否则老方案一载入就自动循环跑，风险远大于收益。
    FlowScene legacy;
    legacy.extrasFromJson(QJsonObject());
    QVERIFY(legacy.flowName().isEmpty());
    QCOMPARE(legacy.flowMode(), 1);
}

void IntegrationTest::testExecutionStatusController()
{
    // Step 3 回归：执行状态机（状态文本/触发计数/耗时）与按钮态规则。
    // 按钮态规则（运行控制与流程模式解耦，对齐 VisionMaster 的操作逻辑）：
    //   开始执行——任何模式下只要没在跑就可点（暂停中语义=继续）；
    //   暂停/继续——运行中显示「暂停」，暂停中显示「继续」；
    //   停止执行——运行中与暂停中都可点；
    //   单次执行——仅软触发且未运行时可用。
    // 历史缺陷（现场反馈"连续模式点开始没用"）：旧规则"仅软触发才启用开始"，且 Idle/Paused 落在
    // default 分支不刷新按钮态 → 连续模式下停止后「开始」永久禁用、点了也没反应。
    QStatusBar bar;
    ExecutionStatusController ctrl(&bar);

    // 无控件/无执行器：只跑状态机，不得崩溃
    ctrl.updateButtons(ExecutionState::Running);
    ctrl.onStarted();
    QCOMPARE(ctrl.stateText(), QStringLiteral("状态: 运行中"));
    ctrl.onFinished();   // 无执行器 → 按非连续处理
    QCOMPARE(ctrl.triggerCount(), 1);
    QVERIFY(ctrl.lastRunMs() >= 0);
    QCOMPARE(ctrl.stateText(), QStringLiteral("状态: 空闲"));
    ctrl.onError(QStringLiteral("boom"));
    QCOMPARE(ctrl.stateText(), QStringLiteral("状态: 错误"));
    ctrl.onStopped();
    QCOMPARE(ctrl.stateText(), QStringLiteral("状态: 已停止"));

    // 暂停/继续的状态反馈（与执行器 executionPaused/Parked/Resumed 三信号一一对应）
    ctrl.onPaused();
    QCOMPARE(ctrl.stateText(), QStringLiteral("状态: 暂停中…"));
    ctrl.onParked();
    QCOMPARE(ctrl.stateText(), QStringLiteral("状态: 已暂停"));
    ctrl.onResumed();
    QCOMPARE(ctrl.stateText(), QStringLiteral("状态: 运行中"));

    // 按钮态：注入真实控件 + 执行器
    QAction startAction;
    QAction stopAction;
    QToolButton singleShot;
    QToolButton pauseBtn;
    FlowExecutor ex;
    ctrl.setControls(&startAction, &stopAction, &singleShot, &pauseBtn);
    ctrl.setExecutorProvider([&ex]() { return &ex; });

    // 软触发 + 停止：可开始、可单次；不可停止；暂停按钮禁用
    ex.setFlowMode(FlowMode::SoftwareTrigger);
    ctrl.updateButtons(ExecutionState::Stopped);
    QVERIFY(startAction.isEnabled() && singleShot.isEnabled());
    QVERIFY(!stopAction.isEnabled() && !pauseBtn.isEnabled());

    // 运行中：可停止、可暂停（文字「暂停」）；开始/单次禁用
    ctrl.updateButtons(ExecutionState::Running);
    QVERIFY(!startAction.isEnabled() && !singleShot.isEnabled());
    QVERIFY(stopAction.isEnabled() && pauseBtn.isEnabled());
    QCOMPARE(pauseBtn.text(), QStringLiteral("暂停"));

    // 暂停中：开始（=继续）与停止都必须可用，暂停按钮变「继续」
    ctrl.updateButtons(ExecutionState::Paused);
    QVERIFY2(startAction.isEnabled(), "暂停中「开始执行」必须可用（语义=继续），不得是死按钮");
    QVERIFY2(stopAction.isEnabled(), "暂停中必须能停止");
    QVERIFY(pauseBtn.isEnabled());
    QCOMPARE(pauseBtn.text(), QStringLiteral("继续"));
    QVERIFY(!singleShot.isEnabled());

    // 连续模式 + 停止：开始必须可用（旧规则在此永久禁用 → 现场"连续模式点开始没用"）
    ex.setFlowMode(FlowMode::Continuous);
    ctrl.updateButtons(ExecutionState::Stopped);
    QVERIFY2(startAction.isEnabled(), "连续模式下停止后「开始执行」必须可用（现场回归点）");
    QVERIFY2(!stopAction.isEnabled(), "未运行时不得可停止");
    QVERIFY(!singleShot.isEnabled());   // 单次执行保持"以软触发跑一次"的语义
    QVERIFY(!pauseBtn.isEnabled());

    // 硬触发 + 空闲（Idle）：同样可开始（进入等待触发）
    ex.setFlowMode(FlowMode::HardwareTrigger);
    ctrl.updateButtons(ExecutionState::Idle);
    QVERIFY2(startAction.isEnabled(), "硬触发/空闲下「开始执行」必须可用（进入等待触发）");
    QVERIFY(!stopAction.isEnabled() && !pauseBtn.isEnabled());
}

void IntegrationTest::testRecentFilesMenu()
{
    // Step 4 回归：最近打开列表 = 去重置顶 + 上限 8 条 + 清空 + 跨实例往返。
    // 注入临时 ini，避免污染注册表（与原实现同键 "recentFiles" 的契约不变）。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ini = dir.filePath(QStringLiteral("recent.ini"));

    QMenu menu;
    QString opened;
    RecentFilesMenu recent(&menu, [&opened](const QString &p) { opened = p; }, ini, nullptr);
    QVERIFY(recent.files().isEmpty());

    for (int i = 0; i < 10; ++i)
        recent.add(QStringLiteral("C:/proj/p%1.vfp").arg(i));

    QStringList files = recent.files();
    QCOMPARE(files.size(), RecentFilesMenu::MaxEntries);          // 截断到 8 条
    QCOMPARE(files.first(), QStringLiteral("C:/proj/p9.vfp"));    // 最新置顶
    QVERIFY(!files.contains(QStringLiteral("C:/proj/p0.vfp")));   // 最旧被挤出

    // 重复加入 → 去重并置顶，总数不增长
    recent.add(QStringLiteral("C:/proj/p5.vfp"));
    files = recent.files();
    QCOMPARE(files.size(), RecentFilesMenu::MaxEntries);
    QCOMPARE(files.first(), QStringLiteral("C:/proj/p5.vfp"));

    // 菜单第一条即最新记录，触发后回调收到该路径（MainWindow 用它打开方案）
    QVERIFY(recent.menu() != nullptr);
    QVERIFY(!recent.menu()->actions().isEmpty());
    recent.menu()->actions().first()->trigger();
    QCOMPARE(opened, QStringLiteral("C:/proj/p5.vfp"));

    // 清空后菜单回到"（无最近记录）"占位（禁用）
    recent.clear();
    QVERIFY(recent.files().isEmpty());
    QVERIFY(!recent.menu()->actions().isEmpty());
    QVERIFY(!recent.menu()->actions().first()->isEnabled());

    // 跨实例往返（同一 ini）
    RecentFilesMenu recent2(&menu, [](const QString &) {}, ini, nullptr);
    QVERIFY(recent2.files().isEmpty());
    recent2.add(QStringLiteral("C:/proj/x.vfp"));
    RecentFilesMenu recent3(&menu, [](const QString &) {}, ini, nullptr);
    QCOMPARE(recent3.files().first(), QStringLiteral("C:/proj/x.vfp"));
}

void IntegrationTest::testModuleIdNotRecycled()
{
    // S4：模块号必须单调且不复用。旧实现删除节点后回收其模块号，新建节点会复用 →
    // "新节点"继承"旧节点"在执行器缓存里的输出变量表（m_nodeOutputVars）/输出数据/
    // 有效标记，结果错，是工业平台最坏故障之一。
    m_scene->clearScene();

    NodeBase *a = m_scene->createNode(NodeBase::IMAGE_ACQUISITION, QPointF(0, 0), QStringLiteral("A"));
    NodeBase *b = m_scene->createNode(NodeBase::IMAGE_ACQUISITION, QPointF(0, 0), QStringLiteral("B"));
    QVERIFY(a && b);
    const int idA = a->moduleId();
    const int idB = b->moduleId();
    QVERIFY2(idA != idB, "新建节点模块号重复");

    m_scene->removeNode(a);   // 旧实现：idA 进回收池，下一个新建节点会拿到它

    NodeBase *c = m_scene->createNode(NodeBase::IMAGE_ACQUISITION, QPointF(0, 0), QStringLiteral("C"));
    QVERIFY(c != nullptr);
    // 关键断言（判别项）：不复用时 c 的模块号必严格大于本测试内已分配的最大号 idB
    // （模块号单调增长）。旧实现会回收 idA（或任意更早的回收号）→ c 的号 <= idA < idB
    // → 此断言失败（S4 未修）。仅比 "!= idA" 不够稳健：更早的回收号更小也会落入此路径。
    QVERIFY2(c->moduleId() > idB,
             qPrintable(QStringLiteral("模块号被回收复用：删 A 后新建 C 拿到了 %1（<= 已分配的 %2），S4 未修")
                        .arg(c->moduleId()).arg(idB)));
    QVERIFY(c->moduleId() > idA);
}

QTEST_MAIN(IntegrationTest)
#include "integration_test.moc"
