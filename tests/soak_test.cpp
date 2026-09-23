// 长稳（soak）测试 —— 对应 NFR2.4（长时间连续运行）/ NFR2.6（稳定性）前置验证。
//
// 做什么：连续模式下长时间跑一条**真实**链路（读盘 → HALCON 读图 → OpenCV 形态学/Blob →
//         公式计算 → 条件计数 → 延时），逐秒采样 FlowExecutor 的运行期统计（轮次/失败轮次/
//         进程句柄数/工作集），据此判断：
//           ① 是不是真在出轮（节拍没有停摆）；② 有没有失败轮次；
//           ③ 句柄/内存有没有**持续增长**（泄漏信号）。
//
// 与 perf_test 的分工：perf_test 测"编辑操作耗时随规模的变化"，本用例测"运行期是否越跑越差"。
// 与 integration_test 的统计用例分工：那里只跑 2 轮验证计数语义，这里跑长跑验证**增长趋势**。
//
// 时长与产物由环境变量控制（默认值面向 CI；现场 72h 长跑见 tools/soak.ps1）：
//   VFP_SOAK_SECONDS   运行秒数（默认 20）
//   VFP_SOAK_PROGRESS  进度文件路径（逐秒追加一行，便于 tail -f 观察长跑；可空）
//   VFP_SOAK_REPORT    结果报告文件路径（结尾写入汇总与逐节点统计；可空）
//
// 门限设计：句柄/内存门限取**宽松值**（句柄 ≤ 500、工作集 ≤ 300 MB），目的是抓"持续增长"这种
// 量级明显的问题，而不是把正常波动判成失败；实际数值一律打印出来供人复核。
#include <QtTest/QtTest>
#include <QObject>
#include <QString>
#include <QPointF>
#include <QList>
#include <QFile>
#include <QTextStream>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QSignalSpy>

#include "FlowScene.h"
#include "FlowExecutor.h"
#include "FlowRuntimeStats.h"
#include "NodeBase.h"
#include "Port.h"
#include "PortConnectivity.h"
#include "OpencvUtil.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

class SoakTest : public QObject
{
    Q_OBJECT
private slots:
    void testContinuousSoak();
};

namespace {

qint64 envSeconds()
{
    bool ok = false;
    const qint64 v = qEnvironmentVariable("VFP_SOAK_SECONDS").toLongLong(&ok);
    return (ok && v > 0) ? v : 20;
}

struct Sample {
    int sec = 0;
    quint64 rounds = 0;
    quint64 handles = 0;
    quint64 workingSet = 0;
    quint64 failedRounds = 0;
};

struct FlowStep {
    NodeBase::NodeType category;
    const char *name;
    QPointF pos;
};

} // namespace

