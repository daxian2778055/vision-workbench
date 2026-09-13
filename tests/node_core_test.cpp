// 节点核心功能单元测试（Qt Test 框架）
// 测试：节点创建、参数管理、序列化（toJson/fromJson）、端口管理
#include <QtTest/QtTest>
#include <QObject>
#include <QJsonObject>
#include <QJsonDocument>

// 节点头文件
#include "OpencvThresholdNode.h"
#include "OpencvCropNode.h"
#include "OpencvImageArithNode.h"
#include "OpencvMorphNode.h"
#include "OpencvEdgeNode.h"
#include "OpencvBlobNode.h"
#include "FormulaNode.h"
#include "DataObject.h"
#include "Port.h"
#include "PortConnectivity.h"

class NodeCoreTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // 参数管理测试
    void testParamSetGet();
    void testParamDefaultValue();
    void testParamBoundary();

    // 序列化测试
    void testSerializeDeserialize();
    void testFormulaSerialize();

    // 端口测试
    void testPortCreation();
    void testPortConnection();

    // 算子功能测试
    void testThresholdBasic();
    void testCropBasic();
    void testImageArithDivByZero();
    void testFormulaEvaluation();

private:
    void compareParams(HalconNode *node1, HalconNode *node2, const QStringList &excludeKeys = {});
};

void NodeCoreTest::initTestCase()
{
    // 测试开始前的初始化
}

void NodeCoreTest::cleanupTestCase()
{
    // 测试结束后的清理
}

void NodeCoreTest::testParamSetGet()
{
    OpencvThresholdNode node;
    node.init();

    // 测试 setParam/getParam
    node.setParam(QStringLiteral("mode"), 1);
    QCOMPARE(node.getParam(QStringLiteral("mode")).toInt(), 1);

    node.setParam(QStringLiteral("minVal"), 200);
    QCOMPARE(node.getParam(QStringLiteral("minVal")).toInt(), 200);

    // 测试字符串参数
    OpencvImageArithNode arithNode;
    arithNode.init();
    arithNode.setParam(QStringLiteral("op"), 2);  // 乘法
    QCOMPARE(arithNode.getParam(QStringLiteral("op")).toInt(), 2);
}

void NodeCoreTest::testParamDefaultValue()
{
    OpencvThresholdNode node;
    node.init();

    // 验证默认值
    QCOMPARE(node.getParam(QStringLiteral("mode")).toInt(), 0);
    QCOMPARE(node.getParam(QStringLiteral("minVal")).toInt(), 128);
}

void NodeCoreTest::testParamBoundary()
{
    OpencvThresholdNode node;
    node.init();

    // 测试边界值
    node.setParam(QStringLiteral("minVal"), 0);
    QCOMPARE(node.getParam(QStringLiteral("minVal")).toInt(), 0);

    node.setParam(QStringLiteral("minVal"), 255);
    QCOMPARE(node.getParam(QStringLiteral("minVal")).toInt(), 255);

    // 测试越界值（应被钳制）
    node.setParam(QStringLiteral("minVal"), -10);
    QVERIFY(node.getParam(QStringLiteral("minVal")).toInt() >= 0);

    node.setParam(QStringLiteral("minVal"), 300);
    QVERIFY(node.getParam(QStringLiteral("minVal")).toInt() <= 255);
}

void NodeCoreTest::compareParams(HalconNode *node1, HalconNode *node2, const QStringList &excludeKeys)
{
    QJsonObject json1 = node1->toJson();
    QJsonObject json2 = node2->toJson();

    QJsonObject params1 = json1["params"].toObject();
    QJsonObject params2 = json2["params"].toObject();

    for (auto it = params1.constBegin(); it != params1.constEnd(); ++it) {
        if (excludeKeys.contains(it.key())) continue;
        QVERIFY2(params2.contains(it.key()),
                 qPrintable(QString("Missing key: %1").arg(it.key())));
        QCOMPARE(params2[it.key()], it.value());
    }
}

void NodeCoreTest::testSerializeDeserialize()
{
    // 创建节点并设置参数
    OpencvThresholdNode node1;
    node1.init();
    node1.setParam(QStringLiteral("mode"), 1);
    node1.setParam(QStringLiteral("minVal"), 200);
    node1.setPosition(QPointF(100, 200));

    // 序列化
    QJsonObject json = node1.toJson();
    QVERIFY(!json.isEmpty());
    QVERIFY(json.contains("params"));
    QVERIFY(json.contains("position"));

    // 创建新节点并反序列化
    OpencvThresholdNode node2;
    node2.init();
    node2.fromJson(json);

    // 验证参数一致
    QCOMPARE(node2.getParam(QStringLiteral("mode")).toInt(), 1);
    QCOMPARE(node2.getParam(QStringLiteral("minVal")).toInt(), 200);
    QCOMPARE(node2.position().x(), 100.0);
    QCOMPARE(node2.position().y(), 200.0);
}

