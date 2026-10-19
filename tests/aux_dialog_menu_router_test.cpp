// AuxDialogMenuRouterTest（词条切片 B 第二抽）：菜单对话框路由面的纯接线座。
// 无 MainWindow、无对话框构造——动作用裸 QAction，展示钩子与工厂全替身份为探针；
// 钉的是路由语义本身：固定键透传、懒构造（钩子不调工厂则不建窗）、场景缺失守卫
// （先报回调、不进展示）、场景快照与工厂交递。
// 红臂破坏位 = 场景守卫位取反（toFlowSceneDialog 的 if (!scene) 取反）——缺失腿与
// 在场腿双向皆赤（守卫两侧语义同源一位），固定键与快照腿臂内原样绿。
#include <QAction>
#include <QDialog>
#include <QtTest/QtTest>

#include "AuxDialogMenuRouter.h"
#include "FlowScene.h"

namespace {

struct Probe {
    QStringList shownKeys;
    int factoryBuilds = 0;
    QList<FlowScene *> makeSceneArg;
    QList<QList<FlowScene *>> makeListArg;
    int missingReports = 0;
    AuxDialogMenuRouter::DialogFactory lastFactory;
};

AuxDialogMenuRouter::ShowDialog probeHook(Probe &p)
{
    return [&p](const QString &key, const AuxDialogMenuRouter::DialogFactory &factory) {
        p.shownKeys.append(key);
        p.lastFactory = factory;   // 钩子不调工厂：未展示不建窗（懒构造钉在此压）
    };
}

AuxDialogMenuRouter::DialogFactory countingSceneFactory(Probe &p)
{
    return [&p]() -> QDialog * {
        ++p.factoryBuilds;
        return new QDialog();      // 展示当下才建真窗
    };
}

AuxDialogMenuRouter::SceneFactory handoffSceneFactory(Probe &p)
{
    return [&p](FlowScene *scene) -> QDialog * {
        ++p.factoryBuilds;
        p.makeSceneArg.append(scene);
        return new QDialog();
    };
}

} // namespace

class AuxDialogMenuRouterTest : public QObject
{
    Q_OBJECT

private slots:
    void testFixedKeyDialogRouting();        // 通讯五枚：四枚同键 commManager、监视另键；懒构造钉
    void testSystemTableKeysAndLazyBuilds(); // 系统菜单九枚键序 + 未展示不建窗
    void testScenesSnapshotAtTriggerHandoff(); // 列表触发当下快照，工厂收同一份
    void testSceneGuardMissing();            // 缺场景：先报回调、不进展示、不建窗
    void testSceneGuardPresentHandsOff();    // 在场景：进展示，工厂收到同一场景指针
};

void AuxDialogMenuRouterTest::testFixedKeyDialogRouting()
{
    Probe probe;
    AuxDialogMenuRouter router(probeHook(probe));

    QAction device(this);
    QAction receiveEvent(this);
    QAction sendEvent(this);
    QAction heartbeat(this);
    QAction monitor(this);

    const AuxDialogMenuRouter::DialogFactory factory = countingSceneFactory(probe);
    router.toDialog(&device, this, QStringLiteral("commManager"), factory);
    router.toDialog(&receiveEvent, this, QStringLiteral("commManager"), factory);
    router.toDialog(&sendEvent, this, QStringLiteral("commManager"), factory);
    router.toDialog(&heartbeat, this, QStringLiteral("commManager"), factory);
    router.toDialog(&monitor, this, QStringLiteral("commMonitor"), factory);

    device.trigger();
    receiveEvent.trigger();
    sendEvent.trigger();
    heartbeat.trigger();
    monitor.trigger();

    QCOMPARE(probe.shownKeys,
             QStringList({ QStringLiteral("commManager"), QStringLiteral("commManager"),
                           QStringLiteral("commManager"), QStringLiteral("commManager"),
                           QStringLiteral("commMonitor") }));
    // 钩子未被调工厂 → 开发层面未展示不建窗（现管线里 create 由 showDialog 同步调起）
    QCOMPARE(probe.factoryBuilds, 0);
    // 工厂可取用：钩子把工厂交回调用链，真正展示时才建窗
    QVERIFY(probe.lastFactory);
    delete probe.lastFactory();
    QCOMPARE(probe.factoryBuilds, 1);
}

