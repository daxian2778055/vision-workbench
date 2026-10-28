// 流水线计数核心（2026-10-28）：跟踪 ID 稳定性 + 过线去重。
// 这几条腿直接决定"数得对不对"：漏检后 ID 不能断（否则重复计），抖动不能计两次，
// 反向要能区分，久未跟上的轨迹要丢弃。
#include "ObjectTracking.h"
#include "ObjectCountingNode.h"
#include "OpencvUtil.h"
#include "DnnDetectNode.h"

#include <QtTest/QtTest>
#include <cstring>

class ObjectCountingTest : public QObject
{
    Q_OBJECT
private slots:
    void trackerKeepsIdAcrossMissingFrames();
    void trackerDropsStaleTracks();
    void crossingCountsOncePerTrack();
    void jitterNearLineDoesNotCount();
    void reverseDirectionIsReported();
    void unconfirmedTracksDoNotCount();
    void nodeReportsMissingModel();
    void nodeReportsDegenerateLine();
    void nodeResetsCounterOnRequest();
    void yoloLayoutV8AndV5BothDecode();

private:
    static DetectionBox makeBox(float cx, float cy, float w = 40, float h = 40,
                                const QString &cls = QStringLiteral("part"));
};

DetectionBox ObjectCountingTest::makeBox(float cx, float cy, float w, float h, const QString &cls)
{
    DetectionBox b;
    b.x = cx - w * 0.5f;
    b.y = cy - h * 0.5f;
    b.w = w;
    b.h = h;
    b.className = cls;
    b.confidence = 0.9;
    return b;
}

void ObjectCountingTest::trackerKeepsIdAcrossMissingFrames()
{
    ObjectTracker tracker;
    const int id0 = tracker.update({makeBox(100, 100)})[0].id;
    tracker.update({makeBox(104, 100)});
    tracker.update({});                       // 漏检一帧
    const auto tracks = tracker.update({makeBox(112, 100)});
    QCOMPARE(tracks.size(), 1);
    QCOMPARE(tracks[0].id, id0);              // ID 必须延续
    QVERIFY(tracks[0].hits >= 3);
}

void ObjectCountingTest::trackerDropsStaleTracks()
{
    ObjectTracker::Config cfg;
    cfg.maxMissed = 2;
    ObjectTracker tracker(cfg);
    tracker.update({makeBox(100, 100)});
    for (int i = 0; i < 3; ++i)
        tracker.update({});                   // 超过 maxMissed
    QVERIFY2(tracker.tracks().isEmpty(), "久未跟上的轨迹没有丢弃");
}

void ObjectCountingTest::crossingCountsOncePerTrack()
{
    // 竖直线 x=200；物体从 x=150 走到 x=250，穿一次只计一次
    LineCounter counter({200, 0}, {200, 400});
    ObjectTracker tracker;
    int events = 0;
    for (int x = 150; x <= 250; x += 10) {
        const auto tracks = tracker.update({makeBox(float(x), 200)});
        const auto ev = counter.update(tracks, false, {});
        events += ev.size();
    }
    QCOMPARE(events, 1);
}

void ObjectCountingTest::jitterNearLineDoesNotCount()
{
    // 贴着线抖动（±0.8px 容差内）：不能计
    LineCounter counter({200, 0}, {200, 400});
    ObjectTracker tracker;
    int events = 0;
    const float xs[] = {199.2f, 200.4f, 199.6f, 200.2f, 199.8f};
    for (float x : xs) {
        const auto tracks = tracker.update({makeBox(x, 200)});
        events += counter.update(tracks, false, {}).size();
    }
    QCOMPARE(events, 0);
}

void ObjectCountingTest::reverseDirectionIsReported()
{
    LineCounter counter({200, 0}, {200, 400});
    ObjectTracker tracker;
    QVector<LineCounter::Event> all;
    for (int x = 250; x >= 150; x -= 10) {
        const auto tracks = tracker.update({makeBox(float(x), 200)});
        all += counter.update(tracks, false, {});
    }
    QCOMPARE(all.size(), 1);
    QCOMPARE(all[0].direction, -1);   // 正侧 → 负侧
}

void ObjectCountingTest::unconfirmedTracksDoNotCount()
{
    // 只出现 1 帧就消失的"闪现目标"不得计入（confirmedOnly=true）
    ObjectTracker::Config cfg;
    cfg.minHits = 3;
    ObjectTracker tracker(cfg);
    LineCounter counter({200, 0}, {200, 400});
    int events = 0;
    // 左侧出现一帧
    auto tracks = tracker.update({makeBox(150, 200)});
    events += counter.update(tracks, true, [&](const ObjectTrack &t) {
                  return tracker.confirmed(t);
              }).size();
    // 右侧出现一帧（不同位置 → 新 ID，且都不确认）
    tracks = tracker.update({makeBox(260, 200)});
    events += counter.update(tracks, true, [&](const ObjectTrack &t) {
                  return tracker.confirmed(t);
              }).size();
    QCOMPARE(events, 0);
}