void SoakTest::testContinuousSoak()
{
    const int seconds = int(envSeconds());
    const QString progressPath = qEnvironmentVariable("VFP_SOAK_PROGRESS");
    const QString reportPath = qEnvironmentVariable("VFP_SOAK_REPORT");

    // ── 1. 造一张真实图片：每轮都要走"读盘 → HALCON 读图 → OpenCV 处理" ──
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString imgPath = tmp.filePath(QStringLiteral("soak.png"));
    {
        cv::Mat img(480, 640, CV_8UC1, cv::Scalar(30));
        cv::rectangle(img, cv::Rect(200, 150, 120, 90), cv::Scalar(220), cv::FILLED);
        cv::circle(img, cv::Point(420, 320), 40, cv::Scalar(180), cv::FILLED);
        cv::line(img, cv::Point(40, 440), cv::Point(600, 420), cv::Scalar(150), 3);
        QVERIFY2(cv::imwrite(imgPath.toStdString(), img), "造图失败");
    }

    // ── 2. 建流程（类别与展示名与注册表一致；连线用 PortConnectivity 找首个兼容端口，不靠猜类型）──
    FlowScene scene;
    FlowExecutor exec;
    exec.setFlowName(QStringLiteral("SoakContinuous"));
    exec.setFlowMode(FlowMode::Continuous);
    exec.setLoopIntervalMs(0);          // 满速跑，压力最大
    exec.setStatsLogIntervalMs(0);      // 关掉执行器自有日志，本轮由测试自己采样

    const QList<FlowStep> steps = {
        { NodeBase::IMAGE_ACQUISITION, "读取图像", QPointF(0, 0) },
        { NodeBase::IMAGE_PROCESSING,  "形态学",   QPointF(180, 0) },
        { NodeBase::IMAGE_PROCESSING,  "Blob分析", QPointF(360, 0) },
        { NodeBase::LOGIC,             "公式计算", QPointF(540, 0) },
        { NodeBase::LOGIC,             "条件计数", QPointF(720, 0) },
        { NodeBase::LOGIC,             "延时",     QPointF(900, 0) },
    };
    QList<NodeBase *> nodes;
    for (const FlowStep &s : steps) {
        NodeBase *n = scene.createNode(s.category, s.pos, QString::fromUtf8(s.name));
        QVERIFY2(n != nullptr, qPrintable(QStringLiteral("创建节点失败: %1").arg(QString::fromUtf8(s.name))));
        nodes.append(n);
    }
    nodes[0]->setParam(QStringLiteral("filePath"), imgPath);
    nodes[5]->setParam(QStringLiteral("delayMs"), 0);

    for (int i = 0; i + 1 < nodes.size(); ++i) {
        bool linked = false;
        for (Port *out : nodes[i]->outputPorts()) {
            for (Port *in : nodes[i + 1]->inputPorts()) {
                if (PortConnectivity::canConnectPorts(out, in) && scene.createConnection(out, in)) {
                    linked = true;
                    break;
                }
            }
            if (linked)
                break;
        }
        QVERIFY2(linked, qPrintable(QStringLiteral("无法建立 %1 -> %2 连线")
                                        .arg(nodes[i]->fullName(), nodes[i + 1]->fullName())));
    }

    exec.setFlowScene(&scene);
    exec.resetRuntimeStats();

    // ── 3. 起跑 + 逐秒采样 ──
    exec.startExecution();
    QTRY_VERIFY_WITH_TIMEOUT(exec.getState() == ExecutionState::Running, 5000);

    QFile progressFile;
    bool progressOk = false;
    if (!progressPath.isEmpty()) {
        progressOk = progressFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    }
    QTextStream progressStream(&progressFile);

    QList<Sample> samples;
    QElapsedTimer timer;
    timer.start();
    int nextSampleSec = 1;
    while (timer.elapsed() < qint64(seconds) * 1000) {
        QTest::qWait(200);
        const int elapsedSec = int(timer.elapsed() / 1000);
        if (elapsedSec + 1 < nextSampleSec)
            continue;
        const FlowRuntimeStats s = exec.runtimeStats();
        Sample smp;
        smp.sec = nextSampleSec;
        smp.rounds = s.rounds;
        smp.handles = s.processHandleCount;
        smp.workingSet = s.processWorkingSetBytes;
        smp.failedRounds = s.failedRounds;
        samples.append(smp);

        const QString line =
            QStringLiteral("[%1s] 轮次=%2 失败轮次=%3 句柄=%4 工作集=%5 MB")
                .arg(smp.sec)
                .arg(smp.rounds)
                .arg(smp.failedRounds)
                .arg(smp.handles)
                .arg(smp.workingSet / (1024 * 1024));
        qInfo().noquote() << line;
        if (progressOk) {
            progressStream << line << '\n';
            progressStream.flush();     // 长跑时 tail -f 必须能立刻看到
        }
        ++nextSampleSec;
    }

    // ── 4. 停：长跑后必须能干净退出（线程回收也是长稳的一部分）──
    exec.stopExecution();
    QVERIFY2(exec.wait(8000), "长跑后执行器线程未退出");
    const FlowRuntimeStats finalStats = exec.runtimeStats();
    exec.setFlowScene(nullptr);
    QCoreApplication::processEvents();

    // ── 5. 判定 ──
    QVERIFY2(samples.size() >= 3,
             qPrintable(QStringLiteral("采样点不足（%1 个），VFP_SOAK_SECONDS 太短").arg(samples.size())));
    // 基线必须避开预热期：首轮要懒初始化（实测首轮读图 ~1.9s，第 1 秒时轮次还是 0、句柄也还在涨），
    // 否则"预热分配"会被当成"泄漏增长"。预热长度取 1~5 秒（长跑固定 5 秒足够）。
    const int warmupSec = qMin(5, qMax(1, seconds / 4));
    int baseIdx = 0;
    for (int i = 0; i < samples.size(); ++i) {
        if (samples.at(i).sec >= warmupSec) {
            baseIdx = i;
            break;
        }
    }
    const Sample base = samples.at(baseIdx);
    const Sample last = samples.last();
    const qint64 handleGrowth = qint64(last.handles) - qint64(base.handles);
    const qint64 wsGrowthMB = (qint64(last.workingSet) - qint64(base.workingSet)) / (1024 * 1024);

    const QString summary =
        QStringLiteral("长稳 %1s：轮次 %2（%3 轮/秒，基线 %4 → 末次 %5）| 失败轮次 %6 | "
                       "句柄 %7 → %8（Δ%9，预热 %10s 后起算）| 工作集 %11 → %12 MB（Δ%13）")
            .arg(seconds)
            .arg(finalStats.rounds)
            .arg(double(finalStats.rounds) / double(qMax(1, seconds)), 0, 'f', 1)
            .arg(base.rounds).arg(last.rounds)
            .arg(finalStats.failedRounds)
            .arg(base.handles).arg(last.handles).arg(handleGrowth).arg(warmupSec)
            .arg(base.workingSet / (1024 * 1024)).arg(last.workingSet / (1024 * 1024)).arg(wsGrowthMB);
    qInfo().noquote() << summary;

    if (!reportPath.isEmpty()) {
        QFile report(reportPath);
        if (report.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QTextStream rs(&report);
            rs << QStringLiteral("VisionFlowPlatform 长稳测试报告\n") << summary << '\n';
            // 逐秒序列：判断"预热一次性分配"还是"持续线性增长"全靠它（长跑复盘也用同一份数据）
            rs << QStringLiteral("逐秒序列（秒/轮次/句柄/工作集MB/失败轮次）:\n");
            for (const Sample &s : samples) {
                rs << QStringLiteral("  %1\t%2\t%3\t%4\t%5\n")
                          .arg(s.sec).arg(s.rounds).arg(s.handles)
                          .arg(s.workingSet / (1024 * 1024)).arg(s.failedRounds);
            }
            rs << QStringLiteral("逐节点（执行次数 / 失败 / 最大耗时 ms）:\n");
            for (NodeBase *n : nodes) {
                const NodeRuntimeStat st = finalStats.nodes.value(n->fullName());
                rs << QStringLiteral("  %1: %2 / %3 / %4\n")
                          .arg(n->fullName()).arg(st.executions).arg(st.failures).arg(st.maxMs);
            }
            rs.flush();
        }
    }

    QVERIFY2(finalStats.rounds >= 10,
             qPrintable(QStringLiteral("长跑期间只出了 %1 轮，节拍异常").arg(finalStats.rounds)));
    QVERIFY2(finalStats.failedRounds == 0,
             qPrintable(QStringLiteral("长跑出现 %1 个失败轮次").arg(finalStats.failedRounds)));
    QVERIFY2(handleGrowth <= 500,
             qPrintable(QStringLiteral("句柄增长 %1（基线 %2 → %3），疑似句柄泄漏")
                            .arg(handleGrowth).arg(base.handles).arg(last.handles)));
    QVERIFY2(wsGrowthMB <= 300,
             qPrintable(QStringLiteral("工作集增长 %1 MB（基线 %2 → %3 MB），疑似内存泄漏")
                            .arg(wsGrowthMB)
                            .arg(base.workingSet / (1024 * 1024))
                            .arg(last.workingSet / (1024 * 1024))));
    for (NodeBase *n : nodes) {
        const NodeRuntimeStat st = finalStats.nodes.value(n->fullName());
        QVERIFY2(st.executions > 0,
                 qPrintable(QStringLiteral("节点 %1 在长跑期间从未执行").arg(n->fullName())));
        QVERIFY2(st.failures == 0,
                 qPrintable(QStringLiteral("节点 %1 出现 %2 次失败").arg(n->fullName()).arg(st.failures)));
    }
}

QTEST_MAIN(SoakTest)
#include "soak_test.moc"
