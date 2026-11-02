// B1 统一输出端口门槛线：
//   1) 注册表里每个"聚合型"算子（HalconNode 家族）对外只有一个输出端口；
//   2) 第 2..N 个输出被折叠成同一端口对象上的具名子结果（不是丢掉）；
//   3) 数据类算子从唯一端口仍能取到主值（Number/Measure）。
// 这条用例一次守住全部工具：以后新增算子只要忘了走 HalconNode::init()/addOutputPort，
// 就会在这里变成"多了一个红点"。
#include <QtTest/QtTest>
#include <QObject>
#include <QSharedPointer>

#include "NodeRegistry.h"
#include "NodeBase.h"
#include "HalconNode.h"
#include "DataObject.h"
#include "Port.h"
#include "FormulaNode.h"
#include "OpencvTemplateMatchNode.h"
#include "OpencvThresholdNode.h"
#include "FlowScene.h"
#include "ProjectManager.h"
#include <QJsonArray>
#include <QJsonObject>

class SingleOutputPortTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { registerAllNodes(); }   // 注册表按需初始化（NodeFactory 首次使用）
    void testAllRegisteredToolsHaveOneOutputPort();
    void testTemplateMatchOnlyOnePort();
    void testFoldedSubOutputsStillReachable();
    void testDataNodePrimaryValueOnSinglePort();
    void testDataObjectAttachMeasureKeepsImageSlot();
    void testLegacyOutPortIndexMigratesToSinglePort();
};

void SingleOutputPortTest::testAllRegisteredToolsHaveOneOutputPort()
{
    const QList<NodeRegistration> regs = NodeRegistry::instance().all();
    QVERIFY2(regs.size() > 10, qPrintable(QStringLiteral("注册表为空/过小：%1").arg(regs.size())));

    int aggregated = 0;
    QStringList offenders;
    for (const NodeRegistration &reg : regs) {
        NodeBase *node = NodeRegistry::instance().createById(reg.id, nullptr);
        if (!node)
            continue;
        node->init();
        const int outN = node->outputPorts().size();
        if (node->aggregatesOutputPorts()) {
            ++aggregated;
            // 0 = 纯汇聚/发送类算子（如发送数据），本就不产数据；>1 才是违反单输出契约
            if (outN > 1) {
                offenders << QStringLiteral("%1(%2) 出端口=%3").arg(reg.id, reg.displayName).arg(outN);
            }
        }
        delete node;
    }

    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral("以下算子仍有多个输出端口：") + offenders.join(QStringLiteral("；"))));
    QVERIFY2(aggregated >= regs.size() - 2,
             qPrintable(QStringLiteral("聚合算子只覆盖 %1/%2 —— 有节点没走 HalconNode 家族")
                            .arg(aggregated).arg(regs.size())));
}

void SingleOutputPortTest::testTemplateMatchOnlyOnePort()
{
    OpencvTemplateMatchNode node;
    node.init();
    QCOMPARE(node.outputPorts().size(), 1);
    // 聚合端口对下游一视同仁；"这个算子出不出图"记在声明类型上
    QCOMPARE(node.outputPorts().first()->dataType(), PortDataType::Any);
    // 模板匹配本来就没有"输出图像"主端口：唯一端口就是「匹配结果」，不再另加子结果
    QCOMPARE(node.outputPorts().first()->name(), QStringLiteral("匹配结果"));
    QCOMPARE(node.logicalOutputs().size(), 0);
}

void SingleOutputPortTest::testFoldedSubOutputsStillReachable()
{
    OpencvThresholdNode node;
    node.init();
    QCOMPARE(node.outputPorts().size(), 1);
    QCOMPARE(node.logicalOutputs().size(), 2);
    // 折叠的是"接口上的点"，不是数据：旧读取者按端口号取（getOutputData(1/2)）仍可用
    // （用例/脚本/旧插件路径），此处只钉住名字与顺序没有漂移。
    QCOMPARE(node.logicalOutputs().at(0).first, QStringLiteral("二值图"));
    QCOMPARE(node.logicalOutputs().at(1).first, QStringLiteral("前景像素数"));
}

