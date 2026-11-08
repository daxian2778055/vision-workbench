// 模块编辑窗「自动重算」挂钩验证：
//   参数控件变更 → 去抖（400ms）→ autoRecomputeRequested；连续微调只触发一次；关掉开关后不再触发。
// P1-10（对标 VM 确定/取消语义）：自动预览默认**关闭**——改动只暂存，点「确定」才提交；
//   本用例显式勾选后才验证去抖链路。
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
#include <QToolButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QListWidget>
#include <QRadioButton>
#include <QSignalSpy>
#include <QComboBox>
#include <QGroupBox>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>

#include "ModuleEditorDialog.h"
#include "TemplateConfigDialog.h"
#include "ImageDisplayController.h"
#include "HalconWindow.h"
#include "FlowScene.h"
#include "NodeBase.h"
#include "OpencvTemplateMatchNode.h"
#include "OpencvPixelStatsNode.h"
#include "DataObject.h"
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
    void testVmModuleWindowTabs();
    void testDrawRequestsMainCanvas();
    void testCanvasPickWritesRegionWithoutTraining();
    void testTemplateConfigUndoRedo();
    void testAllModuleWindowsArePureTabs();
    void testResultOverlayUsesDisplayConfig();
    void testEditorNodePointerClearsOnDelete();
    void testModuleResultPageUsesResultFields();
    // P0-5：结果表「尺度 Y」不得复制「尺度 X」（XY 模式两轴独立）
    void testTemplateResultTableUsesPerTargetScaleY();
    void testTemplateListCheckAndReorder();
    void testTemplateConfigEnhanceCheckboxWritesEntry();
    void testTemplateConfigScaleModeWritesEntry();
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

    // P1-10：自动预览默认关闭 ⇒ 改参数不应发起重算（改动只暂存，等「确定」提交）
    spin->setValue(1);
    spin->setValue(2);
    QTest::qWait(700);
    QCOMPARE(autoRequests, 0);

    auto *toggle = dlg.findChild<QCheckBox *>(QStringLiteral("autoRecomputeCheck"));
    QVERIFY2(toggle != nullptr, "未找到「自动预览」开关");
    QVERIFY2(!toggle->isChecked(), "自动预览默认应为关闭（P1-10 确定/取消语义）");
    toggle->setChecked(true);           // 显式打开：之后才走「改参数即重算」链路

    // 连续微调三次 → 去抖后只请求一次
    spin->setValue(3);
    spin->setValue(4);
    spin->setValue(5);
    QCOMPARE(autoRequests, 0);          // 去抖期内不应立即请求
    QTest::qWait(700);
    QCOMPARE(autoRequests, 1);

    // 再次变更仍能触发（挂钩未失效）
    spin->setValue(6);
    QTest::qWait(700);
    QCOMPARE(autoRequests, 2);

    // 关掉「自动重算」后，再改也不请求
    toggle->setChecked(false);
    spin->setValue(7);
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

