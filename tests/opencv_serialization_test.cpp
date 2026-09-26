// OpenCV 节点序列化测试（Qt Test 框架）
// 测试所有 OpenCV 节点的 toJson/fromJson 序列化功能
#include <QtTest/QtTest>
#include <QObject>
#include <QJsonObject>
#include <QJsonDocument>

// OpenCV 节点头文件
#include "OpencvThresholdNode.h"
#include "OpencvAdaptiveThresholdNode.h"
#include "OpencvCropNode.h"
#include "OpencvImageArithNode.h"
#include "OpencvRotateNode.h"
#include "OpencvMorphNode.h"
#include "OpencvEdgeNode.h"
#include "OpencvBlobNode.h"
#include "OpencvPixelStatsNode.h"
#include "OpencvFitLineNode.h"
#include "OpencvFitCircleNode.h"
#include "OpencvCaliperNode.h"
#include "OpencvAngleNode.h"
#include "OpencvTemplateMatchNode.h"
#include "OpencvFeatureMatchNode.h"
#include "OpencvCalibNode.h"
#include "OpencvQrNode.h"
#include "OpencvClassifyNode.h"
#include "OpencvTrainClassifierNode.h"
#include "OpencvTrackNode.h"

// HALCON 节点
#include "FormulaNode.h"
#include "CounterNode.h"
#include "DelayNode.h"
#include "LoopNode.h"
#include "ConditionalNode.h"

// 其他
#include "ZxingBarcodeNode.h"
#include "TesseractOcrNode.h"
#include "DnnInferNode.h"

class OpencvSerializationTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // OpenCV 图像处理节点序列化测试
    void testThresholdSerialization();
    void testAdaptiveThresholdSerialization();
    void testCropSerialization();
    void testImageArithSerialization();
    void testRotateSerialization();
    void testMorphSerialization();
    void testEdgeSerialization();
    void testBlobSerialization();
    void testPixelStatsSerialization();

    // OpenCV 几何测量节点序列化测试
    void testFitLineSerialization();
    void testFitCircleSerialization();
    void testCaliperSerialization();
    void testAngleSerialization();

    // OpenCV 模板匹配节点序列化测试
    void testTemplateMatchSerialization();
    void testFeatureMatchSerialization();

    // OpenCV 标定节点序列化测试
    void testCalibSerialization();

    // OpenCV 识别节点序列化测试
    void testQrSerialization();

    // 深度学习节点序列化测试
    void testClassifySerialization();
    void testTrainClassifierSerialization();
    void testDnnInferSerialization();

    // 跟踪节点序列化测试
    void testTrackSerialization();

    // 逻辑控制节点序列化测试
    void testFormulaSerialization();
    void testCounterSerialization();
    void testDelaySerialization();

    // 第三方节点序列化测试
    void testZxingSerialization();
    void testTesseractSerialization();

private:
    // 辅助函数：验证序列化和反序列化
    template<typename NodeType>
    void verifySerialization(const QString &nodeName,
                            const QVariantMap &testParams,
                            const QPointF &testPos = QPointF(100, 200));

    // 辅助函数：设置节点参数
    void setNodeParams(HalconNode *node, const QVariantMap &params);
};

void OpencvSerializationTest::initTestCase()
{
}

void OpencvSerializationTest::cleanupTestCase()
{
}

template<typename NodeType>
void OpencvSerializationTest::verifySerialization(const QString &nodeName,
                                                  const QVariantMap &testParams,
                                                  const QPointF &testPos)
{
    // 创建节点
    NodeType node1;
    node1.init();
    node1.setName(nodeName);
    node1.setPosition(testPos);

    // 设置测试参数
    setNodeParams(&node1, testParams);

    // 序列化
    QJsonObject json = node1.toJson();
    QVERIFY2(!json.isEmpty(), qPrintable(QString("%1: toJson 返回空 JSON").arg(nodeName)));
    QVERIFY2(json.contains("params"), qPrintable(QString("%1: JSON 缺少 params 字段").arg(nodeName)));
    QVERIFY2(json.contains("position"), qPrintable(QString("%1: JSON 缺少 position 字段").arg(nodeName)));

    // 验证参数已保存
    QJsonObject params = json["params"].toObject();
    for (auto it = testParams.constBegin(); it != testParams.constEnd(); ++it) {
        QVERIFY2(params.contains(it.key()),
                 qPrintable(QString("%1: JSON 缺少参数 %2").arg(nodeName, it.key())));
    }

    // 创建新节点并反序列化
    NodeType node2;
    node2.init();
    node2.fromJson(json);

    // 验证参数一致
    for (auto it = testParams.constBegin(); it != testParams.constEnd(); ++it) {
        QVariant v1 = it.value();
        QVariant v2 = node2.getParam(it.key());

        // 浮点数使用近似比较
        if (v1.type() == QVariant::Double) {
            QVERIFY2(qFuzzyCompare(v1.toDouble(), v2.toDouble()),
                     qPrintable(QString("%1: 参数 %2 不一致: %3 != %4")
                               .arg(nodeName, it.key())
                               .arg(v1.toDouble())
                               .arg(v2.toDouble())));
        } else {
            QCOMPARE(v2, v1);
        }
    }

    // 验证位置
    QCOMPARE(node2.position().x(), testPos.x());
    QCOMPARE(node2.position().y(), testPos.y());
}

