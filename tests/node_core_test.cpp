// 节点核心功能单元测试（Qt Test 框架）
// 测试：节点创建、参数管理、序列化（toJson/fromJson）、端口管理
#include <QtTest/QtTest>
#include <QObject>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>

// 节点头文件
#include "OpencvThresholdNode.h"
#include "OpencvCropNode.h"
#include "OpencvImageArithNode.h"
#include "OpencvMorphNode.h"
#include "OpencvEdgeNode.h"
#include "OpencvBlobNode.h"
#include "OpencvCaliperNode.h"
#include "ColorConversionNode.h"
#include "OpencvPixelStatsNode.h"
#include "OpencvAngleNode.h"
#include "OpencvFitLineNode.h"
#include "OpencvFitCircleNode.h"
#include "OpencvQrNode.h"
#include "OpencvClassifyNode.h"
#include "OpencvDefectNode.h"
#include "OpencvFeatureMatchNode.h"
#include "ZxingBarcodeNode.h"
#include "TesseractOcrNode.h"
#include "DeepOcrNode.h"
#include "DnnInferNode.h"
#include "DnnDetectNode.h"
#include "DnnSegmentNode.h"
#include "AnomalyDetectNode.h"
#include "ObjectCountingNode.h"
#include "OpencvTrainClassifierNode.h"
#include "OpencvCalibNode.h"
#include "OpencvTrackNode.h"
#include "FeatureSearchNode.h"
#include "DataObject.h"
#include "ImageDisplayController.h"   // 判 NG 时叠加层改红的腿
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QScopedPointer>
#include <QWidget>
#include "BlurNode.h"
#include "MedianFilterNode.h"
#include "SharpenNode.h"
#include "ContrastStretchNode.h"
#include "HistogramEqualizeNode.h"
#include "GrayStretchNode.h"
#include "ResizeNode.h"
#include "ImageAddNode.h"
#include "ImageMulNode.h"
#include "TopHatNode.h"
#include "BottomHatNode.h"
#include <QElapsedTimer>
#include <QMetaType>
#include "TranslateImageNode.h"
#include "MirrorNode.h"
#include "OpencvRotateNode.h"
#include "FormulaNode.h"
#include "ImageReadNode.h"
#include "DataObject.h"
#include "OpencvUtil.h"
#include "Port.h"
#include "PortConnectivity.h"
#include <QTemporaryDir>
#include <QImage>
#include <opencv2/imgproc.hpp>

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
    void testThresholdVmModesAndRoi();
    void testThresholdResultDisplaySpec();
    void testGenericResultJudgment();
    void testMorphVmParamsAndRoi();
    void testMorphResultDisplaySpec();
    void testFilterVmParamsAndRoi();
    void testEnhanceVmParamsAndRoi();
    void testNormalizeAndResizeVmParams();
    void testAffineVmParams();
    void testBlobThresholdVmModes();
    void testBlobShapeFilters();
    void testBlobSortAndJudgment();
    void testBlobShapeMetricsVm();
    void testVmRegressionGuards();
    void testBlobVmOutputAndBoundary();
    void testBlobResultDisplaySpec();
    void testRemainingResultDisplaySpecs();
    void testCaliperVmParams();
    void testCaliperScoring();
    void testCaliperPositionScoringAndResults();
    void testCaliperProjectionDirection();
    void testCaliperParamVisibility();
    void testCaliperBlurEdge();
    void testResultNgRoundStartReset();
    void testCustomExecuteStillJudged();
    void testCaliperAcquisitionSortOrder();
    void testResultStateSnapshot();
    void testCaliperPairSelection();
    void testCaliperJudgment();
    void testStabilityGuards();
    void testRuntimeParamNormalization();
    void testAtomicNodeFlagsSerialization();
    void testImageArithAndHatOpenCv();
    void testCropBasic();
    void testImageArithDivByZero();
    void testFormulaEvaluation();
    void testImageReadAddSingleLocalFile();

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
    node.setParam(QStringLiteral("binaryType"), 1);
    QCOMPARE(node.getParam(QStringLiteral("binaryType")).toInt(), 1);

    node.setParam(QStringLiteral("lowThreshold"), 200);
    QCOMPARE(node.getParam(QStringLiteral("lowThreshold")).toInt(), 200);

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

    // 验证默认值（对齐 VM 图像二值化：默认均值二值化，低阈值 100）
    QCOMPARE(node.getParam(QStringLiteral("binaryType")).toInt(), 1);
    QCOMPARE(node.getParam(QStringLiteral("lowThreshold")).toInt(), 100);
}

void NodeCoreTest::testParamBoundary()
{
    OpencvThresholdNode node;
    node.init();

    // 测试边界值
    node.setParam(QStringLiteral("lowThreshold"), 0);
    QCOMPARE(node.getParam(QStringLiteral("lowThreshold")).toInt(), 0);

    node.setParam(QStringLiteral("lowThreshold"), 255);
    QCOMPARE(node.getParam(QStringLiteral("lowThreshold")).toInt(), 255);

    // 测试越界值（应被钳制）
    node.setParam(QStringLiteral("lowThreshold"), -10);
    QVERIFY(node.getParam(QStringLiteral("lowThreshold")).toInt() >= 0);

    node.setParam(QStringLiteral("lowThreshold"), 300);
    QVERIFY(node.getParam(QStringLiteral("lowThreshold")).toInt() <= 255);
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
    node1.setParam(QStringLiteral("binaryType"), 0);
    node1.setParam(QStringLiteral("lowThreshold"), 200);
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
    QCOMPARE(node2.getParam(QStringLiteral("binaryType")).toInt(), 0);
    QCOMPARE(node2.getParam(QStringLiteral("lowThreshold")).toInt(), 200);
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

    // 验证端口数量（B1 统一输出：二值图/前景像素数已折叠进同一端口，不再是 3 个红点）
    QCOMPARE(node.inputPorts().size(), 1);   // 图像输入
    QCOMPARE(node.outputPorts().size(), 1);  // 唯一「输出」：图像 + 子结果

    // 验证端口类型：聚合端口对下游一视同仁（Any），承诺图像与否记在声明类型上
    QCOMPARE(node.inputPorts()[0]->dataType(), PortDataType::Image);
    QCOMPARE(node.outputPorts()[0]->dataType(), PortDataType::Any);
    QCOMPARE(node.logicalOutputs().size(), 2);   // 二值图 + 前景像素数
    QCOMPARE(node.logicalOutputs().at(0).first, QStringLiteral("二值图"));
    QCOMPARE(node.logicalOutputs().at(1).first, QStringLiteral("前景像素数"));
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
    node.setParam(QStringLiteral("binaryType"), 0);  // 硬阈值
    node.setParam(QStringLiteral("lowThreshold"), 128);

    // 执行（注意：run 方法需要输入图像）
    // 由于 HALCON 环境限制，这里只验证参数设置
    QCOMPARE(node.getParam(QStringLiteral("binaryType")).toInt(), 0);
    QCOMPARE(node.getParam(QStringLiteral("lowThreshold")).toInt(), 128);
}

// VM 图像二值化口径：五种类型都能出图；ROI 外按「黑色/原图」处理。
void NodeCoreTest::testThresholdVmModesAndRoi()
{
    OpencvThresholdNode node;
    node.init();
    node.setParam(QStringLiteral("nonRoiProcess"), 0);

    auto runOn = [&node](const cv::Mat &gray) -> cv::Mat {
        node.setInputImage(OpencvUtil::matToHimage(gray));
        node.run(false);
        Q_ASSERT(node.getParam(QStringLiteral("moduleStatus")).toBool());
        auto out = node.getOutputData(1);
        Q_ASSERT(out && out->getHImage().IsInitialized());
        return OpencvUtil::himageToMat(out->getHImage());
    };

    // ① 硬阈值：低<高，区间内置 255
    {
        cv::Mat img(100, 100, CV_8U, cv::Scalar(0));
        cv::rectangle(img, cv::Rect(10, 10, 40, 40), cv::Scalar(210), cv::FILLED);
        node.setParam(QStringLiteral("binaryType"), 0);
        node.setParam(QStringLiteral("lowThreshold"), 100);
        node.setParam(QStringLiteral("highThreshold"), 255);
        cv::Mat out = runOn(img);
        QCOMPARE(int(out.at<uchar>(20, 20)), 255);
        QCOMPARE(int(out.at<uchar>(5, 5)), 0);
        QCOMPARE(node.getParam(QStringLiteral("foregroundPixels")).toInt(), 40 * 40);
    }

    // ② 硬阈值：低>高，区间外置 255（VM 帮助原文）
    {
        cv::Mat img(60, 60, CV_8U, cv::Scalar(0));
        cv::rectangle(img, cv::Rect(10, 10, 20, 20), cv::Scalar(100), cv::FILLED);
        cv::rectangle(img, cv::Rect(35, 35, 20, 20), cv::Scalar(200), cv::FILLED);
        node.setParam(QStringLiteral("lowThreshold"), 200);
        node.setParam(QStringLiteral("highThreshold"), 50);
        cv::Mat out = runOn(img);
        QCOMPARE(int(out.at<uchar>(2, 2)), 255);      // 0 在区间外 → 目标
        QCOMPARE(int(out.at<uchar>(15, 15)), 0);      // 100 在区间内 → 背景
        QCOMPARE(int(out.at<uchar>(40, 40)), 0);      // 200 在区间内 → 背景
    }

    // ③ 均值二值化：小块亮区相对局部均值更高 → 目标
    {
        cv::Mat img(100, 100, CV_8U, cv::Scalar(50));
        cv::rectangle(img, cv::Rect(40, 40, 20, 20), cv::Scalar(200), cv::FILLED);
        node.setParam(QStringLiteral("binaryType"), 1);
        node.setParam(QStringLiteral("kernelWidth"), 31);
        node.setParam(QStringLiteral("kernelHeight"), 31);
        node.setParam(QStringLiteral("compareType"), 0);   // 大于等于
        node.setParam(QStringLiteral("thresholdOffset"), 20);
        cv::Mat out = runOn(img);
        QCOMPARE(int(out.at<uchar>(50, 50)), 255);
        QCOMPARE(int(out.at<uchar>(10, 10)), 0);
    }

    // ④ 高斯二值化：小块亮区相对高斯背景更高 → 目标
    {
        cv::Mat img(100, 100, CV_8U, cv::Scalar(50));
        cv::rectangle(img, cv::Rect(47, 47, 5, 5), cv::Scalar(200), cv::FILLED);
        node.setParam(QStringLiteral("binaryType"), 2);
        node.setParam(QStringLiteral("gaussKernelSize"), 31);
        node.setParam(QStringLiteral("gaussSigma"), 10.0);
        cv::Mat out = runOn(img);
        QCOMPARE(int(out.at<uchar>(49, 49)), 255);
        QCOMPARE(int(out.at<uchar>(10, 10)), 0);
    }

    // ⑤ Sauvola：小块暗目标应被检出（大块均匀暗区的内部阈值会低于其灰度，属算法特性）
    {
        cv::Mat img(100, 100, CV_8U, cv::Scalar(200));
        cv::rectangle(img, cv::Rect(47, 47, 5, 5), cv::Scalar(40), cv::FILLED);
        node.setParam(QStringLiteral("binaryType"), 3);
        node.setParam(QStringLiteral("sauvolaAdjustCoef"), 0.2);
        node.setParam(QStringLiteral("sauvolaRange"), 128.0);
        node.setParam(QStringLiteral("sauvolaSegType"), 0);  // 暗于背景
        node.setParam(QStringLiteral("sauvolaWinWidth"), 15);
        node.setParam(QStringLiteral("sauvolaWinHeight"), 15);
        cv::Mat out = runOn(img);
        QCOMPARE(int(out.at<uchar>(49, 49)), 255);
        QCOMPARE(int(out.at<uchar>(10, 10)), 0);
    }

    // ⑥ 自动（OTSU）：双峰图应分开
    {
        cv::Mat img(80, 80, CV_8U, cv::Scalar(30));
        cv::rectangle(img, cv::Rect(40, 0, 40, 80), cv::Scalar(220), cv::FILLED);
        node.setParam(QStringLiteral("binaryType"), 4);
        cv::Mat out = runOn(img);
        QCOMPARE(int(out.at<uchar>(40, 60)), 255);
        QCOMPARE(int(out.at<uchar>(40, 20)), 0);
    }

    // ⑦ ROI 外处理：原图 / 黑色
    {
        cv::Mat img(60, 60, CV_8U, cv::Scalar(200));
        node.setParam(QStringLiteral("binaryType"), 0);
        node.setParam(QStringLiteral("lowThreshold"), 100);
        node.setParam(QStringLiteral("highThreshold"), 255);
        RoiShape roi;
        roi.type = RoiType::Rect;
        roi.p1 = QPointF(10, 10);
        roi.p2 = QPointF(30, 30);
        node.applyGeometryRoi(roi);

        node.setParam(QStringLiteral("nonRoiProcess"), 1);   // 原图
        cv::Mat out = runOn(img);
        QCOMPARE(int(out.at<uchar>(15, 15)), 255);           // ROI 内：目标
        QCOMPARE(int(out.at<uchar>(2, 2)), 200);             // ROI 外：原图

        node.setParam(QStringLiteral("nonRoiProcess"), 0);   // 黑色
        out = runOn(img);
        QCOMPARE(int(out.at<uchar>(2, 2)), 0);
    }
}

// VM 图像二值化「模块结果」展示口径：耗时 / 模块状态 / 检测区域，配置参数不混入。
void NodeCoreTest::testThresholdResultDisplaySpec()
{
    OpencvThresholdNode node;
    node.init();

    const QVector<NodeResultField> spec = node.resultDisplayFields();
    QCOMPARE(int(spec.size()), 7);
    QCOMPARE(spec.at(0).key, QStringLiteral("elapsedMs"));
    QCOMPARE(spec.at(0).label, QStringLiteral("耗时"));
    QCOMPARE(spec.at(1).key, QStringLiteral("moduleStatus"));
    QCOMPARE(spec.at(1).label, QStringLiteral("模块状态"));
    QCOMPARE(spec.at(2).key, QStringLiteral("roiCenterX"));
    QCOMPARE(spec.at(6).key, QStringLiteral("roiAngle"));
    for (const NodeResultField &f : spec) {
        QVERIFY2(f.key != QStringLiteral("binaryType")
                     && f.key != QStringLiteral("lowThreshold")
                     && f.key != QStringLiteral("roiRectRow")
                     && f.key != QStringLiteral("nonRoiProcess"),
                 qPrintable(QStringLiteral("配置参数混进了模块结果规范：%1").arg(f.key)));
    }

    // 硬阈值 + 矩形 ROI：ROI 内 10×10 白块 = 100 个前景像素；检测区域按连续坐标回显。
    cv::Mat img(100, 100, CV_8UC1, cv::Scalar(0));
    cv::rectangle(img, cv::Rect(30, 40, 10, 10), cv::Scalar(255), cv::FILLED);
    node.setParam(QStringLiteral("binaryType"), 0);
    node.setParam(QStringLiteral("lowThreshold"), 100);
    node.setParam(QStringLiteral("highThreshold"), 255);
    node.setParam(QStringLiteral("roiRectRow"), 30);
    node.setParam(QStringLiteral("roiRectCol"), 20);
    node.setParam(QStringLiteral("roiRectWidth"), 40);
    node.setParam(QStringLiteral("roiRectHeight"), 50);
    node.setInputImage(OpencvUtil::matToHimage(img));
    node.run(false);

    QVERIFY2(node.getParam(QStringLiteral("moduleStatus")).toBool(), "二值化用例执行失败");
    QCOMPARE(node.getParam(QStringLiteral("foregroundPixels")).toInt(), 100);
    QCOMPARE(node.getParam(QStringLiteral("roiCenterX")).toDouble(), 40.0);
    QCOMPARE(node.getParam(QStringLiteral("roiCenterY")).toDouble(), 55.0);
    QCOMPARE(node.getParam(QStringLiteral("roiWidth")).toDouble(), 40.0);
    QCOMPARE(node.getParam(QStringLiteral("roiHeight")).toDouble(), 50.0);
    QCOMPARE(node.getParam(QStringLiteral("roiAngle")).toDouble(), 0.0);
}

