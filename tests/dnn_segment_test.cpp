// ONNX 分割功能测试（G-P0-2 / FR13.4）：
//   语义输出解析（单通道 sigmoid+阈值 / 多类 argmax / 维度变体）+ YOLO-Seg 双输出解析
//   （系数·原型、框裁剪、掩膜几何、置信过滤、NMS）+ 连通域统计 + 序列化 + 缺/坏模型降级
//   + 最小 ONNX 分割模型**端到端**（python 生成，缺 python 时跳过）。
// 不依赖真实训练模型：解析逻辑全部用合成张量验证。
#include <QtTest/QtTest>
#include <QObject>
#include <QVector>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QProcess>
#include <QFileInfo>

#include "DnnSegmentNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <HalconCpp.h>

class DnnSegmentTest : public QObject
{
    Q_OBJECT
private slots:
    void testSemanticSingleChannel();      // 单通道：sigmoid 开关 × 阈值
    void testSemanticMultiClass();         // 多类：argmax 前景 + 越界拒绝
    void testSemanticDimsVariants();       // [C,H,W] 与 [H,W] 两种输入布局
    void testYoloSegOutput();              // 双输出：掩膜几何 + 框裁剪 + 严格 >0 判据
    void testYoloSegLowConf();             // 置信阈值滤空 → 空框 + 零掩膜
    void testMaskToBoxes();                // 连通域：minArea 过滤 + 掩膜回写一致性
    void testSigmoidAutoAndForced();        // sigmoid 三态 + 钉住"二次 sigmoid"实测缺陷
    void testSerialization();
    void testMissingAndBadModel();         // 缺模型 / 坏模型路径：降级且无残留输出
    void testEndToEndOnnxSemantic();       // 最小 ONNX 模型端到端（语义路径，需 python + onnx）
    void testEndToEndLetterboxNonSquare(); // 端到端：非等比方图，钉住 letterbox 逆映射（裁灰边 + 缩放）
    void testEndToEndOnnxInstance();       // 端到端：YOLO-Seg 双输出，实例路径 + 按框裁剪
};

// 4D 张量寻址：部分 OpenCV 版本不再提供 4 下标 at 重载，统一用 ptr(两下标) + 线性下标
static inline float &segAt4(cv::Mat &m, int i0, int i1, int i2, int i3)
{
    return m.ptr<float>(i0, i1)[i2 * m.size[3] + i3];
}

// 在 [1,R,N] 的第 n 个锚点上写入框 + 类分数 + K 个掩膜系数（列：0..3 框，4..4+nc-1 类，其后系数）
static void setYoloSegAnchor(cv::Mat &out0, int n, int nc, float cx, float cy, float w, float h,
                             float s0, float s1)
{
    out0.at<float>(0, 0, n) = cx;
    out0.at<float>(0, 1, n) = cy;
    out0.at<float>(0, 2, n) = w;
    out0.at<float>(0, 3, n) = h;
    out0.at<float>(0, 4, n) = s0;
    if (nc > 1)
        out0.at<float>(0, 5, n) = s1;
    // 系数 0（列 4 + nc）置 1：配合原型通道 0 的亮块，掩膜 = 1 · proto > 0
    out0.at<float>(0, 4 + nc, n) = 1.0f;
}

// 构造 YOLO-Seg 双输出：out0 = [1, R, N]（R = 4 + nc + K = 10，N = 16）、out1 = [1, K, mh, mw]（16×16）
static void makeYoloSegOutputs(cv::Mat &out0, cv::Mat &out1)
{
    const int nc = 2, K = 4, N = 16, mh = 16, mw = 16;
    const int R = 4 + nc + K;
    int sz0[] = {1, R, N};
    out0 = cv::Mat::zeros(3, sz0, CV_32F);
    int sz1[] = {1, K, mh, mw};
    out1 = cv::Mat::zeros(4, sz1, CV_32F);

    // 锚点 0：高置信框（输入空间 24,24,16,16）；锚点 2：同框稍低 → 应被 NMS 合并；锚点 1：低置信 → 应被滤掉
    setYoloSegAnchor(out0, 0, nc, 32.f, 32.f, 16.f, 16.f, 0.9f, 0.1f);
    setYoloSegAnchor(out0, 1, nc, 32.f, 32.f, 16.f, 16.f, 0.05f, 0.05f);
    setYoloSegAnchor(out0, 2, nc, 32.f, 32.f, 16.f, 16.f, 0.8f, 0.1f);

    // 原型通道 0：在 (6,6)-(9,9) 亮（原型 16×16 → 输入 64×64，缩放 4；框 24,24,16,16 → 原型 6,6,4,4）
    for (int y = 6; y <= 9; ++y)
        for (int x = 6; x <= 9; ++x)
            segAt4(out1, 0, 0, y, x) = 1.0f;
}