void ObjectCountingTest::nodeReportsMissingModel()
{
    ObjectCountingNode node;
    node.init();
    cv::Mat img(120, 160, CV_8UC1, cv::Scalar(128));
    node.setInputImage(OpencvUtil::matToHimage(img));
    node.run(false);
    QVERIFY2(!node.getParam(QStringLiteral("moduleStatus")).toBool(), "没配模型却判绿");
    QVERIFY2(!node.getParam(QStringLiteral("lastError")).toString().isEmpty(),
             "没配模型却没有可读原因");
}

void ObjectCountingTest::nodeReportsDegenerateLine()
{
    ObjectCountingNode node;
    node.init();
    // 退化线：A==B
    node.setParam(QStringLiteral("lineA_x"), 100);
    node.setParam(QStringLiteral("lineA_y"), 100);
    node.setParam(QStringLiteral("lineB_x"), 100);
    node.setParam(QStringLiteral("lineB_y"), 100);
    cv::Mat img(120, 160, CV_8UC1, cv::Scalar(128));
    node.setInputImage(OpencvUtil::matToHimage(img));
    node.run(false);
    QVERIFY2(!node.getParam(QStringLiteral("moduleStatus")).toBool(), "退化线却判绿");
    QVERIFY2(node.getParam(QStringLiteral("lastError")).toString().contains(QStringLiteral("计数线")),
             qPrintable(node.getParam(QStringLiteral("lastError")).toString()));
}

void ObjectCountingTest::nodeResetsCounterOnRequest()
{
    // resetCounter 参数是一次性的：置 true 后本次运行前清零并自动落回 false
    ObjectCountingNode node;
    node.init();
    node.setParam(QStringLiteral("resetCounter"), true);
    node.setParam(QStringLiteral("modelPath"), QString());
    cv::Mat img(60, 60, CV_8UC1, cv::Scalar(0));
    node.setInputImage(OpencvUtil::matToHimage(img));
    node.run(false);
    QVERIFY2(!node.getParam(QStringLiteral("resetCounter")).toBool(),
             "resetCounter 用完没有复位（下次运行会再次清零）");
}

void ObjectCountingTest::yoloLayoutV8AndV5BothDecode()
{
    // 两种输出布局（2026-10-28）：v8/v11 = [N, 4+类数]；v5/YOLOX = [N, 5+类数]（第 4 列 objectness）。
    // 选错布局的现场表现是"框对了、类别全错"，所以正反两条都要钉住。
    const int inputSize = 64;
    const cv::Size orig(64, 64);   // 与原图相同 ⇒ letterbox 恒等，坐标可直读
    const int nc = 3;

    // v8：一行一个框，[cx,cy,w,h, cls0,cls1,cls2]
    cv::Mat v8(1, 1, CV_32FC(4 + nc));
    {
        float *p = v8.ptr<float>(0);
        const float row[4 + 3] = {32.f, 32.f, 16.f, 16.f, 0.10f, 0.85f, 0.05f};
        std::memcpy(p, row, sizeof(row));
    }
    v8 = v8.reshape(1, 1);   // [1, 7]
    QVector<DetectionBox> b8;
    QVERIFY(DnnDetectNode::parseYoloOutput(v8, inputSize, orig, 0.25, 0.45, b8, /*layout=*/0));
    QCOMPARE(b8.size(), 1);
    QCOMPARE(b8[0].classId, 1);                     // 2 号列（0-based 1）= 最高分
    QVERIFY(qAbs(b8[0].confidence - 0.85) < 1e-4);

    // v5/YOLOX：[cx,cy,w,h, obj, cls0,cls1,cls2]
    cv::Mat v5(1, 1, CV_32FC(5 + nc));
    {
        float *p = v5.ptr<float>(0);
        const float row[5 + 3] = {32.f, 32.f, 16.f, 16.f, 0.90f, 0.10f, 0.80f, 0.05f};
        std::memcpy(p, row, sizeof(row));
    }
    v5 = v5.reshape(1, 1);
    QVector<DetectionBox> b5;
    QVERIFY(DnnDetectNode::parseYoloOutput(v5, inputSize, orig, 0.25, 0.45, b5, /*layout=*/1));
    QCOMPARE(b5.size(), 1);
    QCOMPARE(b5[0].classId, 1);                     // 与 v8 同一类别（不被 objectness 顶掉）
    QVERIFY2(qAbs(b5[0].confidence - 0.90 * 0.80) < 1e-4,
             qPrintable(QStringLiteral("v5 分数应为 obj×cls，实得 %1").arg(b5[0].confidence)));

    // 负对照：把 v5 张量按 v8 解 → 类别必然错（objectness 被当成第 0 类分数）
    QVector<DetectionBox> wrong;
    QVERIFY(DnnDetectNode::parseYoloOutput(v5, inputSize, orig, 0.25, 0.45, wrong, /*layout=*/0));
    QVERIFY2(!wrong.isEmpty() && wrong[0].classId == 0,
             "按 v8 解 v5 张量却没有错位：负对照失效（布局参数可能没生效）");
}

QTEST_MAIN(ObjectCountingTest)
#include "object_counting_test.moc"
