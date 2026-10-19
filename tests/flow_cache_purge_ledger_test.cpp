// FlowCachePurgeLedgerTest（180 行词条切片 A）：缓存清理登记账的纯账簿行为座。
// 不建流图、不碰执行器缓存、也不解引用任何账内指针——五腿只判"记与摘"的形状；
// 假指针（reinterpret_cast 整数地址）入账即征"键用即止"：若有解引用当场崩座。
// 红臂破坏位 = 摘账改窥账（takePending 摘净位换成只取不空置）——摘账即空腿与
// 摘后重记腿当场红，其余三腿在臂内原样绿。
#include <QtTest/QtTest>

#include "FlowCachePurgeLedger.h"

class FlowCachePurgeLedgerTest : public QObject
{
    Q_OBJECT

private slots:
    void testEnqueueTakeRoundTrip();     // 记两账 FIFO 序与账目内容
    void testTakeDrainsLedgerEmpty();    // 摘账取后即空（红臂靶腿一）
    void testEmptyLedgerServesEmpty();   // 空账摘账免炸（原 apply 空账早退的同貌）
    void testReRegisterAfterTake();      // 摘后重记再摘正一（红臂靶腿二）
    void testPointerKeyOnly();           // 键用即止：悬垂假指针与空指针入账不触内存

private:
    static NodeBase *fakeNode(quintptr address)
    {
        return reinterpret_cast<NodeBase *>(address);
    }
};

void FlowCachePurgeLedgerTest::testEnqueueTakeRoundTrip()
{
    FlowCachePurgeLedger ledger;
    ledger.enqueuePurge(fakeNode(0x11), 7);
    ledger.enqueuePurge(fakeNode(0x22), 9);
    const QList<QPair<NodeBase *, int>> taken = ledger.takePending();
    QCOMPARE(taken.size(), 2);
    QCOMPARE(taken.at(0).first, fakeNode(0x11));
    QCOMPARE(taken.at(0).second, 7);
    QCOMPARE(taken.at(1).first, fakeNode(0x22));
    QCOMPARE(taken.at(1).second, 9);
}

void FlowCachePurgeLedgerTest::testTakeDrainsLedgerEmpty()
{
    FlowCachePurgeLedger ledger;
    ledger.enqueuePurge(fakeNode(0x33), 3);
    const QList<QPair<NodeBase *, int>> first = ledger.takePending();
    QCOMPARE(first.size(), 1);
    const QList<QPair<NodeBase *, int>> second = ledger.takePending();
    QVERIFY2(second.isEmpty(),
             "摘账须空置登记账——否则执行线程会在每个安全点重复清同一批条目");
}

void FlowCachePurgeLedgerTest::testEmptyLedgerServesEmpty()
{
    FlowCachePurgeLedger ledger;
    const QList<QPair<NodeBase *, int>> none = ledger.takePending();
    QCOMPARE(none.size(), 0);
    // 连续空摘也不变账（原实现空账早退路径的同貌——保险腿）
    QCOMPARE(ledger.takePending().size(), 0);
}

void FlowCachePurgeLedgerTest::testReRegisterAfterTake()
{
    FlowCachePurgeLedger ledger;
    ledger.enqueuePurge(fakeNode(0x44), 4);
    QCOMPARE(ledger.takePending().size(), 1);
    // 摘后重记：删 A 后又删 B（同模块号或异指针）应各自一账互不吞
    ledger.enqueuePurge(fakeNode(0x55), 5);
    const QList<QPair<NodeBase *, int>> again = ledger.takePending();
    QCOMPARE(again.size(), 1);
    QCOMPARE(again.at(0).first, fakeNode(0x55));
    QCOMPARE(again.at(0).second, 5);
}

void FlowCachePurgeLedgerTest::testPointerKeyOnly()
{
    FlowCachePurgeLedger ledger;
    NodeBase *dangling = reinterpret_cast<NodeBase *>(0xDEAD0000ULL);
    ledger.enqueuePurge(dangling, 42);
    ledger.enqueuePurge(nullptr, 11);   // 执行器空指针卫在外，账簿只认键不认语义
    const auto taken = ledger.takePending();
    QCOMPARE(taken.size(), 2);
    QCOMPARE(taken.at(0).first, dangling);
    QCOMPARE(taken.at(0).second, 42);
    QVERIFY2(taken.at(1).first == nullptr, "空指针照样按键入账（不触任何节点内存）");
    QCOMPARE(taken.at(1).second, 11);
}

QTEST_MAIN(FlowCachePurgeLedgerTest)
#include "flow_cache_purge_ledger_test.moc"