void DnnSegmentTest::testSemanticSingleChannel()
{
    int sz[] = {1, 1, 8, 8};
    cv::Mat out = cv::Mat::zeros(4, sz, CV_32F);
    out.setTo(cv::Scalar(-2.0f));
    for (int y = 3; y <= 5; ++y)
        for (int x = 3; x <= 5; ++x)
            segAt4(out, 0, 0, y, x) = 2.0f;
    segAt4(out, 0, 0, 0, 0) = 0.1f;

    cv::Mat mask;
    // sigmoid 开 + 0.5：logit > 0 即前景；0.1 → sigmoid = 0.525 > 0.5 也算 → 9 + 1 = 10 像素
    QVERIFY(DnnSegmentNode::parseSemanticOutput(out, 0, 0.5, true, mask));
    QCOMPARE(mask.size(), cv::Size(8, 8));
    QCOMPARE(cv::countNonZero(mask), 10);
    QCOMPARE(int(mask.at<uchar>(0, 0)), 255);
    QCOMPARE(int(mask.at<uchar>(7, 7)), 0);

    // sigmoid 关 + 阈值 0.2：0.1 不够 → 只剩 9 像素（证明 useSigmoid 真的生效，不是摆设）
    QVERIFY(DnnSegmentNode::parseSemanticOutput(out, 0, 0.2, false, mask));
    QCOMPARE(cv::countNonZero(mask), 9);
    QCOMPARE(int(mask.at<uchar>(0, 0)), 0);
    QCOMPARE(int(mask.at<uchar>(3, 3)), 255);
}

void DnnSegmentTest::testSemanticMultiClass()
{
    int sz[] = {1, 3, 4, 4};
    cv::Mat out = cv::Mat::zeros(4, sz, CV_32F);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            segAt4(out, 0, 0, y, x) = 0.9f;                  // 类 0 恒高
            segAt4(out, 0, 1, y, x) = 0.1f;                  // 类 1 恒低
            segAt4(out, 0, 2, y, x) = (x < 2) ? 0.95f : 0.1f; // 类 2 只在左半高
        }
    }

    cv::Mat mask;
    QVERIFY(DnnSegmentNode::parseSemanticOutput(out, 2, 0.5, true, mask));
    QCOMPARE(cv::countNonZero(mask), 8);            // 左半 4×2
    QCOMPARE(int(mask.at<uchar>(0, 0)), 255);
    QCOMPARE(int(mask.at<uchar>(0, 3)), 0);

    QVERIFY(DnnSegmentNode::parseSemanticOutput(out, 0, 0.5, true, mask));
    QCOMPARE(cv::countNonZero(mask), 8);            // 右半归 类 0
    QCOMPARE(int(mask.at<uchar>(0, 0)), 0);
    QCOMPARE(int(mask.at<uchar>(0, 3)), 255);

    // 前台类索引越界 → 明确失败（不猜、不返回空掩膜当成功）
    QVERIFY(!DnnSegmentNode::parseSemanticOutput(out, 3, 0.5, true, mask));
}

void DnnSegmentTest::testSemanticDimsVariants()
{
    cv::Mat mask;
    {
        int sz[] = {1, 4, 4};                       // [C,H,W]（无 batch 维）
        cv::Mat o = cv::Mat::zeros(3, sz, CV_32F);
        o.setTo(cv::Scalar(-1.0f));
        o.at<float>(0, 1, 1) = 1.0f;
        QVERIFY(DnnSegmentNode::parseSemanticOutput(o, 0, 0.5, true, mask));
        QCOMPARE(mask.size(), cv::Size(4, 4));
        QCOMPARE(cv::countNonZero(mask), 1);
    }
    {
        cv::Mat o = cv::Mat::zeros(4, 4, CV_32F);   // [H,W]（纯单通道）
        o.at<float>(2, 2) = 1.0f;
        QVERIFY(DnnSegmentNode::parseSemanticOutput(o, 0, 0.5, true, mask));
        QCOMPARE(mask.size(), cv::Size(4, 4));
        QCOMPARE(cv::countNonZero(mask), 1);
        QCOMPARE(int(mask.at<uchar>(2, 2)), 255);
    }
    {
        QVERIFY(!DnnSegmentNode::parseSemanticOutput(cv::Mat(), 0, 0.5, true, mask));
    }
}

void DnnSegmentTest::testYoloSegOutput()
{
    cv::Mat out0, out1;
    makeYoloSegOutputs(out0, out1);

    cv::Mat mask;
    QVector<DetectionBox> boxes;
    QVERIFY(DnnSegmentNode::parseYoloSegOutput(out0, out1, 64, 64, 0.25, 0.45, mask, boxes));

    QCOMPARE(mask.size(), cv::Size(64, 64));
    QCOMPARE(boxes.size(), 1);                      // 低置信滤掉 + 重叠框 NMS 合并
    QCOMPARE(boxes[0].classId, 0);
    QVERIFY(qAbs(boxes[0].confidence - 0.9) < 1e-5);
    QCOMPARE(boxes[0].x, 24.0);
    QCOMPARE(boxes[0].y, 24.0);
    QCOMPARE(boxes[0].w, 16.0);
    QCOMPARE(boxes[0].h, 16.0);

    // 掩膜几何：4×4 原型裁剪后放大到 16×16 框内 = 256 像素；框外必须为 0
    // （框外为 0 同时证明判据是**严格 > 0**：若写成 >= 0，系数为 0 的背景会整体变 255）
    QCOMPARE(cv::countNonZero(mask(cv::Rect(24, 24, 16, 16))), 256);
    QCOMPARE(cv::countNonZero(mask), 256);
}

