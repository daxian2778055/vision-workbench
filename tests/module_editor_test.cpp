// 模块编辑窗「自动重算」挂钩验证：
//   参数控件变更 → 去抖（400ms）→ autoRecomputeRequested；连续微调只触发一次；关掉开关后不再触发。
// 说明：不 show() 窗口，因此可在无桌面的 CI（自托管 Windows runner）上稳定运行。
#include <QtTest>
#include <QScrollArea>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QAction>
#include <QFocusEvent>
#include <QApplication>
#include <QPushButton>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>

#include "ModuleEditorDialog.h"
#include "FlowScene.h"
#include "NodeBase.h"
#include "OpencvTemplateMatchNode.h"
#include "OpencvUtil.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <cmath>

class ModuleEditorTest : public QObject
{
    Q_OBJECT

private slots:
    void testParamEditTriggersDebouncedRecompute();
    void testVariableReferenceInsert();
    void testTeachingButtonLeavesMatchMode();
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

    // 拒绝必须在现场说得出原因：提示标签由 objectName 定位（不靠布局顺序），
    // 「没有目标」和「有目标但那个控件不吃引用」是两句话，混成一句等于没告诉用户下一步做什么。
    auto *hint = dlg.findChild<QLabel *>(QStringLiteral("paramHintLabel"));
    QVERIFY2(hint != nullptr, "未找到参数提示标签（objectName=paramHintLabel）");
    QCOMPARE(hint->text(), QStringLiteral("请先点一下要插入的参数输入框，再选择变量引用"));

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

    // ── U-21：数值型参数（QDoubleSpinBox）拿到引用时，调用方要走「有目标但被拒」那一句话 ──
    // 它内嵌的 qt_spinbox_lineedit 会把自己的校验器先跑一遍，引用串当场被回退，
    // 所以 insertReferenceInto 必须返回 false；若这里还报 true，用户看到的就是假成功。
    auto *spin = new QDoubleSpinBox(panel);
    spin->setRange(0, 1000);
    dlg.refreshParamHooks();
    QFocusEvent focusInSpin(QEvent::FocusIn);
    QApplication::sendEvent(spin, &focusInSpin);

    const QString numericRef = QStringLiteral("{9.numeric}");
    const bool insertedIntoNumeric = dlg.insertReference(numericRef);
    if (insertedIntoNumeric)
        QFAIL("数值型参数不接受引用串，调用方不应报告插入成功");
    QCOMPARE(hint->text(), QStringLiteral("该参数不接受变量引用（只有文本框／多行文本可以插入）"));

    // 对照腿：同一个窗口里换成真能落字的编辑框，必须报成功、且提示里带着刚插入的那个引用。
    // 只断言「含引用串」而不是整句措辞——措辞可以改，「报成功的当次必须把落下去的引用回显出来」才是不变量。
    QFocusEvent focusInEdit(QEvent::FocusIn);
    QApplication::sendEvent(edit, &focusInEdit);
    QVERIFY2(dlg.insertReference(QStringLiteral("{9.text}")), "文本框应接受变量引用");
    QVERIFY2(hint->text().contains(QStringLiteral("{9.text}")),
             qPrintable(QStringLiteral("成功提示里没有回显被插入的引用：[%1]").arg(hint->text())));
}

