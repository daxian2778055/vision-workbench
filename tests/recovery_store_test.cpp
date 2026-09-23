#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "RecoveryStore.h"
#include "ProjectManager.h"
#include "FlowScene.h"
#include "NodeBase.h"
#include "Port.h"
#include "NodeRegistry.h"

// 崩溃恢复 / 自动保存的落盘逻辑（RecoveryStore）。
//
// 为什么值得单独测：这条链路出错的表现是"用户以为有备份，真出事时才发现恢复文件是坏的/
// 是空的/内容与手动保存不一致"——现场无法事后补救。因此这里钉的是**契约**而非实现细节：
//   ① 往返一致（节点/连线/流程名/参数都要原样回来）；
//   ② 恢复文件是**合法方案文件**（用真加载器 ProjectManager::loadProject 打开它）；
//   ③ 坏文件不许冒充"可恢复"；缺元信息不算坏；
//   ④ "是否有未保存改动"用内容基线判定，且依赖"同状态两次序列化字节一致"（单独钉住）；
//   ⑤ 干净退出后不留恢复现场；写失败必须被报出来（不能静默以为存上了）。
//
// 说明：MainWindow 本身无法在 CI 实例化（重量级窗口 + HALCON/相机/数据库初始化，见
// PanelVisibilityStore.h 同类说明），故窗口侧只留薄胶水，策略与落盘逻辑都在这里覆盖。
class RecoveryStoreTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void testDirOverrideAndNames();
    void testRoundTripWithRealProjectJson();
    void testRecoveryFileOpensByRealLoader();
    void testCorruptRecoveryIsRejected();
    void testBaselineAndUnsavedChanges();
    void testEmptyProjectIsNotNagging();
    void testUpToDateSkipsRewrite();
    void testClearIsIdempotent();
    void testWriteFailureReported();
    void testSerializationIsDeterministic();
};

namespace {
/// 造一个"有内容"的当前方案：2 个节点 + 1 条连线 + 一个易识别的参数值
QList<FlowScene *> makeScenes(FlowScene **firstOut = nullptr)
{
    auto *scene = new FlowScene();
    NodeBase *delay = scene->createNode(NodeBase::LOGIC, QPointF(20, 40),
                                        QStringLiteral("Delay"));
    NodeBase *formula = scene->createNode(NodeBase::LOGIC, QPointF(260, 40),
                                         QStringLiteral("Formula"));
    if (delay)
        delay->setParam(QStringLiteral("delayMs"), 1234);
    if (delay && formula && !delay->outputPorts().isEmpty() && !formula->inputPorts().isEmpty())
        scene->createConnection(delay->outputPorts().first(), formula->inputPorts().first(), true);
    scene->setFlowName(QStringLiteral("恢复用例流程"));
    if (firstOut)
        *firstOut = scene;
    return QList<FlowScene *>{scene};
}

/// 按注册表类型 ID 找节点。
/// 注意**不能按显示名找**：createNode 会给同名节点加后缀（"延迟"→"延迟1"…），显示名在
/// "创建 / 恢复"两次之间可能不同，按名定位会得到"节点丢了"的假失败。
/// 失败时把实际清单带出来，便于一眼看出是"真丢"还是"找法不对"。
NodeBase *findNodeByTypeId(FlowScene *scene, const QString &typeId)
{
    for (NodeBase *n : scene->nodes()) {
        if (n->property("vfpNodeTypeId").toString() == typeId)
            return n;
    }
    return nullptr;
}

QString dumpNodes(FlowScene *scene)
{
    QStringList parts;
    for (NodeBase *n : scene->nodes()) {
        parts << QStringLiteral("%1[typeId=%2,delayMs=%3]")
                     .arg(n->name(),
                          n->property("vfpNodeTypeId").toString(),
                          n->getParam(QStringLiteral("delayMs")).toString());
    }
    return parts.join(QStringLiteral(", "));
}
}   // namespace

void RecoveryStoreTest::initTestCase()
{
    registerAllNodes();   // 用例要 createNode("Delay"/"Formula")，注册表必须先填好
}