void DnnSegmentTest::testYoloSegLowConf()
{
    cv::Mat out0, out1;
    makeYoloSegOutputs(out0, out1);

    cv::Mat mask;
    QVector<DetectionBox> boxes;
    // 置信阈值抬到 0.95：三个锚点全被滤 → 空框 + 全零掩膜（不是"空掩膜当成功"）
    QVERIFY(DnnSegmentNode::parseYoloSegOutput(out0, out1, 64, 64, 0.95, 0.45, mask, boxes));
    QVERIFY(boxes.isEmpty());
    QCOMPARE(cv::countNonZero(mask), 0);

    // 形状不匹配（原型不是 4D/3D）→ 失败
    cv::Mat bad = cv::Mat::zeros(4, 4, CV_32F);
    QVERIFY(!DnnSegmentNode::parseYoloSegOutput(out0, bad, 64, 64, 0.25, 0.45, mask, boxes));
    // 系数+框输出维度不足（R <= 4 + K）→ 失败
    int sz[] = {1, 2, 16};
    cv::Mat tiny = cv::Mat::zeros(3, sz, CV_32F);
    QVERIFY(!DnnSegmentNode::parseYoloSegOutput(tiny, out1, 64, 64, 0.25, 0.45, mask, boxes));
}

void DnnSegmentTest::testMaskToBoxes()
{
    cv::Mat mask = cv::Mat::zeros(32, 32, CV_8U);
    mask(cv::Rect(2, 2, 3, 3)).setTo(255);      // 9 像素（小域）
    mask(cv::Rect(20, 20, 5, 5)).setTo(255);    // 25 像素

    QVector<DetectionBox> boxes;
    QCOMPARE(DnnSegmentNode::maskToBoxes(mask, 10, boxes), 1);
    QCOMPARE(boxes.size(), 1);
    QCOMPARE(boxes[0].confidence, 25.0);        // 连通域模式下 confidence 承载面积
    QCOMPARE(boxes[0].x, 20.0);
    QCOMPARE(boxes[0].y, 20.0);
    QCOMPARE(boxes[0].w, 5.0);
    QCOMPARE(boxes[0].h, 5.0);
    QCOMPARE(cv::countNonZero(mask), 25);       // 小域已从掩膜剔除（掩膜/面积/框一致）
    QCOMPARE(int(mask.at<uchar>(2, 2)), 0);

    cv::Mat mask2 = cv::Mat::zeros(32, 32, CV_8U);
    mask2(cv::Rect(2, 2, 3, 3)).setTo(255);
    mask2(cv::Rect(20, 20, 5, 5)).setTo(255);
    QCOMPARE(DnnSegmentNode::maskToBoxes(mask2, 0, boxes), 2);
    QCOMPARE(cv::countNonZero(mask2), 34);

    // 全零掩膜：0 个域、仍返回空掩膜（不崩）
    cv::Mat empty = cv::Mat::zeros(8, 8, CV_8U);
    QCOMPARE(DnnSegmentNode::maskToBoxes(empty, 1, boxes), 0);
    QVERIFY(boxes.isEmpty());
}

// sigmoid 三态：自动按值域判定；并钉住"二次 sigmoid"这条实测缺陷
//（模型图内已含 Sigmoid → 概率背景 0.5 再压一次 = 0.622 > 0.5 → 整幅图被判成前景）
void DnnSegmentTest::testSigmoidAutoAndForced()
{
    // 概率图（已内置 Sigmoid）：值域 [0.5, 0.73] 全在 [0,1] → 自动应为"不开"
    int szProb[] = {1, 1, 2, 2};
    cv::Mat prob = cv::Mat::zeros(4, szProb, CV_32F);
    prob.setTo(cv::Scalar(0.5f));
    segAt4(prob, 0, 0, 0, 0) = 0.73f;
    QVERIFY(!DnnSegmentNode::resolveSigmoid(prob, 0));   // 自动 → 关
    QVERIFY(DnnSegmentNode::resolveSigmoid(prob, 1));    // 强制开
    QVERIFY(!DnnSegmentNode::resolveSigmoid(prob, 2));   // 强制关

    // logits（存在负值）→ 自动应为"开"
    cv::Mat logits = prob.clone();
    segAt4(logits, 0, 0, 1, 1) = -1.0f;
    QVERIFY(DnnSegmentNode::resolveSigmoid(logits, 0));

    // 钉住实测缺陷的后果：概率背景 0.5 再压一次 → 4/4 全亮
    cv::Mat mask;
    QVERIFY(DnnSegmentNode::parseSemanticOutput(prob, 0, 0.5, true, mask));
    QCOMPARE(cv::countNonZero(mask), 4);
    QVERIFY(DnnSegmentNode::parseSemanticOutput(prob, 0, 0.5, false, mask));
    QCOMPARE(cv::countNonZero(mask), 1);                 // 正确姿势：只留真前景

    // 多类（C > 1）走 argmax，与 sigmoid 无关 → 自动判据直接返回 false
    int szMulti[] = {1, 3, 2, 2};
    cv::Mat multi = cv::Mat::zeros(4, szMulti, CV_32F);
    QVERIFY(!DnnSegmentNode::resolveSigmoid(multi, 0));

    // 非连续输入（列切片）也不得崩：内部先 clone
    cv::Mat big = cv::Mat::zeros(4, 4, CV_32F);
    big.setTo(cv::Scalar(-3.0f));
    QVERIFY(DnnSegmentNode::resolveSigmoid(big.col(0), 0));
}

