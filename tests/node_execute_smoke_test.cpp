// 全节点 execute() 冒烟门禁（P0-1 的回归锁）
//
// 背景：HalconNode::init() 把 moduleStatus 初值化为 false，而本族多数节点的 run() 只在失败
// 分支写 false、成功路径从不写位 ⇒ 这些算子被拖进流程即恒判失败（m_stopOnFailure 默认 true
// 还会中断整条流程）。2026-09-25 实测：修复前 63 个可脱机执行的注册节点里 **34 个恒判失败**，
// 其中 23 个是纯契约缺陷（把成功默认值交给基类后即翻正），其余 11 个是"缺外部资源/图里没结果"
// 的真实失败。
//
// 本用例把六件事钉住，全部走 NodeRegistry（新增算子自动进覆盖面，不需要再补用例）：
//   ① 默认参数 + 一张合成图 ⇒ execute() 必须成功，并且"对外承诺图像"的 0 号输出端口必须有
//      真实产出（只判 execute() 抓不到 run() 里 Clear() 后 return 的静默绿灯，见 S-1）；
//   ② 声明了 Image 输入端口的节点，本轮没有图像 ⇒ execute() 必须失败（不许静默绿灯）；
//   ③ 缺模型/未教学的节点 ⇒ 必须显式失败（防"成功默认值"被改成"永不失败"）；
//   ④ 有 ≥2 路 Image 输入端口的节点喂进尺寸不符的两路图 ⇒ 不许"成功且零产出"（专门走 Clear() 分支）；
//   ⑤ 跳过清单与注册表自洽 ⇒ 算子改名/注销时覆盖面缩水必须变红，而不是静默退出覆盖面；
//   ⑥ W-2 数据侧孪生：没有 Image 输入端口的数据节点空载 ⇒ 必须失败并写 moduleStatus=false、
//      清空输出；同时反向钉住"有效输入必须仍成功"和 Delay 的豁免（否则"空载必红"可能只是
//      恰好和"无条件失败"撞在一起）。
// 另：两侧守卫本体都由测试内探针节点做直接回归锁（图像侧：只 Clear() / 默认透传 / 覆写豁免；
// 数据侧：空载 / 有输入零产出 / 正常产出 / 覆写豁免），不依赖"恰好有真实节点走到该分支"
// ——见 outputGuardShapesBehaveAsContracted 与 dataGuardShapesBehaveAsContracted。
#include <QtTest/QtTest>
#include <QHash>
#include <QSet>
#include <QStringList>

#include "NodeRegistry.h"
#include "NodeBase.h"
#include "HalconNode.h"
#include "FormulaNode.h"
#include "Port.h"
#include "DataObject.h"
#include "OpencvUtil.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <HalconCpp.h>

#include <algorithm>

