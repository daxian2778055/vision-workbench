// 全节点 execute() 冒烟门禁（P0-1 的回归锁）
//
// 背景：HalconNode::init() 把 moduleStatus 初值化为 false，而本族多数节点的 run() 只在失败
// 分支写 false、成功路径从不写位 ⇒ 这些算子被拖进流程即恒判失败（m_stopOnFailure 默认 true
// 还会中断整条流程）。2026-09-25 实测：修复前 63 个可脱机执行的注册节点里 **34 个恒判失败**，
// 其中 23 个是纯契约缺陷（把成功默认值交给基类后即翻正），其余 11 个是"缺外部资源/图里没结果"
// 的真实失败。
//
// 本用例把五件事钉住，全部走 NodeRegistry（新增算子自动进覆盖面，不需要再补用例）：
//   ① 默认参数 + 一张合成图 ⇒ execute() 必须成功，并且"对外承诺图像"的 0 号输出端口必须有
//      真实产出（只判 execute() 抓不到 run() 里 Clear() 后 return 的静默绿灯，见 S-1）；
//   ② 声明了 Image 输入端口的节点，本轮没有图像 ⇒ execute() 必须失败（不许静默绿灯）；
//   ③ 缺模型/未教学的节点 ⇒ 必须显式失败（防"成功默认值"被改成"永不失败"）；
//   ④ 有 ≥2 路 Image 输入端口的节点喂进尺寸不符的两路图 ⇒ 不许"成功且零产出"（专门走 Clear() 分支）；
//   ⑤ 跳过清单与注册表自洽 ⇒ 算子改名/注销时覆盖面缩水必须变红，而不是静默退出覆盖面。
// 另：产出守卫本体由三个探针节点（只 Clear() / 默认透传 / 覆写豁免）做直接回归锁，
// 不依赖"恰好有真实节点走到 Clear() 分支"——见 outputGuardShapesBehaveAsContracted。
#include <QtTest/QtTest>
#include <QHash>
#include <QSet>
#include <QStringList>

#include "NodeRegistry.h"
#include "NodeBase.h"
#include "HalconNode.h"
#include "Port.h"
#include "DataObject.h"
#include "OpencvUtil.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <HalconCpp.h>

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
    {QStringLiteral("TesseractOcrNode"), QStringLiteral("合成图内无文字 ⇒ 无结果是失败")},
};

// 个别节点需要把参数调到与合成图匹配的值，否则失败是"参数语义"而非"契约"
const QHash<QString, QPair<QString, QVariant>> kParamTweaks = {
    {QStringLiteral("ColorConversionNode"),
     {QStringLiteral("conversionType"), 3 /* GRAY_TO_RGB：输入是单通道灰度图 */}},
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
        int executed = 0;

        for (const NodeRegistration &reg : regs) {
            if (kNeedsDeviceOrIo.contains(reg.id) || kFailsWithoutResource.contains(reg.id))
                continue;
            NodeBase *node = makeNode(reg.id, this);
            if (!node) {
                notCreated << reg.id;
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
        // 覆盖面断言放在实质结论之后：软下限写错时不得掩盖上面的真实命中
        QVERIFY2(notCreated.isEmpty(),
                 qPrintable(QStringLiteral("注册表条目创建失败：") + notCreated.join(QStringLiteral(", "))));
        QVERIFY2(executed == eligible,
                 qPrintable(QStringLiteral("受检数 %1 ≠ 应检数 %2（有注册节点未进入冒烟）").arg(executed).arg(eligible)));
        QVERIFY2(eligible >= 50, qPrintable(QStringLiteral("覆盖面过小：%1").arg(eligible)));
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