// U-30（推进计划 §3.31）：编辑窗的「保存模板图像」按钮是教学的另一半——它自己写盘，
// 所以「写完留在匹配模式」这条口径在这个站点上必须单独钉一次：节点 execute 里的翻转
// 由 FeatureMatchTest 的落盘腿管，这里管的是用户真正会点的那个按钮。
// 用非空 templatePath 走免对话框分支 ⇒ 无桌面 CI 也能跑。
void ModuleEditorTest::testTeachingButtonLeavesMatchMode()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "临时目录建不出来，后面的断言无从谈起");

    cv::Mat canvas(400, 400, CV_8UC1, cv::Scalar(18));
    for (int i = 0; i < 128; i += 8) {
        cv::line(canvas, cv::Point(136, 136 + i), cv::Point(263, 136 + i), cv::Scalar(220), 2);
        cv::line(canvas, cv::Point(136 + i, 136), cv::Point(136 + i, 263), cv::Scalar(30), 2);
    }

    OpencvTemplateMatchNode node;
    node.init();
    node.setInputImage(OpencvUtil::matToHimage(canvas));
    node.setParam(QStringLiteral("roiCenterCol"), 200.0);
    node.setParam(QStringLiteral("roiCenterRow"), 200.0);
    node.setParam(QStringLiteral("roiWidth"), 128.0);
    node.setParam(QStringLiteral("roiHeight"), 128.0);
    node.setParam(QStringLiteral("roiAngle"), 0.0);
    node.setParam(QStringLiteral("trainFromImage"), true);   // 用户这一轮要教学

    ModuleEditorDialog dlg(&node);
    QPushButton *saveBtn = nullptr;
    for (QPushButton *b : dlg.findChildren<QPushButton *>()) {
        if (b->text() == QStringLiteral("保存模板图像")) {
            saveBtn = b;
            break;
        }
    }
    QVERIFY2(saveBtn != nullptr, "未找到「保存模板图像」按钮 ⇒ 本条腿的前提（那个按钮还在）已失效");
    QLabel *hint = dlg.findChild<QLabel *>(QStringLiteral("paramHintLabel"));
    QVERIFY2(hint != nullptr, "未找到提示标签（objectName=paramHintLabel）");

    const QString tpl = dir.filePath(QStringLiteral("tpl_button.png"));
    node.setParam(QStringLiteral("templatePath"), tpl);
    saveBtn->click();

    QVERIFY2(QFileInfo::exists(tpl), qPrintable(QStringLiteral("点按钮没有写出模板文件：") + tpl));
    const cv::Mat written = cv::imread(tpl.toStdString(), cv::IMREAD_GRAYSCALE);
    QVERIFY2(!written.empty(), "模板文件读不回像素");
    // 落盘的必须是 ROI 那块，而不是「随手存了张图」：尺寸对上，均值也得上。
    QVERIFY2(written.rows == 128 && written.cols == 128,
             qPrintable(QStringLiteral("模板尺寸不是 ROI 的 128×128：实际 %1×%2")
                        .arg(written.cols).arg(written.rows)));
    const double roiMean = cv::mean(canvas(cv::Rect(136, 136, 128, 128)))[0];
    QVERIFY2(std::abs(cv::mean(written)[0] - roiMean) <= 8.0,
             qPrintable(QStringLiteral("模板内容与 ROI 区域不符：均值 %1 vs %2")
                        .arg(cv::mean(written)[0]).arg(roiMean)));
    QVERIFY2(!node.getParam(QStringLiteral("trainFromImage")).toBool(),
             "按钮落盘成功后节点仍停在教学态 ⇒ 下一轮 execute 会重教并覆掉刚存的模板");
    QVERIFY2(hint->text().contains(tpl),
             qPrintable(QStringLiteral("成功提示没有回显落盘路径（现场无从确认存到哪）：") + hint->text()));

    // ── 负对照：写盘失败时不得翻回匹配模式（那时磁盘上没有可用模板，翻回去等于让节点下一轮平白判红）──
    // 让 imwrite 必然失败：把模板路径挂在一个「普通文件」下面，父目录建不出来。
    const QString blocker = dir.filePath(QStringLiteral("blocker.bin"));
    {
        QFile f(blocker);
        QVERIFY2(f.open(QIODevice::WriteOnly), "写失败腿的 blocker 文件建不出来");
        f.write("x");
    }
    const QString badPath = blocker + QStringLiteral("/tpl_bad.png");
    node.setParam(QStringLiteral("trainFromImage"), true);
    node.setParam(QStringLiteral("templatePath"), badPath);
    saveBtn->click();

    QVERIFY2(!QFileInfo::exists(badPath), qPrintable(QStringLiteral("本该写失败的模板竟然落盘了：") + badPath));
    QVERIFY2(node.getParam(QStringLiteral("trainFromImage")).toBool(),
             "模板写失败却翻回匹配模式 ⇒ 下一轮因找不到文件平白判红，真实原因（保存失败）被掩盖");
    QVERIFY2(hint->text().contains(QStringLiteral("失败")),
             qPrintable(QStringLiteral("写失败时现场提示应说「失败」，实际：") + hint->text()));
}

QTEST_MAIN(ModuleEditorTest)

// 源文件内定义 Q_OBJECT 类时，AUTOMOC 要求显式包含生成的 moc
#include "module_editor_test.moc"
