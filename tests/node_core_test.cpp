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
#include "OpencvCaliperNode.h"
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
    void testMorphVmParamsAndRoi();
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
    void testCaliperVmParams();
    void testCaliperScoring();
    void testCaliperPositionScoringAndResults();
    void testCaliperProjectionDirection();
    void testCaliperParamVisibility();
    void testCaliperBlurEdge();
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
    {
        OpencvCaliperNode any;
        setup(any, baseImg(), 100.0);
        any.setParam(QStringLiteral("edgeMode"), 1);
        any.setParam(QStringLiteral("pairWidth"), 30);
        any.run(false);
        const int nAny = any.getParam(QStringLiteral("pairCount")).toInt();

        OpencvCaliperNode restricted;
        setup(restricted, baseImg(), 100.0);
        restricted.setParam(QStringLiteral("edgeMode"), 1);
        restricted.setParam(QStringLiteral("pairWidth"), 30);
        restricted.setParam(QStringLiteral("edge1Polarity"), 1);
        restricted.run(false);
        const int nP1 = restricted.getParam(QStringLiteral("pairCount")).toInt();

        OpencvCaliperNode restricted2;
        setup(restricted2, baseImg(), 100.0);
        restricted2.setParam(QStringLiteral("edgeMode"), 1);
        restricted2.setParam(QStringLiteral("pairWidth"), 30);
        restricted2.setParam(QStringLiteral("edge1Polarity"), 2);
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
        c.setParam(QStringLiteral("pairWidth"), 200);   // 放宽硬上限，两种宽度都能配对
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

    // ② 启用间距计分 + 边缘对宽度 20：宽度 20 的边对分数严格更高
    {
        OpencvCaliperNode c;
        setup(c);
        c.setParam(QStringLiteral("expectedPairWidth"), 20.0);
        c.setParam(QStringLiteral("scoreSpacing"), true);
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
        c.setParam(QStringLiteral("scoreSpacing"), true);
        c.setParam(QStringLiteral("maxCount"), 1);
        c.run(false);
        const QVector<double> w = widths(c);
        qInfo("caliper-score maxCount: kept=%d firstWidth=%.1f",
              w.size(), w.isEmpty() ? -1.0 : w.first());
        QCOMPARE(w.size(), 1);
        QVERIFY2(qAbs(w.first() - 20.0) <= 1.5,
                 "maxCount=1 应留下分数最高（宽度=期望宽度）的边对");
    }

    // ④ 曲线类型反转（递增）：偏差大的反而分高 → maxCount=1 留下宽度 15 的边对
    {
        OpencvCaliperNode c;
        setup(c);
        c.setParam(QStringLiteral("expectedPairWidth"), 20.0);
        c.setParam(QStringLiteral("scoreSpacing"), true);
        c.setParam(QStringLiteral("curveType"), 0);
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
        c.setParam(QStringLiteral("pairWidth"), 200);
        c.setParam(QStringLiteral("maxCount"), 1);
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
        c.setParam(QStringLiteral("maxCount"), 0);
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
    QVERIFY(panel->findChild<QWidget *>(QStringLiteral("curveX0")));

    // ① 单边缘（默认）：边对专属参数隐藏；计分全关时曲线参数也隐藏
    QCOMPARE(mode->currentIndex(), 0);
    QVERIFY2(rowIsHidden(QStringLiteral("edge1Polarity")), "单边缘模式不应显示边缘1极性");
    QVERIFY2(rowIsHidden(QStringLiteral("expectedPairWidth")), "单边缘模式不应显示边缘对宽度");
    QVERIFY2(rowIsHidden(QStringLiteral("scoreSpacing")), "单边缘模式不应显示间距计分");
    QVERIFY2(rowIsHidden(QStringLiteral("curveX0")), "计分全关时不应显示曲线参数");

    // ② 切到边缘对：边对专属参数出现
    mode->setCurrentIndex(1);
    QVERIFY2(!rowIsHidden(QStringLiteral("edge1Polarity")), "边缘对模式应显示边缘1极性");
    QVERIFY2(!rowIsHidden(QStringLiteral("expectedPairWidth")), "边缘对模式应显示边缘对宽度");
    QVERIFY2(!rowIsHidden(QStringLiteral("scoreSpacing")), "边缘对模式应显示间距计分");

    // ③ 官方：计分项名称随边缘模式变化
    const QString pairLabel = labelText(QStringLiteral("scoreContrast"));
    mode->setCurrentIndex(0);
    const QString singleLabel = labelText(QStringLiteral("scoreContrast"));
    qInfo("caliper-visibility: pair='%s' single='%s'",
          qPrintable(pairLabel), qPrintable(singleLabel));
    QVERIFY2(pairLabel.contains(QStringLiteral("边缘对对比度")),
             qPrintable(QStringLiteral("边缘对模式应叫「边缘对对比度」，实际 %1").arg(pairLabel)));
    QVERIFY2(singleLabel.contains(QStringLiteral("对比度"))
                 && !singleLabel.contains(QStringLiteral("边缘对")),
             qPrintable(QStringLiteral("单边缘模式应叫「对比度」，实际 %1").arg(singleLabel)));

    // ④ 勾任一计分开关 → 曲线参数出现；取消 → 再隐藏
    contrast->setChecked(true);
    QVERIFY2(!rowIsHidden(QStringLiteral("curveX0")), "启用计分后应显示曲线参数");
    QVERIFY(!rowIsHidden(QStringLiteral("curveType")));
    contrast->setChecked(false);
    QVERIFY2(rowIsHidden(QStringLiteral("curveX0")), "关闭计分后曲线参数应重新隐藏");

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

QTEST_MAIN(NodeCoreTest)
#include "node_core_test.moc"
