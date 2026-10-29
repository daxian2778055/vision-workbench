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
    void reverseThenForwardCountsOnce();
    void differentClassesDoNotStealIdentity();
    void nodeReportsMissingModel();
    void nodeReportsDegenerateLine();
    void nodeResetsCounterOnRequest();
    void yoloLayoutV8AndV5BothDecode();
    void dnnParamsAreRegistered();
    void yoloBlobRejectsZeroScaleAndLettersboxes();

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

void ObjectCountingTest::reverseThenForwardCountsOnce()
{
    // P2-2（2026-10-28）：方向过滤不得把"被过滤的穿越"写进已计集合。
    // 回流/返工场景：目标先反向穿一次（被过滤），再正向穿回——必须计 1。
    LineCounter counter({200, 0}, {200, 400});
    ObjectTracker tracker;
    int events = 0;
    // 只接受正向（负侧→正侧）
    auto isConfirmed = [&](const ObjectTrack &t) { return tracker.confirmed(t); };

    // 1) 先反向：从正侧走到负侧（250→150）——被过滤，不计，也不得留痕迹
    for (int x = 250; x >= 150; x -= 10) {
        const auto tracks = tracker.update({makeBox(float(x), 200)});
        events += counter.update(tracks, false, isConfirmed, +1).size();
    }
    QCOMPARE(events, 0);
    // 2) 再正向：从负侧走回正侧（150→250）——应当计 1
    for (int x = 150; x <= 250; x += 10) {
        const auto tracks = tracker.update({makeBox(float(x), 200)});
        events += counter.update(tracks, false, isConfirmed, +1).size();
    }
    QCOMPARE(events, 1);
}

void ObjectCountingTest::differentClassesDoNotStealIdentity()
{
    // 2026-10-28 复核 P3：两个不同类别的框高度重叠时不得互相"吃掉"身份，
    // 否则 className 被改写、按类计数会串。
    ObjectTracker tracker;
    tracker.update({makeBox(100, 100, 40, 40, QStringLiteral("A"))});
    const auto tracks = tracker.update({makeBox(101, 100, 40, 40, QStringLiteral("B"))});
    // 不匹配 → 原轨迹 miss、新轨迹诞生：总数应为 2，且 A 的名字没被改写
    QCOMPARE(tracks.size(), 2);
    for (const auto &t : tracks) {
        if (t.hits == 1 && t.missed == 0) {
            // 新建的这条
            QCOMPARE(t.className, QStringLiteral("B"));
        } else if (t.missed > 0) {
            // 旧的这条
            QCOMPARE(t.className, QStringLiteral("A"));
        }
    }
}

void ObjectCountingTest::dnnParamsAreRegistered()
{
    // r14 复核 P1-7 / P1-8：登记是"能不能用"的一半——run 读了没登记的键，缺省值会把推理带偏。
    // 计数节点：scale 缺省被 toDouble() 读成 0 ⇒ 全零 blob；检测节点：两个枚举落在列表外，
    // 面板上没有「推理后端 / 输出布局」。
    ObjectCountingNode counting;
    counting.init();
    QStringList have;
    for (const ParamSpec &sp : counting.paramSpecs())
        have << sp.name;
    const QStringList want{QStringLiteral("scale"), QStringLiteral("swapRB"),
                           QStringLiteral("mean"), QStringLiteral("dnnBackend"),
                           QStringLiteral("yoloLayout")};
    for (const QString &k : want)
        QVERIFY2(have.contains(k),
                 qPrintable(QStringLiteral("计数节点缺登记参数 %1（现有：%2）")
                                .arg(k, have.join(QLatin1Char(',')))));

    // 缺省值必须"能真跑"：scale 不能是 0
    QVERIFY2(counting.getParam(QStringLiteral("scale")).toDouble() > 0.0,
             "scale 缺省为 0：送进网络的是全零 blob（P1-7 的现场症状）");
    QCOMPARE(counting.getParam(QStringLiteral("swapRB")).toBool(), true);
    QCOMPARE(counting.getParam(QStringLiteral("mean")).toString(), QStringLiteral("0,0,0"));

    DnnDetectNode detect;
    detect.init();
    QStringList dhave;
    for (const ParamSpec &sp : detect.paramSpecs())
        dhave << sp.name;
    QVERIFY2(dhave.contains(QStringLiteral("dnnBackend")),
             qPrintable(QStringLiteral("检测节点缺 dnnBackend（P1-8，现有：%1）")
                            .arg(dhave.join(QLatin1Char(',')))));
    QVERIFY2(dhave.contains(QStringLiteral("yoloLayout")),
             "检测节点缺 yoloLayout：YOLOX/v5 模型在检测节点上无法选对布局");
}

void ObjectCountingTest::yoloBlobRejectsZeroScaleAndLettersboxes()
{
    // ① 根因形态：scale=0 必须拒绝（blobFromImage 会给出全零 blob，现场表现为"成功但计数 0"）
    cv::Mat img(48, 64, CV_8UC3, cv::Scalar(10, 20, 30));   // 非方图，顺带验 letterbox
    cv::Mat blob;
    QVERIFY2(!DnnDetectNode::yoloBlob(img, 64, 0.0, true, cv::Scalar(0, 0, 0), &blob),
             "scale=0 被接受了：会得到全零 blob 却仍报成功");

    // ② 正常路径：形状 1x3x64x64、非零、灰边(114)在
    QVERIFY(DnnDetectNode::yoloBlob(img, 64, 1.0 / 255.0, true, cv::Scalar(0, 0, 0), &blob));
    QCOMPARE(blob.dims, 4);
    QCOMPARE(blob.size[1], 3);
    QCOMPARE(blob.size[2], 64);
    QCOMPARE(blob.size[3], 64);
    double mn = 0.0, mx = 0.0;
    cv::minMaxLoc(blob.reshape(1, 1), &mn, &mx);
    QVERIFY2(mx > 0.0, "blob 全零：前处理参数没生效");
    // 64x48 放进 64x64：上下各 8 行灰边 114 ⇒ 左上角像素应贴近 114/255
    const cv::Mat flat = blob.reshape(1, 1);
    const float corner = flat.at<float>(0, 0);   // 首元素属于灰边
    QVERIFY2(qAbs(corner - 114.0f / 255.0f) < 0.02f,
             qPrintable(QStringLiteral("letterbox 灰边缺失（corner=%1）：几何与反算不一致")
                            .arg(corner)));
}

QTEST_MAIN(ObjectCountingTest)
#include "object_counting_test.moc"