void RecoveryStoreTest::testDirOverrideAndNames()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    RecoveryStore store(tmp.path());
    QCOMPARE(store.dirPath(), tmp.path());
    QCOMPARE(RecoveryStore::recoveryFileName(), QStringLiteral("recovery.vfp"));
    QCOMPARE(RecoveryStore::metaKey(), QStringLiteral("recoveryMeta"));

    // 默认目录（正式运行用）必须非空、可写、以 recovery 结尾——否则自动保存会静默失败
    const QString def = RecoveryStore::defaultDirPath();
    QVERIFY2(!def.isEmpty(), "默认恢复目录为空");
    QVERIFY2(def.endsWith(QStringLiteral("recovery")), qPrintable(def));
    QVERIFY2(QDir().mkpath(def), qPrintable(def));
    QFile probe(QDir(def).filePath(QStringLiteral("probe.tmp")));
    QVERIFY2(probe.open(QIODevice::WriteOnly), qPrintable(def));
    probe.close();
    probe.remove();
}

void RecoveryStoreTest::testRoundTripWithRealProjectJson()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    RecoveryStore store(tmp.path());

    ProjectManager pm;
    QList<FlowScene *> scenes = makeScenes();
    QCOMPARE(scenes.first()->nodes().size(), 2);
    QCOMPARE(scenes.first()->connections().size(), 1);

    const QDateTime when = QDateTime::fromString(QStringLiteral("2026-09-23T10:20:30"),
                                                Qt::ISODate);
    const QString origin = QStringLiteral("D:/projects/工件检测.vfp");
    QVERIFY2(store.saveRecovery(pm.buildProjectJson(scenes), origin, when),
             "写入恢复文件失败");

    const RecoveryStore::Info info = store.info();
    QVERIFY(info.exists);
    QVERIFY(info.readable);
    QVERIFY(info.metaPresent);
    QCOMPARE(info.originalPath, origin);
    QCOMPARE(info.savedAt, when);
    QVERIFY(info.size > 0);

    QJsonObject back;
    QVERIFY(store.loadRecovery(&back));
    QCOMPARE(back.value(QStringLiteral("scenes")).toArray().size(), 1);

    // 用真加载器的"应用"入口把它还原成场景：节点/连线/流程名/参数都要原样回来
    ProjectManager loader;
    QList<FlowScene *> restored;
    QVERIFY(loader.applyProjectJson(back, restored));
    QCOMPARE(restored.size(), 1);
    FlowScene *scene = restored.first();
    QCOMPARE(scene->nodes().size(), 2);
    QCOMPARE(scene->connections().size(), 1);
    QCOMPARE(scene->flowName(), QStringLiteral("恢复用例流程"));

    // 参数：恢复出"有节点但参数全丢"是最难查的一类，必须逐项钉住
    NodeBase *delay = findNodeByTypeId(scene, QStringLiteral("DelayNode"));
    QVERIFY2(delay, qPrintable(QStringLiteral("恢复后找不到延时节点，实际节点：%1")
                                   .arg(dumpNodes(scene))));
    QCOMPARE(delay->getParam(QStringLiteral("delayMs")).toInt(), 1234);

    qDeleteAll(scenes);
    qDeleteAll(restored);
}

void RecoveryStoreTest::testRecoveryFileOpensByRealLoader()
{
    // 设计取舍：恢复文件本身是合法方案文件，用户也可以直接「加载项目」打开它。
    // 这条一旦坏了，等于把"最后一根救命稻草"砍掉，故用真加载器（含文件读写/解析/应用）验证。
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    RecoveryStore store(tmp.path());

    ProjectManager pm;
    QList<FlowScene *> scenes = makeScenes();
    QVERIFY(store.saveRecovery(pm.buildProjectJson(scenes), QString(),
                               QDateTime::currentDateTime()));

    QString exported;
    QVERIFY(store.exportForLoad(&exported));
    QVERIFY(QFile::exists(exported));

    ProjectManager loader;
    QList<FlowScene *> loaded;
    QVERIFY2(loader.loadProject(exported, loaded), "真加载器无法打开导出的恢复文件");
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.first()->nodes().size(), 2);
    QCOMPARE(loaded.first()->connections().size(), 1);

    qDeleteAll(scenes);
    qDeleteAll(loaded);
}