// VM「结果显示 → 结果判断」通用条件：AND/OR、结果字段取值、判 NG 保留输出并写原因。
void NodeCoreTest::testGenericResultJudgment()
{
    OpencvPixelStatsNode node;
    node.init();
    cv::Mat img(10, 10, CV_8UC1, cv::Scalar(0));
    img(cv::Rect(5, 0, 5, 10)).setTo(cv::Scalar(255));
    node.setInputImage(OpencvUtil::matToHimage(img));
    // process() 从输入端口取图（setInputImage 只服务直接 run() 的用例），补一份端口数据
    auto inputObj = QSharedPointer<DataObject>::create();
    inputObj->setHImage(OpencvUtil::matToHimage(img));
    node.setInputData(0, inputObj);

    auto condition = [](const QString &param, const QString &op, const QString &value) {
        QJsonObject o;
        o.insert(QStringLiteral("param"), param);
        o.insert(QStringLiteral("op"), op);
        o.insert(QStringLiteral("value"), value);
        return o;
    };
    auto setConditions = [&node](int mode, const QJsonArray &conditions) {
        node.setParam(QStringLiteral("resJudgeMode"), mode);
        node.setParam(QStringLiteral("resJudgeConditions"),
                      QJsonDocument(conditions).toJson(QJsonDocument::Compact));
    };

    // ① AND 全部满足 → OK
    QJsonArray pass;
    pass.append(condition(QStringLiteral("minGray"), QStringLiteral(">="), QStringLiteral("0")));
    pass.append(condition(QStringLiteral("maxGray"), QStringLiteral("<="), QStringLiteral("255")));
    setConditions(0, pass);
    QVERIFY2(node.execute(), "AND 条件全部满足时应判 OK");
    QVERIFY(node.getParam(QStringLiteral("moduleStatus")).toBool());
    QVERIFY(node.getParam(QStringLiteral("lastError")).toString().isEmpty());

    // ② AND 一条不满足 → NG，但结果有效（保留输出、写原因）
    QJsonArray fail;
    fail.append(condition(QStringLiteral("maxGray"), QStringLiteral("<="), QStringLiteral("200")));
    setConditions(0, fail);
    QVERIFY2(!node.execute(), "maxGray=255 不满足 <=200，应判 NG");
    QVERIFY(!node.getParam(QStringLiteral("moduleStatus")).toBool());
    QVERIFY2(node.isResultNg(), "结果判断 NG 应保留输出（resultNg）");
    QVERIFY2(node.getParam(QStringLiteral("lastError")).toString().contains(
                 QStringLiteral("结果判断")),
             qPrintable(QStringLiteral("判红原因缺少结果判断说明：%1")
                            .arg(node.getParam(QStringLiteral("lastError")).toString())));
    QVERIFY2(node.getOutputData(0) != nullptr, "判 NG 但结果有效时输出必须保留");
    QCOMPARE(node.getParam(QStringLiteral("maxGray")).toDouble(), 255.0);

    // ③ OR 任一满足 → OK；全部不满足 → NG
    QJsonArray orPass;
    orPass.append(condition(QStringLiteral("minGray"), QStringLiteral(">="), QStringLiteral("10")));
    orPass.append(condition(QStringLiteral("maxGray"), QStringLiteral(">="), QStringLiteral("250")));
    setConditions(1, orPass);
    QVERIFY2(node.execute(), "OR 任一条件满足时应判 OK");
    QVERIFY2(!node.isResultNg(), "下一轮应复位 resultNg（否则上一轮红色残留）");

    QJsonArray orFail;
    orFail.append(condition(QStringLiteral("minGray"), QStringLiteral(">="), QStringLiteral("10")));
    orFail.append(condition(QStringLiteral("maxGray"), QStringLiteral("<"), QStringLiteral("100")));
    setConditions(1, orFail);
    QVERIFY2(!node.execute(), "OR 全部不满足时应判 NG");
    QVERIFY(node.isResultNg());

    // ④ 清空条件 → 回到节点自身判断（OK）
    node.setParam(QStringLiteral("resJudgeConditions"), QString());
    QVERIFY2(node.execute(), "无通用判断条件时应恢复 OK");

    QString reason;
    // ⑤ 非法 JSON / 顶层非数组：旧实现 return true 静默放行，等于"没有判断"
    node.setParam(QStringLiteral("resJudgeConditions"), QStringLiteral("{broken"));
    reason.clear();
    QVERIFY2(!node.evaluateGenericResultJudgment(&reason),
             "非法 JSON 必须判 NG，不能静默当通过");
    QVERIFY2(reason.contains(QStringLiteral("条件配置格式错误")), qPrintable(reason));

    node.setParam(QStringLiteral("resJudgeConditions"), QStringLiteral("{\"a\":1}"));
    reason.clear();
    QVERIFY2(!node.evaluateGenericResultJudgment(&reason), "顶层非数组应判 NG");
    QVERIFY2(reason.contains(QStringLiteral("顶层不是数组")), qPrintable(reason));

    // ⑥ 坏条件/空字段名：旧实现 continue 跳过，等于该条件恒通过（OR 下尤其危险）
    QJsonArray broken;
    broken.append(QJsonValue(42));
    broken.append(condition(QString(), QStringLiteral(">="), QStringLiteral("0")));
    setConditions(0, broken);
    reason.clear();
    QVERIFY2(!node.evaluateGenericResultJudgment(&reason), "坏条件与空字段名应判 NG");
    QVERIFY2(reason.contains(QStringLiteral("格式错误"))
                 || reason.contains(QStringLiteral("缺少字段名")),
             qPrintable(reason));

    // ⑦ != 空值/空期望：旧实现 !equal 恒 true，产线漏检
    QJsonArray neEmptyExpected;
    neEmptyExpected.append(
        condition(QStringLiteral("minGray"), QStringLiteral("!="), QString()));
    setConditions(0, neEmptyExpected);
    reason.clear();
    QVERIFY2(!node.evaluateGenericResultJudgment(&reason),
             "期望值为空时 != 不应恒通过");
    QVERIFY2(reason.contains(QStringLiteral("缺少可比较的值")), qPrintable(reason));

    node.setParam(QStringLiteral("emptyField"), QString());   // valid QVariant，但值为空串
    QJsonArray neEmptyActual;
    neEmptyActual.append(
        condition(QStringLiteral("emptyField"), QStringLiteral("!="), QStringLiteral("abc")));
    setConditions(0, neEmptyActual);
    reason.clear();
    QVERIFY2(!node.evaluateGenericResultJudgment(&reason),
             "实际值为空串时 != 不应恒通过");

    // ⑧ 正常的数值 != 仍应可用（避免修过头）
    QJsonArray neOk;
    neOk.append(condition(QStringLiteral("maxGray"), QStringLiteral("!="), QStringLiteral("1")));
    setConditions(0, neOk);
    reason.clear();
    QVERIFY2(node.evaluateGenericResultJudgment(&reason), "正常的数值 != 仍应通过");

    // ⑨ 框架级公共字段（elapsedMs / roi*）不在节点参数表里，由 NodeBase::execute()
    //    通过 commonResultFields() 注入；判断与结果表快照共用同一份取值。
    QVariantMap common;
    common.insert(QStringLiteral("elapsedMs"), 12.0);
    common.insert(QStringLiteral("roiCenterX"), 30.0);
    QJsonArray commonPass;
    commonPass.append(
        condition(QStringLiteral("elapsedMs"), QStringLiteral("<"), QStringLiteral("1000")));
    commonPass.append(
        condition(QStringLiteral("roiCenterX"), QStringLiteral("=="), QStringLiteral("30")));
    setConditions(0, commonPass);
    QString commonReason;
    QVERIFY2(node.evaluateResultJudgment(common, &commonReason), qPrintable(commonReason));

    QJsonArray commonFail;
    commonFail.append(
        condition(QStringLiteral("elapsedMs"), QStringLiteral(">"), QStringLiteral("1000")));
    setConditions(0, commonFail);
    commonReason.clear();
    QVERIFY2(!node.evaluateResultJudgment(common, &commonReason),
             "公共字段条件不满足时应判 NG");
    QVERIFY2(commonReason.contains(QStringLiteral("耗时"))
                 || commonReason.contains(QStringLiteral("elapsedMs")),
             qPrintable(QStringLiteral("原因里应显示公共字段的中文名：%1").arg(commonReason)));
    QVERIFY2(!commonReason.contains(QStringLiteral("缺少结果字段")),
             "elapsedMs 必须从 commonFields 读到，不能报缺字段");

    node.setParam(QStringLiteral("resJudgeConditions"), QString());
}

// VM 形态学口径：膨胀/腐蚀/开/闭、三种结构元、宽高独立、迭代次数 0 = 不处理；ROI 外保留原图。
void NodeCoreTest::testMorphVmParamsAndRoi()
{
    OpencvMorphNode node;
    node.init();
    QCOMPARE(node.getParam(QStringLiteral("morphType")).toInt(), 1);      // 默认腐蚀
    QCOMPARE(node.getParam(QStringLiteral("morphShape")).toInt(), 0);     // 默认矩形
    QCOMPARE(node.getParam(QStringLiteral("iterations")).toInt(), 1);
    QCOMPARE(node.getParam(QStringLiteral("kernelWidth")).toInt(), 3);
    QCOMPARE(node.getParam(QStringLiteral("kernelHeight")).toInt(), 3);

    auto runOn = [&node](const cv::Mat &gray) -> cv::Mat {
        node.setInputImage(OpencvUtil::matToHimage(gray));
        node.run(false);
        Q_ASSERT(node.getParam(QStringLiteral("moduleStatus")).toBool());
        auto out = node.getOutputData(1);
        Q_ASSERT(out && out->getHImage().IsInitialized());
        return OpencvUtil::himageToMat(out->getHImage());
    };

    // ① 腐蚀：10×10 方块缩成 8×8
    {
        cv::Mat img(100, 100, CV_8U, cv::Scalar(0));
        cv::rectangle(img, cv::Rect(10, 10, 10, 10), cv::Scalar(255), cv::FILLED);
        node.setParam(QStringLiteral("morphType"), 1);
        node.setParam(QStringLiteral("kernelWidth"), 3);
        node.setParam(QStringLiteral("kernelHeight"), 3);
        node.setParam(QStringLiteral("iterations"), 1);
        cv::Mat out = runOn(img);
        QCOMPARE(cv::countNonZero(out), 8 * 8);
        QCOMPARE(int(out.at<uchar>(10, 10)), 0);     // 角点被腐蚀掉
    }

    // ② 膨胀：10×10 方块长成 12×12
    {
        cv::Mat img(100, 100, CV_8U, cv::Scalar(0));
        cv::rectangle(img, cv::Rect(10, 10, 10, 10), cv::Scalar(255), cv::FILLED);
        node.setParam(QStringLiteral("morphType"), 0);
        cv::Mat out = runOn(img);
        QCOMPARE(cv::countNonZero(out), 12 * 12);
        QCOMPARE(int(out.at<uchar>(9, 20)), 255);
    }

    // ③ 宽高独立：7×3 矩形核对 3×3 的结果不同
    {
        cv::Mat img(100, 100, CV_8U, cv::Scalar(0));
        cv::rectangle(img, cv::Rect(20, 20, 20, 20), cv::Scalar(255), cv::FILLED);
        node.setParam(QStringLiteral("morphType"), 1);
        node.setParam(QStringLiteral("kernelWidth"), 7);
        node.setParam(QStringLiteral("kernelHeight"), 3);
        cv::Mat out = runOn(img);
        QCOMPARE(cv::countNonZero(out), 14 * 18);
    }

    // ④ 三种结构元都能跑
    {
        cv::Mat img(100, 100, CV_8U, cv::Scalar(0));
        cv::rectangle(img, cv::Rect(30, 30, 30, 30), cv::Scalar(255), cv::FILLED);
        node.setParam(QStringLiteral("morphType"), 0);
        node.setParam(QStringLiteral("kernelWidth"), 9);
        node.setParam(QStringLiteral("kernelHeight"), 9);
        for (int shape = 0; shape <= 2; ++shape) {
            node.setParam(QStringLiteral("morphShape"), shape);
            cv::Mat out = runOn(img);
            QVERIFY(cv::countNonZero(out) > 0);
        }
    }

    // ⑤ 迭代 0 = 不处理
    {
        cv::Mat img(100, 100, CV_8U, cv::Scalar(0));
        cv::rectangle(img, cv::Rect(10, 10, 30, 30), cv::Scalar(255), cv::FILLED);
        node.setParam(QStringLiteral("morphShape"), 0);
        node.setParam(QStringLiteral("iterations"), 0);
        cv::Mat out = runOn(img);
        cv::Mat diff;
        cv::absdiff(img, out, diff);
        QCOMPARE(cv::countNonZero(diff), 0);
    }

    // ⑥ ROI 外保留原图（ROI 内膨胀，ROI 外仍是原图）
    {
        cv::Mat img(100, 100, CV_8U, cv::Scalar(0));
        cv::rectangle(img, cv::Rect(30, 30, 10, 10), cv::Scalar(255), cv::FILLED);
        node.setParam(QStringLiteral("morphType"), 0);
        node.setParam(QStringLiteral("kernelWidth"), 3);
        node.setParam(QStringLiteral("kernelHeight"), 3);
        node.setParam(QStringLiteral("iterations"), 1);
        RoiShape roi;
        roi.type = RoiType::Rect;
        roi.p1 = QPointF(20, 20);
        roi.p2 = QPointF(40, 40);
        node.applyGeometryRoi(roi);
        cv::Mat out = runOn(img);
        QCOMPARE(int(out.at<uchar>(29, 35)), 255);    // ROI 内膨胀到 29
        QCOMPARE(int(out.at<uchar>(28, 35)), 0);      // ROI 外保留原图（原图是 0）
    }
}

// VM 形态学处理「模块结果」展示口径：耗时 / 模块状态 / 检测区域，配置参数不混入。
void NodeCoreTest::testMorphResultDisplaySpec()
{
    OpencvMorphNode node;
    node.init();

    const QVector<NodeResultField> spec = node.resultDisplayFields();
    QCOMPARE(int(spec.size()), 7);
    QCOMPARE(spec.at(0).key, QStringLiteral("elapsedMs"));
    QCOMPARE(spec.at(1).key, QStringLiteral("moduleStatus"));
    QCOMPARE(spec.at(2).key, QStringLiteral("roiCenterX"));
    QCOMPARE(spec.at(6).key, QStringLiteral("roiAngle"));
    for (const NodeResultField &f : spec) {
        QVERIFY2(f.key != QStringLiteral("morphType")
                     && f.key != QStringLiteral("kernelWidth")
                     && f.key != QStringLiteral("roiRectRow"),
                 qPrintable(QStringLiteral("配置参数混进了模块结果规范：%1").arg(f.key)));
    }

    cv::Mat img(100, 100, CV_8UC1, cv::Scalar(0));
    cv::rectangle(img, cv::Rect(30, 30, 10, 10), cv::Scalar(255), cv::FILLED);
    node.setParam(QStringLiteral("morphType"), 0);
    node.setParam(QStringLiteral("iterations"), 1);
    node.setParam(QStringLiteral("roiRectRow"), 20);
    node.setParam(QStringLiteral("roiRectCol"), 20);
    node.setParam(QStringLiteral("roiRectWidth"), 20);
    node.setParam(QStringLiteral("roiRectHeight"), 20);
    node.setInputImage(OpencvUtil::matToHimage(img));
    node.run(false);

    QVERIFY2(node.getParam(QStringLiteral("moduleStatus")).toBool(), "形态学用例执行失败");
    QCOMPARE(node.getParam(QStringLiteral("roiCenterX")).toDouble(), 30.0);
    QCOMPARE(node.getParam(QStringLiteral("roiCenterY")).toDouble(), 30.0);
    QCOMPARE(node.getParam(QStringLiteral("roiWidth")).toDouble(), 20.0);
    QCOMPARE(node.getParam(QStringLiteral("roiHeight")).toDouble(), 20.0);
    QCOMPARE(node.getParam(QStringLiteral("roiAngle")).toDouble(), 0.0);
}

// VM 滤波口径：高斯核/中值矩形核；ROI 外保留原图。
void NodeCoreTest::testFilterVmParamsAndRoi()
{
    auto runOn = [](HalconNode &node, const cv::Mat &gray) -> cv::Mat {
        node.setInputImage(OpencvUtil::matToHimage(gray));
        node.run(false);
        Q_ASSERT(node.getParam(QStringLiteral("moduleStatus")).toBool());
        auto out = node.getOutputData(1);
        Q_ASSERT(out && out->getHImage().IsInitialized());
        return OpencvUtil::himageToMat(out->getHImage());
    };

    // ① 高斯滤波：默认核 3，单像素亮点被摊开
    {
        BlurNode blur;
        blur.init();
        QCOMPARE(blur.getParam(QStringLiteral("gaussKernelSize")).toInt(), 3);
        cv::Mat img(100, 100, CV_8U, cv::Scalar(0));
        img.at<uchar>(50, 50) = 255;
        cv::Mat out = runOn(blur, img);
        QVERIFY2(out.at<uchar>(50, 50) < 255, "高斯滤波应把单像素亮点摊开");
        QVERIFY2(out.at<uchar>(50, 50) > 0, "高斯滤波后中心仍应有能量");
        QCOMPARE(int(out.at<uchar>(10, 10)), 0);
    }

    // ② 中值滤波：方形 3×3 去除椒盐点
    {
        MedianFilterNode median;
        median.init();
        QCOMPARE(median.getParam(QStringLiteral("kernelWidth")).toInt(), 3);
        QCOMPARE(median.getParam(QStringLiteral("kernelHeight")).toInt(), 3);
        cv::Mat img(100, 100, CV_8U, cv::Scalar(100));
        img.at<uchar>(50, 50) = 255;
        cv::Mat out = runOn(median, img);
        QCOMPARE(int(out.at<uchar>(50, 50)), 100);
    }

    // ③ 中值滤波：矩形 5×3 走自研直方图路径
    {
        MedianFilterNode median;
        median.init();
        median.setParam(QStringLiteral("kernelWidth"), 5);
        median.setParam(QStringLiteral("kernelHeight"), 3);
        cv::Mat img(100, 100, CV_8U, cv::Scalar(100));
        img.at<uchar>(50, 50) = 255;
        cv::Mat out = runOn(median, img);
        QCOMPARE(int(out.at<uchar>(50, 50)), 100);
    }

    // ④ ROI 外保留原图：高斯/中值都只处理 ROI 内
    {
        cv::Mat img(100, 100, CV_8U, cv::Scalar(100));
        img.at<uchar>(50, 50) = 255;
        RoiShape roi;
        roi.type = RoiType::Rect;
        roi.p1 = QPointF(5, 5);
        roi.p2 = QPointF(25, 25);

        BlurNode blur;
        blur.init();
        blur.applyGeometryRoi(roi);
        cv::Mat blurOut = runOn(blur, img);
        QCOMPARE(int(blurOut.at<uchar>(50, 50)), 255);   // ROI 外原图

        MedianFilterNode median;
        median.init();
        median.applyGeometryRoi(roi);
        cv::Mat medianOut = runOn(median, img);
        QCOMPARE(int(medianOut.at<uchar>(50, 50)), 255);
    }
}

// VM 图像增强/归一化口径：锐化强度 100=1.0、对比度 100=不变、直方图均衡；ROI 外保留原图。
void NodeCoreTest::testEnhanceVmParamsAndRoi()
{
    auto runOn = [](HalconNode &node, const cv::Mat &gray) -> cv::Mat {
        node.setInputImage(OpencvUtil::matToHimage(gray));
        node.run(false);
        Q_ASSERT(node.getParam(QStringLiteral("moduleStatus")).toBool());
        return OpencvUtil::himageToMat(node.getOutputImage());
    };

    // ① 锐化：默认强度 0 = 原图；强度 200 在台阶边缘产生过冲
    {
        SharpenNode sharpen;
        sharpen.init();
        QCOMPARE(sharpen.getParam(QStringLiteral("sharpenStrength")).toInt(), 0);
        QCOMPARE(sharpen.getParam(QStringLiteral("sharpenKernelSize")).toInt(), 3);
        cv::Mat img(100, 100, CV_8U, cv::Scalar(100));
        cv::rectangle(img, cv::Rect(50, 0, 50, 100), cv::Scalar(150), cv::FILLED);
        cv::Mat out = runOn(sharpen, img);
        cv::Mat diff;
        cv::absdiff(img, out, diff);
        QCOMPARE(cv::countNonZero(diff), 0);   // 强度 0 不锐化

        sharpen.setParam(QStringLiteral("sharpenStrength"), 200);
        out = runOn(sharpen, img);
        QVERIFY2(out.at<uchar>(50, 49) < 100, "锐化应让边缘暗侧更暗");
        QVERIFY2(out.at<uchar>(50, 50) > 150, "锐化应让边缘亮侧更亮");
    }

    // ② 对比度：默认 100 = 原图；200 以 128 为中心拉开
    {
        ContrastStretchNode contrast;
        contrast.init();
        QCOMPARE(contrast.getParam(QStringLiteral("contrastFactor")).toInt(), 100);
        cv::Mat img(100, 100, CV_8U, cv::Scalar(100));
        img.at<uchar>(20, 20) = 200;
        cv::Mat out = runOn(contrast, img);
        QCOMPARE(int(out.at<uchar>(10, 10)), 100);

        contrast.setParam(QStringLiteral("contrastFactor"), 200);
        out = runOn(contrast, img);
        QCOMPARE(int(out.at<uchar>(10, 10)), 72);    // 100 → 72
        QCOMPARE(int(out.at<uchar>(20, 20)), 255);   // 200 → 超上限饱和
    }

    // ③ 直方图均衡：低对比度渐变被拉开到 0~255
    {
        HistogramEqualizeNode equalize;
        equalize.init();
        cv::Mat img(100, 100, CV_8U);
        for (int y = 0; y < 100; ++y)
            for (int x = 0; x < 100; ++x)
                img.at<uchar>(y, x) = uchar(100 + x / 10);   // 100~109
        cv::Mat out = runOn(equalize, img);
        double mn = 0.0, mx = 0.0;
        cv::minMaxLoc(out, &mn, &mx);
        QVERIFY2(mn < 50.0 && mx > 200.0, "直方图均衡应拉开灰度范围");
    }

    // ④ ROI 外保留原图（对比度只作用于 ROI 内）
    {
        ContrastStretchNode contrast;
        contrast.init();
        contrast.setParam(QStringLiteral("contrastFactor"), 200);
        cv::Mat img(100, 100, CV_8U, cv::Scalar(100));
        RoiShape roi;
        roi.type = RoiType::Rect;
        roi.p1 = QPointF(5, 5);
        roi.p2 = QPointF(25, 25);
        contrast.applyGeometryRoi(roi);
        cv::Mat out = runOn(contrast, img);
        QCOMPARE(int(out.at<uchar>(50, 50)), 100);   // ROI 外原图
    }
}

