// ONNX 目标检测功能测试（🥈：YOLO 输出解析 / NMS / 坐标映射 + 序列化 + 缺模型降级）
// 不依赖真实模型：解析逻辑用合成张量验证；模型缺失时节点应优雅降级。
#include <QtTest/QtTest>
#include <QObject>
#include <QVector>
#include <QJsonObject>
#include <QString>

#include "DnnDetectNode.h"
#include "DataObject.h"
#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>

class DnnDetectTest : public QObject
{
    Q_OBJECT
private slots:
    void testParseYoloBasic();      // 2D [N,C]：重叠框 NMS 合并 + 低置信过滤
    void testParseYoloV5Layout();   // 3D [1,N,C]（v5 布局）
    void testParseYoloV8Layout();   // 3D [1,C,N]（v8 布局，需转置）
    void testParseYoloLetterbox();  // letterbox 坐标映射（原图≠输入尺寸）
    void testSerialization();
    void testMissingModel();
};

// 在 2D [N,C] 矩阵中写入一个框（C = 4 + numClasses）
static void fillBox(cv::Mat &m, int row, float cx, float cy, float w, float h,
                   float s0, float s1)
{
    float *r = m.ptr<float>(row);
    r[0] = cx; r[1] = cy; r[2] = w; r[3] = h; r[4] = s0; r[5] = s1;
}

// 验证：1 个框、类别 0、置信≈0.9、左上角 (270,270)、宽高 100
static void assertSingleBox(const QVector<DetectionBox> &boxes, double conf)
{
    QCOMPARE(boxes.size(), 1);
    QCOMPARE(boxes[0].classId, 0);
    QVERIFY2(qAbs(boxes[0].confidence - conf) < 1e-3,
             qPrintable(QString("置信应为 %1，实际 %2").arg(conf).arg(boxes[0].confidence)));
    QCOMPARE(boxes[0].x, 270.0);
    QCOMPARE(boxes[0].y, 270.0);
    QCOMPARE(boxes[0].w, 100.0);
    QCOMPARE(boxes[0].h, 100.0);
}

void DnnDetectTest::testParseYoloBasic()
{
    // 3 个框：A(0.9)/B(0.85) 完全重合、C(0.1) 低置信；期望 NMS 后仅 A
    cv::Mat m(3, 6, CV_32F);
    fillBox(m, 0, 320, 320, 100, 100, 0.9f, 0.1f);
    fillBox(m, 1, 320, 320, 100, 100, 0.85f, 0.1f);
    fillBox(m, 2, 320, 320, 100, 100, 0.1f, 0.9f);
    QVector<DetectionBox> boxes;
    QVERIFY(DnnDetectNode::parseYoloOutput(m, 640, cv::Size(640, 640), 0.25, 0.45, boxes));
    assertSingleBox(boxes, 0.9);
}

void DnnDetectTest::testParseYoloV5Layout()
{
    // v5：[1, N, C]，N=100（仅前 3 行有效）
    int sz[] = {1, 100, 6};
    cv::Mat out(3, sz, CV_32F);
    for (int n = 0; n < 3; ++n)
        for (int c = 0; c < 6; ++c)
            out.at<float>(0, n, c) = 0.f;
    out.at<float>(0, 0, 0) = 320; out.at<float>(0, 0, 1) = 320;
    out.at<float>(0, 0, 2) = 100; out.at<float>(0, 0, 3) = 100;
    out.at<float>(0, 0, 4) = 0.9f; out.at<float>(0, 0, 5) = 0.1f;
    out.at<float>(0, 1, 0) = 320; out.at<float>(0, 1, 1) = 320;
    out.at<float>(0, 1, 2) = 100; out.at<float>(0, 1, 3) = 100;
    out.at<float>(0, 1, 4) = 0.85f; out.at<float>(0, 1, 5) = 0.1f;
    out.at<float>(0, 2, 0) = 320; out.at<float>(0, 2, 1) = 320;
    out.at<float>(0, 2, 2) = 100; out.at<float>(0, 2, 3) = 100;
    out.at<float>(0, 2, 4) = 0.1f; out.at<float>(0, 2, 5) = 0.9f;
    QVector<DetectionBox> boxes;
    QVERIFY(DnnDetectNode::parseYoloOutput(out, 640, cv::Size(640, 640), 0.25, 0.45, boxes));
    assertSingleBox(boxes, 0.9);
}