void DnnSegmentTest::testSerialization()
{
    DnnSegmentNode n1;
    n1.init();
    n1.setParam(QStringLiteral("modelPath"), QStringLiteral("/models/unet.onnx"));
    n1.setParam(QStringLiteral("inputWidth"), 512);
    n1.setParam(QStringLiteral("inputHeight"), 320);
    n1.setParam(QStringLiteral("segMode"), 1);
    n1.setParam(QStringLiteral("classIndex"), 2);
    n1.setParam(QStringLiteral("binaryThresh"), 0.7);
    n1.setParam(QStringLiteral("minArea"), 50);
    n1.setParam(QStringLiteral("keepRatio"), false);
    n1.setParam(QStringLiteral("sigmoidMode"), 2);
    QJsonObject j = n1.toJson();

    DnnSegmentNode n2;
    n2.init();
    n2.fromJson(j);
    QCOMPARE(n2.getParam(QStringLiteral("modelPath")).toString(),
             QStringLiteral("/models/unet.onnx"));
    QCOMPARE(n2.getParam(QStringLiteral("inputWidth")).toInt(), 512);
    QCOMPARE(n2.getParam(QStringLiteral("inputHeight")).toInt(), 320);
    QCOMPARE(n2.getParam(QStringLiteral("segMode")).toInt(), 1);
    QCOMPARE(n2.getParam(QStringLiteral("classIndex")).toInt(), 2);
    QCOMPARE(n2.getParam(QStringLiteral("binaryThresh")).toDouble(), 0.7);
    QCOMPARE(n2.getParam(QStringLiteral("minArea")).toInt(), 50);
    QCOMPARE(n2.getParam(QStringLiteral("keepRatio")).toBool(), false);
    QCOMPARE(n2.getParam(QStringLiteral("sigmoidMode")).toInt(), 2);
}

void DnnSegmentTest::testMissingAndBadModel()
{
    // ① 未设模型路径：明确报错、统计清零、输出清空（下游不得读到残留）
    DnnSegmentNode n1;
    n1.init();
    n1.run();
    QVERIFY2(!n1.getParam(QStringLiteral("moduleStatus")).toBool(),
             "缺模型时 moduleStatus 应为 false");
    QVERIFY2(!n1.getParam(QStringLiteral("lastError")).toString().isEmpty(),
             "缺模型时应写入 lastError");
    QCOMPARE(n1.getParam(QStringLiteral("segmentCount")).toInt(), 0);
    QCOMPARE(n1.getParam(QStringLiteral("maskArea")).toInt(), 0);
    QVERIFY(!n1.getOutputImage().IsInitialized());

    // ② 坏模型路径 + 有输入图：走加载失败分支（不得崩、不得留下残留输出）
    DnnSegmentNode n2;
    n2.init();
    n2.setParam(QStringLiteral("modelPath"),
                QStringLiteral("/nonexistent/definitely_missing.onnx"));
    cv::Mat img(64, 64, CV_8UC3, cv::Scalar(0, 0, 0));
    n2.setInputImage(OpencvUtil::matToHimage(img));
    n2.run();
    QVERIFY(!n2.getParam(QStringLiteral("moduleStatus")).toBool());
    QVERIFY2(!n2.getParam(QStringLiteral("lastError")).toString().isEmpty(),
             "坏模型路径时应写入 lastError");
    QVERIFY(!n2.getOutputImage().IsInitialized());
    QVERIFY(!n2.getOutputData(1));
}

// 生成/定位端到端用例所需的 ONNX 模型：
//   · 环境变量给了现成模型（VFP_SEG_TEST_MODEL / VFP_SEG_TEST_INST_MODEL）→ 直接用（便于换真模型试跑）；
//   · 否则用 python 现生成（脚本从编译期给出的源码目录、或 exe 上溯的仓库根寻找）；
//   · 环境不具备时把原因写入 g_modelGenReason 并返回空串（调用方 QSKIP，不制造环境相关红灯）。
static QString g_modelGenReason;