void RecoveryStoreTest::testCorruptRecoveryIsRejected()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    RecoveryStore store(tmp.path());
    const QString path = QDir(tmp.path()).filePath(RecoveryStore::recoveryFileName());

    // ① 截断的 JSON（模拟断电写一半；QSaveFile 已让这种情况极难发生，但用户可能手工改坏）
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"scenes\":[{\"nodes\"");
        f.close();
    }
    RecoveryStore::Info info = store.info();
    QVERIFY(info.exists);
    QVERIFY(!info.readable);
    QJsonObject back;
    QVERIFY(!store.loadRecovery(&back));
    QVERIFY(!store.exportForLoad(nullptr));   // 不可读时不导出（也不允许崩在空指针上）

    // ② 合法 JSON 但根不是对象（数组）→ 同样不可恢复
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[]");
        f.close();
    }
    QVERIFY(!store.info().readable);

    // ③ 合法对象但缺 recoveryMeta：**仍可恢复**，只是元信息缺失
    //    （不能因为少了元信息就说"损坏"——老版本/手工构造的恢复文件会走到这里）
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"scenes\":[]}");
        f.close();
    }
    info = store.info();
    QVERIFY(info.exists);
    QVERIFY(info.readable);
    QVERIFY(!info.metaPresent);
    QVERIFY(info.originalPath.isEmpty());
    QVERIFY(!info.savedAt.isValid());
    QVERIFY(store.loadRecovery(&back));
}

void RecoveryStoreTest::testBaselineAndUnsavedChanges()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    RecoveryStore store(tmp.path());

    ProjectManager pm;
    QList<FlowScene *> scenes = makeScenes();
    const QJsonObject json = pm.buildProjectJson(scenes);

    // 尚无基线 + 方案有内容 → 视为"有未保存改动"（关窗时应当提示）
    QVERIFY(!store.hasBaseline());
    QVERIFY(store.hasUnsavedChanges(json));

    // 标记已落盘 → 无改动
    store.markSaved(json);
    QVERIFY(store.hasBaseline());
    QVERIFY(!store.hasUnsavedChanges(json));

    // 改一个参数 → 立刻变为"有改动"（这就是自动保存与关窗提示的触发条件）
    NodeBase *delay = findNodeByTypeId(scenes.first(), QStringLiteral("DelayNode"));
    QVERIFY2(delay, qPrintable(QStringLiteral("场景里找不到延时节点：%1")
                                   .arg(dumpNodes(scenes.first()))));
    delay->setParam(QStringLiteral("delayMs"), 4321);
    QVERIFY(store.hasUnsavedChanges(pm.buildProjectJson(scenes)));

    qDeleteAll(scenes);
}

void RecoveryStoreTest::testEmptyProjectIsNotNagging()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    RecoveryStore store(tmp.path());

    ProjectManager pm;
    FlowScene *empty = new FlowScene();
    QList<FlowScene *> scenes{empty};
    const QJsonObject emptyJson = pm.buildProjectJson(scenes);

    // 刚装完打开软件就退出：空白方案不该弹"有未保存修改"
    QVERIFY(RecoveryStore::isEmptyProject(emptyJson));
    QVERIFY(!store.hasUnsavedChanges(emptyJson));
    // 口径：没有 scenes 键、或 scenes 为空数组，都按"没有流程"处理
    QVERIFY(RecoveryStore::isEmptyProject(QJsonObject{}));
    QVERIFY(RecoveryStore::isEmptyProject(
        QJsonObject{{QStringLiteral("scenes"), QJsonArray{}}}));

    // 加一个节点就算有内容 → 需要提示/自动保存
    empty->createNode(NodeBase::LOGIC, QPointF(0, 0), QStringLiteral("Delay"));
    const QJsonObject json = pm.buildProjectJson(scenes);
    QVERIFY(!RecoveryStore::isEmptyProject(json));
    QVERIFY(store.hasUnsavedChanges(json));

    qDeleteAll(scenes);
}