// VM 归一化「均值标准差归一化」+ 图像缩放口径。
void NodeCoreTest::testNormalizeAndResizeVmParams()
{
    auto runOn = [](HalconNode &node, const cv::Mat &gray) -> cv::Mat {
        node.setInputImage(OpencvUtil::matToHimage(gray));
        node.run(false);
        Q_ASSERT(node.getParam(QStringLiteral("moduleStatus")).toBool());
        return OpencvUtil::himageToMat(node.getOutputImage());
    };

    // ① 均值标准差归一化：原图均值 120/标准差 20 → 目标均值 128/标准差 20
    {
        GrayStretchNode normalize;
        normalize.init();
        QCOMPARE(normalize.getParam(QStringLiteral("meanVal")).toDouble(), 128.0);
        QCOMPARE(normalize.getParam(QStringLiteral("stdVal")).toDouble(), 20.0);
        cv::Mat img(100, 100, CV_8U, cv::Scalar(100));
        cv::rectangle(img, cv::Rect(50, 0, 50, 100), cv::Scalar(140), cv::FILLED);
        cv::Mat out = runOn(normalize, img);
        QCOMPARE(int(out.at<uchar>(10, 10)), 108);   // 100 → 108
        QCOMPARE(int(out.at<uchar>(10, 80)), 148);   // 140 → 148
    }

    // ② 归一化 ROI 外保留原图
    {
        GrayStretchNode normalize;
        normalize.init();
        cv::Mat img(100, 100, CV_8U, cv::Scalar(100));
        RoiShape roi;
        roi.type = RoiType::Rect;
        roi.p1 = QPointF(5, 5);
        roi.p2 = QPointF(25, 25);
        normalize.applyGeometryRoi(roi);
        cv::Mat out = runOn(normalize, img);
        QCOMPARE(int(out.at<uchar>(50, 50)), 100);   // ROI 外原图
    }

    // ③ 图像缩放：VM 默认 800×800；显式 200×100 最近邻
    {
        ResizeNode resize;
        resize.init();
        QCOMPARE(resize.getParam(QStringLiteral("widthValue")).toInt(), 800);
        QCOMPARE(resize.getParam(QStringLiteral("heightValue")).toInt(), 800);
        QCOMPARE(resize.getParam(QStringLiteral("interType")).toInt(), 1);
        QCOMPARE(resize.getParam(QStringLiteral("antiAliasing")).toBool(), false);

        cv::Mat img(100, 100, CV_8U, cv::Scalar(0));
        cv::rectangle(img, cv::Rect(50, 0, 50, 100), cv::Scalar(255), cv::FILLED);
        resize.setParam(QStringLiteral("widthValue"), 200);
        resize.setParam(QStringLiteral("heightValue"), 100);
        resize.setParam(QStringLiteral("interType"), 0);   // 最近邻
        cv::Mat out = runOn(resize, img);
        QCOMPARE(out.cols, 200);
        QCOMPARE(out.rows, 100);
        QCOMPARE(int(out.at<uchar>(50, 150)), 255);
        QCOMPARE(int(out.at<uchar>(50, 50)), 0);
    }
}

// VM 仿射口径：平移/镜像/旋转；锁定输出尺寸 vs 扩展画布；常量/邻近复制填充。
void NodeCoreTest::testAffineVmParams()
{
    auto runOn = [](HalconNode &node, const cv::Mat &gray) -> cv::Mat {
        node.setInputImage(OpencvUtil::matToHimage(gray));
        node.run(false);
        Q_ASSERT(node.getParam(QStringLiteral("moduleStatus")).toBool());
        return OpencvUtil::himageToMat(node.getOutputImage());
    };

    // ① 平移：默认 0；未锁定 → 画布扩展；锁定 → 原尺寸 + 常量填充
    {
        TranslateImageNode translate;
        translate.init();
        QCOMPARE(translate.getParam(QStringLiteral("moveXValue")).toInt(), 0);
        QCOMPARE(translate.getParam(QStringLiteral("moveYValue")).toInt(), 0);
        QCOMPARE(translate.getParam(QStringLiteral("lockOutputImageSize")).toBool(), false);

        cv::Mat img(100, 100, CV_8U, cv::Scalar(0));
        img.at<uchar>(0, 0) = 255;
        translate.setParam(QStringLiteral("moveXValue"), 10);
        translate.setParam(QStringLiteral("moveYValue"), 5);
        cv::Mat out = runOn(translate, img);
        QCOMPARE(out.cols, 110);
        QCOMPARE(out.rows, 105);
        QCOMPARE(int(out.at<uchar>(5, 10)), 255);

        translate.setParam(QStringLiteral("lockOutputImageSize"), true);
        translate.setParam(QStringLiteral("extensionValue"), 200);
        out = runOn(translate, img);
        QCOMPARE(out.cols, 100);
        QCOMPARE(out.rows, 100);
        QCOMPARE(int(out.at<uchar>(0, 0)), 200);   // 常量填充
        QCOMPARE(int(out.at<uchar>(5, 10)), 255);
    }

    // ② 镜像：水平 / 垂直 / 水平垂直
    {
        MirrorNode mirror;
        mirror.init();
        QCOMPARE(mirror.getParam(QStringLiteral("mirrorOrientation")).toInt(), 0);
        cv::Mat img(100, 100, CV_8U, cv::Scalar(0));
        img.at<uchar>(0, 0) = 255;
        cv::Mat out = runOn(mirror, img);
        QCOMPARE(int(out.at<uchar>(0, 99)), 255);   // 水平翻转

        mirror.setParam(QStringLiteral("mirrorOrientation"), 1);
        out = runOn(mirror, img);
        QCOMPARE(int(out.at<uchar>(99, 0)), 255);   // 垂直翻转

        mirror.setParam(QStringLiteral("mirrorOrientation"), 2);
        out = runOn(mirror, img);
        QCOMPARE(int(out.at<uchar>(99, 99)), 255);  // 水平垂直
    }

    // ③ 旋转：0° 原图；90° 未锁定 → 画布扩展到旋转后包围盒
    {
        OpencvRotateNode rotate;
        rotate.init();
        QCOMPARE(rotate.getParam(QStringLiteral("rotateAngle")).toDouble(), 0.0);
        QCOMPARE(rotate.getParam(QStringLiteral("lockOutputImageSize")).toBool(), false);
        cv::Mat img(60, 100, CV_8U, cv::Scalar(0));   // 高 60、宽 100
        img.at<uchar>(0, 0) = 255;
        cv::Mat out = runOn(rotate, img);
        QCOMPARE(out.cols, 100);
        QCOMPARE(out.rows, 60);
        QCOMPARE(int(out.at<uchar>(0, 0)), 255);

        rotate.setParam(QStringLiteral("rotateAngle"), 90.0);
        out = runOn(rotate, img);
        QVERIFY(out.cols > 0 && out.rows > 0);
        QVERIFY2(out.rows > 60 || out.cols > 60, "90° 旋转后包围盒应扩展");
    }
}

// VM Blob 阈值口径：不二值化/单阈值/双阈值/自动/软阈值（固定/相对）+ 极性 + ROI。
void NodeCoreTest::testBlobThresholdVmModes()
{
    OpencvBlobNode blob;
    blob.init();
    QCOMPARE(blob.getParam(QStringLiteral("thresholdType")).toInt(), 1);   // 默认单阈值
    QCOMPARE(blob.getParam(QStringLiteral("lowThreshold")).toInt(), 100);
    QCOMPARE(blob.getParam(QStringLiteral("highThreshold")).toInt(), 150);
    QCOMPARE(blob.getParam(QStringLiteral("findNum")).toInt(), 100);

    auto runOn = [&blob](const cv::Mat &gray) {
        blob.setInputImage(OpencvUtil::matToHimage(gray));
        blob.run(false);
        // Release 下 Q_ASSERT 会被编译掉，前置条件必须走 QVERIFY2 才真的守得住
        QVERIFY2(blob.getParam(QStringLiteral("moduleStatus")).toBool(),
                 "Blob 用例的前置执行失败了");
    };

    // ① 单阈值 + 亮于背景
    {
        cv::Mat img(60, 60, CV_8U, cv::Scalar(100));
        cv::rectangle(img, cv::Rect(10, 10, 20, 20), cv::Scalar(200), cv::FILLED);
        blob.setParam(QStringLiteral("thresholdType"), 1);
        blob.setParam(QStringLiteral("polarity"), 0);
        blob.setParam(QStringLiteral("lowThreshold"), 150);
        runOn(img);
        QCOMPARE(blob.getParam(QStringLiteral("blobCount")).toInt(), 1);
        QCOMPARE(blob.getParam(QStringLiteral("maxBlobArea")).toDouble(), 400.0);
    }

    // ② 单阈值 + 暗于背景
    {
        cv::Mat img(60, 60, CV_8U, cv::Scalar(200));
        cv::rectangle(img, cv::Rect(10, 10, 20, 20), cv::Scalar(50), cv::FILLED);
        blob.setParam(QStringLiteral("polarity"), 1);
        blob.setParam(QStringLiteral("lowThreshold"), 150);
        runOn(img);
        QCOMPARE(blob.getParam(QStringLiteral("blobCount")).toInt(), 1);
        QCOMPARE(blob.getParam(QStringLiteral("maxBlobArea")).toDouble(), 400.0);
    }

    // ③ 双阈值：低<高 → 区间内
    {
        cv::Mat img(60, 60, CV_8U, cv::Scalar(0));
        cv::rectangle(img, cv::Rect(10, 10, 10, 10), cv::Scalar(120), cv::FILLED);
        blob.setParam(QStringLiteral("thresholdType"), 2);
        blob.setParam(QStringLiteral("lowThreshold"), 100);
        blob.setParam(QStringLiteral("highThreshold"), 150);
        runOn(img);
        QCOMPARE(blob.getParam(QStringLiteral("blobCount")).toInt(), 1);
        QCOMPARE(blob.getParam(QStringLiteral("maxBlobArea")).toDouble(), 100.0);
    }

    // ④ 双阈值：低>高 → 区间外（120 在区间内被排除，0 成为前景）
    {
        cv::Mat img(60, 60, CV_8U, cv::Scalar(0));
        cv::rectangle(img, cv::Rect(10, 10, 10, 10), cv::Scalar(120), cv::FILLED);
        blob.setParam(QStringLiteral("lowThreshold"), 150);
        blob.setParam(QStringLiteral("highThreshold"), 100);
        runOn(img);
        QCOMPARE(blob.getParam(QStringLiteral("blobCount")).toInt(), 1);
        QCOMPARE(blob.getParam(QStringLiteral("maxBlobArea")).toDouble(), 60.0 * 60 - 100);
    }

    // ⑤ 软阈值（固定）：柔和度 0 时等价硬阈值（亮于背景）
    {
        cv::Mat img(60, 60, CV_8U, cv::Scalar(100));
        cv::rectangle(img, cv::Rect(10, 10, 20, 20), cv::Scalar(200), cv::FILLED);
        blob.setParam(QStringLiteral("thresholdType"), 4);
        blob.setParam(QStringLiteral("softLowThreshold"), 100);
        blob.setParam(QStringLiteral("softHighThreshold"), 150);
        blob.setParam(QStringLiteral("softness"), 0);
        blob.setParam(QStringLiteral("polarity"), 0);
        runOn(img);
        QCOMPARE(blob.getParam(QStringLiteral("blobCount")).toInt(), 1);
        QCOMPARE(blob.getParam(QStringLiteral("maxBlobArea")).toDouble(), 400.0);
    }

    // ⑥ ROI：只统计 ROI 内那个 Blob
    {
        cv::Mat img(80, 80, CV_8U, cv::Scalar(0));
        cv::rectangle(img, cv::Rect(5, 5, 10, 10), cv::Scalar(255), cv::FILLED);
        cv::rectangle(img, cv::Rect(50, 50, 10, 10), cv::Scalar(255), cv::FILLED);
        blob.setParam(QStringLiteral("thresholdType"), 1);
        blob.setParam(QStringLiteral("polarity"), 0);
        blob.setParam(QStringLiteral("lowThreshold"), 100);
        RoiShape roi;
        roi.type = RoiType::Rect;
        roi.p1 = QPointF(0, 0);
        roi.p2 = QPointF(30, 30);
        blob.applyGeometryRoi(roi);
        runOn(img);
        QCOMPARE(blob.getParam(QStringLiteral("blobCount")).toInt(), 1);
        QCOMPARE(blob.getParam(QStringLiteral("maxBlobArea")).toDouble(), 100.0);
    }
}

// VM Blob 批 2：矩形度 / 轴比 / 周长等形状筛选（每项 enable + min/max）。
void NodeCoreTest::testBlobShapeFilters()
{
    OpencvBlobNode blob;
    blob.init();
    QCOMPARE(blob.getParam(QStringLiteral("selectByArea")).toBool(), true);
    QCOMPARE(blob.getParam(QStringLiteral("selectByRectWidth")).toBool(), false);
    QCOMPARE(blob.getParam(QStringLiteral("selectByRectangularity")).toBool(), false);

    auto runOn = [&blob](const cv::Mat &gray) {
        blob.setInputImage(OpencvUtil::matToHimage(gray));
        blob.run(false);
        // 本用例有些配置会「全部被筛掉 → 判红」，那是预期结果，不能拿 moduleStatus
        // 当执行成功的前置（旧 Q_ASSERT 在 Release 被编译掉，一直没暴露）。
        // 这里只确认 run 真的写出过结果字段。
        QVERIFY2(blob.getParam(QStringLiteral("blobCount")).isValid(),
                 "Blob 形状筛选用例没有产出结果字段");
    };
    auto resetFilters = [&blob]() {
        blob.setParam(QStringLiteral("selectByRectangularity"), false);
        blob.setParam(QStringLiteral("axisRatioEnable"), false);
        blob.setParam(QStringLiteral("selectByPerimeter"), false);
    };

    // ① 矩形度：方块通过，三角形被筛掉
    {
        cv::Mat square(60, 60, CV_8U, cv::Scalar(0));
        cv::rectangle(square, cv::Rect(20, 20, 20, 20), cv::Scalar(255), cv::FILLED);
        blob.setParam(QStringLiteral("selectByRectangularity"), true);
        blob.setParam(QStringLiteral("minRectangularity"), 0.9);
        runOn(square);
        QCOMPARE(blob.getParam(QStringLiteral("blobCount")).toInt(), 1);

        cv::Mat tri(60, 60, CV_8U, cv::Scalar(0));
        std::vector<cv::Point> pts{{10, 50}, {50, 50}, {30, 10}};
        cv::fillConvexPoly(tri, pts, cv::Scalar(255));
        runOn(tri);
        QCOMPARE(blob.getParam(QStringLiteral("blobCount")).toInt(), 0);
    }

    // ② 轴比：方块通过，细长矩形被筛掉
    {
        resetFilters();
        blob.setParam(QStringLiteral("axisRatioEnable"), true);
        blob.setParam(QStringLiteral("minAxisRatio"), 0.8);
        cv::Mat square(60, 60, CV_8U, cv::Scalar(0));
        cv::rectangle(square, cv::Rect(20, 20, 20, 20), cv::Scalar(255), cv::FILLED);
        runOn(square);
        QCOMPARE(blob.getParam(QStringLiteral("blobCount")).toInt(), 1);

        cv::Mat longRect(60, 60, CV_8U, cv::Scalar(0));
        cv::rectangle(longRect, cv::Rect(25, 5, 10, 50), cv::Scalar(255), cv::FILLED);
        runOn(longRect);
        QCOMPARE(blob.getParam(QStringLiteral("blobCount")).toInt(), 0);
    }

    // ③ 周长：20×20 方块周长≈80
    {
        resetFilters();
        blob.setParam(QStringLiteral("selectByPerimeter"), true);
        blob.setParam(QStringLiteral("minPerimeter"), 70.0);
        blob.setParam(QStringLiteral("maxPerimeter"), 100.0);
        cv::Mat square(60, 60, CV_8U, cv::Scalar(0));
        cv::rectangle(square, cv::Rect(20, 20, 20, 20), cv::Scalar(255), cv::FILLED);
        runOn(square);
        QCOMPARE(blob.getParam(QStringLiteral("blobCount")).toInt(), 1);

        blob.setParam(QStringLiteral("minPerimeter"), 100.0);
        runOn(square);
        QCOMPARE(blob.getParam(QStringLiteral("blobCount")).toInt(), 0);
    }
}

// VM Blob 批 3：13 种排序特征 / 升序降序不排序；数量/面积/总面积为 OK/NG 条件。
void NodeCoreTest::testBlobSortAndJudgment()
{
    OpencvBlobNode blob;
    blob.init();
    QCOMPARE(blob.getParam(QStringLiteral("sortFeature")).toInt(), 0);   // 面积
    QCOMPARE(blob.getParam(QStringLiteral("sortMode")).toInt(), 1);      // 降序
    QCOMPARE(blob.getParam(QStringLiteral("okWhenNumIsZero")).toBool(), false);

    cv::Mat img(100, 100, CV_8U, cv::Scalar(0));
    cv::rectangle(img, cv::Rect(10, 10, 10, 10), cv::Scalar(255), cv::FILLED);    // 100
    cv::rectangle(img, cv::Rect(40, 10, 20, 20), cv::Scalar(255), cv::FILLED);    // 400
    cv::rectangle(img, cv::Rect(70, 10, 30, 30), cv::Scalar(255), cv::FILLED);    // 900
    blob.setInputImage(OpencvUtil::matToHimage(img));

    // ① 面积降序 + 只留 2 个 → 900、400
    blob.setParam(QStringLiteral("findNum"), 2);
    blob.setParam(QStringLiteral("sortFeature"), 0);
    blob.setParam(QStringLiteral("sortMode"), 1);
    blob.run(false);
    QCOMPARE(blob.getParam(QStringLiteral("blobCount")).toInt(), 2);
    const auto list = blob.getOutputData(4);
    QVERIFY(list);
    const QVector<MeasureResult> rects = list->measureResults();
    QCOMPARE(rects.size(), 2);
    QCOMPARE(rects[0].value, 900.0);
    QCOMPARE(rects[1].value, 400.0);

    // ② 质心 X 升序 → 100、400、900
    blob.setParam(QStringLiteral("findNum"), 100);
    blob.setParam(QStringLiteral("sortFeature"), 4);
    blob.setParam(QStringLiteral("sortMode"), 0);
    blob.run(false);
    const QVector<MeasureResult> byX = blob.getOutputData(4)->measureResults();
    QCOMPARE(byX.size(), 3);
    QCOMPARE(byX[0].value, 100.0);
    QCOMPARE(byX[1].value, 400.0);
    QCOMPARE(byX[2].value, 900.0);

    // ③ 不排序：数量不变
    blob.setParam(QStringLiteral("sortMode"), 2);
    blob.run(false);
    QCOMPARE(blob.getParam(QStringLiteral("blobCount")).toInt(), 3);

    // ④ 数量检查：3 个 Blob 在 [2,5] 内 OK；[4,5] 判 NG
    blob.setParam(QStringLiteral("sortMode"), 1);
    blob.setParam(QStringLiteral("blobNumLimitEnable"), true);
    blob.setParam(QStringLiteral("blobNumLimitLow"), 2);
    blob.setParam(QStringLiteral("blobNumLimitHigh"), 5);
    blob.run(false);
    QVERIFY(blob.getParam(QStringLiteral("moduleStatus")).toBool());

    blob.setParam(QStringLiteral("blobNumLimitLow"), 4);
    blob.run(false);
    QVERIFY(!blob.getParam(QStringLiteral("moduleStatus")).toBool());
    QVERIFY(blob.getParam(QStringLiteral("judgeReason")).toString().contains(QStringLiteral("数量")));

    // ⑤ 单个 Blob 面积检查：100 不在 [200,1000] → NG
    blob.setParam(QStringLiteral("blobNumLimitEnable"), false);
    blob.setParam(QStringLiteral("blobAreaLimitEnable"), true);
    blob.setParam(QStringLiteral("blobAreaLimitLow"), 200.0);
    blob.setParam(QStringLiteral("blobAreaLimitHigh"), 1000.0);
    blob.run(false);
    QVERIFY(!blob.getParam(QStringLiteral("moduleStatus")).toBool());
    QVERIFY(blob.getParam(QStringLiteral("judgeReason")).toString().contains(QStringLiteral("面积")));

    // ⑥ 总面积检查：1400 在 [1000,2000] OK；上限 1000 NG
    blob.setParam(QStringLiteral("blobAreaLimitEnable"), false);
    blob.setParam(QStringLiteral("blobTotalAreaLimitEnable"), true);
    blob.setParam(QStringLiteral("blobTotalAreaLimitLow"), 1000.0);
    blob.setParam(QStringLiteral("blobTotalAreaLimitHigh"), 2000.0);
    blob.run(false);
    QVERIFY(blob.getParam(QStringLiteral("moduleStatus")).toBool());

    blob.setParam(QStringLiteral("blobTotalAreaLimitHigh"), 1000.0);
    blob.run(false);
    QVERIFY(!blob.getParam(QStringLiteral("moduleStatus")).toBool());
    QVERIFY(blob.getParam(QStringLiteral("judgeReason")).toString().contains(QStringLiteral("总面积")));

    // ⑦ 无目标：默认 NG；OKWhenNumIsZero=true → OK
    blob.setParam(QStringLiteral("blobTotalAreaLimitEnable"), false);
    cv::Mat empty(60, 60, CV_8U, cv::Scalar(0));
    blob.setInputImage(OpencvUtil::matToHimage(empty));
    blob.run(false);
    QVERIFY(!blob.getParam(QStringLiteral("moduleStatus")).toBool());
    blob.setParam(QStringLiteral("okWhenNumIsZero"), true);
    blob.run(false);
    QVERIFY(blob.getParam(QStringLiteral("moduleStatus")).toBool());
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

void NodeCoreTest::testImageReadAddSingleLocalFile()
{
    // 回归（2026-11-04 现场闪退）：addLocalImageFiles 在"加入后恰好 1 张"分支里
    // 先 setParam(mode=SingleImage) 再取 m_imageFiles.first()；前者会触发 updateImageFiles()
    // 把列表清空 ⇒ 对空表 first()，release 下越界读直接崩溃。
    ImageReadNode node;
    node.init();

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("one.png"));
    QImage img(16, 12, QImage::Format_Grayscale8);
    img.fill(128);
    QVERIFY2(img.save(path), "测试图应能写入临时目录");

    QStringList files;
    files << path;
    QVERIFY2(node.addLocalImageFiles(files), "单张本地图应添加成功");
    QCOMPARE(node.localImageFiles().size(), 1);
    QCOMPARE(node.getParam(QStringLiteral("filePath")).toString(), path);
    QCOMPARE(node.getParam(QStringLiteral("mode")).toInt(),
             static_cast<int>(ImageReadNode::Mode::SingleImage));
}

