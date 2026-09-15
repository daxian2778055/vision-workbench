// 场景编辑性能测试（对应需求 NFR1.1：拖拽创建连线/算子的响应时间不超过 100ms）
//
// 本测试量化两处会随流程规模增长的编辑路径：
//   1) FlowScene::recordUndo()  —— 快照式撤销，每次结构变更前把整个场景序列化为 JSON
//      （createNode / createConnection / removeNode / removeConnection / 移动结束 / ROI 应用
//        都会调用；拖动过程中不调用，移动结束才记录一次）
//   2) FlowScene::createConnection() —— 含新增的创建期闭环检测（反向可达性遍历）
//
// 目的：把"撤销快照可能拖慢交互"从推测变成可复核的数据，并作为回归基线。
#include <QtTest/QtTest>
#include <QObject>
#include <QElapsedTimer>
#include <QPointF>
#include <QList>
#include <algorithm>

#include "FlowScene.h"
#include "NodeBase.h"
#include "Port.h"
#include "Connection.h"

class PerfTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    /// 撤销快照耗时随节点规模的变化
    void testUndoSnapshotScaling();
    /// 单条连线创建（含闭环检测）耗时
    void testCreateConnectionLatency();

private:
    void fillTo(int nodeCount);
    FlowScene *m_scene = nullptr;
};

void PerfTest::initTestCase()
{
    m_scene = new FlowScene();
}

void PerfTest::cleanupTestCase()
{
    delete m_scene;
    m_scene = nullptr;
}

void PerfTest::fillTo(int nodeCount)
{
    int index = 0;
    while (m_scene->nodes().size() < nodeCount) {
        const qreal x = 100.0 + (index % 12) * 160.0;
        const qreal y = 100.0 + (index / 12) * 110.0;
        m_scene->createNode(NodeBase::IMAGE_PROCESSING, QPointF(x, y),
                            QStringLiteral("二值化"));
        ++index;
    }
}

void PerfTest::testUndoSnapshotScaling()
{
    const QList<int> scales = {20, 60, 120};
    double msAt120 = 0.0;

    for (int target : scales) {
        fillTo(target);

        // 逐次计时取中位数：单次系统调度抖动（杀毒扫描、CI 机器上其它进程抢占）不应让门槛误报；
        // 真实回归会抬高每一档规模的中位数，依然拦得住。最差样本只打印、不参与判定。
        const int reps = 10;
        QList<double> samples;
        for (int i = 0; i < reps; ++i) {
            QElapsedTimer timer;
            timer.start();
            m_scene->recordUndo();
            samples.append(double(timer.nsecsElapsed()) / 1.0e6);
        }
        std::sort(samples.begin(), samples.end());
        const double perCallMs = samples.at(reps / 2);
        const double worstMs = samples.last();

        qInfo().noquote() << QStringLiteral("撤销快照: %1 节点 -> 中位数 %2 ms/次 | 最差 %3 ms")
                                 .arg(target)
                                 .arg(perCallMs, 0, 'f', 2)
                                 .arg(worstMs, 0, 'f', 2);

        if (target == 120)
            msAt120 = perCallMs;
    }

    // NFR1.1 的 100ms 预算：单次编辑操作的快照开销必须留有余量（按中位数口径判定）
    QVERIFY2(msAt120 < 100.0,
             qPrintable(QStringLiteral("120 节点场景单次撤销快照耗时中位数 %1 ms，已逼近/超出 100ms 预算")
                            .arg(msAt120, 0, 'f', 2)));
}

void PerfTest::testCreateConnectionLatency()
{
    fillTo(120);

    const QList<NodeBase *> nodes = m_scene->nodes();
    QVERIFY2(nodes.size() >= 100, "场景节点数不足，无法进行连线测试");

    QElapsedTimer timer;
    int created = 0;
    double firstMs = -1.0;        ///< 首次调用（含懒初始化）
    QList<double> steady;         ///< 排除首次后的逐条样本

    // 串成链：node[i].out -> node[i+1].in，每条连线都触发一次闭环检测 + 一次撤销快照
    for (int i = 0; i + 1 < nodes.size() && created < 100; ++i) {
        NodeBase *from = nodes[i];
        NodeBase *to = nodes[i + 1];
        if (from->outputPorts().isEmpty() || to->inputPorts().isEmpty())
            continue;

        timer.restart();
        MyProject::Connection *conn =
            m_scene->createConnection(from->outputPorts().first(), to->inputPorts().first());
        const double ms = double(timer.nsecsElapsed()) / 1.0e6;

        if (!conn)
            continue;   // 已连接/类型不匹配等，跳过

        if (created == 0) {
            firstMs = ms;           // 首条含注册表/HALCON/Qt 图形项的懒初始化，单独统计
        } else {
            steady.append(ms);
        }
        ++created;
    }

    // 判定用「中位数 + 超预算样本数」：单次调度抖动（CI 机器上还有别的进程）不应让门槛误报，
    // 而真实变慢会让中位数与超预算样本数一起抬升，依然拦得住。最差样本只打印、不参与判定。
    std::sort(steady.begin(), steady.end());
    const double medianMs = steady.isEmpty() ? 0.0 : steady.at(steady.size() / 2);
    const double p95Ms = steady.isEmpty()
                             ? 0.0
                             : steady.at(qMin(steady.size() - 1, steady.size() * 95 / 100));
    const double worstSteadyMs = steady.isEmpty() ? 0.0 : steady.last();
    const int toleratedOverBudget = qMax(1, int(steady.size() / 20));   // 容忍 5% 的外部干扰样本
    const int overBudget = int(std::count_if(steady.begin(), steady.end(),
                                             [](double v) { return v >= 100.0; }));

    qInfo().noquote()
        << QStringLiteral("连线创建: 共 %1 条 | 首次 %2 ms | 稳态中位数 %3 ms | p95 %4 ms | 最差 %5 ms | 超预算样本 %6/%7")
               .arg(created)
               .arg(firstMs, 0, 'f', 2)
               .arg(medianMs, 0, 'f', 2)
               .arg(p95Ms, 0, 'f', 2)
               .arg(worstSteadyMs, 0, 'f', 2)
               .arg(overBudget)
               .arg(steady.size());

    QVERIFY2(created >= 50, "有效连线样本不足，测试无意义");
    QVERIFY2(medianMs < 100.0,
             qPrintable(QStringLiteral("稳态下单次连线创建耗时中位数 %1 ms，超出 NFR1.1 的 100ms 预算")
                            .arg(medianMs, 0, 'f', 2)));
    QVERIFY2(overBudget <= toleratedOverBudget,
             qPrintable(QStringLiteral("稳态样本中 %1/%2 条超出 100ms 预算（容忍上限 %3 条），耗时出现系统性变差")
                            .arg(overBudget)
                            .arg(steady.size())
                            .arg(toleratedOverBudget)));
}

QTEST_MAIN(PerfTest)
#include "perf_test.moc"