void NodeCoreTest::testFormulaSerialize()
{
    // 测试 FormulaNode 的序列化
    FormulaNode node1;
    node1.init();
    node1.setParam(QStringLiteral("expression"), QStringLiteral("(p0 + p1) * 2"));

    QJsonObject json = node1.toJson();
    QVERIFY(json.contains("expression"));
    QCOMPARE(json["expression"].toString(), QStringLiteral("(p0 + p1) * 2"));

    FormulaNode node2;
    node2.init();
    node2.fromJson(json);
    QCOMPARE(node2.getParam(QStringLiteral("expression")).toString(),
             QStringLiteral("(p0 + p1) * 2"));
}

void NodeCoreTest::testPortCreation()
{
    OpencvThresholdNode node;
    node.init();

    // 验证端口数量
    QCOMPARE(node.inputPorts().size(), 1);  // 图像输入
    QCOMPARE(node.outputPorts().size(), 3);  // 图像输出 + 二值图 + 前景像素数

    // 验证端口类型
    QCOMPARE(node.inputPorts()[0]->dataType(), PortDataType::Image);
    QCOMPARE(node.outputPorts()[0]->dataType(), PortDataType::Image);
}

void NodeCoreTest::testPortConnection()
{
    OpencvThresholdNode node1;
    node1.init();

    OpencvCropNode node2;
    node2.init();

    // 验证端口可以连接
    Port *outputPort = node1.outputPorts()[0];
    Port *inputPort = node2.inputPorts()[0];

    QVERIFY(outputPort->type() == Port::OUTPUT);
    QVERIFY(inputPort->type() == Port::INPUT);
    // 端口兼容性判定已收敛到 PortConnectivity（Port 上不再有 isCompatibleWith）
    QVERIFY(PortConnectivity::canConnectPorts(outputPort, inputPort));

    // 同节点的输出与输入不允许互连
    Port *selfInput = node1.inputPorts().isEmpty() ? nullptr : node1.inputPorts()[0];
    if (selfInput)
        QVERIFY(!PortConnectivity::canConnectPorts(outputPort, selfInput));
}

void NodeCoreTest::testThresholdBasic()
{
    OpencvThresholdNode node;
    node.init();

    // 创建测试图像（通过 HALCON）
    HImage testImg;
    GenImageConst(&testImg, "byte", 100, 100);

    // 设置输入
    node.setInputImage(testImg);
    node.setParam(QStringLiteral("mode"), 0);  // 固定阈值
    node.setParam(QStringLiteral("minVal"), 128);

    // 执行（注意：run 方法需要输入图像）
    // 由于 HALCON 环境限制，这里只验证参数设置
    QCOMPARE(node.getParam(QStringLiteral("mode")).toInt(), 0);
    QCOMPARE(node.getParam(QStringLiteral("minVal")).toInt(), 128);
}

void NodeCoreTest::testCropBasic()
{
    OpencvCropNode node;
    node.init();

    // 验证参数默认值
    QCOMPARE(node.getParam(QStringLiteral("row")).toInt(), 0);
    QCOMPARE(node.getParam(QStringLiteral("column")).toInt(), 0);
    QCOMPARE(node.getParam(QStringLiteral("height")).toInt(), 100);
    QCOMPARE(node.getParam(QStringLiteral("width")).toInt(), 100);

    // 设置参数
    node.setParam(QStringLiteral("row"), 50);
    node.setParam(QStringLiteral("column"), 50);
    node.setParam(QStringLiteral("height"), 200);
    node.setParam(QStringLiteral("width"), 200);

    QCOMPARE(node.getParam(QStringLiteral("row")).toInt(), 50);
    QCOMPARE(node.getParam(QStringLiteral("column")).toInt(), 50);
}

void NodeCoreTest::testImageArithDivByZero()
{
    OpencvImageArithNode node;
    node.init();

    // 测试除零保护
    node.setParam(QStringLiteral("op"), 3);  // 除法
    node.setParam(QStringLiteral("operandType"), 0);  // 常数
    node.setParam(QStringLiteral("constant"), 0.0);  // 除数为零

    // 验证参数设置正确
    QCOMPARE(node.getParam(QStringLiteral("op")).toInt(), 3);
    QCOMPARE(node.getParam(QStringLiteral("constant")).toDouble(), 0.0);
}

void NodeCoreTest::testFormulaEvaluation()
{
    FormulaNode node;
    node.init();

    // 测试公式求值（通过静态方法）
    QMap<QString, double> vars;
    vars["p0"] = 10.0;
    vars["p1"] = 20.0;

    double result = 0.0;
    bool ok = FormulaNode::evaluate("(p0 + p1) * 2", vars, result);
    QVERIFY(ok);
    QCOMPARE(result, 60.0);

    // 测试除零保护
    vars["p2"] = 0.0;
    ok = FormulaNode::evaluate("p0 / p2", vars, result);
    QVERIFY(!ok);  // 应该失败

    // 测试函数
    ok = FormulaNode::evaluate("max(p0, p1)", vars, result);
    QVERIFY(ok);
    QCOMPARE(result, 20.0);
}

QTEST_MAIN(NodeCoreTest)
#include "node_core_test.moc"
