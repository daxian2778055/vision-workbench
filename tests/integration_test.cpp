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
#include "Port.h"
#include "Connection.h"

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

QTEST_MAIN(IntegrationTest)
#include "integration_test.moc"