void ModuleEditorTest::testVmModuleWindowTabs()
{
    // VM 4.4 模块窗框架：4 个页签（基本参数 / 特征模板 / 运行参数 / 结果显示），
    // 输入端口只有 1 个。旧框架的 关于/输入参数/输出参数/ROI配置/参数面板/连线/工具特性 不得再出现。
    FlowScene scene;
    NodeBase *node = scene.createNode(NodeBase::IMAGE_PROCESSING, QPointF(80, 80),
                                      QStringLiteral("模板匹配"));
    QVERIFY(node);
    QCOMPARE(node->property("vfpNodeTypeId").toString(), QStringLiteral("OpencvTemplateMatchNode"));
    QCOMPARE(node->inputPorts().size(), 1);

    ModuleEditorDialog dlg(node);
    QTabWidget *tabs = dlg.findChild<QTabWidget *>(QStringLiteral("vmTabs"));
    QVERIFY2(tabs, "模块窗没有 VM 页签容器");
    QStringList names;
    for (int i = 0; i < tabs->count(); ++i)
        names << tabs->tabText(i);
    QStringList expected {QStringLiteral("基本参数"), QStringLiteral("特征模板"),
                          QStringLiteral("运行参数"), QStringLiteral("结果显示")};
    QCOMPARE(names, expected);
    // VM 轮廓匹配的固定结果判断行（数量/角度/尺度X/Y/分数/匹配点X/Y/匹配框X/Y）
    QTableWidget *judge = dlg.findChild<QTableWidget *>(QStringLiteral("templateJudgeTable"));
    QVERIFY2(judge, "模板匹配没有 VM 固定结果判断表");
    QCOMPARE(judge->rowCount(), 10);
    QCOMPARE(judge->columnCount(), 4);
    // VM 基本参数：输出掩膜开关 + ROI 区域类型/ROI 创建单选圆钮
    QVERIFY2(dlg.findChild<QCheckBox *>(QStringLiteral("outputMaskSwitch")),
             "缺少 VM 输出掩膜开关");
    QStringList radios;
    for (QRadioButton *r : dlg.findChildren<QRadioButton *>())
        radios << r->text();
    QVERIFY2(radios.contains(QStringLiteral("图形类型")) && radios.contains(QStringLiteral("图像类型")),
             qPrintable(QStringLiteral("缺少 ROI 区域类型单选：%1").arg(radios.join(QStringLiteral("、")))));
    QVERIFY2(radios.contains(QStringLiteral("绘制")) && radios.contains(QStringLiteral("继承")),
             qPrintable(QStringLiteral("缺少 ROI 创建单选：%1").arg(radios.join(QStringLiteral("、")))));

    // VM 基本参数的组顺序：图像输入 → ROI掩膜图像（订阅）→ ROI区域 → 检测区域。
    // 之前订阅组被搬到 ROI 组下面，与 VM 截图不符（2026-11-04 现场对照）。
    QStringList groupOrder;
    for (int i = 0; i < tabs->widget(0)->layout()->count(); ++i) {
        if (auto *gb = qobject_cast<QGroupBox *>(tabs->widget(0)->layout()->itemAt(i)->widget()))
            groupOrder << gb->title();
    }
    const int iInput = groupOrder.indexOf(QStringLiteral("图像输入"));
    const int iSub = groupOrder.indexOf(QStringLiteral("ROI掩膜图像（订阅）"));
    const int iRoi = groupOrder.indexOf(QStringLiteral("ROI区域"));
    const int iDetect = groupOrder.indexOf(QStringLiteral("检测区域"));
    QVERIFY2(iInput >= 0 && iSub > iInput && iRoi > iSub && iDetect > iRoi,
             qPrintable(QStringLiteral("基本参数组顺序不是 VM 口径：%1")
                            .arg(groupOrder.join(QStringLiteral(" → ")))));
    // 「（参数修改后自动执行）」是全角括号，旧的半角前缀匹配漏隐藏（快照里仍可见）。
    for (QLabel *lb : dlg.findChildren<QLabel *>()) {
        if (lb->text().contains(QStringLiteral("参数修改后自动执行")))
            QVERIFY2(!lb->isVisibleTo(&dlg), "自动预览说明标签仍未隐藏");
    }

    // 标题按 VM：模块号 + 模块名
    QVERIFY2(dlg.windowTitle().startsWith(QString::number(node->moduleId())),
             qPrintable(QStringLiteral("标题不是模块号开头：%1").arg(dlg.windowTitle())));

    // 可选自检：设置 VFP_UI_SNAPSHOT 后把模块窗渲染成 PNG，供人工对照 VM 布局。
    const QString snapshot = qEnvironmentVariable("VFP_UI_SNAPSHOT");
    if (!snapshot.isEmpty()) {
        dlg.resize(520, 680);
        dlg.show();
        QTest::qWait(120);
        QVERIFY2(dlg.grab().save(snapshot), qPrintable(QStringLiteral("快照保存失败：%1").arg(snapshot)));
        // 特征模板页：训练入口是否显眼（VM：先框区域，再点训练）
        if (tabs->count() > 1) {
            tabs->setCurrentIndex(1);
            QTest::qWait(80);
            QVERIFY2(dlg.grab().save(snapshot + QStringLiteral(".feature.png")),
                     "特征模板页快照保存失败");
        }
        // 运行参数页：确认去掉 roiCol/roiRow 后没有空行/重复 ROI 字段
        if (tabs->count() > 2) {
            tabs->setCurrentIndex(2);
            QTest::qWait(80);
            QVERIFY2(dlg.grab().save(snapshot + QStringLiteral(".run.png")),
                     "运行参数页快照保存失败");
        }
    }

    // 可选自检：模板配置窗（图标工具条 + 右栏）
    const QString tmplSnapshot = qEnvironmentVariable("VFP_UI_SNAPSHOT_TMPL");
    if (!tmplSnapshot.isEmpty()) {
        TemplateConfigDialog cfg(qobject_cast<OpencvTemplateMatchNode *>(node));
        cfg.resize(1024, 720);
        cfg.show();
        QTest::qWait(120);
        QVERIFY2(cfg.grab().save(tmplSnapshot),
                 qPrintable(QStringLiteral("模板配置窗快照保存失败：%1").arg(tmplSnapshot)));
    }
}

