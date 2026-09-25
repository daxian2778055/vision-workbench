// 全节点 execute() 冒烟门禁（P0-1 的回归锁）
//
// 背景：HalconNode::init() 把 moduleStatus 初值化为 false，而本族多数节点的 run() 只在失败
// 分支写 false、成功路径从不写位 ⇒ 这些算子被拖进流程即恒判失败（m_stopOnFailure 默认 true
// 还会中断整条流程）。2026-09-25 实测：修复前 63 个可脱机执行的注册节点里 **34 个恒判失败**，
// 其中 23 个是纯契约缺陷（把成功默认值交给基类后即翻正），其余 11 个是"缺外部资源/图里没结果"
// 的真实失败。
//
// 本用例把三件事钉住，全部走 NodeRegistry（新增算子自动进覆盖面，不需要再补用例）：
//   ① 默认参数 + 一张合成图 ⇒ execute() 必须成功；
//   ② 声明了 Image 输入端口的节点，本轮没有图像 ⇒ execute() 必须失败（不许静默绿灯）；
//   ③ 缺模型/未教学的节点 ⇒ 必须显式失败（防"成功默认值"被改成"永不失败"）。
#include <QtTest/QtTest>
#include <QHash>
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
        QStringList failures;
        int checked = 0;

        const QList<NodeRegistration> regs = NodeRegistry::instance().all();
        for (const NodeRegistration &reg : regs) {
            if (kNeedsDeviceOrIo.contains(reg.id) || kFailsWithoutResource.contains(reg.id))
                continue;
            NodeBase *node = makeNode(reg.id, this);
            QVERIFY2(node != nullptr, qPrintable(QStringLiteral("注册表里的 %1 创建失败").arg(reg.id)));
            feedImage(node, himg);
            ++checked;
            if (!node->execute())
                failures << reg.id;
            delete node;
        }
        QVERIFY2(checked >= 50, qPrintable(QStringLiteral("覆盖面过小：%1").arg(checked)));
        QVERIFY2(failures.isEmpty(),
                 qPrintable(QStringLiteral("以下算子默认参数 + 有效图像仍被判失败：") + failures.join(QStringLiteral(", "))));
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
        QVERIFY2(checked >= 40, qPrintable(QStringLiteral("覆盖面过小：%1").arg(checked)));
        QVERIFY2(wronglySucceeded.isEmpty(),
                 qPrintable(QStringLiteral("以下算子没有任何输入图像却判成功：") + wronglySucceeded.join(QStringLiteral(", "))));
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