// VM Blob 形状度量：数值腿。守三条曾出错的契约：
//   ① 度量与 Blob 在图中的位置无关（轮廓坐标必须与质心同为全图坐标）；
//   ② 矩形度 = 连通域面积 / 最小外接矩形面积，矩形为 1；
//   ③ 主轴角度是二阶中心矩，与 box 角度是两项独立特征。
void NodeCoreTest::testBlobShapeMetricsVm()
{
    auto metricsFor = [](const cv::Mat &img) -> BlobShapeMetrics {
        cv::Mat labels, stats, centroids;
        cv::connectedComponentsWithStats(img, labels, stats, centroids, 8);
        if (stats.rows < 2)
            return BlobShapeMetrics();
        return OpencvBlobNode::measureShape(labels, 1, stats.ptr<int>(1),
                                           centroids.at<double>(1, 0),
                                           centroids.at<double>(1, 1));
    };
    auto rectAt = [](int ox, int oy) {
        cv::Mat img(400, 500, CV_8UC1, cv::Scalar(0));
        cv::rectangle(img, cv::Rect(ox, oy, 40, 20), cv::Scalar(255), cv::FILLED);
        return img;
    };

    const BlobShapeMetrics m0 = metricsFor(rectAt(10, 10));
    const BlobShapeMetrics m1 = metricsFor(rectAt(200, 150));
    qInfo("blob-metrics: (10,10) circ=%.4f rect=%.4f bias=%.2f axis=%.2f box=%.2f",
          m0.circularity, m0.rectangularity, m0.centerBias, m0.axisAngle, m0.boxAngle);
    qInfo("blob-metrics: (200,150) circ=%.4f rect=%.4f bias=%.2f axis=%.2f box=%.2f",
          m1.circularity, m1.rectangularity, m1.centerBias, m1.axisAngle, m1.boxAngle);

    // ① 平移不变。修复前 findContours 无 offset：box.center 与轮廓点是子图坐标，
    //    而 centroids 是全图坐标 → 圆形度/质心偏移会随 Blob 位置变化。
    QVERIFY2(qAbs(m0.circularity - m1.circularity) < 1e-6,
             qPrintable(QStringLiteral("圆形度随位置变化：%1 vs %2")
                            .arg(m0.circularity).arg(m1.circularity)));
    QVERIFY2(qAbs(m0.rectangularity - m1.rectangularity) < 1e-6,
             "矩形度随位置变化");
    QVERIFY2(qAbs(m0.centerBias - m1.centerBias) < 1e-6,
             "质心偏移随位置变化");
    QVERIFY2(m1.centerBias < 1.5,
             qPrintable(QStringLiteral("矩形质心偏移应≈0，实际 %1（旧代码会是 hypot(200,150)≈250）")
                            .arg(m1.centerBias)));

    // ② 官方公式：矩形度 = 1；圆形度 0..1；角度落在 (-90, 90]
    QVERIFY2(qAbs(m0.rectangularity - 1.0) < 0.02,
             qPrintable(QStringLiteral("矩形块矩形度应≈1，实际 %1（+1 修正前小方块 >1 被 clamp）")
                            .arg(m0.rectangularity)));
    QVERIFY(m0.circularity > 0.0 && m0.circularity <= 1.0);
    QVERIFY(m0.axisAngle > -90.0 && m0.axisAngle <= 90.0);

    // ③ 倾斜长条：二阶矩主轴角度必须明显偏离 0
    cv::Mat bar(200, 200, CV_8UC1, cv::Scalar(0));
    cv::rectangle(bar, cv::Rect(50, 92, 100, 16), cv::Scalar(255), cv::FILLED);
    cv::Mat rotated;
    cv::warpAffine(bar, rotated,
                   cv::getRotationMatrix2D(cv::Point2f(100, 100), -30.0, 1.0), bar.size());
    const BlobShapeMetrics mr = metricsFor(rotated);
    qInfo("blob-metrics: rotated axis=%.2f box=%.2f", mr.axisAngle, mr.boxAngle);
    // warpAffine(-30°) 在图像里是视觉顺时针 30°；VM 口径「顺时针为正」，
    // 所以这里必须是 **+30** 而不是 -30，把符号约定一并锁死。
    QVERIFY2(mr.axisAngle > 20.0 && mr.axisAngle < 40.0,
             qPrintable(QStringLiteral("顺时针 30° 的二阶矩主轴角度应为 +30 左右，实际 %1")
                            .arg(mr.axisAngle)));

    // ④ 不对称形状：二阶矩主轴 ≠ box 角度（旧代码 c.axisAngle = sm.boxAngle 恒等）
    cv::Mat lshape(200, 200, CV_8UC1, cv::Scalar(0));
    cv::rectangle(lshape, cv::Rect(40, 40, 100, 20), cv::Scalar(255), cv::FILLED);
    cv::rectangle(lshape, cv::Rect(40, 40, 20, 90), cv::Scalar(255), cv::FILLED);
    const BlobShapeMetrics ml = metricsFor(lshape);
    qInfo("blob-metrics: L-shape axis=%.2f box=%.2f", ml.axisAngle, ml.boxAngle);
    QVERIFY2(qAbs(ml.axisAngle - ml.boxAngle) > 1.0,
             qPrintable(QStringLiteral("二阶中心矩主轴角度与 box 角度不应恒等：%1 vs %2")
                            .arg(ml.axisAngle).arg(ml.boxAngle)));
}

// VM 回归护栏：软阈值（相对）不整图前景、判红原因进结果表、位深不一致不破坏 ROI。
void NodeCoreTest::testVmRegressionGuards()
{
    // ① 软阈值（相对）：背景 100、目标 200，两侧尾部比例都设 0。
    //    旧代码左侧 cum>=0 立刻取 a=0、右侧 keep=total 把 b 压到最低灰度 →
    //    a==b 退化成硬阈值，整图变前景。修复后应只保留目标块。
    {
        OpencvBlobNode blob;
        blob.init();
        cv::Mat img(60, 60, CV_8UC1, cv::Scalar(100));
        cv::rectangle(img, cv::Rect(10, 10, 10, 10), cv::Scalar(200), cv::FILLED);
        blob.setParam(QStringLiteral("thresholdType"), 5);
        blob.setParam(QStringLiteral("softLeftRatio"), 0);
        blob.setParam(QStringLiteral("softRightRatio"), 0);
        blob.setParam(QStringLiteral("softLowRatio"), 40);
        blob.setParam(QStringLiteral("softHighRatio"), 60);
        blob.setParam(QStringLiteral("polarity"), 0);
        blob.setInputImage(OpencvUtil::matToHimage(img));
        blob.run(false);
        const int cnt = blob.getParam(QStringLiteral("blobCount")).toInt();
        const double area = blob.getParam(QStringLiteral("maxBlobArea")).toDouble();
        qInfo("soft-relative: count=%d maxArea=%.0f", cnt, area);
        QVERIFY2(area < 3600.0,
                 qPrintable(QStringLiteral("背景不应被整片判成前景，最大 Blob 面积 %1").arg(area)));
    }

    // ② 判红原因必须同时写进 lastError（结果表/失败轮只认该键或 *Note 结尾键）
    {
        OpencvBlobNode blob;
        blob.init();
        blob.setParam(QStringLiteral("thresholdType"), 1);
        blob.setParam(QStringLiteral("lowThreshold"), 150);
        blob.setParam(QStringLiteral("polarity"), 0);
        blob.setParam(QStringLiteral("blobNumLimitEnable"), true);
        blob.setParam(QStringLiteral("blobNumLimitLow"), 2);
        blob.setParam(QStringLiteral("blobNumLimitHigh"), 3);
        cv::Mat img(60, 60, CV_8UC1, cv::Scalar(0));
        cv::rectangle(img, cv::Rect(10, 10, 20, 20), cv::Scalar(200), cv::FILLED);
        blob.setInputImage(OpencvUtil::matToHimage(img));
        blob.run(false);
        const bool ok = blob.getParam(QStringLiteral("moduleStatus")).toBool();
        const QString reason = blob.getParam(QStringLiteral("lastError")).toString();
        qInfo("judge-reason: ok=%d reason=%s", int(ok), qPrintable(reason));
        QVERIFY2(!ok, "只有 1 个 Blob、数量下限 2，应判红");
        QVERIFY2(reason.contains(QStringLiteral("Blob 数量")),
                 qPrintable(QStringLiteral("lastError 未带判红原因：%1").arg(reason)));
    }

    // ③ keepOriginalOutside：16 位原图 + 8 位处理图，ROI 内已处理数据不能被冲掉
    {
        cv::Mat src16(10, 10, CV_16UC1, cv::Scalar(4096));
        cv::Mat proc = cv::Mat::zeros(10, 10, CV_8UC1);
        proc(cv::Rect(2, 2, 5, 5)).setTo(200);
        cv::Mat allowed = cv::Mat::zeros(10, 10, CV_8UC1);
        allowed(cv::Rect(2, 2, 5, 5)).setTo(255);
        OpencvUtil::keepOriginalOutside(proc, src16, allowed);
        QCOMPARE(proc.type(), CV_8UC1);
        QCOMPARE(int(proc.at<uchar>(4, 4)), 200);
        QCOMPARE(int(proc.at<uchar>(0, 0)), 16);   // 4096 / 256
    }
}

// VM Blob 剩余四项：像素超界 / 最大内接矩形输出数量 / 轮廓输出使能 / Blob 图像输出。
void NodeCoreTest::testBlobVmOutputAndBoundary()
{
    auto makeImg = []() {
        cv::Mat img(60, 80, CV_8UC1, cv::Scalar(0));
        cv::rectangle(img, cv::Rect(0, 20, 12, 12), cv::Scalar(255), cv::FILLED);   // 贴左边缘
        cv::rectangle(img, cv::Rect(40, 20, 20, 20), cv::Scalar(255), cv::FILLED);  // 居中
        return img;
    };
    auto baseSetup = [&makeImg](OpencvBlobNode &b) {
        b.init();
        b.setParam(QStringLiteral("thresholdType"), 1);
        b.setParam(QStringLiteral("lowThreshold"), 150);
        b.setParam(QStringLiteral("polarity"), 0);
        b.setParam(QStringLiteral("selectByArea"), false);
        b.setParam(QStringLiteral("okWhenNumIsZero"), true);
        b.setInputImage(OpencvUtil::matToHimage(makeImg()));
    };
    auto outGray = [](OpencvBlobNode &b) -> cv::Mat {
        auto obj = b.getOutputData(1);
        if (!obj || !obj->getHImage().IsInitialized())
            return cv::Mat();
        cv::Mat m = OpencvUtil::himageToMat(HImage(obj->getHImage()));
        if (m.channels() == 3)
            cv::cvtColor(m, m, cv::COLOR_BGR2GRAY);
        return m;
    };

    // ① 像素超界：关闭时两个都在；开启且阈值 0 时贴左边缘的 Blob 被删除
    {
        OpencvBlobNode b;
        baseSetup(b);
        b.run(false);
        QCOMPARE(b.getParam(QStringLiteral("blobCount")).toInt(), 2);
    }
    {
        OpencvBlobNode b;
        baseSetup(b);
        b.setParam(QStringLiteral("boundaryPixelEnable"), true);
        b.setParam(QStringLiteral("maxBoundaryPixels"), 0);
        b.run(false);
        const int cnt = b.getParam(QStringLiteral("blobCount")).toInt();
        const double cx = b.getParam(QStringLiteral("maxBlobCentroidCol")).toDouble();
        qInfo("boundary-pixel: count=%d maxBlobCx=%.1f", cnt, cx);
        QCOMPARE(cnt, 1);
        QVERIFY2(cx > 30.0,
                 qPrintable(QStringLiteral("剩下的应是居中的 Blob，实际质心 x=%1").arg(cx)));
    }

    // ② 最大内接矩形输出数量：20×20 实心块的最大内接矩形就是 20×20
    {
        OpencvBlobNode b;
        baseSetup(b);
        b.setParam(QStringLiteral("maxInscribedRectCount"), 1);
        b.run(false);
        const QString s = b.getParam(QStringLiteral("inscribedRects")).toString();
        qInfo("inscribed-rects: %s", qPrintable(s));
        QVERIFY2(s.contains(QStringLiteral(",20,20,0")),
                 qPrintable(QStringLiteral("内接矩形应含 20×20，实际 %1").arg(s)));
    }

    // ③ 轮廓输出使能：开启后标记图上出现轮廓白线（255），关闭时只有填色（<=40）
    {
        OpencvBlobNode off;
        baseSetup(off);
        off.run(false);
        cv::Mat mOff = outGray(off);
        QVERIFY(!mOff.empty());
        double mxOff = 0.0;
        cv::minMaxLoc(mOff, nullptr, &mxOff);

        OpencvBlobNode on;
        baseSetup(on);
        on.setParam(QStringLiteral("contourOutputEnable"), true);
        on.run(false);
        cv::Mat mOn = outGray(on);
        QVERIFY(!mOn.empty());
        double mxOn = 0.0;
        cv::minMaxLoc(mOn, nullptr, &mxOn);
        qInfo("contour-output: maxOff=%.0f maxOn=%.0f", mxOff, mxOn);
        QVERIFY2(mxOff < 255.0, "轮廓关闭时标记图不应有 255 轮廓线");
        QVERIFY2(mxOn > 250.0, "轮廓开启后标记图应有 255 轮廓线");
    }

    // ④ Blob 图像输出：关闭后端口 1 不再带图
    {
        OpencvBlobNode b;
        baseSetup(b);
        b.setParam(QStringLiteral("blobImageOutput"), false);
        b.run(false);
        cv::Mat m = outGray(b);
        QVERIFY2(m.empty(), "Blob 图像输出关闭后不应再产出标记图");
    }
}

// VM Blob「模块结果」展示口径：官方字段顺序、配置参数不混入、逐斑点数组与分组一一对应。
void NodeCoreTest::testBlobResultDisplaySpec()
{
    OpencvBlobNode blob;
    blob.init();

    // ① 规范：模块级 4 项 + 18 项逐斑点（斑点%1）+ 5 项检测区域，且不含任何配置键
    const QVector<NodeResultField> spec = blob.resultDisplayFields();
    QCOMPARE(int(spec.size()), 27);
    QCOMPARE(spec.at(0).key, QStringLiteral("elapsedMs"));
    QCOMPARE(spec.at(0).label, QStringLiteral("耗时"));
    QCOMPARE(spec.at(0).unit, QStringLiteral("ms"));
    QCOMPARE(spec.at(1).key, QStringLiteral("moduleStatus"));
    QCOMPARE(spec.at(1).label, QStringLiteral("模块状态"));
    QCOMPARE(spec.at(2).key, QStringLiteral("blobCount"));
    QCOMPARE(spec.at(2).label, QStringLiteral("Blob个数"));
    QCOMPARE(spec.at(3).key, QStringLiteral("totalBlobArea"));
    QCOMPARE(spec.at(3).label, QStringLiteral("总面积"));
    QCOMPARE(spec.at(4).key, QStringLiteral("blobStatuses"));
    for (int i = 4; i < 22; ++i)   // 4 个模块级 + 18 个逐斑点，之后是 5 个检测区域字段
        QCOMPARE(spec.at(i).group, QStringLiteral("斑点%1"));
    for (const NodeResultField &f : spec) {
        QVERIFY2(f.key != QStringLiteral("thresholdType")
                     && f.key != QStringLiteral("lowThreshold")
                     && f.key != QStringLiteral("roiRectRow")
                     && f.key != QStringLiteral("showBlobBox"),
                 qPrintable(QStringLiteral("配置参数混进了模块结果规范：%1").arg(f.key)));
    }

    // ② 逐斑点数组：两张横放矩形（20×10 与 10×10），不排序 ⇒ 数组顺序 = 连通域标签顺序
    cv::Mat img(60, 60, CV_8UC1, cv::Scalar(0));
    cv::rectangle(img, cv::Rect(10, 10, 20, 10), cv::Scalar(255), cv::FILLED);
    cv::rectangle(img, cv::Rect(40, 20, 10, 10), cv::Scalar(255), cv::FILLED);
    blob.setParam(QStringLiteral("thresholdType"), 1);
    blob.setParam(QStringLiteral("lowThreshold"), 150);
    blob.setParam(QStringLiteral("polarity"), 0);
    blob.setParam(QStringLiteral("sortMode"), 2);   // 不排序
    blob.setInputImage(OpencvUtil::matToHimage(img));
    blob.run(false);
    QVERIFY2(blob.getParam(QStringLiteral("moduleStatus")).toBool(), "Blob 用例执行失败");
    QCOMPARE(blob.getParam(QStringLiteral("blobCount")).toInt(), 2);
    QCOMPARE(blob.getParam(QStringLiteral("totalBlobArea")).toDouble(), 300.0);

    auto text = [&blob](const char *key) {
        return blob.getParam(QString::fromLatin1(key)).toString();
    };
    QCOMPARE(text("blobStatuses"), QStringLiteral("1,1"));
    QCOMPARE(text("blobAreas"), QStringLiteral("200,100"));
    QCOMPARE(text("blobCentroids"), QStringLiteral("19.50,14.50;44.50,24.50"));
    QCOMPARE(text("blobCentroidXs"), QStringLiteral("19.50,44.50"));
    QCOMPARE(text("blobCentroidYs"), QStringLiteral("14.50,24.50"));
    QCOMPARE(text("blobBboxCenterXs"), QStringLiteral("19.50,44.50"));
    QCOMPARE(text("blobBboxCenterYs"), QStringLiteral("14.50,24.50"));
    QCOMPARE(text("blobBboxWidths"), QStringLiteral("20,10"));
    QCOMPARE(text("blobBboxHeights"), QStringLiteral("10,10"));
    QCOMPARE(text("blobRectangularities"), QStringLiteral("1.0000,1.0000"));

    auto numbers = [&text](const char *key) {
        QVector<double> out;
        const QStringList parts =
            text(key).split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &p : parts)
            out.push_back(p.toDouble());
        return out;
    };
    const QVector<double> perimeters = numbers("blobPerimeters");
    const QVector<double> longAxes = numbers("blobLongAxes");
    const QVector<double> shortAxes = numbers("blobShortAxes");
    const QVector<double> angles = numbers("blobBoxAngles");
    QCOMPARE(int(perimeters.size()), 2);
    QCOMPARE(int(longAxes.size()), 2);
    QCOMPARE(int(shortAxes.size()), 2);
    QCOMPARE(int(angles.size()), 2);
    QVERIFY2(perimeters.at(0) > 50.0 && perimeters.at(0) < 65.0,
             qPrintable(QStringLiteral("20×10 矩形周长异常：%1").arg(perimeters.at(0))));
    QVERIFY2(longAxes.at(0) > 18.0 && longAxes.at(0) < 21.0
                 && shortAxes.at(0) > 8.0 && shortAxes.at(0) < 11.0,
             qPrintable(QStringLiteral("20×10 长短轴异常：%1/%2")
                            .arg(longAxes.at(0)).arg(shortAxes.at(0))));
    for (double a : angles)
        QVERIFY2(a >= -90.0 && a <= 90.0,
                 qPrintable(QStringLiteral("角度越界（应 (-90,90]）：%1").arg(a)));
    QVERIFY2(qAbs(longAxes.at(1) - 9.0) < 1.5 && qAbs(shortAxes.at(1) - 9.0) < 1.5,
             qPrintable(QStringLiteral("10×10 长短轴异常：%1/%2")
                            .arg(longAxes.at(1)).arg(shortAxes.at(1))));
}

