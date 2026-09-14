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
#include "FormulaNode.h"
#include "ScriptSecurityPolicy.h"
#include "Port.h"
#include "Connection.h"
#include <QElapsedTimer>
#include <QThread>
#include <QCoreApplication>
#include <QProcess>

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
    void testConditionalBranchSkipClearsStaleOutput();
    void testNestedLoopIterations();
    void testRuntimeStatsCounters();

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

QTEST_MAIN(IntegrationTest)
#include "integration_test.moc"