void OpencvSerializationTest::setNodeParams(HalconNode *node, const QVariantMap &params)
{
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        node->setParam(it.key(), it.value());
    }
}

// ==================== OpenCV 图像处理节点 ====================

void OpencvSerializationTest::testThresholdSerialization()
{
    QVariantMap params;
    params["mode"] = 1;           // OTSU
    params["minVal"] = 200;
    verifySerialization<OpencvThresholdNode>("OpenCV二值化", params);
}

void OpencvSerializationTest::testAdaptiveThresholdSerialization()
{
    QVariantMap params;
    params["direction"] = 1;      // 亮变暗
    params["blockSize"] = 21;
    params["cValue"] = 10;
    verifySerialization<OpencvAdaptiveThresholdNode>("OpenCV自适应阈值", params);
}

void OpencvSerializationTest::testCropSerialization()
{
    QVariantMap params;
    params["row"] = 50;
    params["column"] = 100;
    params["height"] = 200;
    params["width"] = 300;
    verifySerialization<OpencvCropNode>("OpenCV ROI裁剪", params);
}

void OpencvSerializationTest::testImageArithSerialization()
{
    QVariantMap params;
    params["op"] = 2;             // 乘法
    params["operandType"] = 0;    // 常数
    params["constant"] = 2.5;
    verifySerialization<OpencvImageArithNode>("OpenCV图像运算", params);
}

void OpencvSerializationTest::testRotateSerialization()
{
    QVariantMap params;
    params["angle"] = 45.0;
    params["scale"] = 1.5;
    verifySerialization<OpencvRotateNode>("OpenCV图像旋转", params);
}

void OpencvSerializationTest::testMorphSerialization()
{
    QVariantMap params;
    params["op"] = 2;             // 开运算
    params["kernelSize"] = 7;
    params["iterations"] = 2;
    verifySerialization<OpencvMorphNode>("OpenCV形态学", params);
}

void OpencvSerializationTest::testEdgeSerialization()
{
    QVariantMap params;
    params["lowThreshold"] = 50;
    params["highThreshold"] = 150;
    params["kernelSize"] = 5;
    verifySerialization<OpencvEdgeNode>("OpenCV边缘检测", params);
}

void OpencvSerializationTest::testBlobSerialization()
{
    QVariantMap params;
    params["minArea"] = 100;
    params["maxArea"] = 10000;
    params["threshold"] = 128;
    verifySerialization<OpencvBlobNode>("OpenCV连通域", params);
}

void OpencvSerializationTest::testPixelStatsSerialization()
{
    QVariantMap params;
    params["roiRow"] = 10;
    params["roiCol"] = 20;
    params["roiHeight"] = 100;
    params["roiWidth"] = 200;
    verifySerialization<OpencvPixelStatsNode>("OpenCV灰度统计", params);
}

// ==================== OpenCV 几何测量节点 ====================

void OpencvSerializationTest::testFitLineSerialization()
{
    QVariantMap params;
    params["edgeThreshold"] = 30;
    params["minLength"] = 50;
    verifySerialization<OpencvFitLineNode>("OpenCV直线拟合", params);
}

void OpencvSerializationTest::testFitCircleSerialization()
{
    QVariantMap params;
    params["edgeThreshold"] = 40;
    params["minRadius"] = 10;
    params["maxRadius"] = 500;
    verifySerialization<OpencvFitCircleNode>("OpenCV圆拟合", params);
}

void OpencvSerializationTest::testCaliperSerialization()
{
    QVariantMap params;
    params["numCalipers"] = 20;
    params["caliperLength"] = 100;
    params["edgeThreshold"] = 25;
    verifySerialization<OpencvCaliperNode>("OpenCV卡尺测量", params);
}

void OpencvSerializationTest::testAngleSerialization()
{
    QVariantMap params;
    params["r1a"] = 100.0;
    params["c1a"] = 100.0;
    params["r1b"] = 200.0;
    params["c1b"] = 100.0;
    params["r2a"] = 100.0;
    params["c2a"] = 100.0;
    params["r2b"] = 100.0;
    params["c2b"] = 200.0;
    verifySerialization<OpencvAngleNode>("OpenCV角度测量", params);
}

// ==================== OpenCV 模板匹配节点 ====================

void OpencvSerializationTest::testTemplateMatchSerialization()
{
    QVariantMap params;
    params["mode"] = 0;           // 训练模式
    params["matchThreshold"] = 0.8;
    params["maxMatches"] = 5;
    verifySerialization<OpencvTemplateMatchNode>("OpenCV模板匹配", params);
}