// 现场问题（2026-11-04）：模块窗的「绘制」点了没反应——它操作的是本窗里已被隐藏的画布。
// 现在必须发出 templateRoiPickRequested（ROI 区域=模板框），由 MainWindow 在主画布上拾取；
// 检测区域的两个按钮发 searchRegionPickRequested（矩形/多边形）。
void ModuleEditorTest::testDrawRequestsMainCanvas()
{
    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("searchRegionCol"), 100);
    node.setParam(QStringLiteral("searchRegionRow"), 80);
    node.setParam(QStringLiteral("searchRegionWidth"), 200);
    node.setParam(QStringLiteral("searchRegionHeight"), 160);
    ModuleEditorDialog dlg(&node);

    QSignalSpy tmplSpy(&dlg, &ModuleEditorDialog::templateRoiPickRequested);
    QSignalSpy searchSpy(&dlg, &ModuleEditorDialog::searchRegionPickRequested);
    QSignalSpy coordSpy(&dlg, &ModuleEditorDialog::coordinatePickRequested);
    QComboBox *shape = dlg.findChild<QComboBox *>(QStringLiteral("roiShapeCombo"));
    QVERIFY2(shape, "ROI 区域缺少形状下拉（旋转矩形/矩形）");
    QCOMPARE(shape->count(), 2);

    QPushButton *drawBtn = nullptr;
    QPushButton *boxBtn = nullptr;
    QPushButton *polyBtn = nullptr;
    for (QPushButton *b : dlg.findChildren<QPushButton *>()) {
        if (b->text() == QStringLiteral("绘制"))
            drawBtn = b;
        else if (b->text() == QStringLiteral("框选"))
            boxBtn = b;
        else if (b->text() == QStringLiteral("多边形"))
            polyBtn = b;
    }
    QVERIFY2(drawBtn, "ROI 区域没有「绘制」按钮 ⇒ 现场无从发起绘制");
    QVERIFY2(boxBtn && polyBtn, "检测区域没有「框选/多边形」绘制入口");

    // ROI 区域：默认旋转矩形 → 切换成矩形，两次都要发模板框拾取请求
    drawBtn->click();
    QCOMPARE(tmplSpy.count(), 1);
    QCOMPARE(tmplSpy.at(0).at(1).toBool(), true);   // 旋转矩形
    shape->setCurrentIndex(1);                      // 矩形
    drawBtn->click();
    QCOMPARE(tmplSpy.count(), 2);
    QCOMPARE(tmplSpy.at(1).at(1).toBool(), false);

    // 检测区域：矩形 / 多边形
    boxBtn->click();
    QCOMPARE(searchSpy.count(), 1);
    QCOMPARE(searchSpy.at(0).at(1).toBool(), false);
    polyBtn->click();
    QCOMPARE(searchSpy.count(), 2);
    QCOMPARE(searchSpy.at(1).at(1).toBool(), true);

    // VM 同款：每个检测区域输入框右侧的取点按钮（0=左列 1=上行 2=宽度 3=高度）
    QToolButton *pickWidth = dlg.findChild<QToolButton *>(QStringLiteral("searchPick2"));
    QVERIFY2(pickWidth, "检测区域「宽度」缺少图像取点按钮");
    pickWidth->click();
    QCOMPARE(coordSpy.count(), 1);
    QCOMPARE(coordSpy.at(0).at(1).toInt(), 2);
}

// 画布拾取 → 参数写回（与 UI 信号链互补）：直接调控制器，钉住
//   ① ROI 区域绘制写回模板框且**不自动训练**（VM：先框区域，再点训练）；
//   ② 检测区域矩形/多边形写回 searchRegion*，多边形优先。
void ModuleEditorTest::testCanvasPickWritesRegionWithoutTraining()
{
    FlowScene scene;
    NodeBase *node = scene.createNode(NodeBase::IMAGE_PROCESSING, QPointF(80, 80),
                                      QStringLiteral("模板匹配"));
    QVERIFY(node);
    OpencvTemplateMatchNode *tmpl = qobject_cast<OpencvTemplateMatchNode *>(node);
    QVERIFY(tmpl);

    HalconWindow view;
    ImageDisplayController controller(&view, nullptr);

    // ① ROI 区域（模板框）：旋转矩形绘制后只写 roi* 参数，训练必须仍为 false
    QVERIFY(controller.startTemplateRoiPick(node, /*rotatedRect=*/true));
    RoiShape roi;
    roi.type = RoiType::RotatedRect;
    roi.p1 = QPointF(200.0, 160.0);
    roi.width = 120.0;
    roi.height = 90.0;
    roi.angleDeg = 0.0;
    QVERIFY(controller.writeRoiToNode(roi) != nullptr);
    QCOMPARE(tmpl->getParam(QStringLiteral("roiWidth")).toInt(), 120);
    QCOMPARE(tmpl->getParam(QStringLiteral("roiHeight")).toInt(), 90);
    QVERIFY2(!tmpl->getParam(QStringLiteral("trainFromImage")).toBool(),
             "框完 ROI 自动进入训练态 ⇒ 用户会以为「训练模板」按钮是摆设");

    // ② 检测区域：矩形 → 4 个 searchRegion 参数 + 清掉旧多边形
    QVERIFY(controller.startSearchRegionPick(node, /*polygon=*/false));
    RoiShape rect;
    rect.type = RoiType::Rect;
    rect.p1 = QPointF(40.0, 30.0);
    rect.p2 = QPointF(240.0, 190.0);
    QVERIFY(controller.writeRoiToNode(rect) != nullptr);
    QCOMPARE(tmpl->getParam(QStringLiteral("searchRegionCol")).toInt(), 40);
    QCOMPARE(tmpl->getParam(QStringLiteral("searchRegionRow")).toInt(), 30);
    QCOMPARE(tmpl->getParam(QStringLiteral("searchRegionWidth")).toInt(), 200);
    QCOMPARE(tmpl->getParam(QStringLiteral("searchRegionHeight")).toInt(), 160);
    QVERIFY(tmpl->getParam(QStringLiteral("searchRegionPolygon")).toString().isEmpty());

    // ③ 单字段取点：宽度/高度按当前左列/上行计算
    QVERIFY(controller.startCoordinatePick(node, 2));   // 宽度
    RoiShape pointW;
    pointW.type = RoiType::Point;
    pointW.p1 = QPointF(250.0, 100.0);
    QVERIFY(controller.writeRoiToNode(pointW) != nullptr);
    QCOMPARE(tmpl->getParam(QStringLiteral("searchRegionWidth")).toInt(), 210);
    QCOMPARE(tmpl->getParam(QStringLiteral("searchRegionCol")).toInt(), 40);

    QVERIFY(controller.startCoordinatePick(node, 3));   // 高度
    RoiShape pointH;
    pointH.type = RoiType::Point;
    pointH.p1 = QPointF(100.0, 200.0);
    QVERIFY(controller.writeRoiToNode(pointH) != nullptr);
    QCOMPARE(tmpl->getParam(QStringLiteral("searchRegionHeight")).toInt(), 170);

    // ④ 检测区域：多边形 → 写点串，矩形宽高归零（多边形优先于矩形）
    QVERIFY(controller.startSearchRegionPick(node, /*polygon=*/true));
    RoiShape poly;
    poly.type = RoiType::Polygon;
    poly.points = {QPointF(20.0, 20.0), QPointF(220.0, 40.0), QPointF(120.0, 200.0)};
    QVERIFY(controller.writeRoiToNode(poly) != nullptr);
    QVERIFY(!tmpl->getParam(QStringLiteral("searchRegionPolygon")).toString().isEmpty());
    QCOMPARE(tmpl->getParam(QStringLiteral("searchRegionWidth")).toInt(), 0);
    QCOMPARE(tmpl->getParam(QStringLiteral("searchRegionHeight")).toInt(), 0);
}