static QString ensureTestModel(const QString &scriptName, const QString &envVar)
{
    g_modelGenReason.clear();
    const QByteArray env = qgetenv(envVar.toLatin1().constData());
    if (!env.isEmpty() && QFileInfo::exists(QString::fromLocal8Bit(env))) {
        return QString::fromLocal8Bit(env);
    }

    QStringList candidates;
#ifdef VFP_TEST_SOURCE_DIR
    candidates << QStringLiteral(VFP_TEST_SOURCE_DIR "/tests/") + scriptName;
#endif
    // 兜底：从可执行文件向上找仓库根（build/bin/Release → 上三级）
    candidates << QCoreApplication::applicationDirPath() + QStringLiteral("/../../../tests/") + scriptName;
    QString script;
    for (const QString &c : candidates) {
        if (QFileInfo::exists(c)) {
            script = c;
            break;
        }
    }
    if (script.isEmpty()) {
        g_modelGenReason = QStringLiteral("未找到 tests/%1，跳过端到端用例").arg(scriptName);
        return QString();
    }

    static QTemporaryDir tmp;   // 进程内保持到测试结束；同一脚本多次调用共享同一模型
    if (!tmp.isValid()) {
        g_modelGenReason = QStringLiteral("无法创建临时目录，跳过端到端用例");
        return QString();
    }
    const QString modelPath = tmp.filePath(scriptName + QStringLiteral(".onnx"));
    if (QFileInfo::exists(modelPath)) {
        return modelPath;
    }
    QProcess py;
    py.setProcessChannelMode(QProcess::MergedChannels);
    py.start(QStringLiteral("python"), {script, modelPath});
    if (!py.waitForFinished(60000) || py.exitCode() != 0 || !QFileInfo::exists(modelPath)) {
        g_modelGenReason = QStringLiteral("python/onnx 生成失败，跳过端到端用例：%1")
                               .arg(QString::fromLocal8Bit(py.readAll()).trimmed());
        return QString();
    }
    return modelPath;
}