// 其余算子的「模块结果」最低口径：HalconNode 默认 耗时/模块状态(+矩形 ROI 检测区域)，
// 测量/识别类算子在默认口径上追加自己的结果字段；配置参数一律不出现。
void NodeCoreTest::testRemainingResultDisplaySpecs()
{
    auto containsKey = [](const QVector<NodeResultField> &fields, const QString &key) {
        for (const NodeResultField &f : fields) {
            if (f.key == key)
                return true;
        }
        return false;
    };

    // ① 默认口径：矩形 ROI 图像算子 = 7 项；无 ROI = 2 项
    BlurNode blur;
    blur.init();
    const QVector<NodeResultField> blurSpec = blur.resultDisplayFields();
    QCOMPARE(int(blurSpec.size()), 7);
    QCOMPARE(blurSpec.at(0).key, QStringLiteral("elapsedMs"));
    QCOMPARE(blurSpec.at(1).key, QStringLiteral("moduleStatus"));
    QCOMPARE(blurSpec.at(2).key, QStringLiteral("roiCenterX"));
    QCOMPARE(blurSpec.at(6).key, QStringLiteral("roiAngle"));
    QVERIFY2(!containsKey(blurSpec, QStringLiteral("gaussKernelSize")),
             "Blur 配置参数混进了结果规范");

    MirrorNode mirror;
    mirror.init();
    QCOMPARE(int(mirror.resultDisplayFields().size()), 2);

    // ② 灰度统计：结果字段 + 数值腿
    OpencvPixelStatsNode stats;
    stats.init();
    const QVector<NodeResultField> statsSpec = stats.resultDisplayFields();
    QCOMPARE(int(statsSpec.size()), 5);
    QCOMPARE(statsSpec.at(2).key, QStringLiteral("minGray"));
    QCOMPARE(statsSpec.at(4).key, QStringLiteral("meanGray"));
    QVERIFY2(containsKey(statsSpec, QStringLiteral("minGray")), "灰度统计规范缺少 minGray");
    {
        cv::Mat gray(10, 10, CV_8UC1, cv::Scalar(0));
        gray(cv::Rect(5, 0, 5, 10)).setTo(cv::Scalar(255));
        stats.setInputImage(OpencvUtil::matToHimage(gray));
        stats.run(false);
        QVERIFY2(stats.getParam(QStringLiteral("moduleStatus")).toBool(), "灰度统计执行失败");
        QCOMPARE(stats.getParam(QStringLiteral("minGray")).toDouble(), 0.0);
        QCOMPARE(stats.getParam(QStringLiteral("maxGray")).toDouble(), 255.0);
        QCOMPARE(stats.getParam(QStringLiteral("meanGray")).toDouble(), 127.5);
    }

    // ③ 角度测量：默认两条线（竖直 + 45° 斜线）→ 夹角 45°
    OpencvAngleNode angle;
    angle.init();
    const QVector<NodeResultField> angleSpec = angle.resultDisplayFields();
    QCOMPARE(int(angleSpec.size()), 12);
    QVERIFY2(containsKey(angleSpec, QStringLiteral("angle")), "角度规范缺少夹角");
    QVERIFY2(containsKey(angleSpec, QStringLiteral("parallel")), "角度规范缺少平行标记");
    angle.run(false);
    QVERIFY2(angle.getParam(QStringLiteral("moduleStatus")).toBool(), "角度测量执行失败");
    QVERIFY2(qAbs(angle.getParam(QStringLiteral("angle")).toDouble() - 45.0) < 1e-6,
             qPrintable(QStringLiteral("默认夹角应为 45°，实际 %1")
                            .arg(angle.getParam(QStringLiteral("angle")).toDouble())));

    // ④ 边缘检测 / 拟合 / 识别类：字段数量与关键键
    OpencvEdgeNode edge;
    edge.init();
    const QVector<NodeResultField> edgeSpec = edge.resultDisplayFields();
    QCOMPARE(int(edgeSpec.size()), 3);
    QCOMPARE(edgeSpec.at(2).key, QStringLiteral("edgePixels"));

    OpencvFitLineNode fitLine;
    fitLine.init();
    const QVector<NodeResultField> fitLineSpec = fitLine.resultDisplayFields();
    QCOMPARE(int(fitLineSpec.size()), 11);
    QVERIFY2(containsKey(fitLineSpec, QStringLiteral("fitAngle")), "直线拟合规范缺少角度");
    QVERIFY2(containsKey(fitLineSpec, QStringLiteral("fitPointCount")), "直线拟合规范缺少点数");

    OpencvFitCircleNode fitCircle;
    fitCircle.init();
    const QVector<NodeResultField> fitCircleSpec = fitCircle.resultDisplayFields();
    QCOMPARE(int(fitCircleSpec.size()), 11);
    QVERIFY2(containsKey(fitCircleSpec, QStringLiteral("fitRadius")), "圆拟合规范缺少半径");

    OpencvQrNode qr;
    qr.init();
    const QVector<NodeResultField> qrSpec = qr.resultDisplayFields();
    QCOMPARE(int(qrSpec.size()), 4);
    QCOMPARE(qrSpec.at(2).key, QStringLiteral("qrText"));
    QCOMPARE(qrSpec.at(3).key, QStringLiteral("decodeCount"));

    OpencvClassifyNode classify;
    classify.init();
    const QVector<NodeResultField> classifySpec = classify.resultDisplayFields();
    QCOMPARE(int(classifySpec.size()), 5);
    QCOMPARE(classifySpec.at(2).key, QStringLiteral("classId"));
    QCOMPARE(classifySpec.at(4).key, QStringLiteral("confidence"));

    OpencvDefectNode defect;
    defect.init();
    const QVector<NodeResultField> defectSpec = defect.resultDisplayFields();
    QCOMPARE(int(defectSpec.size()), 9);
    QCOMPARE(defectSpec.at(7).key, QStringLiteral("defectCount"));
    QCOMPARE(defectSpec.at(8).key, QStringLiteral("defectArea"));

    OpencvFeatureMatchNode feature;
    feature.init();
    const QVector<NodeResultField> featureSpec = feature.resultDisplayFields();
    QCOMPARE(int(featureSpec.size()), 11);
    QVERIFY2(containsKey(featureSpec, QStringLiteral("matchScore")), "特征匹配规范缺少分数");
    QVERIFY2(containsKey(featureSpec, QStringLiteral("inlierCount")), "特征匹配规范缺少内点数");

    // ⑤ 识别 / 深度学习 / 标定 / 跟踪类：字段数量与关键键
    ZxingBarcodeNode zxing;
    zxing.init();
    const QVector<NodeResultField> zxingSpec = zxing.resultDisplayFields();
    QCOMPARE(int(zxingSpec.size()), 4);
    QCOMPARE(zxingSpec.at(2).key, QStringLiteral("barcodeText"));
    QCOMPARE(zxingSpec.at(3).key, QStringLiteral("barcodeFormat"));

    TesseractOcrNode tess;
    tess.init();
    const QVector<NodeResultField> tessSpec = tess.resultDisplayFields();
    QCOMPARE(int(tessSpec.size()), 7);
    QCOMPARE(tessSpec.at(2).key, QStringLiteral("ocrText"));
    QCOMPARE(tessSpec.at(6).key, QStringLiteral("ocrWordCount"));

    DeepOcrNode deepOcr;
    deepOcr.init();
    const QVector<NodeResultField> deepOcrSpec = deepOcr.resultDisplayFields();
    QCOMPARE(int(deepOcrSpec.size()), 7);
    QVERIFY2(containsKey(deepOcrSpec, QStringLiteral("ocrConfidence")),
             "DeepOCR 规范缺少置信度");

    DnnInferNode dnnInfer;
    dnnInfer.init();
    const QVector<NodeResultField> dnnInferSpec = dnnInfer.resultDisplayFields();
    QCOMPARE(int(dnnInferSpec.size()), 6);
    QCOMPARE(dnnInferSpec.at(2).key, QStringLiteral("topClass"));
    QCOMPARE(dnnInferSpec.at(5).key, QStringLiteral("topKClasses"));

    DnnDetectNode dnnDetect;
    dnnDetect.init();
    const QVector<NodeResultField> dnnDetectSpec = dnnDetect.resultDisplayFields();
    QCOMPARE(int(dnnDetectSpec.size()), 3);
    QCOMPARE(dnnDetectSpec.at(2).key, QStringLiteral("detCount"));

    DnnSegmentNode dnnSegment;
    dnnSegment.init();
    const QVector<NodeResultField> dnnSegmentSpec = dnnSegment.resultDisplayFields();
    QCOMPARE(int(dnnSegmentSpec.size()), 5);
    QCOMPARE(dnnSegmentSpec.at(2).key, QStringLiteral("segmentCount"));
    QCOMPARE(dnnSegmentSpec.at(4).key, QStringLiteral("sigmoidApplied"));

    AnomalyDetectNode anomaly;
    anomaly.init();
    const QVector<NodeResultField> anomalySpec = anomaly.resultDisplayFields();
    QCOMPARE(int(anomalySpec.size()), 12);   // 7 公共（含矩形 ROI）+ 5 异常统计
    QCOMPARE(anomalySpec.at(7).key, QStringLiteral("anomalyCount"));
    QCOMPARE(anomalySpec.at(11).key, QStringLiteral("sampleCount"));

    ObjectCountingNode counting;
    counting.init();
    const QVector<NodeResultField> countingSpec = counting.resultDisplayFields();
    QCOMPARE(int(countingSpec.size()), 5);
    QCOMPARE(countingSpec.at(2).key, QStringLiteral("totalCount"));
    QCOMPARE(countingSpec.at(4).key, QStringLiteral("activeTracks"));

    OpencvTrainClassifierNode train;
    train.init();
    const QVector<NodeResultField> trainSpec = train.resultDisplayFields();
    QCOMPARE(int(trainSpec.size()), 6);
    QCOMPARE(trainSpec.at(2).key, QStringLiteral("trainStatus"));
    QCOMPARE(trainSpec.at(5).key, QStringLiteral("featDim"));

    OpencvCalibNode calib;
    calib.init();
    const QVector<NodeResultField> calibSpec = calib.resultDisplayFields();
    QCOMPARE(int(calibSpec.size()), 14);
    QCOMPARE(calibSpec.at(2).key, QStringLiteral("calibrated"));
    QCOMPARE(calibSpec.at(13).key, QStringLiteral("calibP2"));

    OpencvTrackNode track;
    track.init();
    const QVector<NodeResultField> trackSpec = track.resultDisplayFields();
    QCOMPARE(int(trackSpec.size()), 6);
    QCOMPARE(trackSpec.at(2).key, QStringLiteral("trackCol"));
    QCOMPARE(trackSpec.at(5).key, QStringLiteral("trackLost"));

    FeatureSearchNode featureSearch;
    featureSearch.init();
    const QVector<NodeResultField> searchSpec = featureSearch.resultDisplayFields();
    QCOMPARE(int(searchSpec.size()), 7);
    QCOMPARE(searchSpec.at(2).key, QStringLiteral("hitCount"));
    QCOMPARE(searchSpec.at(6).key, QStringLiteral("topInliers"));
}


// VM 卡尺工具（IMVSCaliperModu）对齐：滤波尺寸 / 边缘1极性 / 排序方式 / 越界填充。
// 官方工作流：投影 → 一维差分滤波（滤波核可调）→ 局部极值 → 得分 → 亚像素边缘点。
void NodeCoreTest::testCaliperVmParams()
{
    auto baseImg = []() {
        cv::Mat img(200, 200, CV_8UC1, cv::Scalar(50));
        cv::rectangle(img, cv::Rect(90, 0, 20, 200), cv::Scalar(200), cv::FILLED);
        return img;
    };
    // 卡尺线竖直 (col=100, row 50..150)，法线水平 → 剖面依次跨过 x=90 / x=110 两条边缘
    auto setup = [](OpencvCaliperNode &c, const cv::Mat &img, double col) {
        c.init();
        c.setParam(QStringLiteral("row1"), 50.0);
        c.setParam(QStringLiteral("col1"), col);
        c.setParam(QStringLiteral("row2"), 150.0);
        c.setParam(QStringLiteral("col2"), col);
        c.setParam(QStringLiteral("halfWidth"), 30);
        c.setParam(QStringLiteral("step"), 5.0);
        // 阈值 60：真实阶跃 150 一定能过；随机噪声在 k=1 下相邻差常过 60，
        // 大核把噪声平均后梯度幅值降下来就过不了 —— 正是 VM「滤波尺寸 + 边缘阈值」的配合。
        c.setParam(QStringLiteral("threshold"), 60.0);
        // VM 默认 EdgeMode=1（边缘对）、最大结果数=1；本用例主体按单边缘 + 全量输出
        c.setParam(QStringLiteral("edgeMode"), 0);
        c.setParam(QStringLiteral("maxCount"), 1000);
        c.setInputImage(OpencvUtil::matToHimage(img));
    };
    auto firstPoint = [](OpencvCaliperNode &c) -> QPointF {
        auto obj = c.getOutputData(1);
        if (!obj)
            return QPointF();
        const MeasureResult r = obj->getMeasureResult();
        if (r.extraValues.size() < 2)
            return QPointF();
        return QPointF(r.extraValues[0], r.extraValues[1]);
    };

    // ① 滤波尺寸：加固定椒盐噪声后，大核应比一阶差分更少报伪边缘
    {
        cv::Mat noisy = baseImg();
        cv::RNG rng(12345);
        for (int i = 0; i < 400; ++i) {
            const int y = rng.uniform(0, noisy.rows);
            const int x = rng.uniform(60, 140);
            noisy.at<uchar>(y, x) = static_cast<uchar>(rng.uniform(0, 256));
        }
        OpencvCaliperNode c1;
        setup(c1, noisy, 100.0);
        c1.setParam(QStringLiteral("filterSize"), 1);
        c1.run(false);
        const int n1 = c1.getParam(QStringLiteral("edgeCount")).toInt();

        OpencvCaliperNode c5;
        setup(c5, noisy, 100.0);
        c5.setParam(QStringLiteral("filterSize"), 5);
        c5.run(false);
        const int n5 = c5.getParam(QStringLiteral("edgeCount")).toInt();
        qInfo("caliper-filter: size1=%d size5=%d", n1, n5);
        QVERIFY2(n1 > 0, "噪声图上一阶差分应能报出边缘");
        QVERIFY2(n5 < n1,
                 qPrintable(QStringLiteral("滤波核 5 应抑制噪声伪边缘：%1 vs %2").arg(n5).arg(n1)));
        // 21 个剖面 × 亮带 2 条边 = 42 条真实边缘；大核不能把它们也滤掉
        QVERIFY2(n5 >= 42,
                 qPrintable(QStringLiteral("滤波核 5 不应丢掉真实边缘，实际只剩 %1 条").arg(n5)));
    }

    // ② 边缘1极性（边对模式）：约束第二条边后，边对数应明显变化
    //    VM 枚举索引与官方 UI 顺序一致：0=从黑到白、1=从白到黑、2=任意极性
    {
        OpencvCaliperNode any;
        setup(any, baseImg(), 100.0);
        any.setParam(QStringLiteral("edgeMode"), 1);
        any.setParam(QStringLiteral("edge1Polarity"), 2);
        any.run(false);
        const int nAny = any.getParam(QStringLiteral("pairCount")).toInt();

        OpencvCaliperNode restricted;
        setup(restricted, baseImg(), 100.0);
        restricted.setParam(QStringLiteral("edgeMode"), 1);
        restricted.setParam(QStringLiteral("edge1Polarity"), 0);
        restricted.run(false);
        const int nP1 = restricted.getParam(QStringLiteral("pairCount")).toInt();

        OpencvCaliperNode restricted2;
        setup(restricted2, baseImg(), 100.0);
        restricted2.setParam(QStringLiteral("edgeMode"), 1);
        restricted2.setParam(QStringLiteral("edge1Polarity"), 1);
        restricted2.run(false);
        const int nP2 = restricted2.getParam(QStringLiteral("pairCount")).toInt();
        qInfo("caliper-edge1-polarity: any=%d p1=%d p2=%d", nAny, nP1, nP2);
        QVERIFY2(nAny > 0, "亮带应能配出边对");
        QVERIFY2(nP1 == 0 || nP2 == 0,
                 "第二边是固定极性，两个互斥的 edge1Polarity 不应同时配出边对");
        QVERIFY2(nP1 + nP2 == nAny,
                 qPrintable(QStringLiteral("两种极性应互补覆盖全部边对：%1 + %2 vs %3")
                                .arg(nP1).arg(nP2).arg(nAny)));
    }

    // ③ 排序方式：方向逆向应把输出点序反过来
    {
        OpencvCaliperNode fwd;
        setup(fwd, baseImg(), 100.0);
        fwd.setParam(QStringLiteral("sortType"), 0);
        fwd.run(false);
        const QPointF p0 = firstPoint(fwd);

        OpencvCaliperNode rev;
        setup(rev, baseImg(), 100.0);
        rev.setParam(QStringLiteral("sortType"), 3);
        rev.run(false);
        const QPointF p3 = firstPoint(rev);
        qInfo("caliper-sort: forward=(%.1f,%.1f) reverse=(%.1f,%.1f)",
              p0.x(), p0.y(), p3.x(), p3.y());
        QVERIFY2(qAbs(p0.y() - p3.y()) > 1.0 || qAbs(p0.x() - p3.x()) > 1.0,
                 "方向逆向应让首个输出点变成原来的最后一个");
    }

    // ④ 越界填充：卡尺靠图像左边缘、剖面大量越界，均匀亮背景不应产生伪边缘
    {
        cv::Mat flat(200, 200, CV_8UC1, cv::Scalar(180));
        OpencvCaliperNode c;
        setup(c, flat, 5.0);          // 剖面 x 从 -25 到 35
        c.run(false);
        const int n = c.getParam(QStringLiteral("edgeCount")).toInt();
        qInfo("caliper-border: edgeCount=%d", n);
        QVERIFY2(n == 0,
                 qPrintable(QStringLiteral("边界复制后均匀亮背景不应凭空出现边缘，实际 %1 条").arg(n)));
    }
}