namespace {

// 有结构的合成图：纯色图会把"合法地没有结果"与"契约缺陷"混在一起，分不清
cv::Mat makePart(int w = 64, int h = 64)
{
    cv::Mat img(h, w, CV_8UC1, cv::Scalar(40));
    cv::rectangle(img, cv::Rect(12, 12, 24, 18), cv::Scalar(210), cv::FILLED);
    cv::circle(img, cv::Point(46, 20), 8, cv::Scalar(130), cv::FILLED);
    cv::line(img, cv::Point(6, 52), cv::Point(58, 46), cv::Scalar(235), 2);
    return img;
}

// 需要设备 / 网络 / 磁盘副作用 / 真实解释器的节点：本门禁不执行（不是"判定通过"）
const QHash<QString, QString> kNeedsDeviceOrIo = {
    {QStringLiteral("ImageReadNode"), QStringLiteral("读磁盘文件")},
    {QStringLiteral("MvsImageSourceNode"), QStringLiteral("需 MVS 相机")},
    {QStringLiteral("HalconImageSourceNode"), QStringLiteral("需采集设备")},
    {QStringLiteral("CameraIoNode"), QStringLiteral("需相机/GPIO 设备")},
    {QStringLiteral("SendDataNode"), QStringLiteral("需通讯对端")},
    {QStringLiteral("ReceiveDataNode"), QStringLiteral("需通讯对端（会等待）")},
    {QStringLiteral("WriteFileNode"), QStringLiteral("写盘副作用")},
    {QStringLiteral("ScriptNode"), QStringLiteral("启动外部解释器（超时/环境相关）")},
};

// 缺外部资源或图里没有可识别结果 ⇒ 必须显式失败的那批（同时是②③的反向守卫）
const QHash<QString, QString> kFailsWithoutResource = {
    {QStringLiteral("AnomalyDetectNode"), QStringLiteral("未教学，无基线模型")},
    {QStringLiteral("OpencvTrainClassifierNode"), QStringLiteral("无数据集目录")},
    {QStringLiteral("OpencvClassifyNode"), QStringLiteral("无分类模型文件")},
    {QStringLiteral("DnnInferNode"), QStringLiteral("无 ONNX 模型")},
    {QStringLiteral("DnnDetectNode"), QStringLiteral("无 ONNX 模型")},
    {QStringLiteral("DnnSegmentNode"), QStringLiteral("无 ONNX 模型")},
    {QStringLiteral("OpencvCalibNode"), QStringLiteral("无标定数据")},
    {QStringLiteral("OpencvQrNode"), QStringLiteral("合成图内无二维码 ⇒ 无结果是失败")},
    {QStringLiteral("ZxingBarcodeNode"), QStringLiteral("合成图内无条形码 ⇒ 无结果是失败")},
    {QStringLiteral("TesseractOcrNode"), QStringLiteral("语言数据目录不存在 ⇒ 引擎初始化失败（路径已在 kParamTweaks 里钉死）")},
    // 特征定位族：本合成件**没有可教学的纹理**（实测 ORB 整图 0 点 / 中心教学窗 0 点；
    // SIFT 整图 6 点 / 教学窗 0 点 ⇒ 换默认检测器也拿不到点）。判失败是诚实结果，
    // 让它"成功"的唯一办法是把零对应点判成命中 = 静默绿灯。归类专测见 FeatureMatchTest。
    {QStringLiteral("OpencvFeatureMatchNode"), QStringLiteral("图内无特征可教学 ⇒ 无结果是失败")},
};

// W-2 的专测清单：这八个节点的 process() 走 HalconNode::processDataOutputs() 且**受两道判据约束**。
// 清单与 dataOnlyNode()（端口类型口径）互为正反两面校验：不一致就是"新增了数据节点没进专测"
// 或"某节点的端口类型口径变了"，两种都必须变红。
const QStringList kW2GuardedNodeIds = {
    QStringLiteral("ClassifyNode"), QStringLiteral("CounterNode"), QStringLiteral("FilterNode"),
    QStringLiteral("FormulaNode"), QStringLiteral("FormatNode"), QStringLiteral("ProtocolParseNode"),
    QStringLiteral("RecordNode"), QStringLiteral("SortNode"),
};

// 被端口类型口径**命中但有意豁免**的节点：豁免必须带理由并在这里显式列出，
// 不能靠把判据改弱（例如改成"按类名认"）来解决——那样下一次多收就没人发现了。
// DelayNode：延时的职责是时间门控（循环体/暂停/节拍），`数据输入` 只是可选透传；
//   强制必填后，全量门禁实测 4 个套件 / 14 个测试函数变红（SoakTest 1、NodeGroupTest 2、
//   FlowSnippetTest 1、IntegrationTest 10）；前三个套件已 grep 核实跑的都是纯延时流程。
//   且它空载时不产出任何"参数派生假结果"（run() 明确清空输出）⇒ 与 LoopNode
//   "透传即视为成功（E5）"同族。由 delayStaysGreenOnEmptyLoad 反向钉住，防止豁免被顺手删掉。
// SubFlowNode：process() 是 `return invoke()`（不是 `{ run(); return true; }` 形态），成功与否
//   由子流程自己的执行结果决定；Any 端口是调用接口 ⇒ 留在断言① 的喂图冒烟里。
const QHash<QString, QString> kW2ExemptDataOnlyNodes = {
    {QStringLiteral("DelayNode"), QStringLiteral("时间门控/循环体，空载合法；与 LoopNode 的 E5 透传口径同族")},
    {QStringLiteral("SubFlowNode"), QStringLiteral("成功由子流程执行结果决定，Any 端口是调用接口而非数据输入")},
};

// ---- A5：成功但"全端口零产出"的节点清单（闸的白名单）----
// 由 baseProcessNodesWithDataPortsAreMeasured() 在真实注册表上量出来，不是手抄的猜测。
// SubFlowNode 唯一的零产出路径是 `invoke()` 里"无执行器上下文"那一支（设计期自检：
// src/SubFlowNode.cpp 的 `if (!exec)` ⇒ 清空输出并返回 true）。它和 DelayNode 的 E5
// "透传即视为成功"同族：没有数据可传时，空就是诚实结果，而不是编造出来的假结果。
// 冒烟没有执行器 ⇒ 本条量到的就是这一支；有执行器时的产出契约不在本条口径内
// （要另测，见 SubFlowTest）。
const QHash<QString, QString> kZeroOutputOnSuccessExemptions = {
    {QStringLiteral("SubFlowNode"),
     QStringLiteral("本条只量到设计期无执行器的空转支；有执行器时的产出契约由 SubFlowTest 专测")},
};

// 个别节点需要把参数调到与合成图匹配的值，否则失败是"参数语义"而非"契约"
const QHash<QString, QPair<QString, QVariant>> kParamTweaks = {
    {QStringLiteral("ColorConversionNode"),
     {QStringLiteral("conversionType"), 3 /* GRAY_TO_RGB：输入是单通道灰度图 */}},
    // TesseractOcrNode 的"缺外部资源"前提是**随 cwd 变**的：留空时它按
    // `exe 旁 tessdata/` → 相对路径 `thirdparty/tesseract/tessdata` 兜底
    // （src/TesseractOcrNode.cpp:60~69），从仓库根目录直跑 exe 时兜底路径命中，
    // 于是"没配资源"的节点真的拿到了 eng.traineddata 并认出文本 ⇒ 断言按 ctest
    // 的 cwd（build/）才成立。这里把路径钉死成不存在的目录，两种 cwd 下同一口径。
    {QStringLiteral("TesseractOcrNode"),
     {QStringLiteral("tessdataPath"), QStringLiteral("__vfp_smoke_no_such_tessdata__")}},
};

NodeBase *makeNode(const QString &id, QObject *parent)
{
    NodeBase *node = NodeRegistry::instance().createById(id, parent);
    if (!node)
        return nullptr;
    node->init();   // 端口在 init() 里建立（与 FlowScene::createNode 同一条路径）
    if (kParamTweaks.contains(id)) {
        const auto tweak = kParamTweaks.value(id);
        node->setParam(tweak.first, tweak.second);
    }
    return node;
}

void feedImage(NodeBase *node, const HalconCpp::HImage &himg)
{
    const int inCount = node->inputPorts().size();
    for (int i = 0; i < inCount; ++i) {
        auto d = QSharedPointer<DataObject>::create();
        d->setHImage(himg);
        node->setInputData(i, d);
    }
}

bool requiresImage(NodeBase *node)
{
    auto *hn = qobject_cast<HalconNode *>(node);
    return hn && hn->requiresInputImage();
}

// 0 号输出端口是否对外承诺"图像"（HalconNode::init() 默认加的就是 Image 型"输出图像"）
bool port0PromisesImage(NodeBase *node)
{
    const QList<Port *> outs = node->outputPorts();
    return !outs.isEmpty() && outs.first()->dataType() == PortDataType::Image;
}

// 有几路输入是"图像"：断言④ 需要"至少两路图像"才谈得上尺寸不符
int imageInputPortCount(NodeBase *node)
{
    int n = 0;
    for (Port *p : node->inputPorts())
        if (p && p->dataType() == PortDataType::Image)
            ++n;
    return n;
}

// W-2 口径：**有输入端口但一路 Image 都没有** ⇒ 纯数据型节点（分类/计数/延时/筛选/格式化/
// 公式/协议解析/记录/排序）。判据取端口类型而非类名清单，新增数据节点会自动进入覆盖面。
// 零输入端口的节点（图像读取、相机源、子流程）不算：它们没有"必填数据输入"可空载。
bool dataOnlyNode(NodeBase *node)
{
    return !node->inputPorts().isEmpty() && imageInputPortCount(node) == 0;
}

// 给数据端口喂一个**类型相符**的值。不能沿用 feedImage：把图像对象连到 Number 端口上，
// DataObject::getData() 仍是无值的（setHImage 不写 m_data），在 W-2 口径里就是空载。
void feedDataInputs(NodeBase *node)
{
    const QList<Port *> ports = node->inputPorts();
    for (int i = 0; i < ports.size(); ++i) {
        const PortDataType t = ports[i] ? ports[i]->dataType() : PortDataType::Any;
        QVariant v;
        DataObject::DataType dt = DataObject::DataType::Number;
        switch (t) {
        case PortDataType::String:
            v = QVariant(QStringLiteral("12,34,56"));
            dt = DataObject::DataType::String;
            break;
        case PortDataType::Array:
            v = QVariant::fromValue(QVector<double>{3.0, 1.0, 2.0});
            dt = DataObject::DataType::Array;
            break;
        case PortDataType::Bool:
            v = QVariant(true);
            dt = DataObject::DataType::Bool;
            break;
        default:   // Number / Any / 其他：给数值
            v = QVariant(42.0);
            break;
        }
        node->setInputData(i, QSharedPointer<DataObject>::create(dt, v));
    }
}

// 成功之后 0 号端口是否真有图像：只判 execute()==true 抓不到"内部清空即返回"的静默绿灯
bool imageOutputEmpty(NodeBase *node)
{
    const QSharedPointer<DataObject> out = node->getOutputData(0);
    return !(out && out->getHImage().IsInitialized());
}

// ---- 产出守卫的直接回归锁（不依赖"恰好有真实节点走到 Clear() 分支"）----

// S-1 的形态本身：run() 只把输出图清掉就返回，从不写 moduleStatus=false
class SilentClearNode : public HalconNode
{
public:
    explicit SilentClearNode(QObject *parent = nullptr) : HalconNode(parent) {}
    void run(bool /*autoSwitch*/) override { m_outputImage.Clear(); }
};

// 基类默认 run() 会透传输入图 ⇒ 守卫不许把它判成失败（防"守卫被写成无条件失败"）
class PassThroughNode : public HalconNode
{
public:
    explicit PassThroughNode(QObject *parent = nullptr) : HalconNode(parent) {}
};

// 豁免通道：确实只产测量值/区域、不透传图像的节点必须还能成功
class MeasureOnlyNode : public HalconNode
{
public:
    explicit MeasureOnlyNode(QObject *parent = nullptr) : HalconNode(parent) {}
    bool requiresImageOutput() const override { return false; }
    void run(bool /*autoSwitch*/) override { m_outputImage.Clear(); }
};

// ---- 数据侧守卫（W-2）的直接回归锁：与上面三条同理，不能依赖"恰好有真实节点走到该分支" ----

// 一个数值进 + 一个数值出，process() 走 helper：两条数据判据各自的本体都要能被单独钉住
class DataProbeBase : public HalconNode
{
public:
    explicit DataProbeBase(QObject *parent = nullptr) : HalconNode(parent) {}
    void init() override
    {
        HalconNode::init();                                        // 0 号图像进/出（本探针不用）
        addInputPort(QStringLiteral("数值输入"), PortDataType::Number);
        addOutputPort(QStringLiteral("数值输出"), PortDataType::Number);
    }
    bool process() override { return processDataOutputs(); }
    void feedNumberInput()
    {
        setInputData(m_inputPorts.size() - 1,
                     QSharedPointer<DataObject>::create(DataObject::DataType::Number, QVariant(7.0)));
    }
    bool dataOutputEmpty() const { return getOutputData(m_outputPorts.size() - 1).isNull(); }

protected:
    void publishNumberOutput()
    {
        setOutputData(m_outputPorts.size() - 1,
                      QSharedPointer<DataObject>::create(DataObject::DataType::Number, QVariant(1.0)));
    }
};

// W-2 修前的形态：吃不到数据也照样"成功"
class DataSilentNode : public DataProbeBase
{
public:
    void run(bool /*autoSwitch*/) override {}
};

class DataProducingNode : public DataProbeBase
{
public:
    void run(bool /*autoSwitch*/) override { publishNumberOutput(); }
};

// 豁免通道：确实只产状态、不产数据的节点必须还能成功
class DataOutputExemptNode : public DataProbeBase
{
public:
    bool requiresDataOutput() const override { return false; }
    void run(bool /*autoSwitch*/) override {}
};

} // namespace

class NodeExecuteSmokeTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        registerAllNodes();   // 独立测试进程不经过 MainWindow/NodeFactory，注册表需自行填充
    }

    void registryIsNotEmpty()
    {
        // 空表必须红：否则"零节点冒烟"会静默通过（本用例要抓的正是这类假绿）
        const int n = NodeRegistry::instance().all().size();
        QVERIFY2(n > 60, qPrintable(QStringLiteral("注册表规模异常：%1").arg(n)));
    }

    void defaultParamsWithImageSucceed()
    {
        const HalconCpp::HImage himg = OpencvUtil::matToHimage(makePart());
        const QList<NodeRegistration> regs = NodeRegistry::instance().all();

        // eligible 由注册表直接算出（不进循环），才能校验"确有节点被静默漏掉"这一维
        int skipped = 0;
        for (const NodeRegistration &reg : regs) {
            if (kNeedsDeviceOrIo.contains(reg.id) || kFailsWithoutResource.contains(reg.id))
                ++skipped;
        }
        const int eligible = regs.size() - skipped;

        QStringList failures;
        QStringList emptyOutput;   // S-1：判成功但零产出（下游只会看到"缺输入图像"，掩盖真因）
        QStringList notCreated;
        QStringList dataOnly;      // W-2：没有 Image 输入端口的纯数据节点，本条喂图无从判定
        int executed = 0;

        for (const NodeRegistration &reg : regs) {
            if (kNeedsDeviceOrIo.contains(reg.id) || kFailsWithoutResource.contains(reg.id))
                continue;
            NodeBase *node = makeNode(reg.id, this);
            if (!node) {
                notCreated << reg.id;
                continue;
            }
            // 数据型节点的输入是数值/字符串/数组，喂一张图等于什么都没喂（见 dataOnlyNode 注释）
            // ⇒ 不进本条，由 dataNodesFailOnEmptyLoad / dataNodesSucceedWithTypedInput 专测。
            if (dataOnlyNode(node) && !kW2ExemptDataOnlyNodes.contains(reg.id)) {
                dataOnly << reg.id;
                delete node;
                continue;
            }
            feedImage(node, himg);
            ++executed;
            const bool ok = node->execute();
            if (!ok)
                failures << reg.id;
            else if (port0PromisesImage(node) && imageOutputEmpty(node))
                emptyOutput << reg.id;
            delete node;
        }
        QVERIFY2(failures.isEmpty(),
                 qPrintable(QStringLiteral("以下算子默认参数 + 有效图像仍被判失败：") + failures.join(QStringLiteral(", "))));
        QVERIFY2(emptyOutput.isEmpty(),
                 qPrintable(QStringLiteral("以下算子判成功但 0 号图像输出端口为空（静默绿灯）：")
                            + emptyOutput.join(QStringLiteral(", "))));
        // W-2 覆盖面自洽：分流出去的必须正是"受 W-2 约束的那八个"。多出来=新增数据节点没进专测；
        // 少了=某节点加了图像端口或改了类型，两条专测的覆盖面正在缩水。
        // （DelayNode / SubFlowNode 也命中端口类型口径，但在这里显式豁免 ⇒ 不进 dataOnly 分流，
        //  仍按原样喂图；豁免理由见 kW2ExemptDataOnlyNodes。）
        QStringList actualDataOnly = dataOnly;
        QStringList expectedDataOnly = kW2GuardedNodeIds;
        actualDataOnly.sort();
        expectedDataOnly.sort();
        QVERIFY2(actualDataOnly == expectedDataOnly,
                 qPrintable(QStringLiteral("数据型节点口径与 W-2 专测清单不一致：实际 [")
                            + actualDataOnly.join(QStringLiteral(", ")) + QStringLiteral("] 期望 [")
                            + expectedDataOnly.join(QStringLiteral(", ")) + QStringLiteral("]")));
        // 覆盖面数字随门禁一起打印：文档里引用的这几个数必须能由同一条 ctest 日志复现
        qDebug().noquote() << QStringLiteral("W2-SCOPE regs=%1 eligible=%2 dataOnly=%3 executed=%4 exempt=%5")
                                  .arg(regs.size()).arg(eligible).arg(dataOnly.size())
                                  .arg(executed).arg(kW2ExemptDataOnlyNodes.size());
        // 覆盖面断言放在实质结论之后：软下限写错时不得掩盖上面的真实命中
        QVERIFY2(notCreated.isEmpty(),
                 qPrintable(QStringLiteral("注册表条目创建失败：") + notCreated.join(QStringLiteral(", "))));
        QVERIFY2(executed == eligible - dataOnly.size(),
                 qPrintable(QStringLiteral("受检数 %1 ≠ 应检数 %2（有注册节点未进入冒烟）")
                            .arg(executed).arg(eligible - dataOnly.size())));
        // 软下限只用来抓"覆盖面塌了"：本轮实测 45（53 应检 − 8 个 W-2 数据型分流），留 5 的余量
        QVERIFY2(eligible - dataOnly.size() >= 40, qPrintable(QStringLiteral("覆盖面过小：%1").arg(eligible)));
    }

    // 产出守卫本体（S-1 的直接回归锁）：三种形态各自钉死。遍历式断言只能覆盖"恰好有真实
    // 节点走到 Clear() 分支"的那些情形，守卫本身被删掉/改成无条件都会漏过去。
    void outputGuardShapesBehaveAsContracted()
    {
        const HalconCpp::HImage himg = OpencvUtil::matToHimage(makePart());

        SilentClearNode silent;          // 不传 parent：栈对象由作用域销毁
        silent.init();
        feedImage(&silent, himg);
        QVERIFY2(!silent.execute(),
                 "run() 只 Clear() 就返回 ⇒ 必须判失败（这正是 S-1 的静默绿灯形态）");
        QVERIFY(!silent.getParam(QStringLiteral("moduleStatus")).toBool());
        QVERIFY(imageOutputEmpty(&silent));

        PassThroughNode passThrough;
        passThrough.init();
        feedImage(&passThrough, himg);
        QVERIFY2(passThrough.execute(), "基类默认透传有产出 ⇒ 守卫不得把它判成失败");
        QVERIFY(!imageOutputEmpty(&passThrough));

        MeasureOnlyNode measureOnly;
        measureOnly.init();
        feedImage(&measureOnly, himg);
        QVERIFY2(measureOnly.execute(),
                 "覆写 requiresImageOutput()=false 的节点必须仍可成功（豁免通道不能失效）");
    }

    // W-2 数据侧守卫本体：三条判据各自钉死，不依赖注册表里"恰好有节点走到该分支"。
    void dataGuardShapesBehaveAsContracted()
    {
        {   // 空载守卫：必填数据端口没有值 ⇒ 失败 + 写位 + 清输出
            DataProducingNode n;
            n.init();
            QVERIFY2(!n.execute(), "引用了必填数据输入却没接线 ⇒ 必须判失败");
            QVERIFY(!n.getParam(QStringLiteral("moduleStatus")).toBool());
            QVERIFY(n.dataOutputEmpty());
        }
        {   // 产出守卫：有有效输入，但 run() 什么都不产 ⇒ 仍是失败（W-2 修前的静默绿灯形态）
            DataSilentNode n;
            n.init();
            n.feedNumberInput();
            QVERIFY2(!n.execute(),
                     "有效输入 + run() 零产出 ⇒ 必须判失败（数据侧的 S-1 形态）");
            QVERIFY(!n.getParam(QStringLiteral("moduleStatus")).toBool());
        }
        {   // 反向：正常产出的节点两种情况都要绿
            DataProducingNode n;
            n.init();
            n.feedNumberInput();
            QVERIFY2(n.execute(), "有必填输入且有产出 ⇒ 守卫不得把它判成失败");
            QVERIFY(n.getParam(QStringLiteral("moduleStatus")).toBool());
            QVERIFY(!n.dataOutputEmpty());
        }
        {   // 豁免通道：免掉产出判据后必须仍能成功，但必填判据不受豁免影响
            DataOutputExemptNode n;
            n.init();
            n.feedNumberInput();
            QVERIFY2(n.execute(),
                     "覆写 requiresDataOutput()=false 的节点必须仍可成功（豁免通道不能失效）");
            DataOutputExemptNode m;
            m.init();
            QVERIFY2(!m.execute(),
                     "豁免的是产出判据，不是必填判据：空载仍须失败");
        }
    }

    // W-3：跳过清单一旦因算子改名/注销而失配，该节点会静默退出覆盖面而不是变红
    void skipListsMatchRegistry()
    {
        const QList<NodeRegistration> regs = NodeRegistry::instance().all();
        QSet<QString> ids;
        for (const NodeRegistration &r : regs)
            ids.insert(r.id);

        QStringList stale;
        for (auto it = kNeedsDeviceOrIo.constBegin(); it != kNeedsDeviceOrIo.constEnd(); ++it)
            if (!ids.contains(it.key()))
                stale << it.key();
        for (auto it = kFailsWithoutResource.constBegin(); it != kFailsWithoutResource.constEnd(); ++it)
            if (!ids.contains(it.key()))
                stale << it.key();
        for (auto it = kW2ExemptDataOnlyNodes.constBegin(); it != kW2ExemptDataOnlyNodes.constEnd(); ++it)
            if (!ids.contains(it.key()))
                stale << it.key();
        QStringList missingW2;
        for (const QString &id : kW2GuardedNodeIds)
            if (!ids.contains(id))
                missingW2 << id;
        QVERIFY2(missingW2.isEmpty(),
                 qPrintable(QStringLiteral("W-2 专测清单里有已改名/已注销的 id：")
                            + missingW2.join(QStringLiteral(", "))));
        QVERIFY2(stale.isEmpty(),
                 qPrintable(QStringLiteral("跳过清单里有已改名/已注销的 id（覆盖面正在悄悄缩水）：")
                            + stale.join(QStringLiteral(", "))));

        // 排除表必须全部命中注册表条目，且总数与注册规模自洽
        int skippedInRegistry = 0;
        for (const QString &id : ids)
            if (kNeedsDeviceOrIo.contains(id) || kFailsWithoutResource.contains(id))
                ++skippedInRegistry;
        QVERIFY2(regs.size() - skippedInRegistry >= 50,
                 qPrintable(QStringLiteral("可冒烟规模异常：%1 - %2").arg(regs.size()).arg(skippedInRegistry)));
    }

    // S-1：两路图像尺寸不符会走到 run() 里"只 Clear() 就 return"的分支。这些分支从不写
    // moduleStatus=false ⇒ 契约翻转后判成功但零产出，下游只会看到"缺输入图像"，掩盖真因。
    // 入选口径（N-A 收紧）：**Image 型输入端口 ≥ 2** 的节点。只看"输入端口数 ≥2"会把纯数据型
    // 多输入节点也算进来——给它的数据端口喂一张 32×32 图并不构成有意义的"尺寸不符"，虚增分母。
    void mismatchedImageSizesNeverGreen()
    {
        const HalconCpp::HImage big = OpencvUtil::matToHimage(makePart(64, 64));
        const HalconCpp::HImage small = OpencvUtil::matToHimage(makePart(32, 32));
        QStringList greenWithNoOutput;
        QStringList notCreated;
        int eligible = 0, executed = 0;

        const QList<NodeRegistration> regs = NodeRegistry::instance().all();
        for (const NodeRegistration &reg : regs) {
            if (kNeedsDeviceOrIo.contains(reg.id) || kFailsWithoutResource.contains(reg.id))
                continue;
            NodeBase *node = makeNode(reg.id, this);
            if (!node) {
                notCreated << reg.id;
                continue;
            }
            if (imageInputPortCount(node) < 2) {
                delete node;         // 凑不出"两路图像"，这条分支走不到
                continue;
            }
            ++eligible;
            const int inCount = node->inputPorts().size();
            for (int i = 0; i < inCount; ++i) {
                auto d = QSharedPointer<DataObject>::create();
                d->setHImage(i == 0 ? big : small);
                node->setInputData(i, d);
            }
            ++executed;
            const bool ok = node->execute();
            if (ok && port0PromisesImage(node) && imageOutputEmpty(node))
                greenWithNoOutput << reg.id;
            delete node;
        }
        // 实质结论先断言：覆盖面数字的写法错了也不能把真实命中盖住（本轮就发生过一次）
        QVERIFY2(greenWithNoOutput.isEmpty(),
                 qPrintable(QStringLiteral("尺寸不符时判成功却零产出（静默绿灯）：")
                            + greenWithNoOutput.join(QStringLiteral(", "))));
        QVERIFY2(executed == eligible,
                 qPrintable(QStringLiteral("受检数 %1 ≠ 合格数 %2（有节点被静默跳过）").arg(executed).arg(eligible)));
        QVERIFY2(notCreated.isEmpty(),
                 qPrintable(QStringLiteral("注册表条目创建失败：") + notCreated.join(QStringLiteral(", "))));
        // 下限取本轮实测值 2（口径收紧后只有 ImageSub / ImageDiv 有两个 Image 输入端口）。
        // 这条不再是要害：守卫本体已由 outputGuardShapesBehaveAsContracted 用探针直接钉住，
        // 此处只保证"真实双路图像节点"这条覆盖面不塌。
        QVERIFY2(eligible >= 2, qPrintable(QStringLiteral("双路图像输入节点口径塌了：%1").arg(eligible)));
    }

    void imageInputNodesRejectMissingImage()
    {
        QStringList wronglySucceeded;
        int checked = 0;
        const QList<NodeRegistration> regs = NodeRegistry::instance().all();
        for (const NodeRegistration &reg : regs) {
            if (kNeedsDeviceOrIo.contains(reg.id))
                continue;
            NodeBase *node = makeNode(reg.id, this);
            if (!node)
                continue;
            if (!requiresImage(node)) {
                delete node;   // 不要求图像的节点（公式/计数/条件判定/协议解析等）不在本条口径内
                continue;
            }
            ++checked;
            if (node->execute())
                wronglySucceeded << reg.id;
            delete node;
        }
        // 实质结论先断言：覆盖面下限写错时不能把真实命中盖住
        QVERIFY2(wronglySucceeded.isEmpty(),
                 qPrintable(QStringLiteral("以下算子没有任何输入图像却判成功：") + wronglySucceeded.join(QStringLiteral(", "))));
        QVERIFY2(checked >= 40, qPrintable(QStringLiteral("覆盖面过小：%1").arg(checked)));
    }

    // ---- W-2：数据端口的必填/产出契约（空载不得绿灯）----
    // 这九个数据节点的 process() 原本都是 `run(); return true;` ⇒ 空载时 run() 各自吐出
    // "参数派生的常量"（分类恒为「中」、筛选恒为 false、格式化恒为模板、记录恒写一行空值），
    // 一条从没接线的流程也能整条绿到底。现在其中八个受契约约束，DelayNode 显式豁免（理由见
    // kW2ExemptDataOnlyNodes）。这里钉四件事：
    //   ① 空载必红（且 moduleStatus=false、输出全清）
    //   ② 带类型相符的输入必绿（反向守卫：契约不能写成"无条件失败"）
    //   ③ FormulaNode 的必填集随表达式引用逐端口变化（p0~p3 是可选操作数，不是全必填）
    //   ④ 豁免节点空载仍绿（防止豁免被顺手删掉，也防止判据被改成无条件失败）
    void dataNodesFailOnEmptyLoad()
    {
        QStringList wronglySucceeded;
        QStringList statusNotFalse;
        QStringList staleOutput;
        int checked = 0;
        for (const QString &id : kW2GuardedNodeIds) {
            NodeBase *node = makeNode(id, this);
            QVERIFY2(node != nullptr, qPrintable(id));
            ++checked;
            if (node->execute())
                wronglySucceeded << id;   // 一个输入都不喂 ⇒ 必须失败
            if (node->getParam(QStringLiteral("moduleStatus")).toBool())
                statusNotFalse << id;     // 红灯要能在参数表上看到，不能只体现在返回值
            for (int p = 0; p < node->outputPorts().size(); ++p)
                if (node->getOutputData(p))
                    staleOutput << QStringLiteral("%1#%2").arg(id).arg(p);
            delete node;
        }
        QVERIFY2(wronglySucceeded.isEmpty(),
                 qPrintable(QStringLiteral("数据型节点空载仍判成功（静默绿灯）：")
                            + wronglySucceeded.join(QStringLiteral(", "))));
        QVERIFY2(statusNotFalse.isEmpty(),
                 qPrintable(QStringLiteral("数据型节点空载却未写 moduleStatus=false：")
                            + statusNotFalse.join(QStringLiteral(", "))));
        QVERIFY2(staleOutput.isEmpty(),
                 qPrintable(QStringLiteral("失败轮仍留有输出（下游会读到上一轮结果）：")
                            + staleOutput.join(QStringLiteral(", "))));
        QVERIFY2(checked == kW2GuardedNodeIds.size(),
                 qPrintable(QStringLiteral("覆盖面缩水：%1/%2").arg(checked).arg(kW2GuardedNodeIds.size())));
    }

    void dataNodesSucceedWithTypedInput()
    {
        QStringList failed;
        QStringList statusNotTrue;
        QStringList noOutput;
        for (const QString &id : kW2GuardedNodeIds) {
            NodeBase *node = makeNode(id, this);
            QVERIFY2(node != nullptr, qPrintable(id));
            feedDataInputs(node);       // 每个输入端口给一个类型相符的值
            if (!node->execute())
                failed << id;
            else if (!node->getParam(QStringLiteral("moduleStatus")).toBool())
                statusNotTrue << id;
            bool produced = false;
            for (int p = 0; p < node->outputPorts().size(); ++p)
                produced = produced || bool(node->getOutputData(p));
            if (!produced)
                noOutput << id;
            delete node;
        }
        // 反向守卫：契约不能写成"无条件失败"，否则空载断言只是恰好和真失败撞在一起
        QVERIFY2(failed.isEmpty(),
                 qPrintable(QStringLiteral("数据型节点有有效输入却判失败：") + failed.join(QStringLiteral(", "))));
        QVERIFY2(statusNotTrue.isEmpty(),
                 qPrintable(QStringLiteral("数据型节点成功却未置 moduleStatus=true：")
                            + statusNotTrue.join(QStringLiteral(", "))));
        QVERIFY2(noOutput.isEmpty(),
                 qPrintable(QStringLiteral("数据型节点成功却零产出：") + noOutput.join(QStringLiteral(", "))));
    }

    // FormulaNode 的必填集 = 表达式引用到的 pN：这是"逐端口必填/可选"的唯一非默认实现，
    // 也是"旧项目里常量公式仍可用"的兼容出口。
    //
    // 两条各钉一半，缺一不可（都做过改坏自证）：
    //   · 口径（requiredInputDataPorts() 直接比对）：唯一能区分"p0 必填 / p1 可选"的一条。
    //     实测把 requiredInputDataPorts() 改成恒返回空集 ⇒ 变红就落在本段（FAIL 行见本函数）；
    //     而只把 processDataOutputs() 的空载守卫短路时，下面四条行为用例仍全绿——缺操作数时
    //     evaluate() 失败 → 零产出 → 被**产出守卫**兜住，结果一样，所以行为段区分不了口径。
    //   · 行为（execute() 成败）：钉住"口径判对了、而且真的作用到一轮执行上"。
    void formulaRequiredPortsFollowExpression()
    {
        struct PortCase { const char *expr; int ports[4]; int count; };
        const PortCase portCases[] = {
            { "2 * 3",            { },            0 },   // 无引用 ⇒ 零必填
            { "p0 + 1",           { 0 },          1 },
            { "p0 + p1 + 1",      { 0, 1 },       2 },
            { "pow(p0, 2) + p3",  { 0, 3 },       2 },   // 函数名 pow 不得被误判成引用
            { "p2 * p2",          { 2 },          1 },   // 重复引用只算一个端口
        };
        QStringList wrongSet;
        for (const PortCase &c : portCases) {
            FormulaNode node;
            node.init();
            node.setParam(QStringLiteral("expression"), QString::fromLatin1(c.expr));
            QSet<int> expected;
            for (int i = 0; i < c.count; ++i)
                expected.insert(c.ports[i]);
            const QSet<int> actual = node.requiredInputDataPorts();
            if (actual != expected) {
                const auto fmt = [](const QSet<int> &s) {
                    QList<int> v = s.values();
                    std::sort(v.begin(), v.end());
                    QStringList parts;
                    for (int x : v)
                        parts << QString::number(x);
                    return parts.join(QChar(','));
                };
                wrongSet << QStringLiteral("%1 → 实际[%2] 期望[%3]")
                              .arg(QString::fromLatin1(c.expr)).arg(fmt(actual)).arg(fmt(expected));
            }
        }
        QVERIFY2(wrongSet.isEmpty(),
                 qPrintable(QStringLiteral("公式节点必填端口集合算错：") + wrongSet.join(QStringLiteral("; "))));

        struct Case { const char *expr; bool feedP0; bool expectOk; };
        const Case cases[] = {
            { "2 * 3",        false, true },   // 无引用 ⇒ 零必填端口，空载也合法
            { "p0 + 1",       false, false },  // 引用 p0 却没接线 ⇒ 不得拿 0.0 算出个像样的数
            { "p0 + 1",       true,  true },
            { "p0 + p1 + 1",  true,  false },  // 只喂 p0：p1 仍必填
        };
        QStringList mismatched;
        for (const Case &c : cases) {
            FormulaNode node;
            node.init();
            node.setParam(QStringLiteral("expression"), QString::fromLatin1(c.expr));
            auto d = QSharedPointer<DataObject>::create(DataObject::DataType::Number, QVariant(7.0));
            if (c.feedP0)
                node.setInputData(0, d);
            const bool ok = node.execute();
            if (ok != c.expectOk)
                mismatched << QStringLiteral("%1/feedP0=%2 → ok=%3")
                                  .arg(QString::fromLatin1(c.expr)).arg(c.feedP0).arg(ok);
        }
        QVERIFY2(mismatched.isEmpty(),
                 qPrintable(QStringLiteral("公式节点必填口径不符：") + mismatched.join(QStringLiteral("; "))));
    }

    // 豁免的反向锁：DelayNode 空载必须仍绿。删掉 DelayNode.h 里两条 override 就会变红；
    // 把判据改成"无条件失败"也会变红——两个方向都要有用例挡着。
    void delayStaysGreenOnEmptyLoad()
    {
        NodeBase *node = makeNode(QStringLiteral("DelayNode"), this);
        QVERIFY(node != nullptr);
        QVERIFY2(node->execute(),
                 "延时是时间门控（循环体/暂停/节拍），`数据输入` 只是可选透传 ⇒ 空载必须仍成功");
        QVERIFY2(node->getParam(QStringLiteral("moduleStatus")).toBool(),
                 "成功轮必须置 moduleStatus=true（豁免只免掉必填/产出两条判据，不免掉状态位）");
        // 空载轮不得凭空造数据：透传 nothing 就是 nothing
        QVERIFY2(node->getOutputData(0).isNull(), "延时空载却吐出了数据对象（凭空结果）");
        delete node;
    }

    // ---- A5 第一步：把口径**量出来**，不预设结论 ----
    // 两道产出守卫住在两条不同的 process() 路径上：图像侧在 `HalconNode::process()`，
    // 数据侧在 `processDataOutputs()`（只有显式改用它的节点才走到）。
    // ⇒ "用基类 process() + 又声明了非图像输出端口"的节点，其数据端口无人看守：
    //   run() 里 `if (...) return;` 不写位时，基类只看图像产出就放行 = 绿灯 + 数据端口全空。
    // 本条先把这个集合量出来（谁、几个），再决定补守卫还是补带理由的豁免表。
    void baseProcessNodesWithDataPortsAreMeasured()
    {
        const HalconCpp::HImage himg = OpencvUtil::matToHimage(makePart());
        const QList<NodeRegistration> regs = NodeRegistry::instance().all();
        int candidates = 0;
        int guardedByImageContract = 0;
        QStringList exposed;
        QStringList bare;
        QStringList unguarded;

        for (const NodeRegistration &reg : regs) {
            if (kNeedsDeviceOrIo.contains(reg.id) || kFailsWithoutResource.contains(reg.id))
                continue;
            NodeBase *node = makeNode(reg.id, this);
            if (!node)
                continue;
            if (dataOnlyNode(node) && !kW2ExemptDataOnlyNodes.contains(reg.id)) {
                delete node;   // 已由 dataNodesFailOnEmptyLoad / dataNodesSucceedWithTypedInput 专测
                continue;
            }
            feedImage(node, himg);
            const bool ok = node->execute();
            const QList<Port *> outs = node->outputPorts();
            int dataPorts = 0;
            for (Port *p : outs) {
                if (p && p->dataType() != PortDataType::Image)
                    ++dataPorts;
            }
            if (dataPorts > 0) {
                ++candidates;
                auto *hn = qobject_cast<HalconNode *>(node);
                if (hn && hn->requiresImageOutput())
                    ++guardedByImageContract;
                else
                    unguarded << reg.id;
                if (ok) {
                    bool anyDataProduced = false;
                    bool anyPortProduced = false;
                    for (int i = 0; i < outs.size(); ++i) {
                        const bool empty = node->getOutputData(i).isNull()
                                           || (outs[i] && outs[i]->dataType() == PortDataType::Image
                                               && !node->getOutputData(i)->getHImage().IsInitialized());
                        if (empty)
                            continue;
                        anyPortProduced = true;
                        if (outs[i] && outs[i]->dataType() != PortDataType::Image)
                            anyDataProduced = true;
                    }
                    if (!anyDataProduced)
                        exposed << reg.id;
                    if (!anyPortProduced)
                        bare << reg.id;
                }
            }
            delete node;
        }
        // 口径落空等于什么都没测：这条断言保证下面的数字不是"0 个候选所以全绿"
        QVERIFY2(candidates > 0, "口径落空：注册表里没有任何节点声明非图像输出端口");
        qDebug().noquote()
            << QStringLiteral("A5-SCOPE candidates=%1 imageGuarded=%2 exposedDataPorts=%3 exposedAllPorts=%4")
                   .arg(candidates).arg(guardedByImageContract).arg(exposed.size()).arg(bare.size());
        qDebug().noquote() << QStringLiteral("A5-DATA ids=[%1]").arg(exposed.join(QStringLiteral(", ")));
        qDebug().noquote() << QStringLiteral("A5-BARE ids=[%1]").arg(bare.join(QStringLiteral(", ")));
        qDebug().noquote() << QStringLiteral("A5-UNGUARDED ids=[%1]")
                                  .arg(unguarded.join(QStringLiteral(", ")));
        // 闸：成功却全端口零产出的节点必须逐个出现在带理由的豁免表里（表见文件顶部）。
        QStringList unexpected;
        for (const QString &id : bare)
            if (!kZeroOutputOnSuccessExemptions.contains(id))
                unexpected << id;
        QVERIFY2(unexpected.isEmpty(),
                 qPrintable(QStringLiteral("新增\"成功但零产出\"节点（会把\"没算出东西\"显示成良品）：")
                            + unexpected.join(QStringLiteral(", "))));
    }

    void unconfiguredResourceNodesFail()
    {
        const HalconCpp::HImage himg = OpencvUtil::matToHimage(makePart());
        QStringList wronglySucceeded;
        for (auto it = kFailsWithoutResource.constBegin(); it != kFailsWithoutResource.constEnd(); ++it) {
            NodeBase *node = makeNode(it.key(), this);
            QVERIFY2(node != nullptr, qPrintable(it.key()));
            feedImage(node, himg);
            if (node->execute())
                wronglySucceeded << it.key();
            delete node;
        }
        QVERIFY2(wronglySucceeded.isEmpty(),
                 qPrintable(QStringLiteral("缺外部资源却判成功（会把\"没配模型\"显示成良品）：")
                            + wronglySucceeded.join(QStringLiteral(", "))));
    }
};

QTEST_MAIN(NodeExecuteSmokeTest)
#include "node_execute_smoke_test.moc"
