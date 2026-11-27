// LatestFrameQueueTest（AG4a）：最新帧队列的纯数据契约。
// 不依赖 MVS SDK / 相机 / UI，可在无相机环境下跑；为后续采集线程化提供可验收的语义。
#include <QtTest/QtTest>
#include "LatestFrameQueue.h"

class LatestFrameQueueTest : public QObject
{
    Q_OBJECT
private slots:
    void testLatestWinsAndCounters();
    void testFrameNumberGapsAndDuplicates();
    void testSdkLostPacketsSeparateFromLocalReplacement();
    void testCapacityTwoDropsOlderOnTake();
};

static LatestFrame makeFrame(quint64 seq, int value)
{
    LatestFrame f;
    f.sequence = seq;
    f.bytes = QByteArray(4, char(value));
    f.width = 640;
    f.height = 480;
    f.pixelType = 1;
    f.timestampMs = qint64(seq) * 10;
    return f;
}

void LatestFrameQueueTest::testLatestWinsAndCounters()
{
    LatestFrameQueue q(1);
    for (quint64 i = 1; i <= 5; ++i)
        q.push(makeFrame(i, int(i)));
    LatestFrame got;
    QVERIFY(q.takeLatest(&got));
    QCOMPARE(got.sequence, quint64(5));
    QCOMPARE(got.bytes.at(0), char(5));
    QVERIFY(!q.takeLatest(&got));   // 单槽已经取空
    const LatestFrameQueue::Stats s = q.stats();
    QCOMPARE(s.pushed, quint64(5));
    QCOMPARE(s.replaced, quint64(4));
    QCOMPARE(s.consumed, quint64(1));
    QCOMPARE(s.gaps, quint64(0));
}

void LatestFrameQueueTest::testFrameNumberGapsAndDuplicates()
{
    LatestFrameQueue q;
    q.push(makeFrame(10, 1));
    q.push(makeFrame(13, 2));   // 缺 11、12
    q.push(makeFrame(13, 3));   // 重复帧号
    const LatestFrameQueue::Stats s = q.stats();
    QCOMPARE(s.gaps, quint64(2));
    QCOMPARE(s.duplicates, quint64(1));
}

void LatestFrameQueueTest::testSdkLostPacketsSeparateFromLocalReplacement()
{
    LatestFrameQueue q;
    q.push(makeFrame(1, 1));
    q.push(makeFrame(2, 2));
    q.noteSdkLostPackets(7);
    const LatestFrameQueue::Stats s = q.stats();
    QCOMPARE(s.replaced, quint64(1));   // 本地覆盖
    QCOMPARE(s.sdkLostPackets, quint64(7)); // SDK 丢包不得混进本地计数
    QCOMPARE(s.gaps, quint64(0));
}

void LatestFrameQueueTest::testCapacityTwoDropsOlderOnTake()
{
    LatestFrameQueue q(2);
    q.push(makeFrame(1, 1));
    q.push(makeFrame(2, 2));
    q.push(makeFrame(3, 3));   // 槽满，覆盖最旧的第 1 帧
    LatestFrame got;
    QVERIFY(q.takeLatest(&got));
    QCOMPARE(got.sequence, quint64(3));
    QVERIFY(!q.takeLatest(&got));  // 第 2 帧随取最新一并作废
    const LatestFrameQueue::Stats s = q.stats();
    QCOMPARE(s.replaced, quint64(2));
    QCOMPARE(s.consumed, quint64(1));
}

QTEST_MAIN(LatestFrameQueueTest)
#include "latest_frame_queue_test.moc"