// VM 卡尺步骤6「极值点评分」+「边缘对宽度」语义：
// 官方「期望输出的边缘对的像素间距尺寸……单独调节该参数无法直接筛选出期望的边缘对，
// 仅当……计分方式开启时……作为计分因子的缩放因子使用」。
void NodeCoreTest::testCaliperScoring()
{
    auto makeImg = []() {
        cv::Mat m(200, 300, CV_8UC1, cv::Scalar(50));
        cv::rectangle(m, cv::Rect(90, 0, 20, 200), cv::Scalar(200), cv::FILLED);   // 宽 20
        cv::rectangle(m, cv::Rect(200, 0, 15, 200), cv::Scalar(200), cv::FILLED);  // 宽 15
        return m;
    };
    // 卡尺竖直 col=150、法线水平，剖面 x∈[50,250] 同时跨过两条亮带
    auto setup = [&makeImg](OpencvCaliperNode &c) {
        c.init();
        c.setParam(QStringLiteral("row1"), 50.0);
        c.setParam(QStringLiteral("col1"), 150.0);
        c.setParam(QStringLiteral("row2"), 150.0);
        c.setParam(QStringLiteral("col2"), 150.0);
        c.setParam(QStringLiteral("halfWidth"), 100);
        c.setParam(QStringLiteral("step"), 20.0);
        c.setParam(QStringLiteral("threshold"), 40.0);
        c.setParam(QStringLiteral("edgeMode"), 1);
        c.setParam(QStringLiteral("maxCount"), 1000);
        // 每个子用例只启用自己验证的计分项，避免默认「间距差」干扰
        c.setParam(QStringLiteral("scoreSpacingDiff"), false);
        c.setParam(QStringLiteral("scoreContrast"), false);
        c.setParam(QStringLiteral("scorePairContrast"), false);
        c.setInputImage(OpencvUtil::matToHimage(makeImg()));
    };
    auto widths = [](OpencvCaliperNode &c) -> QVector<double> {
        QVector<double> w;
        auto obj = c.getOutputData(3);
        if (!obj)
            return w;
        const MeasureResult r = obj->getMeasureResult();
        for (int i = 4; i < r.extraValues.size(); i += 5)
            w.append(r.extraValues[i]);
        return w;
    };
    auto scores = [](OpencvCaliperNode &c) -> QVector<double> {
        QVector<double> v;
        const QStringList parts =
            c.getParam(QStringLiteral("edgeScores")).toString()
                .split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &s : parts)
            v.append(s.toDouble());
        return v;
    };
    auto hasWidthNear = [](const QVector<double> &w, double target) -> bool {
        for (double v : w)
            if (qAbs(v - target) <= 1.5)
                return true;
        return false;
    };

    // ① 默认（计分全关）：两种宽度的边对都在，分数退化为对比度归一化 → 全相等
    {
        OpencvCaliperNode c;
        setup(c);
        c.run(false);
        const QVector<double> w = widths(c);
        const QVector<double> s = scores(c);
        qInfo("caliper-score default: pairs=%d w20=%d w15=%d avgScore=%.4f",
              w.size(), int(hasWidthNear(w, 20.0)), int(hasWidthNear(w, 15.0)),
              c.getParam(QStringLiteral("avgScore")).toDouble());
        QVERIFY2(hasWidthNear(w, 20.0) && hasWidthNear(w, 15.0),
                 "默认不应按宽度筛选边对");
        QVERIFY2(s.size() == w.size(), "每个边对都应有一条分数");
        QVERIFY2(qAbs(s.first() - s.last()) < 1e-6,
                 "无计分项时分数只由对比度决定，两条亮带对比度相同 ⇒ 分数应相等");
    }

    // ② 启用间距差计分 + 边缘对宽度 20：宽度 20 的边对分数更高。
    //    间距差输入 = |(距离−宽度)/宽度|，远距离候选偏差大、得分低，不会被选中。
    {
        OpencvCaliperNode c;
        setup(c);
        c.setParam(QStringLiteral("expectedPairWidth"), 20.0);
        c.setParam(QStringLiteral("scoreSpacingDiff"), true);
        c.run(false);
        const QVector<double> w = widths(c);
        const QVector<double> s = scores(c);
        double s20 = -1.0, s15 = -1.0;
        for (int i = 0; i < w.size() && i < s.size(); ++i) {
            if (qAbs(w[i] - 20.0) <= 1.5) s20 = s[i];
            if (qAbs(w[i] - 15.0) <= 1.5) s15 = s[i];
        }
        qInfo("caliper-score spacing: w20=%.4f w15=%.4f", s20, s15);
        QVERIFY2(s20 > s15,
                 qPrintable(QStringLiteral("期望宽度 20 的边对应得分更高：%1 vs %2")
                                .arg(s20).arg(s15)));
        QVERIFY2(hasWidthNear(w, 20.0) && hasWidthNear(w, 15.0),
                 "边缘对宽度只作计分缩放因子，不应把宽度 15 的边对删掉");
    }

    // ③ 官方「最大结果数」：启用计分时按分数由高到低保留 N 个
    {
        OpencvCaliperNode c;
        setup(c);
        c.setParam(QStringLiteral("expectedPairWidth"), 20.0);
        c.setParam(QStringLiteral("scoreSpacingDiff"), true);
        c.setParam(QStringLiteral("maxCount"), 1);
        c.run(false);
        const QVector<double> w = widths(c);
        qInfo("caliper-score maxCount: kept=%d firstWidth=%.1f",
              w.size(), w.isEmpty() ? -1.0 : w.first());
        QCOMPARE(w.size(), 1);
        QVERIFY2(qAbs(w.first() - 20.0) <= 1.5,
                 "maxCount=1 应留下分数最高（宽度=期望宽度）的边对");
    }

    // ④ 间距曲线改「递减」：比值越小分越高 → maxCount=1 留下宽度 15 的边对。
    //    间距输入 = 距离/边缘对宽度（理想 1）；递减曲线 X0=0/X1=1/XC=1 时
    //    ratio=1.0 得 0 分、ratio=0.75 得 25 分。
    {
        OpencvCaliperNode c;
        setup(c);
        c.setParam(QStringLiteral("expectedPairWidth"), 20.0);
        c.setParam(QStringLiteral("scoreSpacing"), true);
        c.setParam(QStringLiteral("spacingCurve"), 1);
        c.setParam(QStringLiteral("spacingX1"), 1.0);
        c.setParam(QStringLiteral("spacingXC"), 1.0);
        c.setParam(QStringLiteral("maxCount"), 1);
        c.run(false);
        const QVector<double> w = widths(c);
        qInfo("caliper-score inverted: kept=%d firstWidth=%.1f",
              w.size(), w.isEmpty() ? -1.0 : w.first());
        QCOMPARE(w.size(), 1);
        QVERIFY2(qAbs(w.first() - 15.0) <= 1.5,
                 "曲线递增时偏差大的边对应得分更高");
    }
}

// VM 卡尺「位置评分」四类 + 官方「模块结果」逐结果字段。
// 官方：投影区域中心点 = 我们矩形 ROI（卡尺线 × ±halfWidth）的中心，即卡尺线中点；
// 候选沿法线的位移 offset 就是「相对投影区域中心的位置差」。
void NodeCoreTest::testCaliperPositionScoringAndResults()
{
    auto makeImg = []() {
        cv::Mat m(200, 300, CV_8UC1, cv::Scalar(50));
        cv::rectangle(m, cv::Rect(145, 0, 20, 200), cv::Scalar(200), cv::FILLED);  // 中心 155（近）
        cv::rectangle(m, cv::Rect(95, 0, 20, 200), cv::Scalar(200), cv::FILLED);   // 中心 105（远）
        return m;
    };
    // 竖直卡尺 col=150，法线向左 → offset = 150 − 中心col；近带 ≈ −5、远带 ≈ +45
    auto setup = [&makeImg](OpencvCaliperNode &c) {
        c.init();
        c.setParam(QStringLiteral("row1"), 50.0);
        c.setParam(QStringLiteral("col1"), 150.0);
        c.setParam(QStringLiteral("row2"), 150.0);
        c.setParam(QStringLiteral("col2"), 150.0);
        c.setParam(QStringLiteral("halfWidth"), 100);
        c.setParam(QStringLiteral("step"), 20.0);
        c.setParam(QStringLiteral("threshold"), 40.0);
        c.setParam(QStringLiteral("edgeMode"), 1);
        c.setParam(QStringLiteral("maxCount"), 1);
        // 只验证位置类计分：关掉默认「间距差」与对比度，避免评分相乘后全部归零
        c.setParam(QStringLiteral("scoreSpacingDiff"), false);
        c.setParam(QStringLiteral("scoreContrast"), false);
        c.setParam(QStringLiteral("scorePairContrast"), false);
        c.setInputImage(OpencvUtil::matToHimage(makeImg()));
    };
    auto firstPosition = [](OpencvCaliperNode &c) -> double {
        const QStringList parts = c.getParam(QStringLiteral("edgePositions")).toString()
                                      .split(QLatin1Char(','), Qt::SkipEmptyParts);
        return parts.isEmpty() ? 9999.0 : parts.first().toDouble();
    };

    // ① 位置（绝对）：靠近中心的边对得分更高
    {
        OpencvCaliperNode c;
        setup(c);
        c.setParam(QStringLiteral("scorePosition"), true);
        c.run(false);
        const double pos = firstPosition(c);
        qInfo("caliper-position absolute: first=%.2f", pos);
        QVERIFY2(qAbs(pos) < 15.0,
                 qPrintable(QStringLiteral("位置计分应留下靠近中心的边对（offset≈−5），实际 %1").arg(pos)));
    }

    // ② 归一位置：官方分母 = 边缘对宽度
    {
        OpencvCaliperNode c;
        setup(c);
        c.setParam(QStringLiteral("expectedPairWidth"), 20.0);
        c.setParam(QStringLiteral("scoreNormPosition"), true);
        c.run(false);
        const double pos = firstPosition(c);
        qInfo("caliper-position normalized: first=%.2f", pos);
        QVERIFY2(qAbs(pos) < 15.0,
                 qPrintable(QStringLiteral("归一位置计分应留下靠近中心的边对，实际 %1").arg(pos)));
    }

    // ③ 相对位置（带符号）：负侧落在曲线 x0 左侧 ⇒ 满分，正侧按偏差降分
    {
        OpencvCaliperNode c;
        setup(c);
        c.setParam(QStringLiteral("scoreRelativePosition"), true);
        c.run(false);
        const double pos = firstPosition(c);
        qInfo("caliper-position relative: first=%.2f", pos);
        QVERIFY2(pos < 0.0,
                 qPrintable(QStringLiteral("相对位置计分应优先负侧（靠近中心的边对），实际 %1").arg(pos)));
    }

    // ④ 灰度计分：官方「按边缘灰度或边缘对灰度均值计分」，能正常产出分数即可
    {
        OpencvCaliperNode c;
        setup(c);
        c.setParam(QStringLiteral("scoreGray"), true);
        c.run(false);
        const double s = c.getParam(QStringLiteral("avgScore")).toDouble();
        qInfo("caliper-gray: avgScore=%.4f", s);
        QVERIFY(s >= 0.0 && s <= 1.0);
    }

    // ⑤ 官方「模块结果」字段：耗时 / 边缘*极性 / 边缘*位置 / 分数 都齐且与结果数一致
    {
        OpencvCaliperNode c;
        setup(c);
        c.setParam(QStringLiteral("maxCount"), 1000);
        c.run(false);
        const int n = c.getParam(QStringLiteral("edgeCount")).toInt();
        const QStringList pol = c.getParam(QStringLiteral("edgePolarities")).toString()
                                    .split(QLatin1Char(','), Qt::SkipEmptyParts);
        const QStringList pos = c.getParam(QStringLiteral("edgePositions")).toString()
                                    .split(QLatin1Char(','), Qt::SkipEmptyParts);
        const QStringList gray = c.getParam(QStringLiteral("edgeGrays")).toString()
                                     .split(QLatin1Char(','), Qt::SkipEmptyParts);
        const QStringList score = c.getParam(QStringLiteral("edgeScores")).toString()
                                      .split(QLatin1Char(','), Qt::SkipEmptyParts);
        const double ms = c.getParam(QStringLiteral("elapsedMs")).toDouble();
        qInfo("caliper-results: n=%d pol=%d pos=%d gray=%d score=%d elapsed=%.2fms",
              n, pol.size(), pos.size(), gray.size(), score.size(), ms);
        QVERIFY(n > 0);
        QCOMPARE(pol.size(), n);
        QCOMPARE(pos.size(), n);
        QCOMPARE(gray.size(), n);
        QCOMPARE(score.size(), n);
        for (const QString &v : pol) {
            const int p = v.toInt();
            QVERIFY2(p == 1 || p == 2,
                     qPrintable(QStringLiteral("极性应按官方 1=黑到白 / 2=白到黑，实际 %1").arg(p)));
        }
        for (const QString &v : gray) {
            const double g = v.toDouble();
            QVERIFY(g >= 0.0 && g <= 255.0);
        }
        QVERIFY(ms >= 0.0);
    }
}

// VM「投影方向」：官方「纵向投影…投向矩形宽 / 横向投影…投向矩形高」。
// 卡尺线竖直 (col=100, row 50..150) 时：横向投影的一维信号沿法线（x），
// 纵向投影的一维信号沿卡尺线（y）。两种方向应分别只能检出与之垂直的边缘。
void NodeCoreTest::testCaliperProjectionDirection()
{
    auto horizontalEdge = []() {          // 水平边缘 y=100（沿 x 方向灰度恒定）
        cv::Mat m(200, 200, CV_8UC1, cv::Scalar(50));
        cv::rectangle(m, cv::Rect(0, 100, 200, 100), cv::Scalar(200), cv::FILLED);
        return m;
    };
    auto verticalEdge = []() {            // 竖直边缘 x=100（沿 y 方向灰度恒定）
        cv::Mat m(200, 200, CV_8UC1, cv::Scalar(50));
        cv::rectangle(m, cv::Rect(100, 0, 100, 200), cv::Scalar(200), cv::FILLED);
        return m;
    };
    auto setup = [](OpencvCaliperNode &c, const cv::Mat &img, int dir) {
        c.init();
        c.setParam(QStringLiteral("row1"), 50.0);
        c.setParam(QStringLiteral("col1"), 100.0);
        c.setParam(QStringLiteral("row2"), 150.0);
        c.setParam(QStringLiteral("col2"), 100.0);
        c.setParam(QStringLiteral("halfWidth"), 30);
        c.setParam(QStringLiteral("step"), 5.0);
        c.setParam(QStringLiteral("threshold"), 40.0);
        c.setParam(QStringLiteral("edgeMode"), 0);
        c.setParam(QStringLiteral("maxCount"), 1000);
        c.setParam(QStringLiteral("scoreContrast"), false);   // 本用例只看几何，不计分
        c.setParam(QStringLiteral("projectionDirection"), dir);
        c.setInputImage(OpencvUtil::matToHimage(img));
    };
    auto firstRow = [](OpencvCaliperNode &c) -> double {
        auto obj = c.getOutputData(1);
        if (!obj)
            return -1.0;
        const MeasureResult r = obj->getMeasureResult();
        return r.extraValues.size() >= 2 ? r.extraValues[1] : -1.0;
    };

    // ① 水平边缘：横向投影（信号沿 x）检不出；纵向投影（信号沿 y）检出 y≈100
    {
        OpencvCaliperNode h;
        setup(h, horizontalEdge(), 0);
        h.run(false);
        const int nH = h.getParam(QStringLiteral("edgeCount")).toInt();
        qInfo("caliper-proj horizontalEdge/transverse: edgeCount=%d", nH);
        QCOMPARE(nH, 0);

        OpencvCaliperNode v;
        setup(v, horizontalEdge(), 1);
        v.run(false);
        const int nV = v.getParam(QStringLiteral("edgeCount")).toInt();
        const double row = firstRow(v);
        qInfo("caliper-proj horizontalEdge/longitudinal: edgeCount=%d firstRow=%.1f", nV, row);
        QVERIFY2(nV > 0, "纵向投影应检出水平边缘");
        // 阶跃落在 y=95 与 y=100 两个采样点之间，梯度中心即 97.5；离散估计精度
        // 受 step 限制，放宽到 step/2 + 余量。修复前是 95（偏一整个采样间隔）。
        QVERIFY2(qAbs(row - 100.0) <= 3.0,
                 qPrintable(QStringLiteral("检出的边缘行应在 100 附近，实际 %1").arg(row)));

        // 细采样反证：step 从 5 降到 1，估计应更贴近真实阶跃位置
        OpencvCaliperNode fine;
        setup(fine, horizontalEdge(), 1);
        fine.setParam(QStringLiteral("step"), 1.0);
        fine.run(false);
        const double fineRow = firstRow(fine);
        qInfo("caliper-proj longitudinal step=1: firstRow=%.2f", fineRow);
        QVERIFY2(qAbs(fineRow - 100.0) < qAbs(row - 100.0),
                 qPrintable(QStringLiteral("细采样应更接近 100：step5=%1 / step1=%2")
                                .arg(row).arg(fineRow)));
    }

    // ② 竖直边缘（互为反证）：横向投影检出，纵向投影检不出
    {
        OpencvCaliperNode h;
        setup(h, verticalEdge(), 0);
        h.run(false);
        const int nH = h.getParam(QStringLiteral("edgeCount")).toInt();
        qInfo("caliper-proj verticalEdge/transverse: edgeCount=%d", nH);
        QVERIFY2(nH > 0, "横向投影应检出竖直边缘");

        OpencvCaliperNode v;
        setup(v, verticalEdge(), 1);
        v.run(false);
        const int nV = v.getParam(QStringLiteral("edgeCount")).toInt();
        qInfo("caliper-proj verticalEdge/longitudinal: edgeCount=%d", nV);
        QCOMPARE(nV, 0);
    }
}