void SingleOutputPortTest::testDataNodePrimaryValueOnSinglePort()
{
    FormulaNode node;
    node.init();
    node.setParam(QStringLiteral("expression"), QStringLiteral("p0 + p1"));

    auto a = QSharedPointer<DataObject>::create(DataObject::DataType::Number, QVariant(4.0));
    auto b = QSharedPointer<DataObject>::create(DataObject::DataType::Number, QVariant(6.0));
    node.setInputData(0, a);
    node.setInputData(1, b);

    QVERIFY2(node.execute(), "公式节点执行应成功");
    const auto out = node.getOutputData(0);
    QVERIFY(out);
    bool ok = false;
    QCOMPARE(out->primaryNumber(&ok), 10.0);
    QVERIFY2(ok, "唯一端口上应能取到数值主值");
    QCOMPARE(out->primaryName(), QStringLiteral("计算结果"));
}

void SingleOutputPortTest::testDataObjectAttachMeasureKeepsImageSlot()
{
    // 统一输出的核心契约：一个对象同时带"主值 + 测量结果"，互不覆盖
    DataObject obj;
    obj.setData(QVariant(12.5));
    obj.setType(DataObject::DataType::Number);
    MeasureResult mr;
    mr.valid = true;
    mr.value = 12.5;
    mr.type = QStringLiteral("template");
    obj.attachMeasureResult(mr);

    QVERIFY(obj.hasMeasureResult());
    QCOMPARE(obj.measureResults().size(), 1);
    QCOMPARE(obj.getType(), DataObject::DataType::Number);   // 主类型不被测量结果抢走
    QCOMPARE(obj.getData().toDouble(), 12.5);
    bool ok = false;
    QCOMPARE(obj.primaryNumber(&ok), 12.5);
    QVERIFY(ok);
}

void SingleOutputPortTest::testLegacyOutPortIndexMigratesToSinglePort()
{
    // 老方案兼容：B1 之前保存的连线可能指向第 2..N 个输出（现在被折叠进唯一端口）。
    // 载入时必须改接到端口 0，否则现场升级后整条线静默消失、结果全空。
    FlowScene scene;
    NodeBase *thr = scene.createNode(NodeBase::NodeType::IMAGE_PROCESSING,
                                     QPointF(100, 100), QStringLiteral("二值化"));
    QVERIFY2(thr, "二值化算子应能从注册表创建");
    QCOMPARE(thr->property("vfpNodeTypeId").toString(), QStringLiteral("OpencvThresholdNode"));
    QCOMPARE(thr->outputPorts().size(), 1);            // 已聚合
    QCOMPARE(thr->logicalOutputPortCount(), 3);        // 输出 / 二值图 / 前景像素数

    NodeBase *fml = scene.createNode(NodeBase::NodeType::IMAGE_PROCESSING,
                                     QPointF(400, 100), QStringLiteral("公式计算"));
    QVERIFY2(fml, "公式计算算子应能从注册表创建");
    QVERIFY(scene.createConnection(thr->outputPorts().first(), fml->inputPorts().first(), true));

    ProjectManager pm;
    QJsonObject root = pm.buildProjectJson({&scene});
    QJsonArray scenes = root.value(QStringLiteral("scenes")).toArray();
    QCOMPARE(scenes.size(), 1);
    QJsonObject sc = scenes.at(0).toObject();
    QJsonArray conns = sc.value(QStringLiteral("connections")).toArray();
    QCOMPARE(conns.size(), 1);
    QJsonObject conn = conns.at(0).toObject();
    conn[QStringLiteral("sourcePortIndex")] = 2;       // 模拟老方案里的旧端口号
    conns[0] = conn;
    sc[QStringLiteral("connections")] = conns;
    scenes[0] = sc;
    root[QStringLiteral("scenes")] = scenes;

    ProjectManager loader;
    QList<FlowScene *> opened;
    QVERIFY2(loader.applyProjectJson(root, opened), "老方案应能载入");
    QCOMPARE(opened.size(), 1);
    QCOMPARE(opened.first()->connections().size(), 1);   // 线没断：已迁移到唯一端口
    qDeleteAll(opened);
}

QTEST_MAIN(SingleOutputPortTest)
#include "single_output_port_test.moc"