// 端到端（语义路径）：Conv3x3 均值 + Sigmoid 的最小模型 → 跑整条算子链 →
// 掩膜应等于"输入亮方块外扩 1 像素"（依赖 sigmoid + 阈值 ⟺ conv > 0 的等价关系）
void DnnSegmentTest::testEndToEndOnnxSemantic()
{
    const QString modelPath = ensureTestModel(QStringLiteral("gen_min_seg_onnx.py"),
                                              QStringLiteral("VFP_SEG_TEST_MODEL"));
    if (modelPath.isEmpty()) {
        QSKIP(qPrintable(g_modelGenReason));
    }

    // 64×64 黑底 + 20×20 白方块（10,10)-(29,29)：等比方图 → letterbox 不缩放不错位
    cv::Mat img(64, 64, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::rectangle(img, cv::Rect(10, 10, 20, 20), cv::Scalar(255, 255, 255), cv::FILLED);

    // 诊断前置 ①：确认 HALCON 图像桥接往返无损（不成立则问题在桥接，不在本算子）
    {
        const cv::Mat back = OpencvUtil::himageToMat(OpencvUtil::matToHimage(img));
        QVERIFY2(!back.empty() && back.type() == CV_8UC3, "图像桥接往返结果异常");
        QCOMPARE(int(back.at<cv::Vec3b>(2, 2)[0]), 0);
        QCOMPARE(int(back.at<cv::Vec3b>(25, 25)[0]), 255);   // 白方块范围 10..29
    }
    // 诊断前置 ②：直接用 cv::dnn 复现同一套预处理与推理，把数值带进断言消息（qWarning 不落盘）
    double netMin = 0, netMax = 0;
    int netFg = 0;
    {
        cv::dnn::Net probeNet = cv::dnn::readNetFromONNX(modelPath.toLocal8Bit().constData());
        QVERIFY2(!probeNet.empty(), "readNetFromONNX 加载失败");
        const cv::Mat blob = cv::dnn::blobFromImage(img, 1.0 / 255.0, cv::Size(64, 64),
                                                    cv::Scalar(0, 0, 0), true, false);
        probeNet.setInput(blob);
        const cv::Mat flat = probeNet.forward().reshape(1, 1);
        cv::minMaxLoc(flat, &netMin, &netMax);
        for (int i = 0; i < flat.cols; ++i)
            if (flat.at<float>(0, i) > 0.5f)
                ++netFg;
    }
    const cv::Mat nodeIn = OpencvUtil::himageToMat(OpencvUtil::matToHimage(img));
    const QString diag =
        QStringLiteral("网络输出 min=%1 max=%2 前景=%3/%4；原图均值=%5、桥接后均值=%6")
            .arg(netMin).arg(netMax).arg(netFg).arg(64 * 64)
            .arg(cv::mean(img)[0]).arg(cv::mean(nodeIn)[0]);

    DnnSegmentNode node;
    node.init();
    node.setParam(QStringLiteral("modelPath"), modelPath);
    node.setParam(QStringLiteral("inputWidth"), 64);
    node.setParam(QStringLiteral("inputHeight"), 64);
    node.setParam(QStringLiteral("keepRatio"), true);
    node.setInputImage(OpencvUtil::matToHimage(img));
    node.run();

    QVERIFY2(node.getParam(QStringLiteral("moduleStatus")).toBool(),
             qPrintable(node.getParam(QStringLiteral("lastError")).toString()));

    // 该模型图内已含 Sigmoid（输出值域 [0.5, 0.73]）→ 自动判据须识别为"已是概率，不再压 sigmoid"；
    // 否则背景 0.5 被二次压缩成 0.622 > 0.5 → 整幅变前景（本轮实测到的缺陷）
    QCOMPARE(node.getParam(QStringLiteral("sigmoidApplied")).toBool(), false);

    // ── 第一次运行（minArea = 0）：几何钉死 ──────────────────────────────────────────
    // 白方块 rows/cols 10..29（400 px），卷积核 3×3 → 前景 = 邻域触及方块的像素 = 9..30（22×22 = 484 px）。
    // 注意：模型输出是**概率**（含 Sigmoid），背景恰为 0.5，浮点误差会让极个别像素 > 0.5 成为离散前景点，
    // 故这里只钉"主体几何"，离散点交给 minArea（下一次运行）清理——这正是 minArea 存在的意义。
    const int area0 = node.getParam(QStringLiteral("maskArea")).toInt();
    HImage outObj(node.getOutputImage());
    const cv::Mat mask = OpencvUtil::himageToMat(outObj);
    QVERIFY(!mask.empty());
    QCOMPARE(mask.size(), img.size());
    QCOMPARE(int(mask.at<uchar>(25, 25)), 255);   // 白方块内（10..29）
    QCOMPARE(int(mask.at<uchar>(2, 2)), 0);       // 远处背景必须干净

    const cv::Rect expectRect(9, 9, 22, 22);
    const int inRect = cv::countNonZero(mask(expectRect));
    QVERIFY2(inRect >= 480,
             qPrintable(QStringLiteral("主体掩膜缺失：22×22 区域内仅 %1 像素；%2").arg(inRect).arg(diag)));
    QVERIFY2(area0 - inRect <= 16,
             qPrintable(QStringLiteral("背景离散点过多：总计 %1，区域内 %2").arg(area0).arg(inRect)));
    QVERIFY2(area0 > 300 && area0 < 900, qPrintable(QStringLiteral("掩膜面积异常：%1").arg(area0)));

    // ── 第二次运行（minArea = 50）：离散点被剔除 → 单连通域 + 框贴合 ────────────────
    node.setParam(QStringLiteral("minArea"), 50);
    node.run();
    QVERIFY2(node.getParam(QStringLiteral("moduleStatus")).toBool(),
             qPrintable(node.getParam(QStringLiteral("lastError")).toString()));
    const int area1 = node.getParam(QStringLiteral("maskArea")).toInt();
    QCOMPARE(node.getParam(QStringLiteral("segmentCount")).toInt(), 1);
    QVERIFY2(area1 >= 480 && area1 <= 500,
             qPrintable(QStringLiteral("minArea 清理后面积异常：%1").arg(area1)));

    // 端口 1/2/3：面积、数量、目标框（连通域框应贴合外扩后的方块 9..30）
    QCOMPARE(node.getOutputData(1)->getData().toInt(), area1);
    QCOMPARE(node.getOutputData(2)->getData().toInt(), 1);
    auto arr = node.getOutputData(3);
    QVERIFY(arr);
    const DetectionResult dr = arr->getDetectionResult();
    QCOMPARE(dr.imageWidth, 64);
    QCOMPARE(dr.imageHeight, 64);
    QCOMPARE(dr.boxes.size(), 1);
    QVERIFY2(dr.boxes[0].x >= 8 && dr.boxes[0].x <= 11,
             qPrintable(QStringLiteral("框 x 异常：%1").arg(dr.boxes[0].x)));
    QVERIFY2(dr.boxes[0].w >= 20 && dr.boxes[0].w <= 25,
             qPrintable(QStringLiteral("框宽异常：%1").arg(dr.boxes[0].w)));

    // 端口 4：叠加图（原图尺寸）
    auto vis = node.getOutputData(4);
    QVERIFY(vis);
    // 注意：图像必须走 getHImage()——setHImage 只写 m_hImage，getHObject() 是另一份（Region/XLD 用）
    const cv::Mat visMat = OpencvUtil::himageToMat(vis->getHImage());
    QVERIFY(!visMat.empty());
    QCOMPARE(visMat.size(), img.size());
}

// 端到端（letterbox 逆映射）：**非等比方图** —— 这一条专钉"先裁灰边再缩放"那段几何。
// 为什么需要它：等比方图（96×96 或 64×64 → 64×64）时 letterbox 是空转，灰边裁剪代码从未被执行，
// 一旦逆映射的裁剪/缩放写错，等比方图用例照样全绿——真模型上非等比方图（绝大多数相机画面）就会整体错位。
void DnnSegmentTest::testEndToEndLetterboxNonSquare()
{
    const QString modelPath = ensureTestModel(QStringLiteral("gen_min_seg_onnx.py"),
                                              QStringLiteral("VFP_SEG_TEST_MODEL"));
    if (modelPath.isEmpty()) {
        QSKIP(qPrintable(g_modelGenReason));
    }

    // 96×64（宽>高，比例 3:2）黑底 + 20×20 白方块（行 10..29、列 10..29）
    // 预处理：s = min(64/96, 64/64) = 2/3 → 缩放为 64×43，上边距 (64-43)/2 = 10 px
    cv::Mat img(64, 96, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::rectangle(img, cv::Rect(10, 10, 20, 20), cv::Scalar(255, 255, 255), cv::FILLED);

    DnnSegmentNode node;
    node.init();
    node.setParam(QStringLiteral("modelPath"), modelPath);
    node.setParam(QStringLiteral("inputWidth"), 64);
    node.setParam(QStringLiteral("inputHeight"), 64);
    node.setParam(QStringLiteral("keepRatio"), true);
    node.setInputImage(OpencvUtil::matToHimage(img));

    // 三种配置各跑一次，先把数据都拿到手：任一配置出错时，断言消息里能同时看到对照值
    struct Probe {
        cv::Mat mask;
        int area = 0;
        int firstRow = 0;    // 掩膜第 0 行的前景像素数（灰边污染带的可观测特征）
        cv::Rect bbox;
        double cx = 0.0, cy = 0.0;
        bool ok = false;
    };
    auto runOnce = [&](bool keepRatio, int padTrim) {
        node.setParam(QStringLiteral("keepRatio"), keepRatio);
        node.setParam(QStringLiteral("padTrim"), padTrim);
        node.run();
        Probe p;
        p.ok = node.getParam(QStringLiteral("moduleStatus")).toBool();
        p.mask = OpencvUtil::himageToMat(HImage(node.getOutputImage()));
        p.area = node.getParam(QStringLiteral("maskArea")).toInt();
        if (!p.mask.empty()) {
            const cv::Moments mm = cv::moments(p.mask, true);
            if (mm.m00 > 0) {
                p.cx = mm.m10 / mm.m00;
                p.cy = mm.m01 / mm.m00;
                p.bbox = cv::boundingRect(p.mask);
            }
            p.firstRow = cv::countNonZero(p.mask.row(0));
        }
        return p;
    };
    auto describe = [](const QString &tag, const Probe &p) {
        return QStringLiteral("%1{ok=%2 面积=%3 框=(%4,%5,%6,%7) 首行=%8 重心=(%9,%10)}")
            .arg(tag).arg(p.ok ? 1 : 0).arg(p.area)
            .arg(p.bbox.x).arg(p.bbox.y).arg(p.bbox.width).arg(p.bbox.height)
            .arg(p.firstRow).arg(p.cx, 0, 'f', 1).arg(p.cy, 0, 'f', 1);
    };

    const Probe dirty = runOnce(true, 0);    // letterbox + 不清理：应复现"灰边污染带"
    const Probe lb = runOnce(true, 2);       // letterbox + 清理 2 像素
    const Probe st = runOnce(false, 1);      // 默认姿势：拉伸（无灰边）
    const QString info = describe(QStringLiteral("letterbox/padTrim=0"), dirty)
                         + QStringLiteral(" | ") + describe(QStringLiteral("letterbox/padTrim=2"), lb)
                         + QStringLiteral(" | ") + describe(QStringLiteral("拉伸"), st);

    // ① 先钉住这个坑真实存在（本用例存在的理由）：灰边被判成前景后，污染带贯通整幅 →
    //    首行即前景、外接框吞掉整幅图。若哪天这条不再成立，说明模型/预处理已换，需重新评估 padTrim。
    QVERIFY2(dirty.ok && dirty.firstRow > 50
                 && dirty.bbox == cv::Rect(0, 0, img.cols, img.rows),
             qPrintable(QStringLiteral("未能复现灰边污染带（padTrim=0）：%1").arg(info)));

    // ② letterbox + padTrim=2：污染带被清掉，几何正确
    QVERIFY2(lb.ok && !lb.mask.empty(), qPrintable(QStringLiteral("letterbox 未成功：%1").arg(info)));
    QCOMPARE(lb.mask.size(), img.size());
    QVERIFY2(lb.firstRow == 0, qPrintable(QStringLiteral("污染带未清干净：%1").arg(info)));
    QVERIFY2(qAbs(lb.cx - 20.0) <= 3.0 && qAbs(lb.cy - 20.0) <= 3.0,
             qPrintable(QStringLiteral("letterbox 重心偏差：%1").arg(info)));
    QVERIFY2(lb.area > 350 && lb.area < 700,
             qPrintable(QStringLiteral("letterbox 面积异常：%1").arg(info)));
    QVERIFY2(cv::countNonZero(lb.mask(cv::Rect(0, 0, 5, 5))) == 0
                 && cv::countNonZero(lb.mask(cv::Rect(88, 56, 8, 8))) == 0,
             qPrintable(QStringLiteral("letterbox 边角残留前景：%1").arg(info)));

    // ③ 默认姿势（拉伸，无灰边）：几何"拉伸 → 反拉伸"，重心同样应回到 (20,20)
    QVERIFY2(st.ok, qPrintable(QStringLiteral("拉伸模式未成功：%1").arg(info)));
    QVERIFY2(qAbs(st.cx - 20.0) <= 3.0 && qAbs(st.cy - 20.0) <= 3.0,
             qPrintable(QStringLiteral("拉伸模式重心偏差：%1").arg(info)));
    QVERIFY2(st.area > 350 && st.area < 700,
             qPrintable(QStringLiteral("拉伸模式面积异常：%1").arg(info)));
}

// 端到端（实例路径）：YOLO-Seg 双输出形态（框+类分数+掩膜系数 / 掩膜原型）→
// 钉住"模式自动识别 + 系数·原型掩膜 + 按框裁剪贴回 + 类别/置信度随框输出"整条链路
void DnnSegmentTest::testEndToEndOnnxInstance()
{
    const QString modelPath = ensureTestModel(QStringLiteral("gen_min_seg_inst_onnx.py"),
                                              QStringLiteral("VFP_SEG_TEST_INST_MODEL"));
    if (modelPath.isEmpty()) {
        QSKIP(qPrintable(g_modelGenReason));
    }

    // 64×64 黑底 + 白方块 10..29；模型原型通道 0 = 3×3 均值 → 亮区外扩 1 像素 = 9..30
    cv::Mat img(64, 64, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::rectangle(img, cv::Rect(10, 10, 20, 20), cv::Scalar(255, 255, 255), cv::FILLED);

    DnnSegmentNode node;
    node.init();
    node.setParam(QStringLiteral("modelPath"), modelPath);
    node.setParam(QStringLiteral("inputWidth"), 64);
    node.setParam(QStringLiteral("inputHeight"), 64);
    node.setParam(QStringLiteral("segMode"), 0);      // 自动：应为实例分割（4D 原型 + 3D 系数）
    node.setInputImage(OpencvUtil::matToHimage(img));
    node.run();
    QVERIFY2(node.getParam(QStringLiteral("moduleStatus")).toBool(),
             qPrintable(node.getParam(QStringLiteral("lastError")).toString()));

    // 掩膜 = 亮区(9..30) ∩ 框(24..39) = 24..30 → 7×7 ≈ 49 像素（留少量浮点边界余量）
    const int area = node.getParam(QStringLiteral("maskArea")).toInt();
    QVERIFY2(area >= 45 && area <= 60,
             qPrintable(QStringLiteral("实例掩膜面积异常：%1（应≈49）").arg(area)));
    QCOMPARE(node.getParam(QStringLiteral("segmentCount")).toInt(), 1);   // 低置信滤掉 + 重叠框 NMS 合并

    const cv::Mat mask = OpencvUtil::himageToMat(node.getOutputImage());
    QVERIFY(!mask.empty());
    QCOMPARE(cv::countNonZero(mask), area);
    QCOMPARE(int(mask.at<uchar>(27, 27)), 255);   // 框内 ∩ 亮区
    QCOMPARE(int(mask.at<uchar>(12, 12)), 0);     // 亮区但**框外** → 必须被裁掉（钉住按框裁剪）
    QCOMPARE(int(mask.at<uchar>(2, 2)), 0);       // 远处背景

    // 目标框：类别与置信度来自 out0（实例模式），几何由框裁剪后的交集决定
    auto arr = node.getOutputData(3);
    QVERIFY(arr);
    const DetectionResult dr = arr->getDetectionResult();
    QCOMPARE(dr.boxes.size(), 1);
    QCOMPARE(dr.boxes[0].classId, 0);
    QVERIFY2(qAbs(dr.boxes[0].confidence - 0.90) < 1e-5,
             qPrintable(QStringLiteral("置信度异常：%1").arg(dr.boxes[0].confidence)));
    QCOMPARE(dr.boxes[0].x, 24.0);
    QCOMPARE(dr.boxes[0].y, 24.0);
    QCOMPARE(dr.boxes[0].w, 16.0);
    QCOMPARE(dr.boxes[0].h, 16.0);

    // 叠加图必须真的有内容（8UC3、原图尺寸）
    auto vis = node.getOutputData(4);
    QVERIFY(vis);
    const cv::Mat visMat = OpencvUtil::himageToMat(vis->getHImage());
    QVERIFY(!visMat.empty());
    QCOMPARE(visMat.size(), img.size());
}

QTEST_MAIN(DnnSegmentTest)
#include "dnn_segment_test.moc"
