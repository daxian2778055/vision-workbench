// 模块编辑窗「自动重算」挂钩验证：
//   参数控件变更 → 去抖（400ms）→ autoRecomputeRequested；连续微调只触发一次；关掉开关后不再触发。
// 说明：不 show() 窗口，因此可在无桌面的 CI（自托管 Windows runner）上稳定运行。
#include <QtTest>
#include <QScrollArea>
#include <QSpinBox>
#include <QCheckBox>

#include "ModuleEditorDialog.h"
#include "FlowScene.h"
#include "NodeBase.h"

class ModuleEditorTest : public QObject
{
    Q_OBJECT

private slots:
    void testParamEditTriggersDebouncedRecompute();
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

QTEST_MAIN(ModuleEditorTest)

// 源文件内定义 Q_OBJECT 类时，AUTOMOC 要求显式包含生成的 moc
#include "module_editor_test.moc"