// 模板配置工具条的撤销/重做：形状落下一块 → 撤销回到空 → 重做恢复。
// VM 工具条这两个按钮是活的，旧版一直置灰。
void ModuleEditorTest::testTemplateConfigUndoRedo()
{
    OpencvTemplateMatchNode node;
    node.init();
    TemplateConfigDialog cfg(&node);

    QPushButton *undoBtn = nullptr;
    QPushButton *redoBtn = nullptr;
    for (QPushButton *b : cfg.findChildren<QPushButton *>()) {
        if (b->toolTip() == QStringLiteral("撤销"))
            undoBtn = b;
        else if (b->toolTip() == QStringLiteral("重做"))
            redoBtn = b;
    }
    QVERIFY2(undoBtn && redoBtn, "模板配置工具条缺少撤销/重做按钮");
    QVERIFY2(!undoBtn->isEnabled(), "没有历史时撤销应置灰");
    QVERIFY2(!redoBtn->isEnabled(), "没有历史时重做应置灰");

    RoiShape rect;
    rect.type = RoiType::Rect;
    rect.p1 = QPointF(10, 10);
    rect.p2 = QPointF(70, 60);
    QVERIFY(QMetaObject::invokeMethod(&cfg, "onRoiEdited", Qt::DirectConnection,
                                      Q_ARG(RoiShape, rect)));
    QVERIFY2(undoBtn->isEnabled(), "落下一块形状后撤销应可用");

    QVERIFY(QMetaObject::invokeMethod(&cfg, "onUndo", Qt::DirectConnection));
    QVERIFY2(!undoBtn->isEnabled(), "撤销回初始状态后撤销应再次置灰");
    QVERIFY2(redoBtn->isEnabled(), "撤销后重做应可用");

    QVERIFY(QMetaObject::invokeMethod(&cfg, "onRedo", Qt::DirectConnection));
    QVERIFY2(undoBtn->isEnabled(), "重做恢复后撤销应再次可用");
    QVERIFY2(!redoBtn->isEnabled(), "重做到栈顶后重做应置灰");
}

