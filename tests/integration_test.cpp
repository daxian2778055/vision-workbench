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
#include "Port.h"
#include "Connection.h"
#include <QElapsedTimer>
#include <QThread>
#include <QCoreApplication>

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

QTEST_MAIN(IntegrationTest)
#include "integration_test.moc"