// VM 卡尺参数条件显隐：官方「边缘模式设置为边缘对时，才有该参数」、
// 「单边缘时该参数为对比度，边缘对时为边缘对对比度」。
void NodeCoreTest::testCaliperParamVisibility()
{
    OpencvCaliperNode node;
    node.init();
    QScopedPointer<QWidget> panel(node.createParamPanel());
    QVERIFY(panel);

    auto rowIsHidden = [&panel](const QString &name) -> bool {
        for (QFormLayout *f : panel->findChildren<QFormLayout *>()) {
            for (int r = 0; r < f->rowCount(); ++r) {
                QLayoutItem *it = f->itemAt(r, QFormLayout::FieldRole);
                QWidget *w = it ? it->widget() : nullptr;
                if (w && w->objectName() == name)
                    return w->isHidden();
            }
        }
        return true;
    };
    auto labelText = [&panel](const QString &name) -> QString {
        for (QFormLayout *f : panel->findChildren<QFormLayout *>()) {
            for (int r = 0; r < f->rowCount(); ++r) {
                QLayoutItem *field = f->itemAt(r, QFormLayout::FieldRole);
                QWidget *w = field ? field->widget() : nullptr;
                if (!w || w->objectName() != name)
                    continue;
                QLayoutItem *li = f->itemAt(r, QFormLayout::LabelRole);
                if (auto *lb = qobject_cast<QLabel *>(li ? li->widget() : nullptr))
                    return lb->text();
            }
        }
        return QString();
    };

    QComboBox *mode = panel->findChild<QComboBox *>(QStringLiteral("edgeMode"));
    QCheckBox *contrast = panel->findChild<QCheckBox *>(QStringLiteral("scoreContrast"));
    QVERIFY(mode);
    QVERIFY(contrast);
    QVERIFY(panel->findChild<QWidget *>(QStringLiteral("edge1Polarity")));
    QVERIFY(panel->findChild<QWidget *>(QStringLiteral("spacingDiffX0")));

    // 官方 XML 默认：EdgeMode=1（边缘对）、ContrastEnable=True（单边缘对比度）、
    // ContrastPairEnable=False、SizeDiffNormEnable=True（间距差）。
    QCOMPARE(mode->currentIndex(), 1);

    // ① 默认边缘对：边对专属参数可见；单边缘对比度隐藏、边缘对对比度可见
    QVERIFY2(!rowIsHidden(QStringLiteral("edge1Polarity")), "边缘对模式应显示边缘1极性");
    QVERIFY2(!rowIsHidden(QStringLiteral("expectedPairWidth")), "边缘对模式应显示边缘对宽度");
    QVERIFY2(!rowIsHidden(QStringLiteral("scoreSpacing")), "边缘对模式应显示间距计分");
    QVERIFY2(rowIsHidden(QStringLiteral("scoreContrast")), "边缘对模式应隐藏单边缘对比度");
    QVERIFY2(!rowIsHidden(QStringLiteral("scorePairContrast")), "边缘对模式应显示边缘对对比度");
    // 默认「间距差」启用 ⇒ 它自己的曲线参数可见
    QVERIFY2(!rowIsHidden(QStringLiteral("spacingDiffX0")), "默认间距差启用，应显示其曲线参数");

    // ② 切到单边缘：边对专属参数隐藏；单边缘对比度出现、边缘对对比度隐藏
    mode->setCurrentIndex(0);
    QVERIFY2(rowIsHidden(QStringLiteral("edge1Polarity")), "单边缘模式不应显示边缘1极性");
    QVERIFY2(rowIsHidden(QStringLiteral("expectedPairWidth")), "单边缘模式不应显示边缘对宽度");
    QVERIFY2(rowIsHidden(QStringLiteral("scoreSpacing")), "单边缘模式不应显示间距计分");
    QVERIFY2(!rowIsHidden(QStringLiteral("scoreContrast")), "单边缘模式应显示对比度");
    QVERIFY2(rowIsHidden(QStringLiteral("scorePairContrast")), "单边缘模式不应显示边缘对对比度");
    QVERIFY2(rowIsHidden(QStringLiteral("spacingDiffX0")),
             "单边缘模式不应显示间距差（pairOnly）的曲线参数");

    // ③ 两个对比度开关的标签与官方 XML 一致（ContrastEnable / ContrastPairEnable）
    QCOMPARE(labelText(QStringLiteral("scoreContrast")), QStringLiteral("对比度"));
    QCOMPARE(labelText(QStringLiteral("scorePairContrast")), QStringLiteral("边缘对对比度"));

    // ④ 单边缘下把适用的计分项全关 → 曲线参数隐藏；启用「位置」→ 重新出现
    QCheckBox *position = panel->findChild<QCheckBox *>(QStringLiteral("scorePosition"));
    QVERIFY(position);
    contrast->setChecked(false);
    QVERIFY2(rowIsHidden(QStringLiteral("contrastX0")), "对比度关闭后不应显示它的曲线参数");
    position->setChecked(true);
    QVERIFY2(!rowIsHidden(QStringLiteral("positionX0")), "启用位置计分后应显示它的曲线参数");
    QVERIFY(!rowIsHidden(QStringLiteral("positionCurve")));

    // ⑤ updateParamPanel 之后显隐仍保持（面板恢复路径）
    node.updateParamPanel(panel.data());
    QVERIFY2(rowIsHidden(QStringLiteral("edge1Polarity")),
             "面板恢复后不应把已隐藏的边对参数重新露出来");
}

// VM「模糊边缘」：官方「开启后，可增强候选边缘点集的提取能力，获取更多候选点集数，
// 从而可在"干扰点较多的图像"或"边缘模糊的图像"场景中更大可能地提取到目标边缘点」。
void NodeCoreTest::testCaliperBlurEdge()
{
    // 弱边缘：背景 100 → 亮带 130，对比度 30；阈值设 50 时默认检不出
    cv::Mat img(200, 200, CV_8UC1, cv::Scalar(100));
    cv::rectangle(img, cv::Rect(90, 0, 20, 200), cv::Scalar(130), cv::FILLED);

    auto edgeCountWith = [&img](bool blur) -> int {
        OpencvCaliperNode c;
        c.init();
        c.setParam(QStringLiteral("row1"), 50.0);
        c.setParam(QStringLiteral("col1"), 100.0);
        c.setParam(QStringLiteral("row2"), 150.0);
        c.setParam(QStringLiteral("col2"), 100.0);
        c.setParam(QStringLiteral("halfWidth"), 30);
        c.setParam(QStringLiteral("step"), 10.0);
        c.setParam(QStringLiteral("threshold"), 50.0);
        c.setParam(QStringLiteral("edgeMode"), 0);
        c.setParam(QStringLiteral("maxCount"), 1000);
        c.setParam(QStringLiteral("scoreContrast"), false);   // 只看候选数量
        c.setParam(QStringLiteral("blurEdge"), blur);
        c.setInputImage(OpencvUtil::matToHimage(img));
        c.run(false);
        return c.getParam(QStringLiteral("edgeCount")).toInt();
    };

    const int nOff = edgeCountWith(false);
    const int nOn = edgeCountWith(true);
    qInfo("caliper-blur-edge: off=%d on=%d", nOff, nOn);
    QVERIFY2(nOff == 0, "对比度 30 < 阈值 50，关闭模糊边缘时不应检出");
    QVERIFY2(nOn > 0, "开启模糊边缘后应纳入更弱的候选");
}

// VM「边缘对」配对：官方未给算法，工程口径是「同剖面内异极性、全部候选对，
// 按分数降序贪心、非重叠」。旧实现强制「原始序列相邻两条边」，一条夹在中间的噪声边
// 就能把真实边对拆散。本用例专门钉住这一点。
void NodeCoreTest::testCaliperPairSelection()
{
    // 亮带 x=90..110，内部再压一条暗线 x=100..102：
    // 剖面边缘序列 = 上升(90) / 下降(100) / 上升(102) / 下降(110)。
    // 真实边对是 (90,110) 宽 20；旧「相邻配对」只会得到 (90,100)=10 与 (102,110)=8。
    cv::Mat img(200, 300, CV_8UC1, cv::Scalar(50));
    cv::rectangle(img, cv::Rect(90, 0, 20, 200), cv::Scalar(200), cv::FILLED);
    cv::rectangle(img, cv::Rect(100, 0, 2, 200), cv::Scalar(80), cv::FILLED);

    OpencvCaliperNode c;
    c.init();
    c.setParam(QStringLiteral("row1"), 50.0);
    c.setParam(QStringLiteral("col1"), 150.0);
    c.setParam(QStringLiteral("row2"), 150.0);
    c.setParam(QStringLiteral("col2"), 150.0);
    c.setParam(QStringLiteral("halfWidth"), 100);
    c.setParam(QStringLiteral("step"), 20.0);
    c.setParam(QStringLiteral("threshold"), 40.0);
    c.setParam(QStringLiteral("edgeMode"), 1);
    c.setParam(QStringLiteral("maxCount"), 1000);
    // 默认「间距差 + 边缘对宽度 10」会把宽 10/8 的噪声对排到最前，遮住真实宽 20 的边对；
    // 本用例只验证配对策略，关掉计分让候选按对比度选优。
    c.setParam(QStringLiteral("scoreSpacingDiff"), false);
    c.setInputImage(OpencvUtil::matToHimage(img));
    c.run(false);

    QVector<double> widths;
    if (auto obj = c.getOutputData(3)) {
        const MeasureResult r = obj->getMeasureResult();
        for (int i = 4; i < r.extraValues.size(); i += 5)
            widths.append(r.extraValues[i]);
    }
    bool hasTruePair = false;
    for (double w : widths)
        hasTruePair = hasTruePair || qAbs(w - 20.0) <= 1.5;
    qInfo("caliper-pair-selection: pairs=%d true20=%d", int(widths.size()), int(hasTruePair));
    QVERIFY2(hasTruePair,
             qPrintable(QStringLiteral("中间噪声边不应拆散真实边对（宽 20）：%1")
                            .arg(widths.size())));
}

// VM「结果判断」：官方 XML 的数量判断 / 宽度判断 / 边缘点0X 判断。
// 不满足时模块判 NG，原因写 judgeNote（*Note 口径，失败轮进结果面板）。
void NodeCoreTest::testCaliperJudgment()
{
    cv::Mat img(200, 200, CV_8UC1, cv::Scalar(50));
    cv::rectangle(img, cv::Rect(90, 0, 20, 200), cv::Scalar(200), cv::FILLED);

    auto setup = [&img](OpencvCaliperNode &c) {
        c.init();
        c.setParam(QStringLiteral("row1"), 50.0);
        c.setParam(QStringLiteral("col1"), 100.0);
        c.setParam(QStringLiteral("row2"), 150.0);
        c.setParam(QStringLiteral("col2"), 100.0);
        c.setParam(QStringLiteral("halfWidth"), 30);
        c.setParam(QStringLiteral("step"), 5.0);
        c.setParam(QStringLiteral("threshold"), 60.0);
        c.setParam(QStringLiteral("edgeMode"), 0);
        c.setParam(QStringLiteral("maxCount"), 1000);
        c.setParam(QStringLiteral("scoreContrast"), false);   // 不做分序，保持采集顺序
        c.setInputImage(OpencvUtil::matToHimage(img));
    };

    // ① 数量判断：结果数 50，上限 10 → NG；上限 1000 → OK
    {
        OpencvCaliperNode c;
        setup(c);
        c.setParam(QStringLiteral("numLimitEnable"), true);
        c.setParam(QStringLiteral("numLimitLow"), 1);
        c.setParam(QStringLiteral("numLimitHigh"), 10);
        c.run(false);
        QVERIFY2(!c.getParam(QStringLiteral("moduleStatus")).toBool(), "数量超上限应判 NG");
        QVERIFY2(c.getParam(QStringLiteral("judgeNote")).toString()
                     .contains(QStringLiteral("数量判断失败")),
                 qPrintable(c.getParam(QStringLiteral("judgeNote")).toString()));

        OpencvCaliperNode ok;
        setup(ok);
        ok.setParam(QStringLiteral("numLimitEnable"), true);
        ok.setParam(QStringLiteral("numLimitLow"), 1);
        ok.setParam(QStringLiteral("numLimitHigh"), 1000);
        ok.run(false);
        QVERIFY2(ok.getParam(QStringLiteral("moduleStatus")).toBool(), "数量在范围内应判 OK");
    }

    // ①b 框架级：判 NG 走 execute 时也保留输出（画布/结果面板能看到红色结果）
    {
        OpencvCaliperNode c;
        setup(c);
        c.setParam(QStringLiteral("numLimitEnable"), true);
        c.setParam(QStringLiteral("numLimitLow"), 1);
        c.setParam(QStringLiteral("numLimitHigh"), 10);
        auto imgObj = QSharedPointer<DataObject>::create();
        imgObj->setHImage(OpencvUtil::matToHimage(img));
        c.setInputData(0, imgObj);
        QVERIFY2(!c.execute(), "判 NG 应返回失败");
        QVERIFY2(c.isResultNg(), "应标记为「判 NG 但结果有效」");
        QVERIFY2(c.getOutputData(1) != nullptr, "判 NG 后边缘点端口不应被清空");
        QVERIFY2(c.getOutputData(1)->getMeasureResult().extraValues.size() > 0,
                 "判 NG 后边缘点结果应仍然存在");

        // VM 口径：判 NG 但结果有效时，画布结果叠加层应为红色（含边对连线/宽度文字）
        ImageDisplayController ctrl(nullptr, nullptr, nullptr);
        const QVector<OverlayShape> overlay = ctrl.collectOverlayFromNode(&c);
        bool foundRed = false;
        for (const OverlayShape &shape : overlay) {
            if (shape.color.red() > 200 && shape.color.green() < 100)
                foundRed = true;
        }
        QVERIFY2(foundRed, "判 NG 后叠加层应改为红色");
    }

    // ② 宽度判断（边对模式）：宽 20 不在 [30,40] → NG；[18,22] → OK
    {
        OpencvCaliperNode c;
        setup(c);
        c.setParam(QStringLiteral("edgeMode"), 1);
        c.setParam(QStringLiteral("scoreSpacingDiff"), false);
        c.setParam(QStringLiteral("edgeWidthLimitEnable"), true);
        c.setParam(QStringLiteral("edgeWidthLimitLow"), 30);
        c.setParam(QStringLiteral("edgeWidthLimitHigh"), 40);
        c.run(false);
        QVERIFY2(!c.getParam(QStringLiteral("moduleStatus")).toBool(), "边对宽度越界应判 NG");
        QVERIFY2(c.getParam(QStringLiteral("judgeNote")).toString()
                     .contains(QStringLiteral("宽度判断失败")),
                 qPrintable(c.getParam(QStringLiteral("judgeNote")).toString()));

        OpencvCaliperNode ok;
        setup(ok);
        ok.setParam(QStringLiteral("edgeMode"), 1);
        ok.setParam(QStringLiteral("scoreSpacingDiff"), false);
        ok.setParam(QStringLiteral("edgeWidthLimitEnable"), true);
        ok.setParam(QStringLiteral("edgeWidthLimitLow"), 18);
        ok.setParam(QStringLiteral("edgeWidthLimitHigh"), 22);
        ok.run(false);
        QVERIFY2(ok.getParam(QStringLiteral("moduleStatus")).toBool(), "宽度在范围内应判 OK");
    }

    // ③ 边缘点0X判断：首个边缘点在 x≈109.5，范围 [0,50] → NG；[100,120] → OK
    {
        OpencvCaliperNode c;
        setup(c);
        c.setParam(QStringLiteral("edge0PointXLimitEnable"), true);
        c.setParam(QStringLiteral("edge0PointXLimitLow"), 0);
        c.setParam(QStringLiteral("edge0PointXLimitHigh"), 50);
        c.run(false);
        QVERIFY2(!c.getParam(QStringLiteral("moduleStatus")).toBool(), "边缘点X越界应判 NG");
        QVERIFY2(c.getParam(QStringLiteral("judgeNote")).toString()
                     .contains(QStringLiteral("边缘点0X判断失败")),
                 qPrintable(c.getParam(QStringLiteral("judgeNote")).toString()));

        OpencvCaliperNode ok;
        setup(ok);
        ok.setParam(QStringLiteral("edge0PointXLimitEnable"), true);
        ok.setParam(QStringLiteral("edge0PointXLimitLow"), 100);
        ok.setParam(QStringLiteral("edge0PointXLimitHigh"), 120);
        ok.run(false);
        QVERIFY2(ok.getParam(QStringLiteral("moduleStatus")).toBool(), "边缘点X在范围内应判 OK");
    }
}

// P0 状态位契约：轮首统一复位。
// ① 禁用节点走 NodeBase::execute() 的早退分支，根本不调 process() —— 只有
//    execute() 开头的复位能清掉上一轮 NG；
// ② 数据型节点走 processDataOutputs()，也要复位；
// ③ 状态位改为 std::atomic 后读写仍正常。
void NodeCoreTest::testResultNgRoundStartReset()
{
    OpencvPixelStatsNode stats;
    stats.init();
    stats.setEnabled(false);
    stats.setResultNg(true);
    QVERIFY(stats.isResultNg());
    QVERIFY2(stats.execute(), "禁用节点应视为通过");
    QVERIFY2(!stats.isResultNg(),
             "禁用节点不调 process()，必须在 NodeBase::execute() 轮首复位 NG");

    FormulaNode formula;
    formula.init();
    formula.setResultNg(true);
    formula.process();          // 走 processDataOutputs() 孪生路径
    QVERIFY2(!formula.isResultNg(), "processDataOutputs() 路径同样要复位 NG");

    stats.setResultNg(true);
    QVERIFY(stats.isResultNg());
    stats.setResultNg(false);
    QVERIFY(!stats.isResultNg());
}

// 覆写过 execute() 的节点也必须走 NodeBase::execute() 的统一判断入口。
// ColorConversionNode 曾直接 `return process()`，绕过判断、轮首复位与异常保护。
void NodeCoreTest::testCustomExecuteStillJudged()
{
    ColorConversionNode node;
    node.init();
    // 必须是彩色图：该节点对灰度图会直接报"无需转换"判失败，判断就没机会跑
    cv::Mat img(16, 16, CV_8UC3, cv::Scalar(80, 100, 120));
    auto obj = QSharedPointer<DataObject>::create();
    obj->setHImage(OpencvUtil::matToHimage(img));
    node.setInputData(0, obj);

    QJsonObject c;
    c.insert(QStringLiteral("param"), QStringLiteral("elapsedMs"));
    c.insert(QStringLiteral("op"), QStringLiteral("<"));
    c.insert(QStringLiteral("value"), QStringLiteral("0"));   // 耗时恒 >= 0 ⇒ 恒不满足
    QJsonArray arr;
    arr.append(c);
    node.setParam(QStringLiteral("resJudgeMode"), 0);
    node.setParam(QStringLiteral("resJudgeConditions"),
                  QJsonDocument(arr).toJson(QJsonDocument::Compact));

    QVERIFY2(!node.execute(), "覆写 execute() 的节点也必须应用统一结果判断");
    QVERIFY2(node.isResultNg(), "应判 NG 但结果有效");
    QVERIFY2(node.getParam(QStringLiteral("lastError")).toString().contains(QStringLiteral("耗时")),
             qPrintable(QStringLiteral("公共字段 elapsedMs 应可读：%1")
                            .arg(node.getParam(QStringLiteral("lastError")).toString())));
}