// 纯页签窗（VM 口径）：所有算子都不再有内置图像区/工具条；
// 通用 ROI/掩膜入口只发请求，实际绘制由主画布承接。
void ModuleEditorTest::testAllModuleWindowsArePureTabs()
{
    FlowScene scene;
    NodeBase *crop = scene.createNode(NodeBase::IMAGE_PROCESSING, QPointF(80, 80),
                                      QStringLiteral("ROI裁剪"));
    QVERIFY(crop);
    ModuleEditorDialog dlg(crop);
    QTabWidget *tabs = dlg.findChild<QTabWidget *>(QStringLiteral("vmTabs"));
    QVERIFY2(tabs, "所有算子都应有 VM 四页签框架（非模板为三页）");
    QCOMPARE(tabs->count(), 3);
    QWidget *toolbarHost = dlg.findChild<QWidget *>(QStringLiteral("moduleToolbarHost"));
    QVERIFY2(toolbarHost && toolbarHost->isHidden(), "模块窗不应再显示顶部工具条");
    HalconWindow *view = dlg.findChild<HalconWindow *>();
    QVERIFY2(view && view->isHidden(), "模块窗不应再内置图像区");

    QPushButton *drawBtn = dlg.findChild<QPushButton *>(QStringLiteral("genericRoiDrawButton"));
    QVERIFY2(drawBtn, "有几何 ROI 的算子应出现「ROI区域/绘制」入口");
    QSignalSpy geoSpy(&dlg, &ModuleEditorDialog::geometryPickRequested);
    drawBtn->click();
    QCOMPARE(geoSpy.count(), 1);

    // 可选自检：非模板窗纯页签快照（人工对照 VM 布局）
    const QString snap = qEnvironmentVariable("VFP_UI_SNAPSHOT_PURETAB");
    if (!snap.isEmpty())
        QVERIFY2(dlg.grab().save(snap), qPrintable(QStringLiteral("快照保存失败：%1").arg(snap)));

    // 掩膜：二值化支持 editMask，模块窗只发主画布编辑请求
    NodeBase *thr = scene.createNode(NodeBase::IMAGE_PROCESSING, QPointF(320, 80),
                                     QStringLiteral("二值化"));
    QVERIFY(thr);
    ModuleEditorDialog maskDlg(thr);
    QPushButton *maskBtn = maskDlg.findChild<QPushButton *>(QStringLiteral("genericMaskEditButton"));
    QVERIFY2(maskBtn, "支持掩膜的算子应出现「掩膜/编辑掩膜」入口");
    QSignalSpy maskSpy(&maskDlg, &ModuleEditorDialog::maskEditRequested);
    maskBtn->click();
    QCOMPARE(maskSpy.count(), 1);

    // 二值化类型切换 → 只显示该类型自己的参数（VM 各类型参数分组）
    QComboBox *typeCombo = maskDlg.findChild<QComboBox *>(QStringLiteral("binaryType"));
    QSpinBox *lowSpin = maskDlg.findChild<QSpinBox *>(QStringLiteral("lowThreshold"));
    QSpinBox *kwSpin = maskDlg.findChild<QSpinBox *>(QStringLiteral("kernelWidth"));
    QVERIFY2(typeCombo && lowSpin && kwSpin, "二值化运行参数控件缺失");
    QVERIFY2(lowSpin->isHidden(), "默认均值二值化不应显示硬阈值参数");
    QVERIFY2(!kwSpin->isHidden(), "默认均值二值化应显示滤波核参数");
    typeCombo->setCurrentIndex(0);   // 硬阈值二值化
    QApplication::processEvents();
    QVERIFY2(!lowSpin->isHidden(), "硬阈值二值化应显示低阈值");
    QVERIFY2(kwSpin->isHidden(), "硬阈值二值化不应显示均值滤波核");

    const QString binarySnap = qEnvironmentVariable("VFP_UI_SNAPSHOT_BINARY");
    if (!binarySnap.isEmpty())
        QVERIFY2(maskDlg.grab().save(binarySnap),
                 qPrintable(QStringLiteral("快照保存失败：%1").arg(binarySnap)));
    const QString binaryRunSnap = qEnvironmentVariable("VFP_UI_SNAPSHOT_BINARY_RUN");
    if (!binaryRunSnap.isEmpty()) {
        if (QTabWidget *maskTabs = maskDlg.findChild<QTabWidget *>(QStringLiteral("vmTabs")))
            maskTabs->setCurrentIndex(1);   // 运行参数页
        QApplication::processEvents();
        QVERIFY2(maskDlg.grab().save(binaryRunSnap),
                 qPrintable(QStringLiteral("快照保存失败：%1").arg(binaryRunSnap)));
    }
}

// 2026-11-06：VM「结果显示 → 图像显示/文本显示」真正作用到画布叠加层。
// 老工程没有 resImage*/resText* 键时保持旧语义；一旦配置，颜色/透明度/线宽/四角文本都要生效。
void ModuleEditorTest::testResultOverlayUsesDisplayConfig()
{
    OpencvPixelStatsNode node;
    node.init();
    node.setParam(QStringLiteral("minGray"), 3.0);
    node.setParam(QStringLiteral("maxGray"), 40.0);
    node.setParam(QStringLiteral("meanGray"), 12.5);
    node.setParam(QStringLiteral("moduleStatus"), true);

    node.setParam(QStringLiteral("resImageColor"), QStringLiteral("#FF0000"));
    node.setParam(QStringLiteral("resImageOpacity"), 40);
    node.setParam(QStringLiteral("resImageLineWidth"), 5);
    node.setParam(QStringLiteral("resShowText"), true);
    node.setParam(QStringLiteral("resTextContent"),
                  QStringLiteral("{module}|{status}|{meanGray}"));
    node.setParam(QStringLiteral("resTextColor"), QStringLiteral("#00FF00"));
    node.setParam(QStringLiteral("resTextFontSize"), 20);
    node.setParam(QStringLiteral("resTextOpacity"), 60);
    node.setParam(QStringLiteral("resTextPosition"), 3);

    MeasureResult line;
    line.valid = true;
    line.type = QStringLiteral("line");
    line.point1 = QPointF(10.0, 20.0);
    line.point2 = QPointF(110.0, 120.0);
    auto data = QSharedPointer<DataObject>::create();
    data->setMeasureResult(line);
    node.setOutputData(0, data);

    HalconWindow view;
    ImageDisplayController controller(&view, nullptr);
    // 执行器推送的结果快照：{elapsed} 只存在于 vars（节点参数表没有 elapsedMs）
    controller.setResultVarsProvider([](NodeBase *n) {
        QVariantMap vars;
        vars.insert(QStringLiteral("meanGray"), 12.5);
        vars.insert(QStringLiteral("elapsedMs"), 21.0);
        if (n)
            vars.insert(QStringLiteral("moduleStatus"),
                        n->getParam(QStringLiteral("moduleStatus")));
        return vars;
    });

    const QVector<OverlayShape> shapes = controller.collectOverlayFromNode(&node);
    const OverlayShape *lineShape = nullptr;
    const OverlayShape *textShape = nullptr;
    for (const OverlayShape &s : shapes) {
        if (s.type == OverlayShape::Type::Line)
            lineShape = &s;
        else if (s.type == OverlayShape::Type::Text && s.textPosition >= 0)
            textShape = &s;
    }
    QVERIFY2(lineShape != nullptr, "结果线没有生成叠加图元");
    QCOMPARE(lineShape->color, QColor(255, 0, 0));   // 「叠加颜色」
    QCOMPARE(lineShape->lineWidth, 5);               // 「线宽」
    QCOMPARE(lineShape->opacity, 40);                // 「透明度」

    QVERIFY2(textShape != nullptr, "文本显示未生成四角文本图元");
    QCOMPARE(textShape->textPosition, 3);            // 「位置」= 右下
    QCOMPARE(textShape->fontSize, 20);               // 「字号」
    QCOMPARE(textShape->opacity, 60);                // 「透明度」
    QCOMPARE(textShape->color, QColor(0, 255, 0));   // 「颜色」
    // {module}/{status}/{meanGray}：meanGray 按 resultDisplayFields 的 2 位精度渲染
    QCOMPARE(textShape->text, QStringLiteral("灰度统计|OK|12.50"));

    // {elapsed} 从执行器快照解析（节点参数表没有 elapsedMs，只能走 vars）
    node.setParam(QStringLiteral("resTextContent"), QStringLiteral("{elapsed}ms"));
    const QVector<OverlayShape> timed = controller.collectOverlayFromNode(&node);
    const OverlayShape *timedText = nullptr;
    for (const OverlayShape &s : timed) {
        if (s.type == OverlayShape::Type::Text && s.textPosition >= 0)
            timedText = &s;
    }
    QVERIFY(timedText != nullptr);
    QCOMPARE(timedText->text, QStringLiteral("21ms"));

    // 取消「显示结果图像」：结果图元整体不再绘制（灰度统计没有 ROI 参照，叠加应为空）
    node.setParam(QStringLiteral("resShowImage"), false);
    QVERIFY2(controller.collectOverlayFromNode(&node).isEmpty(),
             "关闭「显示结果图像」后仍在画结果图元");
}

