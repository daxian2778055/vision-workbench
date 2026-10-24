// 画布编辑锁的释放契约（2026-10-24 现场反馈回归用例）。
//
// 现象：工具拖不进画布、双击能进但进去后节点不能动。
// 根因两条：
//   ① MainWindow::onExecutionError 没有像 Started/Stopped/Finished 那样重算画布锁
//      ——错误路径若没走到 executionFinished，画布永久停在锁定态；
//   ② 双击工具库入画布走 MainWindow::addNode 直接 createNode，绕过了 FlowScene::dropEvent
//      的 m_editLocked 判据（createNode 本身不查锁）。
//
// 本用例钉住引擎侧的一半契约：**流程因节点失败停止后，执行器必须允许编辑**
// （allowsGraphEditing() == true），且按该判据重算后画布真的解锁、节点恢复可拖动。
// 执行器若停在 Running，本用例直接红——那正是"画布永久锁死"的成因。
#include "FlowScene.h"
#include "FlowExecutor.h"
#include "FlowExecutorToken.h"   // U-203：执行器删除权只经令牌（析构 protected，不能栈上构造）
#include "NodeBase.h"
#include "NodeRegistry.h"
#include "NodeGraphicsItem.h"
#include "DataObject.h"
#include "Port.h"

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QPointF>
#include <QGraphicsItem>

class EditLockReleaseTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void failureStopLeavesCanvasEditable();
};

namespace {

NodeBase *addById(FlowScene *scene, const char *typeId, const QPointF &pos)
{
    // createNodeByTypeIdOrName 只创建+init，不入场景（须 adoptNode）
    NodeBase *n = scene->createNodeByTypeIdOrName(QString::fromLatin1(typeId),
                                                  NodeBase::LOGIC,
                                                  QString::fromLatin1(typeId));
    if (n)
        scene->adoptNode(n, pos);
    return n;
}

} // namespace

void EditLockReleaseTest::initTestCase()
{
    registerAllNodes();
}

void EditLockReleaseTest::failureStopLeavesCanvasEditable()
{
    FlowScene scene;
    NodeBase *gen = addById(&scene, "FormulaNode", QPointF(0, 0));
    NodeBase *caller = addById(&scene, "SubFlowNode", QPointF(220, 0));
    QVERIFY(gen && caller);
    gen->setParam(QStringLiteral("expression"), QStringLiteral("1"));
    // 引一个不存在的子流程：运行期必然失败，且不依赖任何硬件
    caller->setParam(QStringLiteral("subFlowName"), QStringLiteral("不存在的子流程"));
    QVERIFY(scene.createConnection(gen->outputPorts().first(),
                                   caller->inputPorts().first(), true));

    // 模拟"运行中"的画布锁定（MainWindow 在开始执行时会这么设）
    scene.setEditLocked(true);
    QVERIFY(scene.isEditLocked());

    auto execToken = FlowExecutorToken::create(new FlowExecutor());
    FlowExecutor &exec = *execToken->executor();
    bool callerFailed = false;
    QObject::connect(&exec, &FlowExecutor::nodeExecuted, &exec,
                     [&callerFailed, caller](NodeBase *n, bool ok) {
                         if (n == caller && !ok)
                             callerFailed = true;
                     },
                     Qt::DirectConnection);

    exec.setFlowName(QStringLiteral("EditLockReleaseTest"));
    exec.setFlowMode(FlowMode::SoftwareTrigger);
    exec.setFlowScene(&scene);
    exec.startExecution();
    bool finished = exec.wait(15000);
    if (!finished) {
        exec.stopExecution();
        exec.wait(3000);
    }
    const bool stillRunning = (exec.getState() == ExecutionState::Running);
    const bool allowsEdit = exec.allowsGraphEditing();
    exec.setFlowScene(nullptr);
    QCoreApplication::processEvents();

    QVERIFY2(callerFailed, "构造的失败节点没有按预期失败，用例前提不成立");
    QVERIFY2(!stillRunning, "失败停止后执行器仍处于 Running：画布会被永久锁死（锁无法重算）");
    QVERIFY2(allowsEdit, "失败停止后 allowsGraphEditing() 仍为 false：画布锁无法释放");

    // 按 MainWindow::updateEditLockForCurrentScene 的判据重算后，画布必须真的可编辑
    scene.setEditLocked(!allowsEdit);
    QVERIFY2(!scene.isEditLocked(), "按执行器判据重算后画布仍锁定");
    NodeGraphicsItem *item = scene.getGraphicsItemForNode(caller);
    QVERIFY(item);
    QVERIFY2(item->flags().testFlag(QGraphicsItem::ItemIsMovable),
             "解锁后节点仍不可拖动（ItemIsMovable 未恢复）");
}

QTEST_MAIN(EditLockReleaseTest)
#include "edit_lock_release_test.moc"
