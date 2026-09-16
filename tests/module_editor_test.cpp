// 模块编辑窗「自动重算」挂钩验证：
//   参数控件变更 → 去抖（400ms）→ autoRecomputeRequested；连续微调只触发一次；关掉开关后不再触发。
// 说明：不 show() 窗口，因此可在无桌面的 CI（自托管 Windows runner）上稳定运行。
#include <QtTest>
#include <QScrollArea>
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QMenu>
#include <QAction>
#include <QFocusEvent>
#include <QApplication>

#include "ModuleEditorDialog.h"
#include "FlowScene.h"
#include "NodeBase.h"

class ModuleEditorTest : public QObject
{
    Q_OBJECT

private slots:
    void testParamEditTriggersDebouncedRecompute();
    void testVariableReferenceInsert();
};

void ModuleEditorTest::testParamEditTriggersDebouncedRecompute()
{
    FlowScene scene;
    NodeBase *node = scene.createNode(NodeBase::IMAGE_PROCESSING, QPointF(120, 120),
                                      QStringLiteral("OpenCV二值化"));
    QVERIFY2(node != nullptr, "无法创建测试节点");

    ModuleEditorDialog dlg(node);
    int autoRequests = 0;
    QObject::connect(&dlg, &ModuleEditorDialog::autoRecomputeRequested, &dlg,
                     [&autoRequests]() { ++autoRequests; }, Qt::DirectConnection);

    // 参数面板由节点自建、控件种类不定；注入一个受挂钩控件，使测试不依赖具体节点实现
    auto *host = dlg.findChild<QScrollArea *>();
    QVERIFY2(host != nullptr, "未找到参数面板容器");
    QWidget *panel = host->widget();
    QVERIFY2(panel != nullptr, "参数面板为空");
    auto *spin = new QSpinBox(panel);   // 以面板为父对象 → 会被挂钩逻辑发现
    spin->setRange(0, 1000);
    dlg.refreshParamHooks();            // 面板内容变化后重新挂钩

    // 连续微调三次 → 去抖后只请求一次
    spin->setValue(1);
    spin->setValue(2);
    spin->setValue(3);
    QCOMPARE(autoRequests, 0);          // 去抖期内不应立即请求
    QTest::qWait(700);
    QCOMPARE(autoRequests, 1);

    // 再次变更仍能触发（挂钩未失效）
    spin->setValue(4);
    QTest::qWait(700);
    QCOMPARE(autoRequests, 2);

    // 关掉「自动重算」后，再改也不请求
    auto *toggle = dlg.findChild<QCheckBox *>();
    QVERIFY2(toggle != nullptr, "未找到「自动重算」开关");
    toggle->setChecked(false);
    spin->setValue(5);
    QTest::qWait(700);
    QCOMPARE(autoRequests, 2);
}

void ModuleEditorTest::testVariableReferenceInsert()
{
    FlowScene scene;
    NodeBase *node = scene.createNode(NodeBase::IMAGE_PROCESSING, QPointF(120, 120),
                                      QStringLiteral("OpenCV二值化"));
    QVERIFY2(node != nullptr, "无法创建测试节点");

    ModuleEditorDialog dlg(node);

    // 注入一个参数编辑框（模拟节点自建的参数控件）
    auto *host = dlg.findChild<QScrollArea *>();
    QVERIFY2(host != nullptr, "未找到参数面板容器");
    QWidget *panel = host->widget();
    QVERIFY2(panel != nullptr, "参数面板为空");
    auto *edit = new QLineEdit(panel);
    edit->setText(QStringLiteral("阈值="));
    edit->setCursorPosition(edit->text().size());
    dlg.refreshParamHooks();   // 挂钩时会给控件装事件过滤器（用于记住最近编辑的控件）

    // 还没有编辑框获得过焦点时应拒绝插入，而不是静默失败
    const bool insertedWithoutTarget = dlg.insertReference(QStringLiteral("{9.none}"));
    if (insertedWithoutTarget)
        QFAIL("没有任何参数编辑框被编辑过时不应报告插入成功");

    // 模拟该编辑框获得焦点（不 show 窗口也能派发焦点事件）
    QFocusEvent focusIn(QEvent::FocusIn);
    QApplication::sendEvent(edit, &focusIn);

    dlg.setVariableReferenceProvider([]() {
        return QStringList{QStringLiteral("{3.foregroundPixels}"),
                           QStringLiteral("{global.triggerCount}")};
    });
    auto *menu = dlg.findChild<QMenu *>(QStringLiteral("varRefMenu"));
    QVERIFY2(menu != nullptr, "未找到变量引用菜单");
    QCOMPARE(menu->actions().size(), 2);
    QCOMPARE(menu->actions().at(0)->text(), QStringLiteral("{3.foregroundPixels}"));

    // 触发第一个菜单项 → 引用插入到光标处（成为下游可解析的表达式）
    menu->actions().at(0)->trigger();
    QCOMPARE(edit->text(), QStringLiteral("阈值={3.foregroundPixels}"));

    // 无可用变量时给出不可点的提示项（避免空菜单让用户以为坏了）
    dlg.setVariableReferenceProvider([]() { return QStringList(); });
    QCOMPARE(menu->actions().size(), 1);
    QVERIFY2(!menu->actions().at(0)->isEnabled(), "空列表提示项应不可点击");
}

QTEST_MAIN(ModuleEditorTest)

// 源文件内定义 Q_OBJECT 类时，AUTOMOC 要求显式包含生成的 moc
#include "module_editor_test.moc"