// 2026-11-06：非模板模块窗的「结果显示」页改按 resultDisplayFields() 渲染，
// 与运行界面结果表同字段/名称/单位/精度；具名输出端口只在没有结果规范时兜底。
void ModuleEditorTest::testModuleResultPageUsesResultFields()
{
    OpencvPixelStatsNode node;
    node.init();
    node.setParam(QStringLiteral("minGray"), 3.0);
    node.setParam(QStringLiteral("maxGray"), 40.0);
    node.setParam(QStringLiteral("meanGray"), 12.5);
    node.setParam(QStringLiteral("moduleStatus"), true);

    ModuleEditorDialog dlg(&node);
    QTableWidget *table = dlg.findChild<QTableWidget *>(QStringLiteral("moduleResultTable"));
    QVERIFY2(table != nullptr, "模块窗缺少结果表（objectName=moduleResultTable）");
    QCOMPARE(table->columnCount(), 4);
    QCOMPARE(table->horizontalHeaderItem(0)->text(), QStringLiteral("结果项"));
    QCOMPARE(table->horizontalHeaderItem(2)->text(), QStringLiteral("单位"));

    auto findRow = [table](const QString &label) {
        for (int r = 0; r < table->rowCount(); ++r) {
            if (table->item(r, 0) && table->item(r, 0)->text() == label)
                return r;
        }
        return -1;
    };
    const int statusRow = findRow(QStringLiteral("模块状态"));
    QVERIFY2(statusRow >= 0, "结果表没有「模块状态」行 ⇒ 仍在按具名输出渲染");
    QCOMPARE(table->item(statusRow, 1)->text(), QStringLiteral("OK"));

    const int meanRow = findRow(QStringLiteral("平均灰度"));
    QVERIFY2(meanRow >= 0, "结果表缺少算子声明的「平均灰度」字段");
    QCOMPARE(table->item(meanRow, 1)->text(), QStringLiteral("12.50"));   // 字段精度 2 位

    // 耗时由执行器统一推送，模块窗不得再自行编造数值
    const int elapsedRow = findRow(QStringLiteral("耗时"));
    QVERIFY2(elapsedRow >= 0, "结果表缺少「耗时」行");
    QCOMPARE(table->item(elapsedRow, 1)->text(), QStringLiteral("-"));
    QCOMPARE(table->item(elapsedRow, 2)->text(), QStringLiteral("ms"));
}

// 复核（扫描清单「删除节点时脏编辑器继续执行」）：编辑器窗口可能比节点活得久。
// m_node 原是裸 NodeBase*，节点析构后指针仍非空 ⇒ 8 处 `if (!m_node)` 全部失效，
// 随后的 m_node->execute() / 参数读写直接踩已释放内存。改成 QPointer 后，
// 节点析构即自动置空，判空逻辑立刻生效。
void ModuleEditorTest::testEditorNodePointerClearsOnDelete()
{
    FlowScene scene;
    NodeBase *node = scene.createNode(NodeBase::IMAGE_PROCESSING, QPointF(40, 40),
                                      QStringLiteral("OpenCV二值化"));
    QVERIFY2(node != nullptr, "无法创建测试节点");

    ModuleEditorDialog dlg(node);
    QVERIFY2(dlg.node() == node, "构造后应持有该节点");

    // 现场形态：删掉节点时模块配置窗还开着
    scene.removeNode(node);
    QVERIFY2(dlg.node() == nullptr,
             "节点删除后编辑器持有的节点必须自动置空（QPointer）；"
             "若仍非空，后续 m_node->execute() 就是 UAF");
}