void AuxDialogMenuRouterTest::testSystemTableKeysAndLazyBuilds()
{
    Probe probe;
    AuxDialogMenuRouter router(probeHook(probe));

    QAction globalTrigger(this);
    QAction globalVariables(this);
    QAction userManagement(this);
    QAction alarmHistory(this);
    QAction inspectionResults(this);
    QAction paramSearch(this);
    QAction codeExport(this);
    QAction operationLog(this);
    QAction report(this);

    const AuxDialogMenuRouter::DialogFactory factory = countingSceneFactory(probe);
    const AuxDialogMenuRouter::ScenesProvider emptyList = []() { return QList<FlowScene *>{}; };
    const AuxDialogMenuRouter::MultiSceneFactory multiFactory =
        [&probe](const QList<FlowScene *> &list) -> QDialog * {
            ++probe.factoryBuilds;
            probe.makeListArg.append(list);
            return new QDialog();
        };

    router.toDialog(&globalTrigger, this, QStringLiteral("globalTrigger"), factory);
    router.toDialog(&globalVariables, this, QStringLiteral("globalVariables"), factory);
    router.toDialog(&userManagement, this, QStringLiteral("userManagement"), factory);
    router.toDialog(&alarmHistory, this, QStringLiteral("alarmHistory"), factory);
    router.toDialog(&inspectionResults, this, QStringLiteral("inspectionResults"), factory);
    router.toScenesDialog(&paramSearch, this, QStringLiteral("paramSearch"), emptyList,
                          multiFactory);
    router.toScenesDialog(&codeExport, this, QStringLiteral("codeExport"), emptyList,
                          multiFactory);
    router.toDialog(&operationLog, this, QStringLiteral("operationLog"), factory);
    router.toDialog(&report, this, QStringLiteral("report"), factory);

    globalTrigger.trigger();
    globalVariables.trigger();
    userManagement.trigger();
    alarmHistory.trigger();
    inspectionResults.trigger();
    paramSearch.trigger();
    codeExport.trigger();
    operationLog.trigger();
    report.trigger();

    QCOMPARE(probe.shownKeys,
             QStringList({ QStringLiteral("globalTrigger"), QStringLiteral("globalVariables"),
                           QStringLiteral("userManagement"), QStringLiteral("alarmHistory"),
                           QStringLiteral("inspectionResults"), QStringLiteral("paramSearch"),
                           QStringLiteral("codeExport"), QStringLiteral("operationLog"),
                           QStringLiteral("report") }));
    QCOMPARE(probe.factoryBuilds, 0);   // 九枚全部未建窗（懒构造钉）
}

void AuxDialogMenuRouterTest::testScenesSnapshotAtTriggerHandoff()
{
    Probe probe;
    AuxDialogMenuRouter router(probeHook(probe));

    FlowScene sceneA(this);
    QAction paramSearch(this);

    AuxDialogMenuRouter::ScenesProvider provider = [&sceneA]() {
        return QList<FlowScene *>{ &sceneA };
    };
    AuxDialogMenuRouter::MultiSceneFactory make =
        [&probe](const QList<FlowScene *> &list) -> QDialog * {
            ++probe.factoryBuilds;
            probe.makeListArg.append(list);
            return new QDialog();
        };
    router.toScenesDialog(&paramSearch, this, QStringLiteral("paramSearch"), provider, make);

    paramSearch.trigger();
    QCOMPARE(probe.shownKeys, QStringList({ QStringLiteral("paramSearch") }));
    QCOMPARE(probe.factoryBuilds, 0);   // 未展示不建窗
    // 工厂交递：钩子里取出的工厂在调用时收到触发当下的那一份列表（内容与指针一致）
    QVERIFY(probe.lastFactory);
    delete probe.lastFactory();
    QCOMPARE(probe.factoryBuilds, 1);
    QVERIFY(probe.makeListArg.size() == 1);
    QCOMPARE(probe.makeListArg.first().size(), 1);
    QCOMPARE(probe.makeListArg.first().first(), &sceneA);
}

void AuxDialogMenuRouterTest::testSceneGuardMissing()
{
    Probe probe;
    AuxDialogMenuRouter router(probeHook(probe));

    QAction flowVariable(this);

    AuxDialogMenuRouter::SceneProvider noScene = []() { return static_cast<FlowScene *>(nullptr); };
    AuxDialogMenuRouter::SceneMissing onMissing = [&probe]() { ++probe.missingReports; };
    router.toFlowSceneDialog(&flowVariable, this, QStringLiteral("flowVariable"), noScene,
                             onMissing, handoffSceneFactory(probe));

    flowVariable.trigger();
    // 缺场景：先报回调一次；不进展示、不建窗（原体静默早退的语义位）
    QCOMPARE(probe.missingReports, 1);
    QVERIFY(probe.shownKeys.isEmpty());
    QCOMPARE(probe.factoryBuilds, 0);
}

void AuxDialogMenuRouterTest::testSceneGuardPresentHandsOff()
{
    Probe probe;
    AuxDialogMenuRouter router(probeHook(probe));

    FlowScene sceneB(this);
    QAction recipe(this);

    AuxDialogMenuRouter::SceneProvider provider = [&sceneB]() { return &sceneB; };
    AuxDialogMenuRouter::SceneMissing onMissing = [&probe]() { ++probe.missingReports; };
    router.toFlowSceneDialog(&recipe, this, QStringLiteral("recipe"), provider, onMissing,
                             handoffSceneFactory(probe));

    recipe.trigger();
    // 在场景：进展示（先记键、不建窗），钩子里的工厂被调用时收到同一场景指针
    QCOMPARE(probe.shownKeys, QStringList({ QStringLiteral("recipe") }));
    QCOMPARE(probe.missingReports, 0);
    QCOMPARE(probe.factoryBuilds, 0);
    QVERIFY(probe.lastFactory);
    delete probe.lastFactory();
    QCOMPARE(probe.factoryBuilds, 1);
    QVERIFY(probe.makeSceneArg.size() == 1);
    QCOMPARE(probe.makeSceneArg.first(), &sceneB);
}

QTEST_MAIN(AuxDialogMenuRouterTest)
#include "aux_dialog_menu_router_test.moc"
