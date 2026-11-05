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
#include "DataObject.h"
#include "Port.h"
#include "FormulaNode.h"
#include "OpencvTemplateMatchNode.h"
#include "OpencvThresholdNode.h"

class SingleOutputPortTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { registerAllNodes(); }   // 注册表按需初始化（NodeFactory 首次使用）
    void testAllRegisteredToolsHaveOneOutputPort();
    void testAllRegisteredToolsHaveAtMostOneInputPort();
    void testTemplateMatchOnlyOnePort();
    void testTemplateMatchSingleInputPort();
    void testTemplateResultJudgment();
    void testFoldedSubOutputsStillReachable();
    void testDataNodePrimaryValueOnSinglePort();
    void testDataObjectAttachMeasureKeepsImageSlot();
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

void SingleOutputPortTest::testAllRegisteredToolsHaveAtMostOneInputPort()
{
    // B2 单输入契约：画布上一个工具最多 1 个输入点（图像源类 0 个，本就无输入）。
    // 第 2..N 个输入必须是逻辑槽（inputSlotCount），不得是可见 Port。
    const QList<NodeRegistration> regs = NodeRegistry::instance().all();
    QVERIFY2(regs.size() > 10, "注册表为空/过小");

    QStringList offenders;
    for (const NodeRegistration &reg : regs) {
        NodeBase *node = NodeRegistry::instance().createById(reg.id, nullptr);
        if (!node)
            continue;
        node->init();
        if (node->inputPorts().size() > 1) {
            offenders << QStringLiteral("%1(%2) 入端口=%3（逻辑槽=%4）")
                             .arg(reg.id, reg.displayName)
                             .arg(node->inputPorts().size())
                             .arg(node->inputSlotCount());
        }
        delete node;
    }
    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral("以下算子仍有多个输入端口：") + offenders.join(QStringLiteral("；"))));
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

void SingleOutputPortTest::testTemplateMatchSingleInputPort()
{
    // B2：画布上一个工具只允许 1 个输入点。模板匹配的「掩膜图像」按 VM 4.4
    // 基本参数页实现为订阅，不是第二个端口。
    OpencvTemplateMatchNode node;
    node.init();
    QCOMPARE(node.inputPorts().size(), 1);
    QCOMPARE(node.inputPorts().first()->name(), QStringLiteral("输入图像"));
    QCOMPARE(node.logicalInputPortCount(), 2);          // 真实输入 0 + 逻辑槽 1
    QCOMPARE(node.inputSlotName(1), QStringLiteral("掩膜图像"));

    // 真实端口不接受订阅绑定（只允许逻辑槽，防止绕过连线语义）
    node.setInputBinding(0, 42, 0);
    QVERIFY(!node.hasInputBinding(0));
    // 逻辑槽可绑定
    node.setInputBinding(1, 42, 0);
    QVERIFY(node.hasInputBinding(1));
    QCOMPARE(node.inputBindings().value(1).sourceModuleId, 42);
}

void SingleOutputPortTest::testTemplateResultJudgment()
{
    // VM「结果显示 → 结果判断」：未启用任何条件时保持旧行为（通过）；
    // 启用后越界必须给出可读原因；「无目标时 OK」优先于其它条件。
    OpencvTemplateMatchNode node;
    node.init();
    QString reason;
    QVERIFY(node.resultJudgmentOk(&reason));

    node.setParam(QStringLiteral("matchCount"), 1);
    node.setParam(QStringLiteral("matchAngle"), 5.0);
    node.setParam(QStringLiteral("matchScore"), 0.9);
    node.setParam(QStringLiteral("resJudgeNumEnable"), true);
    node.setParam(QStringLiteral("resJudgeNumLow"), 2.0);
    node.setParam(QStringLiteral("resJudgeNumHigh"), 5.0);
    QVERIFY2(!node.resultJudgmentOk(&reason), "数量 1 应判 NG");
    QVERIFY2(reason.contains(QStringLiteral("匹配数量")), qPrintable(reason));

    node.setParam(QStringLiteral("resJudgeNumEnable"), false);
    node.setParam(QStringLiteral("resJudgeOkWhenZero"), true);
    node.setParam(QStringLiteral("matchCount"), 0);
    node.setParam(QStringLiteral("resJudgeScoreEnable"), true);
    node.setParam(QStringLiteral("resJudgeScoreLow"), 0.8);
    node.setParam(QStringLiteral("resJudgeScoreHigh"), 1.0);
    QVERIFY2(node.resultJudgmentOk(&reason), "无目标且勾选 OKWhenNumIsZero 应通过");
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

QTEST_MAIN(SingleOutputPortTest)
#include "single_output_port_test.moc"