// P0-5：模板匹配结果表的「尺度X / 尺度Y」必须分别取每目标自己的两个列表。
// 旧实现把尺度 Y 直接复制成尺度 X，非均匀缩放目标在结果表里看不出 Y 向差异。
void ModuleEditorTest::testTemplateResultTableUsesPerTargetScaleY()
{
    OpencvTemplateMatchNode node;
    node.init();
    node.setParam(QStringLiteral("matchList"),
                  QStringLiteral("200,200,0.900,0.00,1.0000,200,200;"
                                 "300,400,0.800,0.00,0.6000,300,400"));
    node.setParam(QStringLiteral("matchScaleXs"), QStringLiteral("1.0000,0.6000"));
    node.setParam(QStringLiteral("matchScaleYs"), QStringLiteral("0.8000,1.3000"));

    ModuleEditorDialog dlg(&node);
    dlg.reloadFromNode();   // 构造函数已填过一次；这里显式重走公开入口，确保读的是当前参数表
    auto *table = dlg.findChild<QTableWidget *>(QStringLiteral("moduleResultTable"));
    QVERIFY2(table != nullptr, "未找到结果表 moduleResultTable");
    QCOMPARE(table->rowCount(), 2);
    QCOMPARE(table->item(0, 6)->text(), QStringLiteral("1.0000"));   // 尺度X
    QCOMPARE(table->item(0, 7)->text(), QStringLiteral("0.8000"));   // 尺度Y
    QCOMPARE(table->item(1, 6)->text(), QStringLiteral("0.6000"));
    QCOMPARE(table->item(1, 7)->text(), QStringLiteral("1.3000"));
}

// P1-3（VM「特征模板」页）：勾选模板 + 上移/下移。
// 复选框状态必须写回 TemplateEntry.enabled（运行时按它过滤）；
// 排序必须同时改列表顺序与当前模板序号，且刷新后勾选状态不丢。
void ModuleEditorTest::testTemplateListCheckAndReorder()
{
    OpencvTemplateMatchNode node;
    node.init();
    TemplateEntry a;
    a.name = QStringLiteral("A");
    a.imagePath = QStringLiteral("a.png");
    TemplateEntry b;
    b.name = QStringLiteral("B");
    b.imagePath = QStringLiteral("b.png");
    node.setTemplates({a, b});
    node.setActiveTemplateIndex(0);

    ModuleEditorDialog dlg(&node);
    dlg.reloadFromNode();
    auto *list = dlg.findChild<QListWidget *>(QStringLiteral("templateList"));
    QVERIFY2(list != nullptr, "未找到模板列表 templateList");
    QCOMPARE(list->count(), 2);
    QCOMPARE(list->item(0)->text(), QStringLiteral("A"));
    QCOMPARE(list->item(0)->checkState(), Qt::Checked);
    QCOMPARE(list->item(1)->checkState(), Qt::Checked);

    // 取消勾选 A → 写回节点；再勾回
    list->item(0)->setCheckState(Qt::Unchecked);
    QVERIFY(!node.templates().at(0).enabled);
    QVERIFY(node.templates().at(1).enabled);
    list->item(0)->setCheckState(Qt::Checked);
    QVERIFY(node.templates().at(0).enabled);

    // 选中 B 后上移 → 列表变 [B,A]，当前模板跟着 B 走到第 0 位
    list->setCurrentRow(1);
    QCOMPARE(node.activeTemplateIndex(), 1);
    auto *up = dlg.findChild<QPushButton *>(QStringLiteral("templateMoveUpButton"));
    QVERIFY2(up != nullptr, "未找到上移按钮 templateMoveUpButton");
    up->click();
    QCOMPARE(node.templates().at(0).name, QStringLiteral("B"));
    QCOMPARE(node.templates().at(1).name, QStringLiteral("A"));
    QCOMPARE(node.activeTemplateIndex(), 0);
    QCOMPARE(list->currentRow(), 0);
    QCOMPARE(list->item(0)->text(), QStringLiteral("B"));
    QCOMPARE(list->item(0)->checkState(), Qt::Checked);   // 重排后勾选状态不丢

    // 再下移 → 回到 [A,B]，当前模板跟回第 1 位
    auto *down = dlg.findChild<QPushButton *>(QStringLiteral("templateMoveDownButton"));
    QVERIFY2(down != nullptr, "未找到下移按钮 templateMoveDownButton");
    down->click();
    QCOMPARE(node.templates().at(0).name, QStringLiteral("A"));
    QCOMPARE(node.templates().at(1).name, QStringLiteral("B"));
    QCOMPARE(node.activeTemplateIndex(), 1);
    QCOMPARE(list->currentRow(), 1);
}

