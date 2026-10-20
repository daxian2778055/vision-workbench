// UpstreamWalkTest（词条切片 B 图谱族首抽）：图上游闭包游走语义的真图座。
// fixture = 实点（HalconNode::init() 自建输入港 + threshold 另加两枚输出港）
// + 手工 MyProject::Connection（构造即挂双港，见 Connection.cpp 7..14）——
// 无场景、无 HALCON 图。每腿收尾先拆连线再退点栈（连线壳挂双港，先后序防悬）。
// 计四腿：线性链序、菱形跨层同名复现（原体既成行为如实钉）、孤立与空入参、
// 平行边同源去重。红臂破坏位 = 去重 contains 位收空——平行边腿当场红，余腿原样绿。
#include <QList>
#include <QtTest>

#include "Connection.h"
#include "OpencvThresholdNode.h"
#include "Port.h"
#include "UpstreamWalk.h"

namespace {

MyProject::Connection *wire(Port *out, Port *in)
{
    return new MyProject::Connection(out, in);   // 构造即挂双港
}

}   // namespace

class UpstreamWalkTest : public QObject
{
    Q_OBJECT

private slots:
    void testLinearChainOrder();            // A→B→C：upstreamOf(C) = [B, A]
    void testDiamondKeepsPerLevelDedup();   // 菱形 A→B→D 与 A→C→D：[B, A, C, A]（原体行为）
    void testIsolatedAndNullEmpty();        // 无连线节点与空入参：空序列
    void testParallelDuplicateEdgesDedup(); // 平行边同源去重（红臂靶腿）
};

void UpstreamWalkTest::testLinearChainOrder()
{
    OpencvThresholdNode a;
    a.init();
    OpencvThresholdNode b;
    b.init();
    OpencvThresholdNode c;
    c.init();

    // A.out → B.in，B.out → C.in
    const QList<MyProject::Connection *> conns = {
        wire(a.outputPorts()[0], b.inputPorts()[0]),
        wire(b.outputPorts()[0], c.inputPorts()[0]),
    };

    const QList<NodeBase *> ups = UpstreamWalk::upstreamOf(&c);
    QCOMPARE(ups.size(), 2);
    QCOMPARE(ups[0], static_cast<NodeBase *>(&b));
    QCOMPARE(ups[1], static_cast<NodeBase *>(&a));

    for (MyProject::Connection *conn : conns)
        delete conn;   // 拆连线在先，点栈解在后
}

void UpstreamWalkTest::testDiamondKeepsPerLevelDedup()
{
    OpencvThresholdNode a;
    a.init();
    OpencvThresholdNode b;
    b.init();
    OpencvThresholdNode cn;
    cn.init();
    OpencvThresholdNode d;
    d.init();

    // 菱形：A → B → D 与 A → C → D（D 的同一输入端口挂两条连线）
    const QList<MyProject::Connection *> conns = {
        wire(a.outputPorts()[0], b.inputPorts()[0]),
        wire(b.outputPorts()[0], d.inputPorts()[0]),
        wire(a.outputPorts()[0], cn.inputPorts()[0]),
        wire(cn.outputPorts()[0], d.inputPorts()[0]),
    };

    // 跨层同名可复现（A 出现两枚）：原体 contains 为现层口径，如实钉不修正
    const QList<NodeBase *> ups = UpstreamWalk::upstreamOf(&d);
    QCOMPARE(ups.size(), 4);
    QCOMPARE(ups[0], static_cast<NodeBase *>(&b));
    QCOMPARE(ups[1], static_cast<NodeBase *>(&a));
    QCOMPARE(ups[2], static_cast<NodeBase *>(&cn));
    QCOMPARE(ups[3], static_cast<NodeBase *>(&a));

    for (MyProject::Connection *conn : conns)
        delete conn;
}

void UpstreamWalkTest::testIsolatedAndNullEmpty()
{
    OpencvThresholdNode lone;
    lone.init();
    QVERIFY(UpstreamWalk::upstreamOf(&lone).isEmpty());   // 端口在而无连线
    QVERIFY(UpstreamWalk::upstreamOf(nullptr).isEmpty()); // 空入参守卫
}

void UpstreamWalkTest::testParallelDuplicateEdgesDedup()
{
    OpencvThresholdNode a;
    a.init();
    OpencvThresholdNode b;
    b.init();
    OpencvThresholdNode d;
    d.init();

    // 同源平行边两枚：B.out → D.in ×2（同层同源）；B 的上游仍是 A
    const QList<MyProject::Connection *> conns = {
        wire(b.outputPorts()[0], d.inputPorts()[0]),
        wire(b.outputPorts()[0], d.inputPorts()[0]),
        wire(a.outputPorts()[0], b.inputPorts()[0]),
    };

    // 现层 contains：同源在 D 层只挂一次（红臂下为 [B, A, B, A]）
    const QList<NodeBase *> ups = UpstreamWalk::upstreamOf(&d);
    QCOMPARE(ups.size(), 2);
    QCOMPARE(ups[0], static_cast<NodeBase *>(&b));
    QCOMPARE(ups[1], static_cast<NodeBase *>(&a));

    for (MyProject::Connection *conn : conns)
        delete conn;
}

QTEST_MAIN(UpstreamWalkTest)
#include "upstream_walk_test.moc"
