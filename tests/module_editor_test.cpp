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
#include <QTabWidget>
#include <QTableWidget>
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

    // ③ 检测区域：多边形 → 写点串，矩形宽高归零（多边形优先于矩形）
    QVERIFY(controller.startSearchRegionPick(node, /*polygon=*/true));
    RoiShape poly;
    poly.type = RoiType::Polygon;
    poly.points = {QPointF(20.0, 20.0), QPointF(220.0, 40.0), QPointF(120.0, 200.0)};
    QVERIFY(controller.writeRoiToNode(poly) != nullptr);
    QVERIFY(!tmpl->getParam(QStringLiteral("searchRegionPolygon")).toString().isEmpty());
    QCOMPARE(tmpl->getParam(QStringLiteral("searchRegionWidth")).toInt(), 0);
    QCOMPARE(tmpl->getParam(QStringLiteral("searchRegionHeight")).toInt(), 0);
}

QTEST_MAIN(ModuleEditorTest)

// 源文件内定义 Q_OBJECT 类时，AUTOMOC 要求显式包含生成的 moc
#include "module_editor_test.moc"