void DnnDetectTest::testParseYoloV8Layout()
{
    // v8：[1, C, N]，C=6，N=100（仅 n=0 有效），需转置还原为 [N,C]
    int sz[] = {1, 6, 100};
    cv::Mat out(3, sz, CV_32F);
    for (int c = 0; c < 6; ++c)
        for (int n = 0; n < 100; ++n)
            out.at<float>(0, c, n) = 0.f;
    // 框 A
    out.at<float>(0, 0, 0) = 320; out.at<float>(0, 1, 0) = 320;
    out.at<float>(0, 2, 0) = 100; out.at<float>(0, 3, 0) = 100;
    out.at<float>(0, 4, 0) = 0.9f; out.at<float>(0, 5, 0) = 0.1f;
    // 框 B（重叠、低一档）
    out.at<float>(0, 0, 1) = 320; out.at<float>(0, 1, 1) = 320;
    out.at<float>(0, 2, 1) = 100; out.at<float>(0, 3, 1) = 100;
    out.at<float>(0, 4, 1) = 0.85f; out.at<float>(0, 5, 1) = 0.1f;
    // 框 C（低置信）
    out.at<float>(0, 0, 2) = 320; out.at<float>(0, 1, 2) = 320;
    out.at<float>(0, 2, 2) = 100; out.at<float>(0, 3, 2) = 100;
    out.at<float>(0, 4, 2) = 0.1f; out.at<float>(0, 5, 2) = 0.9f;
    QVector<DetectionBox> boxes;
    QVERIFY(DnnDetectNode::parseYoloOutput(out, 640, cv::Size(640, 640), 0.25, 0.45, boxes));
    assertSingleBox(boxes, 0.9);
}

void DnnDetectTest::testParseYoloLetterbox()
{
    // 原图 320x240，输入 640：scale=2，padY=80；中心 (320,320) w/h=100 应映射回 (135,95,50,50)
    cv::Mat m(1, 6, CV_32F);
    fillBox(m, 0, 320, 320, 100, 100, 0.9f, 0.1f);
    QVector<DetectionBox> boxes;
    QVERIFY(DnnDetectNode::parseYoloOutput(m, 640, cv::Size(320, 240), 0.25, 0.45, boxes));
    QCOMPARE(boxes.size(), 1);
    QCOMPARE(boxes[0].x, 135.0);
    QCOMPARE(boxes[0].y, 95.0);
    QCOMPARE(boxes[0].w, 50.0);
    QCOMPARE(boxes[0].h, 50.0);
}

void DnnDetectTest::testSerialization()
{
    DnnDetectNode n1;
    n1.init();
    n1.setParam(QStringLiteral("modelPath"), QStringLiteral("/models/yolov8.onnx"));
    n1.setParam(QStringLiteral("inputSize"), 320);
    n1.setParam(QStringLiteral("confThresh"), 0.5);
    n1.setParam(QStringLiteral("nmsThresh"), 0.3);
    QJsonObject j = n1.toJson();

    DnnDetectNode n2;
    n2.init();
    n2.fromJson(j);
    QCOMPARE(n2.getParam(QStringLiteral("modelPath")).toString(),
             QStringLiteral("/models/yolov8.onnx"));
    QCOMPARE(n2.getParam(QStringLiteral("inputSize")).toInt(), 320);
    QCOMPARE(n2.getParam(QStringLiteral("confThresh")).toDouble(), 0.5);
    QCOMPARE(n2.getParam(QStringLiteral("nmsThresh")).toDouble(), 0.3);
}

void DnnDetectTest::testMissingModel()
{
    DnnDetectNode node;
    node.init();
    node.run();   // 未设模型路径、未设输入图像 -> 应优雅降级
    QVERIFY2(!node.getParam(QStringLiteral("moduleStatus")).toBool(),
             "缺模型时 moduleStatus 应为 false");
    QCOMPARE(node.getParam(QStringLiteral("detCount")).toInt(), 0);
}

QTEST_MAIN(DnnDetectTest)
#include "dnn_detect_test.moc"