// ==================== OpenCV 特征匹配节点 ====================

void OpencvSerializationTest::testFeatureMatchSerialization()
{
    // 注册表闸（改坏自证 B5 挣来的）：toJson 存的是整张 m_params ⇒ 漏注册照样能存能取，
    // round-trip 断言照不到。这里只核本节点的 16 个控制参数；不做全量：实测另有 18 个既有
    // 节点的测试参数走手写 createParamPanel 而不进声明式注册表，全量加闸会把另一种写法当缺陷。
    OpencvFeatureMatchNode probe;
    probe.init();
    static const char *const kMustRegister[] = {
        "templatePath", "trainFromImage", "detector", "matcher", "nFeatures",
        "ratioThreshold", "minMatches", "minInliers", "minScore", "ransacThreshold",
        "roiCenterCol", "roiCenterRow", "roiWidth", "roiHeight", "roiAngle",
        "writeFixtureName"};
    for (const char *const key : kMustRegister) {
        const QString name = QString::fromLatin1(key);
        bool found = false;
        for (const ParamSpec &spec : probe.paramSpecs()) {
            if (spec.name == name) {
                found = true;
                break;
            }
        }
        QVERIFY2(found, qPrintable(QStringLiteral("OpenCV特征匹配: 参数 %1 未注册"
                                                   "（面板取不到默认值/范围/标签）").arg(name)));
    }

    QVariantMap params;
    params["detector"] = 1;              // SIFT
    params["matcher"] = 1;               // FLANN
    params["nFeatures"] = 3000;
    params["ratioThreshold"] = 0.8;
    params["minInliers"] = 9;
    params["writeFixtureName"] = QStringLiteral("feat_fix");
    verifySerialization<OpencvFeatureMatchNode>("OpenCV特征匹配", params);
}

// ==================== OpenCV 标定节点 ====================

void OpencvSerializationTest::testCalibSerialization()
{
    QVariantMap params;
    params["boardWidth"] = 9;
    params["boardHeight"] = 6;
    params["squareSize"] = 25.0;
    verifySerialization<OpencvCalibNode>("OpenCV相机标定", params);
}

// ==================== OpenCV 识别节点 ====================

void OpencvSerializationTest::testQrSerialization()
{
    QVariantMap params;
    params["tryHarder"] = true;
    verifySerialization<OpencvQrNode>("OpenCV二维码", params);
}

// ==================== 深度学习节点 ====================

void OpencvSerializationTest::testClassifySerialization()
{
    QVariantMap params;
    params["modelPath"] = "test_model.xml";
    params["numClasses"] = 10;
    verifySerialization<OpencvClassifyNode>("OpenCV分类推理", params);
}

void OpencvSerializationTest::testTrainClassifierSerialization()
{
    QVariantMap params;
    params["classifierType"] = 0;  // HOG
    params["numEpochs"] = 100;
    params["learningRate"] = 0.01;
    verifySerialization<OpencvTrainClassifierNode>("OpenCV分类器训练", params);
}

void OpencvSerializationTest::testDnnInferSerialization()
{
    QVariantMap params;
    params["modelPath"] = "test_model.onnx";
    params["inputSize"] = 224;
    params["scale"] = 0.00392156862745098;
    params["swapRB"] = true;
    params["topK"] = 3;
    verifySerialization<DnnInferNode>("ONNX深度学习推理", params);
}

// ==================== 跟踪节点 ====================

void OpencvSerializationTest::testTrackSerialization()
{
    QVariantMap params;
    params["trackerType"] = 0;    // KCF
    verifySerialization<OpencvTrackNode>("OpenCV目标跟踪", params);
}

// ==================== 逻辑控制节点 ====================

void OpencvSerializationTest::testFormulaSerialization()
{
    QVariantMap params;
    params["expression"] = "(p0 + p1) * 2 / max(p2, 1)";
    verifySerialization<FormulaNode>("公式计算", params);
}

void OpencvSerializationTest::testCounterSerialization()
{
    QVariantMap params;
    params["resetValue"] = 100;
    params["step"] = 2;
    verifySerialization<CounterNode>("条件计数", params);
}

void OpencvSerializationTest::testDelaySerialization()
{
    QVariantMap params;
    params["delayMs"] = 500;
    verifySerialization<DelayNode>("延时控制", params);
}

// ==================== 第三方节点 ====================

void OpencvSerializationTest::testZxingSerialization()
{
    QVariantMap params;
    params["formats"] = 0;        // 所有格式
    verifySerialization<ZxingBarcodeNode>("ZXing条码解码", params);
}

void OpencvSerializationTest::testTesseractSerialization()
{
    QVariantMap params;
    params["language"] = "eng+chi_sim";
    params["tessdataPath"] = "tessdata";
    verifySerialization<TesseractOcrNode>("TesseractOCR", params);
}

QTEST_MAIN(OpencvSerializationTest)
#include "opencv_serialization_test.moc"