// VM「排序方式=方向正向」必须在 maxCount 的分数截断 / 边对贪心配对之后**恢复采集顺序**。
// 采集顺序 = 先剖面、再沿一维信号轴的位置（sigPos）；纵向投影时 offset 在同剖面内恒定，
// 用 offset 当次键会把贪心残留顺序当成方向正向输出。
void NodeCoreTest::testCaliperAcquisitionSortOrder()
{
    auto edgePoints = [](OpencvCaliperNode &c) -> QVector<QPointF> {
        QVector<QPointF> pts;
        auto obj = c.getOutputData(1);
        if (!obj)
            return pts;
        const MeasureResult r = obj->getMeasureResult();
        for (int i = 0; i + 1 < r.extraValues.size(); i += 2)
            pts.append(QPointF(r.extraValues[i], r.extraValues[i + 1]));
        return pts;
    };
    auto pairCenters = [](OpencvCaliperNode &c) -> QVector<QPointF> {
        QVector<QPointF> pts;
        auto obj = c.getOutputData(3);
        if (!obj)
            return pts;
        const MeasureResult r = obj->getMeasureResult();
        for (int i = 0; i + 4 < r.extraValues.size(); i += 5)
            pts.append(QPointF((r.extraValues[i] + r.extraValues[i + 2]) * 0.5,
                               (r.extraValues[i + 1] + r.extraValues[i + 3]) * 0.5));
        return pts;
    };

    // ① 单边：3 个不同灰度的亮块 ⇒ 6 条强度不同的边缘；启用位置评分让分数互不相同
    cv::Mat imgB(60, 200, CV_8UC1, cv::Scalar(30));
    imgB(cv::Rect(40, 0, 20, 60)).setTo(100);
    imgB(cv::Rect(100, 0, 20, 60)).setTo(160);
    imgB(cv::Rect(160, 0, 20, 60)).setTo(220);
    OpencvCaliperNode cB;
    cB.init();
    cB.setParam(QStringLiteral("row1"), 10.0);
    cB.setParam(QStringLiteral("col1"), 110.0);
    cB.setParam(QStringLiteral("row2"), 50.0);
    cB.setParam(QStringLiteral("col2"), 110.0);
    cB.setParam(QStringLiteral("halfWidth"), 100);
    cB.setParam(QStringLiteral("step"), 10.0);
    cB.setParam(QStringLiteral("threshold"), 20.0);
    cB.setParam(QStringLiteral("edgeMode"), 0);
    cB.setParam(QStringLiteral("projectionDirection"), 0);
    cB.setParam(QStringLiteral("scorePosition"), true);
    cB.setParam(QStringLiteral("maxCount"), 12);
    cB.setParam(QStringLiteral("sortType"), 0);
    cB.setInputImage(OpencvUtil::matToHimage(imgB));
    cB.run(false);

    const QVector<QPointF> ptsB = edgePoints(cB);
    qInfo("caliper-acquisition single: points=%d", ptsB.size());
    QVERIFY2(ptsB.size() >= 4, "单边模式应有足够多的边缘点用于检验顺序");
    // 亚像素插值必须把位置夹在 [m, m+1]：二次项接近 0 时若不 clamp，
    // sub 可达巨值 → floor(sub) 未定义、灰度外插、坐标溢出/NaN。
    for (const QPointF &p : ptsB) {
        QVERIFY2(std::isfinite(p.x()) && std::isfinite(p.y())
                     && p.x() >= -2.0 && p.x() <= 202.0
                     && p.y() >= -2.0 && p.y() <= 62.0,
                 qPrintable(QStringLiteral("边缘点坐标应有限且不溢出图像：(%1,%2)")
                                .arg(p.x()).arg(p.y())));
    }
    // 剖面沿卡尺线（row 递增）；卡尺线竖直 u=(0,1) ⇒ 法线 n=(-1,0) 朝 −x，
    // 故同剖面内沿信号轴前进对应 col **递减**。采集顺序 = (row 递增, col 递减)。
    for (int i = 1; i < ptsB.size(); ++i) {
        const QPointF &a = ptsB.at(i - 1);
        const QPointF &b = ptsB.at(i);
        QVERIFY2(b.y() + 1e-6 >= a.y()
                     && (qAbs(b.y() - a.y()) > 1e-6 || b.x() - 1e-6 <= a.x()),
                 qPrintable(QStringLiteral("方向正向应按采集顺序 (row↑, col↓)，"
                                          "第 %1 点 (%2,%3) → 第 %4 点 (%5,%6)")
                                .arg(i - 1).arg(a.x()).arg(a.y())
                                .arg(i).arg(b.x()).arg(b.y())));
    }

    // ② 纵向投影 + 边对：贪心配对会打乱生成顺序，组内必须按信号轴位置递增
    cv::Mat imgC(200, 200, CV_8UC1, cv::Scalar(30));
    imgC(cv::Rect(0, 60, 200, 15)).setTo(200);
    imgC(cv::Rect(0, 90, 200, 15)).setTo(200);
    imgC(cv::Rect(0, 120, 200, 15)).setTo(200);
    OpencvCaliperNode cC;
    cC.init();
    cC.setParam(QStringLiteral("row1"), 50.0);
    cC.setParam(QStringLiteral("col1"), 100.0);
    cC.setParam(QStringLiteral("row2"), 150.0);
    cC.setParam(QStringLiteral("col2"), 100.0);
    cC.setParam(QStringLiteral("halfWidth"), 5);
    cC.setParam(QStringLiteral("step"), 5.0);
    cC.setParam(QStringLiteral("threshold"), 20.0);
    cC.setParam(QStringLiteral("edgeMode"), 1);
    cC.setParam(QStringLiteral("projectionDirection"), 1);   // 信号沿卡尺线（y）
    // maxCount 注册下限是 1，setParam(0) 会被夹到 1；用大值表示"不截断"
    cC.setParam(QStringLiteral("maxCount"), 1000);
    cC.setParam(QStringLiteral("sortType"), 0);
    cC.setInputImage(OpencvUtil::matToHimage(imgC));
    cC.run(false);

    const QVector<QPointF> pairsC = pairCenters(cC);
    qInfo("caliper-acquisition longitudinal pairs: %d", pairsC.size());
    QVERIFY2(pairsC.size() >= 4, "纵向投影应配出足够多的边对");
    int sameProfileChecked = 0;
    for (int i = 1; i < pairsC.size(); ++i) {
        const QPointF &a = pairsC.at(i - 1);
        const QPointF &b = pairsC.at(i);
        if (qAbs(a.x() - b.x()) > 1e-6)
            continue;   // 跨剖面，row 会回跳
        ++sameProfileChecked;
        QVERIFY2(b.y() + 1e-6 >= a.y(),
                 qPrintable(QStringLiteral("同剖面内边对应按信号轴 y 递增："
                                          "(%1,%2) → (%3,%4)")
                                .arg(a.x()).arg(a.y()).arg(b.x()).arg(b.y())));
    }
    QVERIFY2(sameProfileChecked > 0, "应至少有一个剖面配出多条边对");
}

// 一轮执行结果必须能从**单个原子**读出一致快照：分别读 moduleStatus（参数表）与
// isResultNg() 会在「轮首已复位 NG、参数表还是上一轮值」的窗口里拿到矛盾态。
void NodeCoreTest::testResultStateSnapshot()
{
    OpencvPixelStatsNode node;
    node.init();
    cv::Mat img(10, 10, CV_8UC1, cv::Scalar(0));
    img(cv::Rect(5, 0, 5, 10)).setTo(cv::Scalar(255));
    auto obj = QSharedPointer<DataObject>::create();
    obj->setHImage(OpencvUtil::matToHimage(img));
    node.setInputData(0, obj);

    NodeBase::ResultState s = node.resultState();
    QVERIFY2(!s.executed, "未执行时 executed 应为 false");

    node.setParam(QStringLiteral("resJudgeConditions"), QString());
    QVERIFY2(node.execute(), "无条件配置应判 OK");
    s = node.resultState();
    QVERIFY(s.executed);
    QVERIFY(s.moduleOk);
    QVERIFY(!s.resultNg);
    QVERIFY(!node.isResultNg());

    QJsonObject cond;
    cond.insert(QStringLiteral("param"), QStringLiteral("maxGray"));
    cond.insert(QStringLiteral("op"), QStringLiteral(">"));
    cond.insert(QStringLiteral("value"), QStringLiteral("9999"));
    QJsonArray arr;
    arr.append(cond);
    node.setParam(QStringLiteral("resJudgeMode"), 0);
    node.setParam(QStringLiteral("resJudgeConditions"),
                  QJsonDocument(arr).toJson(QJsonDocument::Compact));
    QVERIFY2(!node.execute(), "恒不满足条件应判 NG");
    s = node.resultState();
    QVERIFY(s.executed);
    QVERIFY(!s.moduleOk);
    QVERIFY(s.resultNg);
    // 快照与分别读取必须一致（这正是原子快照要保证的）
    QCOMPARE(s.resultNg, node.isResultNg());
    QCOMPARE(s.moduleOk, node.getParam(QStringLiteral("moduleStatus")).toBool());

    node.resetRoundState();   // 模拟被跳过/轮首遍历复位
    s = node.resultState();
    QVERIFY2(!s.executed, "轮首复位后本轮不应再被当作已执行");
    QVERIFY(!s.resultNg);
    QVERIFY(!s.moduleOk);

    // 禁用节点没跑 process()：本轮状态必须干净，不能挂上一轮 NG / 原因 / 耗时
    node.setParam(QStringLiteral("moduleStatus"), false);
    node.setParam(QStringLiteral("lastError"), QStringLiteral("上一轮判红原因"));
    node.setEnabled(false);
    QVERIFY2(node.execute(), "禁用节点应视为通过");
    const NodeBase::ResultState disabled = node.resultState();
    QVERIFY(disabled.executed);
    QVERIFY(disabled.moduleOk);
    QVERIFY(!disabled.resultNg);
    QVERIFY2(node.lastElapsedMs() == 0, "禁用节点本轮耗时应为 0");
    QVERIFY2(node.getParam(QStringLiteral("lastError")).toString().isEmpty(),
             "禁用节点不应残留上一轮判红原因");
    node.setEnabled(true);
}

// 稳定性防线：参数脏值不得静默变 0、超大输出尺寸与超长 ROI 不得冻结/OOM。
void NodeCoreTest::testStabilityGuards()
{
    // ① HalconNode::setParam 对转换失败的字符串回退声明默认值
    ResizeNode resize;
    resize.init();
    resize.setParam(QStringLiteral("widthValue"), QStringLiteral("not-a-number"));
    const QVariant w = resize.getParam(QStringLiteral("widthValue"));
    QCOMPARE(w.toInt(), 800);
    QVERIFY2(w.typeId() == QMetaType::Int,
             "失败的字符串不应原样留在 Int 参数里（run() 会静默 toInt()=0）");

    // ② Resize 像素总量封顶：注册上限 65000² 会申请约 12 GB
    cv::Mat img(16, 16, CV_8UC1, cv::Scalar(80));
    resize.setInputImage(OpencvUtil::matToHimage(img));
    resize.setParam(QStringLiteral("widthValue"), 65000);
    resize.setParam(QStringLiteral("heightValue"), 65000);
    resize.run(false);
    QVERIFY2(!resize.getParam(QStringLiteral("moduleStatus")).toBool(),
             "65000×65000 应被像素总量封顶拦下");
    QVERIFY2(resize.getParam(QStringLiteral("lastError")).toString().contains(QStringLiteral("过大")),
             "封顶必须给出可读原因");

    // ③ 卡尺超大 ROI 端点：端点夹到图像内 + K 封顶，不再产生十万级剖面
    OpencvCaliperNode caliper;
    caliper.init();
    caliper.setParam(QStringLiteral("row1"), 0.0);
    caliper.setParam(QStringLiteral("col1"), 0.0);
    caliper.setParam(QStringLiteral("row2"), 100000.0);
    caliper.setParam(QStringLiteral("col2"), 100000.0);
    caliper.setParam(QStringLiteral("step"), 1.0);
    caliper.setParam(QStringLiteral("halfWidth"), 10);
    caliper.setInputImage(OpencvUtil::matToHimage(img));
    QElapsedTimer timer;
    timer.start();
    caliper.run(false);
    qInfo("caliper-stability: huge ROI elapsed=%lld ms", timer.elapsed());
    QVERIFY2(timer.elapsed() < 5000,
             qPrintable(QStringLiteral("超大 ROI 不应冻结执行线程（实际 %1 ms）")
                            .arg(timer.elapsed())));
}

// 运行时参数归一化：用 setParamDirect 绕过 setParam 的范围钳制（模拟方案被手工
// 编辑 / fromJson 直写 / 参数引用注入坏值），execute() 之后必须被夹回注册范围。
template <class T>
class ParamInjectProbe : public T
{
public:
    void inject(const QString &key, const QVariant &value)
    {
        this->setParamDirect(key, value);
    }
};

void NodeCoreTest::testRuntimeParamNormalization()
{
    cv::Mat img(16, 16, CV_8UC1, cv::Scalar(80));
    const HalconCpp::HImage himage = OpencvUtil::matToHimage(img);

    auto feed = [&himage](auto &probe) {
        probe.init();
        probe.setInputImage(himage);
    };

    // B3 形态学：iterations 注册 0..10，注入 20 亿
    {
        ParamInjectProbe<OpencvMorphNode> p;
        feed(p);
        p.inject(QStringLiteral("iterations"), 2000000000);
        p.execute();
        QCOMPARE(p.getParam(QStringLiteral("iterations")).toInt(), 10);
    }
    // B4 二值化：gaussSigma 注册 0.1..100，注入 1e300
    {
        ParamInjectProbe<OpencvThresholdNode> p;
        feed(p);
        p.inject(QStringLiteral("gaussSigma"), 1e300);
        p.execute();
        QCOMPARE(p.getParam(QStringLiteral("gaussSigma")).toDouble(), 100.0);
    }
    // B6 DNN 分割：inputWidth 注册 32..4096，注入 100000（否则 letter 分配 30 GB）
    {
        ParamInjectProbe<DnnSegmentNode> p;
        feed(p);
        p.inject(QStringLiteral("inputWidth"), 100000);
        p.execute();
        QCOMPARE(p.getParam(QStringLiteral("inputWidth")).toInt(), 4096);
    }
    // B9 拟合直线：maxContours 注册 1..1000，注入负值
    {
        ParamInjectProbe<OpencvFitLineNode> p;
        feed(p);
        p.inject(QStringLiteral("maxContours"), -1);
        p.execute();
        QCOMPARE(p.getParam(QStringLiteral("maxContours")).toInt(), 1);
    }
    // 脏字符串：必须回退声明默认值，且类型修正为 Double
    {
        ParamInjectProbe<OpencvCaliperNode> p;
        feed(p);
        p.inject(QStringLiteral("threshold"), QStringLiteral("dirty"));
        p.execute();
        const QVariant v = p.getParam(QStringLiteral("threshold"));
        QVERIFY2(v.typeId() == QMetaType::Double, "脏字符串不得留在 Double 参数里");
        QCOMPARE(v.toDouble(), 15.0);
    }
}

// NodeBase 的 4 个执行状态位改为 std::atomic 后：读写仍正确，且 toJson / 三元
// 表达式里的显式 .load() 不丢值（QJsonValue 不接受 atomic 的拷贝赋值）。
void NodeCoreTest::testAtomicNodeFlagsSerialization()
{
    OpencvPixelStatsNode node;
    node.init();
    node.setEnabled(false);
    QVERIFY(!node.isEnabled());
    node.setEnabled(true);
    QVERIFY(node.isEnabled());

    const QJsonObject json = node.toJson();
    QVERIFY2(json.contains(QStringLiteral("enabled")), "toJson 应写出 enabled");
    QVERIFY2(json.value(QStringLiteral("enabled")).toBool(), "enabled 应为 true");
    QVERIFY2(json.contains(QStringLiteral("executionSuccess")),
             "toJson 应写出 executionSuccess（atomic 需显式 load）");

    cv::Mat img(10, 10, CV_8UC1, cv::Scalar(80));
    auto obj = QSharedPointer<DataObject>::create();
    obj->setHImage(OpencvUtil::matToHimage(img));
    node.setInputData(0, obj);
    QVERIFY2(node.execute(), "原子化后节点仍应能正常执行");
    QVERIFY2(node.resultState().executed, "执行后应提交状态快照");
}

// 4 个节点由 HALCON 算法改为 OpenCV 实现后的数值回归：饱和语义与形态学结构必须与
// 原实现一致（顶帽/底帽特意保持"逐次开/闭运算"结构，而不是 morphologyEx 的 iterations）。
void NodeCoreTest::testImageArithAndHatOpenCv()
{
    cv::Mat src(2, 2, CV_8UC1);
    src.at<uchar>(0, 0) = 10;
    src.at<uchar>(0, 1) = 200;
    src.at<uchar>(1, 0) = 250;
    src.at<uchar>(1, 1) = 0;
    const HalconCpp::HImage himg = OpencvUtil::matToHimage(src);

    // ① 图像加法：255 饱和（与 HALCON AddImage 的字节饱和一致）
    {
        ImageAddNode node;
        node.init();
        node.setParam(QStringLiteral("value"), 100.0);
        node.setInputImage(himg);
        node.run(false);
        const cv::Mat out = OpencvUtil::himageToMat(node.getOutputImage());
        QVERIFY2(!out.empty(), "图像加法应产出图像");
        QCOMPARE(static_cast<int>(out.at<uchar>(0, 0)), 110);
        QCOMPARE(static_cast<int>(out.at<uchar>(0, 1)), 255);   // 200+100 饱和
        QCOMPARE(static_cast<int>(out.at<uchar>(1, 0)), 255);   // 250+100 饱和
    }
    // ② 图像乘法：255 饱和
    {
        ImageMulNode node;
        node.init();
        node.setParam(QStringLiteral("value"), 2.0);
        node.setInputImage(himg);
        node.run(false);
        const cv::Mat out = OpencvUtil::himageToMat(node.getOutputImage());
        QVERIFY2(!out.empty(), "图像乘法应产出图像");
        QCOMPARE(static_cast<int>(out.at<uchar>(0, 0)), 20);
        QCOMPARE(static_cast<int>(out.at<uchar>(0, 1)), 255);   // 200*2 饱和
        QCOMPARE(static_cast<int>(out.at<uchar>(1, 0)), 255);   // 250*2 饱和
    }
    // ③ 顶帽：平坦背景应归零，孤立亮点保留 原值-背景
    {
        TopHatNode node;
        node.init();
        node.setParam(QStringLiteral("maskWidth"), 3);
        node.setParam(QStringLiteral("maskHeight"), 3);
        node.setParam(QStringLiteral("iterations"), 1);
        cv::Mat src2(10, 10, CV_8UC1, cv::Scalar(50));
        src2.at<uchar>(5, 5) = 200;
        node.setInputImage(OpencvUtil::matToHimage(src2));
        node.run(false);
        const cv::Mat out = OpencvUtil::himageToMat(node.getOutputImage());
        QVERIFY2(!out.empty(), "顶帽应产出图像");
        QCOMPARE(static_cast<int>(out.at<uchar>(1, 1)), 0);      // 平坦区 → 0
        QCOMPARE(static_cast<int>(out.at<uchar>(5, 5)), 150);    // 200 - 50
    }
    // ④ 底帽：平坦背景应归零，孤立暗点保留
    {
        BottomHatNode node;
        node.init();
        node.setParam(QStringLiteral("maskWidth"), 3);
        node.setParam(QStringLiteral("maskHeight"), 3);
        node.setParam(QStringLiteral("iterations"), 1);
        cv::Mat src3(10, 10, CV_8UC1, cv::Scalar(200));
        src3.at<uchar>(5, 5) = 50;
        node.setInputImage(OpencvUtil::matToHimage(src3));
        node.run(false);
        const cv::Mat out = OpencvUtil::himageToMat(node.getOutputImage());
        QVERIFY2(!out.empty(), "底帽应产出图像");
        QCOMPARE(static_cast<int>(out.at<uchar>(1, 1)), 0);      // 平坦区 → 0
        QCOMPARE(static_cast<int>(out.at<uchar>(5, 5)), 150);    // 200 - 50
    }
}

QTEST_MAIN(NodeCoreTest)
#include "node_core_test.moc"