// P1-9（VM「增强匹配」）：模板配置窗掩膜列表的复选框必须写回 TemplateEntry.enhancedShapes。
// 这里只走 UI：勾选第 1 块掩膜 → 点「确定」→ 节点模板列表里的 enhancedShapes 应为 {0}。
void ModuleEditorTest::testTemplateConfigEnhanceCheckboxWritesEntry()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    cv::Mat tpl(100, 100, CV_8UC1, cv::Scalar(30));
    cv::rectangle(tpl, cv::Rect(10, 10, 30, 80), cv::Scalar(220), 3);
    cv::rectangle(tpl, cv::Rect(60, 10, 30, 80), cv::Scalar(180), 3);
    const QString tplPath = dir.filePath(QStringLiteral("enh-ui.png"));
    QVERIFY(cv::imwrite(tplPath.toStdString(), tpl));

    cv::Mat canvas(200, 300, CV_8UC1, cv::Scalar(30));
    tpl.copyTo(canvas(cv::Rect(100, 50, 100, 100)));

    OpencvTemplateMatchNode node;
    node.init();
    node.setInputImage(OpencvUtil::matToHimage(canvas));
    TemplateEntry e;
    e.name = QStringLiteral("ui-enh");
    e.imagePath = tplPath;
    RoiShape a;
    a.type = RoiType::Rect;
    a.p1 = QPointF(0, 0);
    a.p2 = QPointF(50, 100);
    RoiShape b;
    b.type = RoiType::Rect;
    b.p1 = QPointF(50, 0);
    b.p2 = QPointF(100, 100);
    e.shapes = {a, b};
    e.shape = a;
    node.setTemplates({e});
    node.setActiveTemplateIndex(0);

    TemplateConfigDialog dlg(&node, 0);
    auto *list = dlg.findChild<QListWidget *>(QStringLiteral("templateShapeList"));
    QVERIFY2(list != nullptr, "未找到模板配置窗掩膜列表 templateShapeList");
    QCOMPARE(list->count(), 2);
    QVERIFY(list->item(0)->flags() & Qt::ItemIsUserCheckable);
    QCOMPARE(list->item(0)->checkState(), Qt::Unchecked);

    list->item(0)->setCheckState(Qt::Checked);

    QPushButton *okBtn = nullptr;
    for (QPushButton *b2 : dlg.findChildren<QPushButton *>()) {
        if (b2->text() == QStringLiteral("确定")) {
            okBtn = b2;
            break;
        }
    }
    QVERIFY2(okBtn != nullptr, "未找到模板配置窗「确定」按钮");
    okBtn->click();

    const QVector<TemplateEntry> back = node.templates();
    QCOMPARE(back.size(), 1);
    QCOMPARE(back.first().enhancedShapes, QVector<int>({0}));
}

// VM 模板配置「尺度模式」（默认自动）：配置窗下拉必须写回 TemplateEntry.scaleModeAuto，
// 并同步节点参数 scaleMode；自动时速度尺度输入框不可改（VM 同款）。
void ModuleEditorTest::testTemplateConfigScaleModeWritesEntry()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    cv::Mat tpl(100, 100, CV_8UC1, cv::Scalar(30));
    cv::rectangle(tpl, cv::Rect(10, 10, 30, 80), cv::Scalar(220), 3);
    cv::rectangle(tpl, cv::Rect(60, 10, 30, 80), cv::Scalar(180), 3);
    const QString tplPath = dir.filePath(QStringLiteral("scale-ui.png"));
    QVERIFY(cv::imwrite(tplPath.toStdString(), tpl));

    cv::Mat canvas(200, 300, CV_8UC1, cv::Scalar(30));
    tpl.copyTo(canvas(cv::Rect(100, 50, 100, 100)));

    OpencvTemplateMatchNode node;
    node.init();
    node.setInputImage(OpencvUtil::matToHimage(canvas));
    TemplateEntry e;
    e.name = QStringLiteral("ui-scale");
    e.imagePath = tplPath;
    e.scaleModeAuto = true;
    RoiShape a;
    a.type = RoiType::Rect;
    a.p1 = QPointF(0, 0);
    a.p2 = QPointF(100, 100);
    e.shapes = {a};
    e.shape = a;
    node.setTemplates({e});
    node.setActiveTemplateIndex(0);

    TemplateConfigDialog dlg(&node, 0);
    auto *mode = dlg.findChild<QComboBox *>(QStringLiteral("templateScaleModeCombo"));
    auto *speed = dlg.findChild<QSpinBox *>(QStringLiteral("templateSpeedScaleSpin"));
    QVERIFY2(mode != nullptr, "未找到模板配置窗尺度模式下拉 templateScaleModeCombo");
    QVERIFY2(speed != nullptr, "未找到模板配置窗速度尺度输入框 templateSpeedScaleSpin");
    QCOMPARE(mode->currentIndex(), 0);          // 默认自动
    QVERIFY2(!speed->isEnabled(), "尺度模式=自动时速度尺度不可改（VM 同款）");

    mode->setCurrentIndex(1);                    // 切手动
    QVERIFY2(speed->isEnabled(), "尺度模式=手动时速度尺度应可改");
    speed->setValue(9);

    QPushButton *okBtn = nullptr;
    for (QPushButton *b : dlg.findChildren<QPushButton *>()) {
        if (b->text() == QStringLiteral("确定")) {
            okBtn = b;
            break;
        }
    }
    QVERIFY2(okBtn != nullptr, "未找到模板配置窗「确定」按钮");
    okBtn->click();

    const QVector<TemplateEntry> back = node.templates();
    QCOMPARE(back.size(), 1);
    QCOMPARE(back.first().scaleModeAuto, false);
    QCOMPARE(back.first().speedScale, 9);
    QCOMPARE(node.getParam(QStringLiteral("scaleMode")).toInt(), 0);
}

QTEST_MAIN(ModuleEditorTest)

// 源文件内定义 Q_OBJECT 类时，AUTOMOC 要求显式包含生成的 moc
#include "module_editor_test.moc"