void RecoveryStoreTest::testUpToDateSkipsRewrite()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    RecoveryStore store(tmp.path());

    ProjectManager pm;
    QList<FlowScene *> scenes = makeScenes();
    const QJsonObject json = pm.buildProjectJson(scenes);

    QVERIFY(!store.recoveryUpToDate(json));   // 还没写过
    QVERIFY(store.saveRecovery(json, QString(), QDateTime::currentDateTime()));
    // 同一内容再触发自动保存：应被判定为"已是最新"，从而跳过写盘
    // （否则用户改一下停手后，每分钟都会重写同一份内容 + 状态栏刷屏）
    QVERIFY(store.recoveryUpToDate(json));

    // 内容变了 → 又需要写
    NodeBase *delay = findNodeByTypeId(scenes.first(), QStringLiteral("DelayNode"));
    QVERIFY2(delay, qPrintable(QStringLiteral("场景里找不到延时节点：%1")
                                   .arg(dumpNodes(scenes.first()))));
    delay->setParam(QStringLiteral("delayMs"), 7);
    QVERIFY(!store.recoveryUpToDate(pm.buildProjectJson(scenes)));

    QVERIFY(store.clearRecovery());
    QVERIFY(!store.recoveryUpToDate(json));   // 清掉后需重新写
    qDeleteAll(scenes);
}

void RecoveryStoreTest::testClearIsIdempotent()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    RecoveryStore store(tmp.path());
    const QString recoveryPath = QDir(tmp.path()).filePath(RecoveryStore::recoveryFileName());

    ProjectManager pm;
    QList<FlowScene *> scenes = makeScenes();
    QVERIFY(store.saveRecovery(pm.buildProjectJson(scenes), QString(),
                               QDateTime::currentDateTime()));
    QString exported;
    QVERIFY(store.exportForLoad(&exported));

    // 干净退出：恢复文件与临时导出文件一起清掉，下次启动不该再问"是否恢复"
    QVERIFY(store.clearRecovery());
    QVERIFY(!QFile::exists(recoveryPath));
    QVERIFY(!QFile::exists(exported));
    QVERIFY(!store.info().exists);

    QVERIFY(store.clearRecovery());   // 幂等：再清一次不报错
    qDeleteAll(scenes);
}

void RecoveryStoreTest::testWriteFailureReported()
{
    // 目录不可创建（父路径被一个文件占住）→ 必须返回 false。
    // 自动保存若是"静默失败"，用户会一直以为有备份，这是最坏的失败方式。
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString asFile = tmp.filePath(QStringLiteral("not-a-dir"));
    {
        QFile f(asFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
    }

    RecoveryStore store(asFile + QStringLiteral("/recovery"));
    ProjectManager pm;
    QList<FlowScene *> scenes = makeScenes();
    QVERIFY(!store.saveRecovery(pm.buildProjectJson(scenes), QString(),
                                QDateTime::currentDateTime()));
    QVERIFY(!store.info().exists);
    qDeleteAll(scenes);
}

void RecoveryStoreTest::testSerializationIsDeterministic()
{
    // 内容基线（"是否有未保存改动"）建立在"同一状态两次序列化字节一致"之上：
    // 这条不成立的话，判定会变成随机噪声——自动保存要么疯狂写盘，要么永远不写。
    ProjectManager pm;
    QList<FlowScene *> scenes = makeScenes();
    const QByteArray a = RecoveryStore::compact(pm.buildProjectJson(scenes));
    const QByteArray b = RecoveryStore::compact(pm.buildProjectJson(scenes));
    QCOMPARE(a, b);
    QVERIFY(!a.isEmpty());

    qDeleteAll(scenes);
}

QTEST_MAIN(RecoveryStoreTest)
#include "recovery_store_test.moc"
