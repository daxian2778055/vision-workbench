#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX   // 同上：windows.h 的 min/max 宏会打断 QtCharts 头
#  endif
#endif
// 两项 UI 侧守卫的自动化腿（2026-10-26 复核要求补腿）：
//   1) 标注类别名白名单——`..\..\x` 这类名字能把样本写到标注目录之外（活路径，P2 修复）；
//   2) 导出写检查——open() 成功不代表写成功，写完必须 flush + 查状态。
#include "AnnotationLabelRule.h"
#include "BoxAnnotatorDialog.h"
#include "ModuleEditorRegistry.h"
#include "CsvEscape.h"
#include "YoloAnnotation.h"
#include "RuntimeInterface.h"
#include "RuntimeInterfaceView.h"
#include "HalconWindow.h"
#include "UiLogBus.h"
#include "ExportWriteCheck.h"
#include "FocusGuard.h"
#include "RuntimeInterfaceDesigner.h"
#include "GlobalVariableManager.h"
#include "HelpViewer.h"
#include <QPointer>

#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTextEdit>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QAbstractItemView>
#include <QGroupBox>
#include <QPushButton>
#include <QShortcut>
#include <QTabWidget>
#include <QResizeEvent>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QTableWidget>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QProcess>
#include <QTextStream>
#include <QtTest/QtTest>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QPieSeries>
#include <QtCharts/QPieSlice>

#include <atomic>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QThread>

class UiGuardsTest : public QObject
{
    Q_OBJECT
private slots:
    void annotationLabelAcceptsSafeNames();
    void deleteShortcutIgnoredWhileEditingText();
    void annotationLabelRejectsTraversalAndSeparators();
    void annotationClassDirStaysUnderRoot();
    void annotationClassDirRejectsEscape();
    void annotationClassDirRejectsEmptyRoot();
    void annotationPathIsInsideBoundaries();
    // 闸"有没有接线"的端到端腿：junction 指向 root 之外必须被拦（臂 D 的缺口）
    void annotationClassDirRejectsJunctionEscape();
    void finishTextExportSucceedsOnWritableFile();
    void staleBindingsFindsMissingTargets();
void mainWindowP1ClosureGuards();       // 语言重启确认序 / 切页清理序 / dock 勾选回写（源码守卫）
void p2CanvasEditGuards();              // Delete 删连线 / 右键进撤销 / 撤销焦点守卫（源码守卫）
void p3ModuleEditorGuards();            // 多行/浏览按钮标脏、判断条件重算、回滚回填（源码守卫）
void p4CommDomainGuards();              // 工具条按协议分发、内联焦点守卫、配置失败回滚、空指针（源码守卫）
void p5CameraGuards();                  // 像素格式局部更新、设备占用闸、刷新按钮回填、状态指示（源码守卫）
void p6RuntimeDesignerGuards();         // bindType 保留、保存失败、撤销覆盖、切态询问（源码守卫）
void p7TrainingToolGuards();            // 命令预览刷新、产物限定目录、异常退出、标注类别/清空确认（源码守卫）
void p8DataPanelGuards();               // 输出查看器 setNode/连接、报警真删、汇总可见行、CSV 转义（源码守卫）
void p9SystemGuards();                  // 角色闸、载入中止回滚、多流程打包（源码守卫）
void p10CrossCutGuards();               // 会话信号有消费者、裸指针 QPointer 兜底、标题 tr（源码守卫）
void moduleEditorRegistryCloseAll();    // 权限降级收窗：簿位真销毁
void csvFieldEscapesFormulaInjection(); // 导出字段：列结构＋公式注入统一转义
void csvBomHeaderIsWritten();           // 实测：UTF-8 BOM 三字节必须真的落到文件头
void f1DataPanelFollowupGuards();       // flush 三态、序号持久化、坏文件防护、查询上限提示、耗时有界
void f2ValidationGuards();              // 搜索参数告警、同名拒绝、VOC 写失败、表头/系列编辑、预览红字
void dockDefaultVisibilityGuards();     // 工具箱默认展开、结果表不 tabbed 遮住图像、图像最后 raise
void mvsLocalSourceGuards();            // ImageReadNode 摘除 + MVS 本地读图能力等价
void runtimeDesignerHasOfficialDragAndClipboard();
void runtimeDesignerHasOfficialSectionsAndAlign();
void runtimeDesignerAlignHCenterAndRight();
void runtimeControlAppearanceRoundTripsAndStaysCompatible();
void runtimeDesignerPaletteGroupedByOfficialSections();
void buttonActionsRoundTrip();
void runtimeViewHonorsCurrentPageAndActionTable();
void paramSettingControlRoundTripsAndSeedsWithoutWriteBack();
void switchControlRoundTripsAndSeedsWithoutWriteBack();
void comboBoxRoundTripsAndSeedsWithoutWriteBack();
void paramActionTargetParsesNodeKeyValue();
void imageAndClockControlsRoundTripAndRender();
void canvasConfigAndBarsRoundTrip();
void zoneGeometryScalesCanvasButNotBars();
void zoneAtCanvasPointMapsBarsAndCanvas();
void designerCanvasBarCoordinatesRoundTrip();
void designerControlRightClickEmitsContextMenu();
void subPasswordIsHashedAndVerified();
void designerPanelEditsAreUndoable();
void designerInnerTabEditFilter();
void innerTabsRoundTripAndClampOrphans();
void innerTabStripOffsetsDesignerAndRuntime();
void runtimeViewResizeKeepsZoneAndInnerTabGeometry();
void runtimeViewPageSwitchKeepsPerPageInnerTab();
void multiPageGeometryUsesPageContainerBase();
void lineChartRoundTripsSeriesAndClamps();
void lineChartRuntimeAppendsBoundSamplesAndTrims();
void multiLineChartSharesOneChartAndAppends();
void pieChartUpdatesSliceValues();
void chartTypesRoundTrip();
// B1：画布尺寸「按比例适配」纯函数 + 设计器画布/固定栏配置入口
void canvasScalingHelperScalesCanvasControls();
void designerExposesCanvasAndBarConfig();
// B1 末项：设计器的几何编辑（拖动/缩放/方向键）必须进撤销栈（一次手势一枚改前快照）
void designerGeometryEditPushesUndo();
// B1 回归：属性编辑的撤销点必须停在「改之前」（快照不得与当前布局共享页数组）
void designerPropertyEditUndoRestoresValue();
// B3-2：设置变量 / 运行控制 / OK-NG / 条件信号灯
void designerPaletteAddsNewAndPreviouslyBlockedControls();
void runtimeSetVariableWritesTypedGlobal();
void runtimeRunControlEmitsActions();
void runtimeOkNgAndConditionalLightUpdate();
void runtimeMultiImageRunStatusAndUiLog();
void runtimeTextEntryComboFormatsAndColors();
void subInterfacePagesRoundTripAndDepth();
void b3NewControlsRoundTrip();
void designerB3PropertyPanelsWork();
void subInterfaceCornerRadiusRoundTrips();
void designerAlignCanBeUndone();
void layoutSchemaContractRejectsFutureVersionsAndBadSubPageGraphs();
    void staleBindingsIgnoresValidAndUnbound();
    void staleBindingsTreatsUnknownNodeSetAsUnknown();
    void previewBindingCheckRecomputesOnSetInterface();
    // r15 P1-9：切图保存必须写到「上一张」
    void annotationDialogDoesNotTruncateOnOpen();
    void annotationDialogSwitchSavesToPreviousImage();
    void annotationDialogRollsBackWhenSaveFails();
    void annotationDialogClassComboDoesNotRewritePreviousImage();
    void helpViewerDeletesOnClose();
};

void UiGuardsTest::annotationLabelAcceptsSafeNames()
{
    QString why;
    // 正常类别名：中文、字母/数字/下划线/连字符
    QVERIFY2(annotationClassLabelIsSafe(QStringLiteral("划痕"), &why), qPrintable(why));
    QVERIFY2(annotationClassLabelIsSafe(QStringLiteral("scratch_1"), &why), qPrintable(why));
    QVERIFY2(annotationClassLabelIsSafe(QStringLiteral("class-2"), &why), qPrintable(why));
    // 前后空白会被 trim，不算非法
    QVERIFY2(annotationClassLabelIsSafe(QStringLiteral("  外圈  "), &why), qPrintable(why));
}

void UiGuardsTest::annotationLabelRejectsTraversalAndSeparators()
{
    QString why;
    const QStringList bad = {
        QString(),                      // 空
        QStringLiteral("."),
        QStringLiteral(".."),
        QStringLiteral("../../evil"),   // 路径穿越
        QStringLiteral("..\\..\\evil"),
        QStringLiteral("a/b"),          // 分隔符
        QStringLiteral("a\\b"),
        QStringLiteral("a:b"),          // Windows 盘符/流
        QStringLiteral("a b"),          // 空格
        QStringLiteral("a*b"),
    };
    for (const QString &name : bad) {
        QVERIFY2(!annotationClassLabelIsSafe(name, &why),
                 qPrintable(QStringLiteral("非法类别名被放行：%1").arg(name)));
        QVERIFY2(!why.isEmpty(), "拒绝时必须给出原因");
    }
}

void UiGuardsTest::annotationClassDirStaysUnderRoot()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    QString dir;
    QString why;
    QVERIFY2(annotationClassDir(root.path(), QStringLiteral("划痕"), &dir, &why),
             qPrintable(why));
    QVERIFY(QDir(dir).exists());
    QVERIFY2(dir.startsWith(root.path()), "类别目录没有落在标注目录下");
    QVERIFY2(QFileInfo(dir).fileName() == QStringLiteral("划痕"), "目录名被改写");
}

void UiGuardsTest::annotationClassDirRejectsEscape()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    // 再嵌一层，让"逃逸目标"落在**自己的临时目录内**（<tmp>/evil），
    // 不依赖共享的 %TEMP%——否则别的残留会让这条腿误红（2026-10-26 实测踩过）。
    const QString rootPath = tmp.path() + QStringLiteral("/root");
    QVERIFY(QDir().mkpath(rootPath));

    // 逃逸名一律拒绝，且**目录里不能多出任何东西**（白名单在 mkpath 之前就拦掉）。
    // 注意：`../../evil` 的落点是 QTemporaryDir 的父目录（共享 %TEMP%），本用例无法可靠
    // 核对它与别人的残留——不列进来（2026-10-26 复核：原先列了却只核对 <tmp>/evil）。
    const QStringList evil = {QStringLiteral("../evil"), QStringLiteral(".."),
                              QStringLiteral("a/b"), QStringLiteral("a\\b")};
    for (const QString &name : evil) {
        QString dir = QStringLiteral("sentinel");   // 失败时函数必须清空（否则复用时踩坑）
        QString why;
        QVERIFY2(!annotationClassDir(rootPath, name, &dir, &why),
                 qPrintable(QStringLiteral("逃逸名被放行：%1").arg(name)));
        QVERIFY2(dir.isEmpty(), "被拒绝时不应交出目录（也不应留下上一次的值）");
        QVERIFY2(!why.isEmpty(), "拒绝时必须给出原因");
    }
    // 事后核对：标注目录的父目录里不该出现 evil
    const QString escaped = QDir(rootPath).absoluteFilePath(QStringLiteral("../evil"));
    QVERIFY2(!QFileInfo::exists(escaped), "标注目录之外被创建了文件/目录");
}

void UiGuardsTest::annotationClassDirRejectsEmptyRoot()
{
    // 空 root 会让 dir 变成 "/<label>" 而 mkpath 先于包含检查跑——必须提前拒绝
    QString dir = QStringLiteral("sentinel");
    QString why;
    QVERIFY2(!annotationClassDir(QString(), QStringLiteral("划痕"), &dir, &why),
             "空标注目录被放行");
    QVERIFY2(dir.isEmpty(), "拒绝时必须清空 outDir");
    // 只断"原因非空"挡不住位置退化：删掉早退守卫后 mkpath 会先在盘根建目录、canonical
    // 拿到空串照样拒、原因照样非空 ⇒ 腿仍绿（2026-10-26 复核指出）。要钉住**是哪条原因**。
    QVERIFY2(why.contains(QStringLiteral("标注目录未设置")), qPrintable(why));

    QString dir2 = QStringLiteral("sentinel");
    QVERIFY2(!annotationClassDir(QStringLiteral("   "), QStringLiteral("划痕"), &dir2, &why),
             "空白标注目录被放行");
    QVERIFY2(dir2.isEmpty(), "拒绝时必须清空 outDir");
}

void UiGuardsTest::annotationClassDirRejectsJunctionEscape()
{
    // 臂 D 的缺口：谓词本身有牙，但"annotationClassDir 到底有没有用它"没人守——
    // 把调用换成恒真（annotationPathIsInside(rootCanon, rootCanon)）时七条腿全绿。
    // 这里用目录联接（junction / symlink）做端到端：名字合法（link 全字母），只有包含
    // 检查能拦住；闸没接线的话就会被接受。建不了联接就跳过并打印原因。
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString outside = tmp.path() + QStringLiteral("/outside");
    const QString root = tmp.path() + QStringLiteral("/root");
    QVERIFY(QDir().mkpath(outside));
    QVERIFY(QDir().mkpath(root));
    const QString link = root + QStringLiteral("/link");

#ifdef Q_OS_WIN
    const int rc = QProcess::execute(
        QStringLiteral("cmd"),
        {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
         QDir::toNativeSeparators(link), QDir::toNativeSeparators(outside)});
    // Windows 上不许跳过（2026-10-27 复核）：mklink /J 通常不需要提权，若因策略或非 NTFS
    // 失败，这条是唯一覆盖"闸有没有接线"的端到端腿——跳过等于让它静默失效，直接判红。
    QVERIFY2(rc == 0,
             "本机无法创建 junction（mklink /J 失败）：这条腿是闸接线的唯一端到端守卫，"
             "不能跳过——请确认 TEMP 在 NTFS 上且未禁用 mklink");
#else
    if (!QFile::link(outside, link))
        QSKIP("本机无法创建符号链接，跳过该腿");
#endif
    QVERIFY2(QFileInfo(link).isDir() || QFileInfo(link).isSymLink(), "联接没建起来");

    QString dir;
    QString why;
    const bool accepted = annotationClassDir(root, QStringLiteral("link"), &dir, &why);
    QVERIFY2(!accepted,
             qPrintable(QStringLiteral("junction 指向 root 之外却被接受：%1").arg(dir)));
    QVERIFY2(why.contains(QStringLiteral("越出")), qPrintable(why));

    QDir().rmdir(link);   // 目录联接只能用 rmdir 删（QFile::remove 删不掉目录）
}

void UiGuardsTest::annotationPathIsInsideBoundaries()
{
    // 包含判据的边界（端到端用例触发不到它：白名单先挡掉了逃逸名）
    QVERIFY(annotationPathIsInside(QStringLiteral("/a/b"), QStringLiteral("/a/b")));
    QVERIFY(annotationPathIsInside(QStringLiteral("/a/b"), QStringLiteral("/a/b/c")));
    // 前缀相似但不是子路径——必须判 false（这正是 startsWith 不加分隔符会漏的坑）
    QVERIFY(!annotationPathIsInside(QStringLiteral("/a/b"), QStringLiteral("/a/bc")));
    QVERIFY(!annotationPathIsInside(QStringLiteral("/a/b"), QStringLiteral("/a")));
    QVERIFY(!annotationPathIsInside(QStringLiteral("/a/b"), QStringLiteral("/x/y")));
    // 空串（目录不存在或创建失败）一律 false
    QVERIFY(!annotationPathIsInside(QString(), QStringLiteral("/a/b")));
    QVERIFY(!annotationPathIsInside(QStringLiteral("/a/b"), QString()));
}

void UiGuardsTest::finishTextExportSucceedsOnWritableFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/export.txt");

    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream ts(&f);
    ts << QStringLiteral("hello 导出");
    // 成功路径不弹窗（失败才弹），可直接单测
    QVERIFY2(ExportWriteCheck::finishTextExport(nullptr, f, ts, path),
             "可写文件收尾却报失败");
    QVERIFY(!f.isOpen());

    QFile read(path);
    QVERIFY(read.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString body = QString::fromUtf8(read.readAll());
    QVERIFY2(body.contains(QStringLiteral("hello")), "写入内容不完整");
}

void UiGuardsTest::staleBindingsFindsMissingTargets()
{
    // 绑定失效必须被体检抓到（2026-10-27）：设计器与运行视图共用这一判据
    RuntimeInterface itf;
    RuntimeControl c1;
    c1.type = RuntimeControlType::ValueDisplay;
    c1.bindType = QStringLiteral("global");
    c1.bindKey = QStringLiteral("missingVar");
    itf.currentPage()->controls.append(c1);

    RuntimeControl c2;
    c2.type = RuntimeControlType::ImageView;
    c2.bindType = QStringLiteral("node");
    c2.bindKey = QStringLiteral("不存在的节点");
    itf.currentPage()->controls.append(c2);

    const QStringList bad = itf.staleBindings({QStringLiteral("exists")},
                                              {QStringLiteral("节点A")});
    QCOMPARE(bad.size(), 2);
    QVERIFY2(bad.contains(QStringLiteral("global:missingVar")), qPrintable(bad.join(QLatin1Char(','))));
    QVERIFY2(bad.contains(QStringLiteral("node:不存在的节点")), qPrintable(bad.join(QLatin1Char(','))));
}

void UiGuardsTest::staleBindingsIgnoresValidAndUnbound()
{
    RuntimeInterface itf;
    RuntimeControl ok;
    ok.bindType = QStringLiteral("global");
    ok.bindKey = QStringLiteral("g1");
    itf.currentPage()->controls.append(ok);
    RuntimeControl ok2;
    ok2.bindType = QStringLiteral("node");
    ok2.bindKey = QStringLiteral("n1");
    itf.currentPage()->controls.append(ok2);
    RuntimeControl free;   // 无绑定（静态文本/按钮默认）
    itf.currentPage()->controls.append(free);

    const QStringList bad = itf.staleBindings({QStringLiteral("g1")}, {QStringLiteral("n1")});
    QVERIFY2(bad.isEmpty(), qPrintable(bad.join(QLatin1Char(','))));
}

void UiGuardsTest::staleBindingsTreatsUnknownNodeSetAsUnknown()
{
    // 启动期项目未载入：节点集为空 = 未知，不能全标红（2026-10-27 P3-6 假红）
    RuntimeInterface itf;
    RuntimeControl c;
    c.bindType = QStringLiteral("node");
    c.bindKey = QStringLiteral("尚未载入的节点");
    itf.currentPage()->controls.append(c);
    QVERIFY2(itf.staleBindings({}, {}).isEmpty(), "空节点集却把节点绑定判为失效");
}

void UiGuardsTest::previewBindingCheckRecomputesOnSetInterface()
{
    // P10 修复的行为腿：设计器预览原按「先 setKnownNodeNames（空布局上算体检）再
    // setInterface」调用，后者不重算 ⇒ 预览永不显示失效红字。这里故意沿用旧顺序，
    // 断言装布局后体检已重算；任何调用顺序都必须抓到失效绑定。
    RuntimeInterface layout;
    RuntimeControl c;
    c.type = RuntimeControlType::ValueDisplay;
    c.bindType = QStringLiteral("global");
    c.bindKey = QStringLiteral("missingVar");
    layout.currentPage()->controls.append(c);

    RuntimeInterfaceView view;
    view.setKnownNodeNames({ QStringLiteral("节点A") });   // 旧预览的错误顺序
    view.setInterface(layout);
    QVERIFY2(view.staleBindings().contains(QStringLiteral("global:missingVar")),
             "装载布局后必须重算失效绑定（设计器预览红字）");
}
namespace {

/// 造一个最小数据集：两张力图 + classes.txt + 第一张的已有标注
QString makeAnnotationDataSet(const QString &root)
{
    QDir().mkpath(root);
    for (int i = 0; i < 2; ++i) {
        QImage img(64, 48, QImage::Format_RGB32);
        img.fill(QColor(128, 128, 128));
        img.save(QStringLiteral("%1/img%2.png").arg(root).arg(i));
    }
    QFile cls(root + QStringLiteral("/classes.txt"));
    cls.open(QIODevice::WriteOnly | QIODevice::Text);
    cls.write("part\n");
    cls.close();
    const QString label0 = YoloAnnotation::labelPath(root + QStringLiteral("/img0.png"));
    QFile f(label0);
    f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
    f.write("0 0.500000 0.500000 0.500000 0.500000\n");
    f.close();
    return label0;
}

QString readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.readAll());
}

/// 找出图像列表（两个 QListWidget 里，条目以 .png 结尾的那个）
QListWidget *imageListOf(BoxAnnotatorDialog *dlg)
{
    for (QListWidget *w : dlg->findChildren<QListWidget *>())
        if (w->count() > 0 && w->item(0)->text().endsWith(QStringLiteral(".png")))
            return w;
    return nullptr;
}

}   // namespace

void UiGuardsTest::annotationDialogDoesNotTruncateOnOpen()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString label0 = makeAnnotationDataSet(tmp.path());
    const QString before = readAll(label0);
    QVERIFY2(!before.isEmpty(), "前置条件：第一张图应已有标注");

    BoxAnnotatorDialog dlg(tmp.path());
    // 打开数据集会把第一张载入画布（setCurrentRow(0) → onImageChanged）
    QCOMPARE(readAll(label0), before);   // 必须是逐字不变，不能变成空文件
}

void UiGuardsTest::annotationDialogSwitchSavesToPreviousImage()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString label0 = makeAnnotationDataSet(tmp.path());
    const QString label1 = YoloAnnotation::labelPath(tmp.path() + QStringLiteral("/img1.png"));
    const int linesBefore = readAll(label0).count(QChar('\n'));
    QCOMPARE(linesBefore, 1);

    BoxAnnotatorDialog dlg(tmp.path());
    // 在画布上"画"一个框（onBoxDrawn 是私有槽，用元对象调用，避免依赖鼠标模拟）
    const bool invoked = QMetaObject::invokeMethod(
        &dlg, "onBoxDrawn", Qt::DirectConnection,
        Q_ARG(QRectF, QRectF(8, 8, 24, 16)), Q_ARG(bool, false));
    QVERIFY2(invoked, "调用 onBoxDrawn 失败（槽被改名？）");

    QListWidget *list = imageListOf(&dlg);
    QVERIFY2(list, "找不到图像列表");
    list->setCurrentRow(1);   // 切到第二张：应当先把第一张的 2 个框存进 img0.txt

    const QString after0 = readAll(label0);
    QCOMPARE(after0.count(QChar('\n')), 2);            // 第一张多了一个框
    QVERIFY2(!QFile::exists(label1), "第二张的 txt 被创建了：说明保存写错了目标（P1-9）");
}

void UiGuardsTest::annotationDialogRollsBackWhenSaveFails()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    // 只造一张图 + classes.txt；随后把它的 txt 建成目录，让保存必然失败
    QDir().mkpath(tmp.path());
    {
        QImage img(64, 48, QImage::Format_RGB32);
        img.fill(QColor(120, 120, 120));
        img.save(tmp.path() + QStringLiteral("/img0.png"));
        QFile cls(tmp.path() + QStringLiteral("/classes.txt"));
        cls.open(QIODevice::WriteOnly | QIODevice::Text);
        cls.write("part\n");
        cls.close();
    }
    QVERIFY(QDir().mkpath(YoloAnnotation::labelPath(tmp.path() + QStringLiteral("/img0.png"))));

    BoxAnnotatorDialog dlg(tmp.path());
    // 画一个框（内存里 1 个），再"清空"——保存失败后应当撤回成 1 个
    QVERIFY(QMetaObject::invokeMethod(&dlg, "onBoxDrawn", Qt::DirectConnection,
                                      Q_ARG(QRectF, QRectF(8, 8, 24, 16)), Q_ARG(bool, false)));

    QListWidget *boxList = nullptr;
    for (QListWidget *w : dlg.findChildren<QListWidget *>())
        if (w->count() > 0 && !w->item(0)->text().endsWith(QStringLiteral(".png")))
            boxList = w;
    QVERIFY2(boxList, "找不到标注列表");
    const int before = boxList->count();
    QVERIFY2(before >= 1, "前置条件：画布上应有一个框");

    QVERIFY(QMetaObject::invokeMethod(&dlg, "onClearImage", Qt::DirectConnection));

    QCOMPARE(boxList->count(), before);   // 回滚了：清空没有生效
    bool sawRollback = false;
    for (QLabel *l : dlg.findChildren<QLabel *>())
        if (l->text().contains(QStringLiteral("撤回")))
            sawRollback = true;
    QVERIFY2(sawRollback, "状态栏没有说明已撤回");
}

void UiGuardsTest::annotationDialogClassComboDoesNotRewritePreviousImage()
{
    // P7 修复的行为腿：新接的类别下拉槽在 reloadImages 重建下拉时被 addItem 触发，
    // 此时 m_boxes/m_currentPath 仍属上一张图——原实现改 classId 并保存，写坏旧图。
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir().mkpath(tmp.path());
    for (int i = 0; i < 2; ++i) {
        QImage img(64, 48, QImage::Format_RGB32);
        img.fill(QColor(128, 128, 128));
        QVERIFY(img.save(QStringLiteral("%1/img%2.png").arg(tmp.path()).arg(i)));
    }
    {
        QFile cls(tmp.path() + QStringLiteral("/classes.txt"));
        QVERIFY(cls.open(QIODevice::WriteOnly | QIODevice::Text));
        cls.write("part\nface\n");
        cls.close();
        const QString label0 = YoloAnnotation::labelPath(tmp.path() + QStringLiteral("/img0.png"));
        QFile f(label0);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
        f.write("1 0.500000 0.500000 0.500000 0.500000\n");   // 旧图的框是 classId=1
        f.close();
    }
    const QString label0 = YoloAnnotation::labelPath(tmp.path() + QStringLiteral("/img0.png"));
    const QString before = readAll(label0);
    QVERIFY2(before.startsWith(QStringLiteral("1 ")), "前置：第一张的框应为 classId=1");

    BoxAnnotatorDialog dlg(tmp.path());
    QListWidget *boxList = nullptr;
    for (QListWidget *w : dlg.findChildren<QListWidget *>())
        if (w->count() > 0 && !w->item(0)->text().endsWith(QStringLiteral(".png")))
            boxList = w;
    QVERIFY2(boxList, "找不到标注列表");
    boxList->setCurrentRow(0);   // 让 onClassComboChanged 的选中框条件成立

    // 模拟换目录时的列表重建：m_loading=true 期间 clear/addItem 必发 currentIndexChanged(0)
    QVERIFY(QMetaObject::invokeMethod(&dlg, "reloadImages", Qt::DirectConnection));
    QCOMPARE(readAll(label0), before);   // 上一张的标注必须逐字不变
}

// 复核（扫描清单「设计器 Delete 快捷键劫持文本编辑」）：主窗口 Delete 用
// QShortcut 默认的 WindowShortcut 上下文，只要窗口激活、任何子控件收到 Delete 都会
// 触发——在参数面板输入框里按 Delete 想删字符，画布上选中的节点却被删了。
// 这条腿锁住焦点判定：可编辑控件里必须拦，普通控件里必须放行。
void UiGuardsTest::mainWindowP1ClosureGuards()
{
    // MainWindow 太重无法在 CI 实例化；按项目既有静态守卫惯例读源码钉三处顺序不变量。
    QFile f(QStringLiteral(SRC_DIR "/src/MainWindow.cpp"));
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 MainWindow.cpp");
    const QString src = QString::fromUtf8(f.readAll());
    f.close();

    // ① 语言切换：必须先确认再起新进程（双实例缺陷），且复用 closeEvent 收尾
    const int langFn = src.indexOf(QStringLiteral("void MainWindow::switchLanguage"));
    QVERIFY(langFn >= 0);
    const int langBody = src.indexOf(QStringLiteral("void MainWindow::onExportEncryptedProject"),
                                     langFn);   // 下一个函数定义即本函数末尾
    QVERIFY(langBody > langFn);
    const QString lang = src.mid(langFn, langBody - langFn);
    const int confirm = lang.indexOf(QStringLiteral("confirmCloseWithUnsavedChanges"));
    const int spawn = lang.indexOf(QStringLiteral("close();"));
    QVERIFY2(confirm >= 0 && spawn > confirm,
             "语言切换必须先过未保存确认再触发关闭/重启，否则取消关闭时双实例");

    // ② 切页：运行中禁止切页的守卫必须早于 m_overlayNodes.clear()
    const int tabFn = src.indexOf(QStringLiteral("void MainWindow::onCurrentTabChanged"));
    QVERIFY(tabFn >= 0);
    const QString tab = src.mid(tabFn, 2500);
    QVERIFY2(tab.indexOf(QStringLiteral("m_overlayNodes.clear()"))
                 > tab.indexOf(QStringLiteral("ExecutionState::Running")),
             "切页必须先在守卫里拒绝运行中切换，再清叠加/选中/参数");

    // ③ 启动 dock 回写：paramDock 与 tool/image 一样强制 show 且勾选回写
    const int show = src.indexOf(QStringLiteral("ui->paramDock->show()"), 7000);
    const int check = src.indexOf(QStringLiteral("actionToggleParamDock->setChecked(true)"));
    QVERIFY2(show >= 0 && check > show,
             "paramDock 强制 show 后必须回写菜单勾选态（与 tool/image 同口径）");

    // ④ 恢复默认布局：四个 panel 的勾选必须一并回写（与启动同口径）
    const int resetFn = src.indexOf(QStringLiteral("void MainWindow::resetDockLayout"));
    QVERIFY(resetFn >= 0);
    const int nextFn = src.indexOf(QStringLiteral("void MainWindow::openSoftwareSettings"), resetFn);
    const QString reset = src.mid(resetFn, nextFn - resetFn);
    QVERIFY2(reset.contains(QStringLiteral("actionToggleLogDock->setChecked(true)")),
             "恢复默认布局必须同步 logDock 勾选（同 tool/image/param 口径）");

    // ⑤ 工具库分组过滤：英文模式下显示名变化，比较必须走稳定 key（UserRole）
    QVERIFY2(src.contains(QStringLiteral("groupItem->setData(0, Qt::UserRole, group)")),
             "分组项必须存注册表中文 key 到 UserRole");
    QVERIFY2(src.contains(QStringLiteral("group->data(0, Qt::UserRole).toString() == m_toolActiveGroup"))
             && src.contains(QStringLiteral("groupItem->data(0, Qt::UserRole).toString() == group")),
             "过滤与自动展开必须比稳定 key，不得比随语言变化的显示文本");
}

void UiGuardsTest::p2CanvasEditGuards()
{
    QFile f(QStringLiteral(SRC_DIR "/src/FlowScene.cpp"));
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 FlowScene.cpp");
    const QString flow = QString::fromUtf8(f.readAll());
    f.close();
    const int del = flow.indexOf(QStringLiteral("void FlowScene::deleteSelectedItems"));
    QVERIFY(del >= 0);
    const QString delBody = flow.mid(del, 1800);
    QVERIFY2(delBody.contains(QStringLiteral("ConnectionGraphicsItem"))
                 && delBody.contains(QStringLiteral("removeConnection")),
             "Delete 必须能删选中的连线（不能只靠右键菜单）");

    QFile g(QStringLiteral(SRC_DIR "/src/NodeGraphicsItem.cpp"));
    QVERIFY2(g.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 NodeGraphicsItem.cpp");
    const QString nodeSrc = QString::fromUtf8(g.readAll());
    g.close();
    QVERIFY2(nodeSrc.count(QStringLiteral("scene->recordUndo()")) >= 4,
             "重命名/粘贴参数/启停/禁用上游四类右键编辑必须进撤销栈");

    // P2 复核：单击节点不得清 redo——按下只存快照、松开且真移动才压栈
    const int press = nodeSrc.indexOf(QStringLiteral("void NodeGraphicsItem::mousePressEvent"));
    const int release = nodeSrc.indexOf(QStringLiteral("void NodeGraphicsItem::mouseReleaseEvent"));
    QVERIFY(press >= 0 && release > press);
    const QString pressBody = nodeSrc.mid(press, release - press);
    const QString releaseBody = nodeSrc.mid(release, 700);
    QVERIFY2(!pressBody.contains(QStringLiteral("pushUndoSnapshot"))
             && pressBody.contains(QStringLiteral("m_pressSnapshot = scene->currentSceneSnapshot()")),
             "mousePressEvent 只存快照不入撤销栈（原按下即压栈会清 redo）");
    QVERIFY2(releaseBody.contains(QStringLiteral("manhattanLength()"))
             && releaseBody.contains(QStringLiteral("scene->pushUndoSnapshot(m_pressSnapshot)")),
             "只有松开且真移动才压栈，单击不产生撤销步也不清 redo");

    QFile m(QStringLiteral(SRC_DIR "/src/MainWindow.cpp"));
    QVERIFY2(m.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 MainWindow.cpp");
    const QString main = QString::fromUtf8(m.readAll());
    m.close();
    const int undoFn = main.indexOf(QStringLiteral("QKeySequence::Undo"));
    QVERIFY(undoFn >= 0);
    const QString undoBody = main.mid(undoFn, 900);
    QVERIFY2(undoBody.contains(QStringLiteral("isTextEditingFocus")),
             "Ctrl+Z/Y 必须与 Delete 一样有文本编辑焦点守卫");
    QVERIFY2(main.contains(QStringLiteral("QKeySequence::Find")),
             "算子搜索提示写了 Ctrl+F，必须真有该快捷键");
}

void UiGuardsTest::p3ModuleEditorGuards()
{
    QFile f(QStringLiteral(SRC_DIR "/src/ModuleEditorDialog.cpp"));
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 ModuleEditorDialog.cpp");
    const QString src = QString::fromUtf8(f.readAll());
    f.close();

    // ① hook 白名单必须含多行文本与按钮（P3：改了参数却没标脏/重算）
    const int hooks = src.indexOf(QStringLiteral("static const struct { const char *base; const char *sig; } kHooks"));
    QVERIFY(hooks >= 0);
    const QString hookBody = src.mid(hooks, 900);
    QVERIFY2(hookBody.contains(QStringLiteral("QPlainTextEdit")),
             "多行文本编辑必须挂钩标脏");
    QVERIFY2(hookBody.contains(QStringLiteral("QPushButton")),
             "FilePath 浏览按钮选完文件必须挂钩标脏");

    // ② 判断条件变更后必须请求重算
    const int persist = src.indexOf(QStringLiteral("void ModuleEditorDialog::persistJudgeConditions"));
    QVERIFY(persist >= 0);
    const int next = src.indexOf(QStringLiteral("void ModuleEditorDialog::loadJudgeConditions"), persist);
    const QString body = src.mid(persist, next - persist);
    QVERIFY2(body.contains(QStringLiteral("scheduleAutoRecompute")),
             "改结果判断条件后必须触发自动重算");

    // ③ 取消回滚与外部回填都要刷新判断条件表（动态控件不能只回填自动面板）
    const int restore = src.indexOf(QStringLiteral("void ModuleEditorDialog::restoreParams"));
    const int restoreEnd = src.indexOf(QStringLiteral("void ModuleEditorDialog::resizeEvent"), restore);
    QVERIFY2(src.mid(restore, restoreEnd - restore).contains(QStringLiteral("loadJudgeConditions")),
             "取消回滚必须回填结果判断条件表");
    const int reload = src.indexOf(QStringLiteral("void ModuleEditorDialog::reloadFromNode"));
    const int reloadEnd = src.indexOf(QStringLiteral("void ModuleEditorDialog::loadImage"), reload);
    QVERIFY2(src.mid(reload, reloadEnd - reload).contains(QStringLiteral("loadJudgeConditions")),
             "外部改参回填必须刷新结果判断条件表");

    // ④ 结果判断页的非 ParamSpec 控件（判断方式/显示勾选/文本/颜色等）也必须随回滚回填
    QVERIFY2(src.mid(restore, restoreEnd - restore).contains(QStringLiteral("refreshJudgePageControls"))
             && src.mid(reload, reloadEnd - reload).contains(QStringLiteral("refreshJudgePageControls")),
             "取消回滚与外部回填都必须同步结果判断页控件");
    const int judgeFn = src.indexOf(QStringLiteral("void ModuleEditorDialog::refreshJudgePageControls"));
    QVERIFY(judgeFn > 0);
    const QString judgeBody = src.mid(judgeFn, 2600);
    QVERIFY2(judgeBody.contains(QStringLiteral("m_judgeModeCombo"))
             && judgeBody.contains(QStringLiteral("m_showResultImageCheck"))
             && judgeBody.contains(QStringLiteral("m_showResultTextCheck"))
             && judgeBody.contains(QStringLiteral("m_resultTextEdit"))
             && judgeBody.contains(QStringLiteral("QSignalBlocker")),
             "回填方法必须覆盖判断方式/显示勾选/文本等控件且用信号阻塞避免触发写回");

    // ⑤ UDP 失败路径不得重复发错误信号（noteSendWriteFailure 内部已 emit）
    QFile udp(QStringLiteral(SRC_DIR "/src/UdpCommNode.cpp"));
    QVERIFY2(udp.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 UdpCommNode.cpp");
    const QString udpSrc = QString::fromUtf8(udp.readAll());
    udp.close();
    QVERIFY2(!udpSrc.contains(QStringLiteral("noteSendWriteFailure(reason);\n        emit communicationError")),
             "UDP 写失败只走 noteSendWriteFailure 一次 emit，不得重复发 communicationError");
}

void UiGuardsTest::p4CommDomainGuards()
{
    QFile f(QStringLiteral(SRC_DIR "/src/CommunicationManagerDialog.cpp"));
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 CommunicationManagerDialog.cpp");
    const QString src = QString::fromUtf8(f.readAll());
    f.close();

    // ① 地址空间工具条按协议分发：sync 认 Modbus+PLC、test 认 Modbus+PLC+FINS
    const int sync = src.indexOf(QStringLiteral("RegisterSpaceBar::syncRequested"));
    QVERIFY(sync >= 0);
    const QString syncBody = src.mid(sync, 900);
    QVERIFY2(syncBody.contains(QStringLiteral("PlcCommNode")),
             "数据同步必须支持 PLC，不能对所有非 Modbus 都报无节点");
    const int test = src.indexOf(QStringLiteral("RegisterSpaceBar::testSendRequested"));
    QVERIFY2(src.mid(test, 1200).contains(QStringLiteral("FinsCommNode")),
             "发送测试必须支持 FINS");

    // ② 内联参数回显有焦点守卫（异步刷新不得吞正在输入的 IP）
    const int fn = src.indexOf(QStringLiteral("void CommunicationManagerDialog::syncInlineParamsFromSelection"));
    const int fnEnd = src.indexOf(QStringLiteral("void CommunicationManagerDialog::applyInlineParamChange"), fn);
    QVERIFY2(src.mid(fn, fnEnd - fn).contains(QStringLiteral("focusWidget")),
             "内联参数回显必须检查焦点，避免异步刷新覆盖正在编辑的内容");

    // ③ 配置写回失败有回滚助手，且三处都调用
    QVERIFY2(src.contains(QStringLiteral("rebuildDeviceWithConfig")),
             "设备配置写回必须有失败回滚助手");
    QVERIFY2(src.count(QStringLiteral("rebuildDeviceWithConfig(")) >= 4,
             "Modbus/PLC/FINS 三处配置写回都应走原子重建");

    // ④ 重复事件/心跳添加有返回值检查
    QVERIFY2(src.contains(QStringLiteral("addReceiveEvent(ev)")) &&
             src.contains(QStringLiteral("addSendEvent(ev)")),
             "事件添加的返回值必须被检查，否则重复 ID 假成功");
    QVERIFY2(src.contains(QStringLiteral("registerHeartbeat(device, interval)")),
             "心跳添加必须检查重复注册");

    // ⑤ 收发区：时间戳接收时取、Hex 非法输入要提示
    QFile m(QStringLiteral(SRC_DIR "/src/CommMonitorPane.cpp"));
    QVERIFY2(m.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 CommMonitorPane.cpp");
    const QString monitor = QString::fromUtf8(m.readAll());
    m.close();
    QVERIFY2(monitor.contains(QStringLiteral("list.append({device, data,")),
             "收发条目必须在接收时就记录时间戳，不能在渲染时才取");
    QVERIFY2(monitor.contains(QStringLiteral("输入无效")),
             "Hex 手动发送遇到非法字符必须明确提示，不能静默丢弃");
}

void UiGuardsTest::p5CameraGuards()
{
    QFile g(QStringLiteral(SRC_DIR "/src/GlobalCameraManager.cpp"));
    QVERIFY2(g.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 GlobalCameraManager.cpp");
    const QString mgr = QString::fromUtf8(g.readAll());
    g.close();
    // ① 像素格式只改一个字段，不能用默认 CameraParams 整体覆盖
    const int pf = mgr.indexOf(QStringLiteral("void GlobalCameraManager::updateCameraPixelFormatCache"));
    QVERIFY(pf >= 0);
    QVERIFY2(mgr.mid(pf, 400).contains(QStringLiteral("params.pixelFormat = pixelFormat")),
             "像素格式缓存更新必须只改该字段，保留曝光/增益/帧率");
    // ② 设置设备真检查占用
    const int bind = mgr.indexOf(QStringLiteral("bool GlobalCameraManager::setCameraDevice"));
    QVERIFY2(mgr.mid(bind, 700).contains(QStringLiteral("deviceName ==")),
             "setCameraDevice 必须拒绝同一物理设备被多相机绑定");

    QFile n(QStringLiteral(SRC_DIR "/src/MvsImageSourceNode.cpp"));
    QVERIFY2(n.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 MvsImageSourceNode.cpp");
    const QString node = QString::fromUtf8(n.readAll());
    n.close();
    // ③ 刷新按钮真正回填列表（不是只改按钮文字）
    const int det = node.indexOf(QStringLiteral("detectButton, &QPushButton::clicked"));
    QVERIFY2(node.mid(det, 700).contains(QStringLiteral("refillCameraCombo")),
             "刷新相机按钮必须重新枚举并回填，不能只恢复按钮文字");
    // ④ 面板有相机状态指示并跟随信号
    QVERIFY2(node.contains(QStringLiteral("cameraStateLabel"))
             && node.contains(QStringLiteral("cameraStatusChanged")),
             "MVS 面板必须有相机连接状态指示并跟随状态信号");
}

void UiGuardsTest::p6RuntimeDesignerGuards()
{
    QFile f(QStringLiteral(SRC_DIR "/src/RuntimeInterfaceDesigner.cpp"));
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 RuntimeInterfaceDesigner.cpp");
    const QString src = QString::fromUtf8(f.readAll());
    f.close();

    // ① 参数设置类控件 bindType 不被空下拉覆盖
    const int fn = src.indexOf(QStringLiteral("void RuntimeInterfaceDesigner::onPropertyEdited"));
    const int next = src.indexOf(QStringLiteral("void RuntimeInterfaceDesigner::"), fn + 10);
    const QString body = src.mid(fn, next - fn);
    QVERIFY2(body.contains(QStringLiteral("RuntimeControlType::ParamSetting"))
             && body.contains(QStringLiteral("return;")),
             "参数设置类控件写回后必须提前返回，不能用空 bindTypeCombo 覆盖 node");

    // ② applyAndClose 检查保存失败
    const int apply = src.indexOf(QStringLiteral("void RuntimeInterfaceDesigner::applyAndClose"));
    QVERIFY2(src.mid(apply, 400).contains(QStringLiteral("saveToFile"))
             && src.mid(apply, 400).contains(QStringLiteral("QMessageBox")),
             "应用并关闭必须检查写盘失败并提示，不能吞掉错误");

    // ③ 页重排/页改名进撤销栈
    const int mv = src.indexOf(QStringLiteral("void RuntimeInterfaceDesigner::movePage"));
    QVERIFY2(src.mid(mv, 350).contains(QStringLiteral("pushUndo")),
             "页重排必须进撤销栈");
    const int rn = src.indexOf(QStringLiteral("void RuntimeInterfaceDesigner::renamePage"));
    QVERIFY2(src.mid(rn, 800).contains(QStringLiteral("pushUndo")),
             "页改名必须进撤销栈");

    // ④ 多图像名称/绑定编辑进撤销
    QVERIFY2(src.count(QStringLiteral("markPropertyEdit();   // P6")) >= 2,
             "多图像画面名与绑定编辑都要进属性撤销栈");

    // ⑤ fromJson 失败不改状态（RuntimeInterface.cpp）
    QFile r(QStringLiteral(SRC_DIR "/src/RuntimeInterface.cpp"));
    QVERIFY2(r.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 RuntimeInterface.cpp");
    const QString ri = QString::fromUtf8(r.readAll());
    r.close();
    const int lj = ri.indexOf(QStringLiteral("bool RuntimeInterface::fromJson"));
    const int ljEnd = ri.indexOf(QStringLiteral("bool RuntimeInterface::saveToFile"), lj);
    const QString ljBody = ri.mid(lj, ljEnd - lj);
    QVERIFY2(ljBody.contains(QStringLiteral("RuntimeInterface parsed"))
             && ljBody.contains(QStringLiteral("pages.swap")),
             "fromJson 必须解析到临时对象、成功后才交换，失败不破坏现有布局");

    // ⑥ 运行中切设计态有询问
    QFile m(QStringLiteral(SRC_DIR "/src/MainWindow.cpp"));
    QVERIFY2(m.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 MainWindow.cpp");
    const QString mw = QString::fromUtf8(m.readAll());
    m.close();
    const int sd = mw.indexOf(QStringLiteral("void MainWindow::switchToDesignMode"));
    QVERIFY2(mw.mid(sd, 900).contains(QStringLiteral("anyFlowRunning")),
             "运行中切设计态必须先询问停止/保持运行");
}

void UiGuardsTest::p7TrainingToolGuards()
{
    QFile t(QStringLiteral(SRC_DIR "/src/TrainingRunnerDialog.cpp"));
    QVERIFY2(t.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 TrainingRunnerDialog.cpp");
    const QString tr = QString::fromUtf8(t.readAll());
    t.close();
    // ① 命令预览随所有参数控件刷新
    QVERIFY2(tr.count(QStringLiteral("&TrainingRunnerDialog::refreshCommand")) >= 6,
             "训练参数控件变化必须刷新命令预览（看到=执行）");
    // ② Python 改动使环境结论失效
    QVERIFY2(tr.contains(QStringLiteral("m_envOk = false")),
             "改 Python 路径后环境探测结论必须失效");
    // ③ 产物发现限定输出目录 + best*.onnx，且有重新扫描按钮
    const int dis = tr.indexOf(QStringLiteral("QString TrainingRunnerDialog::discoverOnnx"));
    const int disEnd = tr.indexOf(QStringLiteral("QString TrainingRunnerDialog::resolveClasses"), dis);
    QVERIFY2(tr.mid(dis, disEnd - dis).contains(QStringLiteral("YOLOX_outputs"))
             && tr.mid(dis, disEnd - dis).contains(QStringLiteral("best*.onnx")),
             "产物发现只能扫训练输出目录的 best*.onnx，不能把历史模型误报为本次产物");
    QVERIFY2(tr.contains(QStringLiteral("重新扫描")),
             "执行导出命令后应有重新扫描产物的入口");

    // ⑥ P7 复核三处失实点：data.yaml 预览、探测竞态、产物时间下限/手工选择保护
    QVERIFY2(tr.contains(QStringLiteral("refreshOnEdit(m_dataYaml)")),
             "data.yaml 参与 buildArgs，漏接刷新会让预览与实跑分叉");
    const int probeFn = tr.indexOf(QStringLiteral("&QFutureWatcher<TrainingRunner::EnvStatus>::finished"));
    QVERIFY2(probeFn > 0 && tr.mid(probeFn, 900).contains(QStringLiteral("st.pythonPath")),
             "探测回调必须核对路径，旧路径绿灯不得覆盖已修改标记");
    QVERIFY2(tr.mid(dis, disEnd - dis).contains(QStringLiteral("m_runStartedMs")),
             "产物发现必须以本轮开始时刻为时间下限，历史 best*.onnx 不得误报");
    QVERIFY2(tr.contains(QStringLiteral("m_onnxManuallyChosen = true"))
             && tr.mid(tr.indexOf(QStringLiteral("void TrainingRunnerDialog::refreshArtifacts")), 400)
                    .contains(QStringLiteral("m_onnxManuallyChosen")),
             "重新扫描不得覆盖手工选择的 ONNX");

    // ④ 异常退出区分
    QFile r(QStringLiteral(SRC_DIR "/src/TrainingRunner.cpp"));
    QVERIFY2(r.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 TrainingRunner.cpp");
    const QString runner = QString::fromUtf8(r.readAll());
    r.close();
    QVERIFY2(runner.contains(QStringLiteral("CrashExit")),
             "训练进程异常退出必须有独立提示");

    // ⑤ 标注器：清空确认 + 类别下拉改选中框 + 图像过滤统一
    QFile b(QStringLiteral(SRC_DIR "/src/BoxAnnotatorDialog.cpp"));
    QVERIFY2(b.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 BoxAnnotatorDialog.cpp");
    const QString ann = QString::fromUtf8(b.readAll());
    b.close();
    QVERIFY2(ann.contains(QStringLiteral("onClassComboChanged")),
             "选中已有框后切类别必须改该框，不能只能删了重画");
    const int clr = ann.indexOf(QStringLiteral("m_clearBtn = new QPushButton"));
    QVERIFY2(ann.mid(clr, 600).contains(QStringLiteral("QMessageBox::question")),
             "清空本图（立即写空 txt）按钮点击必须先确认，但确认不能进槽（会阻塞程序化调用）");
    QVERIFY2(ann.contains(QStringLiteral("YoloAnnotation::imageFilters()")),
             "标注器图像过滤必须与 VOC 工具链一致");
}

void UiGuardsTest::p8DataPanelGuards()
{
    // ① 通信监控窗只读：双击写路径必须在槽入口封住，配置表写回不得谎称已落盘
    QFile mb(QStringLiteral(SRC_DIR "/src/ModbusConfigDialog.cpp"));
    QVERIFY2(mb.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 ModbusConfigDialog.cpp");
    const QString modbus = QString::fromUtf8(mb.readAll());
    mb.close();
    const int dbl = modbus.indexOf(QStringLiteral("void ModbusConfigDialog::onCellDoubleClicked"));
    const int wr = modbus.indexOf(QStringLiteral("void ModbusConfigDialog::onWriteValueToRegister"));
    QVERIFY2(dbl >= 0 && wr >= 0
             && modbus.mid(dbl, 180).contains(QStringLiteral("if (m_readOnly)"))
             && modbus.mid(wr, 180).contains(QStringLiteral("if (m_readOnly)")),
             "只读监控窗的双击写入路径必须在两个槽入口都封住");
    QFile cm(QStringLiteral(SRC_DIR "/src/CommunicationManagerDialog.cpp"));
    QVERIFY2(cm.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 CommunicationManagerDialog.cpp");
    const QString cmSrc = QString::fromUtf8(cm.readAll());
    cm.close();
    QVERIFY2(cmSrc.contains(QStringLiteral("if (!cm->updateDeviceConfig(name, cfg))"))
             && cmSrc.contains(QStringLiteral("参数已即时写回配置表"))
             && !cmSrc.contains(QStringLiteral("参数已即时保存")),
             "内存配置表写回必须检查失败，不得谎称已落盘");
    // ① 输出查看器 setNode 先 clear 再设指针；状态标签复位；失败按 namedOutputs 清
    QFile v(QStringLiteral(SRC_DIR "/src/OutputDataViewer.cpp"));
    QVERIFY2(v.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 OutputDataViewer.cpp");
    const QString viewer = QString::fromUtf8(v.readAll());
    v.close();
    const int sn = viewer.indexOf(QStringLiteral("void OutputDataViewer::setNode"));
    QVERIFY2(viewer.mid(sn, 300).indexOf(QStringLiteral("clear()"))
             < viewer.mid(sn, 300).indexOf(QStringLiteral("m_currentNode = node")),
             "setNode 必须先 clear 再设指针，否则 clear 会把刚设的节点抹掉");
    const int clr = viewer.indexOf(QStringLiteral("void OutputDataViewer::clear"));
    QVERIFY2(viewer.mid(clr, 400).contains(QStringLiteral("m_statusLabel")),
             "clear 必须复位状态标签与样式");
    const int fail = viewer.indexOf(QStringLiteral("void OutputDataViewer::onNodeExecuted"));
    QVERIFY2(viewer.mid(fail, 900).contains(QStringLiteral("namedOutputs")),
             "失败清数据必须按 namedOutputs 页签数量，与建页签口径一致");

    // ② MainWidow 有 onNodeExecuted 生产调用点
    QFile mw(QStringLiteral(SRC_DIR "/src/MainWindow.cpp"));
    QVERIFY2(mw.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 MainWindow.cpp");
    const QString main = QString::fromUtf8(mw.readAll());
    mw.close();
    QVERIFY2(main.contains(QStringLiteral("ov->onNodeExecuted(node, ok)")),
             "输出查看器必须有执行结果更新入口，否则打开着也不刷新");

    // ③ 报警历史真删库
    QFile a(QStringLiteral(SRC_DIR "/src/AlarmHistoryDialog.cpp"));
    QVERIFY2(a.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 AlarmHistoryDialog.cpp");
    const QString alarm = QString::fromUtf8(a.readAll());
    a.close();
    QVERIFY2(alarm.contains(QStringLiteral("clearAlarms")),
             "报警历史清空必须真删数据库，不能只清表格 UI");

    // ④ 结果表：clearResults 清历史 + 汇总只算可见行
    QFile rt(QStringLiteral(SRC_DIR "/src/ResultTablePanel.cpp"));
    QVERIFY2(rt.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 ResultTablePanel.cpp");
    const QString result = QString::fromUtf8(rt.readAll());
    rt.close();
    const int cr = result.indexOf(QStringLiteral("void ResultTablePanel::clearResults"));
    const int crEnd = result.indexOf(QStringLiteral("void ResultTablePanel::"), cr + 10);
    QVERIFY2(crEnd > cr && !result.mid(cr, crEnd - cr).contains(QStringLiteral("m_history")),
             "每轮 clearResults 只清当前树，不得清历史表（P8 回归点）");
    const int cs = result.indexOf(QStringLiteral("void ResultTablePanel::clearSchemeResults"));
    QVERIFY2(cs > 0 && result.mid(cs, 300).contains(QStringLiteral("m_history")),
             "切方案的 clearSchemeResults 才清历史表");
    const int us = result.indexOf(QStringLiteral("void ResultTablePanel::updateSummary"));
    QVERIFY2(result.mid(us, 700).contains(QStringLiteral("isHidden")),
             "汇总必须跳过筛选隐藏行");

    // ⑤ CSV 导出写后检查
    QFile pf(QStringLiteral(SRC_DIR "/src/PerformancePanel.cpp"));
    QVERIFY2(pf.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 PerformancePanel.cpp");
    const QString perf = QString::fromUtf8(pf.readAll());
    pf.close();
    const int ex = perf.indexOf(QStringLiteral("void PerformancePanel::exportToCsv"));
    const int exEnd = perf.indexOf(QStringLiteral("void PerformancePanel::"), ex + 10);
    const QString exp = perf.mid(ex, exEnd - ex);
    QVERIFY2(exp.contains(QStringLiteral("QTextStream::Ok")) && exp.contains(QStringLiteral("csvField(")),
             "性能 CSV 必须走公共转义并检查写后状态");

    // ⑥ 报表导出同用公共转义；classes.txt 写失败必须查文件层
    QFile rd(QStringLiteral(SRC_DIR "/src/ReportDialog.cpp"));
    QVERIFY2(rd.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 ReportDialog.cpp");
    const QString rep = QString::fromUtf8(rd.readAll());
    rd.close();
    QVERIFY2(rep.contains(QStringLiteral("csvField(s.flowName)")),
             "报表 CSV 的流程名必须走公共转义（公式注入）");
    QFile ya(QStringLiteral(SRC_DIR "/include/YoloAnnotation.h"));
    QVERIFY2(ya.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 YoloAnnotation.h");
    const QString yo = QString::fromUtf8(ya.readAll());
    ya.close();
    const int sc = yo.indexOf(QStringLiteral("static bool saveClasses"));
    QVERIFY2(sc > 0 && yo.mid(sc, 700).contains(QStringLiteral("f.flush()"))
             && yo.mid(sc, 700).contains(QStringLiteral("f.error()")),
             "classes.txt 写失败必须查文件层 flush/error（不能只看流状态）");
}

void UiGuardsTest::p9SystemGuards()
{
    // ① 角色闸：画布编辑锁含角色判据；模块编辑入口查 canEditScheme
    QFile s(QStringLiteral(SRC_DIR "/include/SessionManager.h"));
    QVERIFY2(s.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 SessionManager.h");
    const QString sess = QString::fromUtf8(s.readAll());
    s.close();
    const int lock = sess.indexOf(QStringLiteral("canvasEditShouldBeLocked"));
    QVERIFY2(sess.mid(lock, 450).contains(QStringLiteral("canEditScheme")),
             "画布编辑锁必须并入角色判据，堵住 Operator 从画布绕过菜单禁用");

    QFile mw(QStringLiteral(SRC_DIR "/src/MainWindow.cpp"));
    QVERIFY2(mw.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 MainWindow.cpp");
    const QString main = QString::fromUtf8(mw.readAll());
    mw.close();
    const int me = main.indexOf(QStringLiteral("void MainWindow::openModuleEditor"));
    QVERIFY2(main.mid(me, 800).contains(QStringLiteral("canEditScheme")),
             "模块编辑入口必须查角色权限");

    // ② 载入中止回滚元数据
    QVERIFY2(main.contains(QStringLiteral("const QString prevFilePath = pm->lastFilePath()"))
             && main.contains(QStringLiteral("pm->setLastFilePath(prevFilePath)")),
             "执行器停止超时的中止分支必须回滚 lastFilePath");

    // ③ 多流程打包
    const int pk = main.indexOf(QStringLiteral("ProjectPackager::pack(m_flowScenes"));
    QVERIFY2(pk >= 0, "打包必须覆盖所有流程页，不能只打当前页");
    QFile pp(QStringLiteral(SRC_DIR "/src/ProjectPackager.cpp"));
    QVERIFY2(pp.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 ProjectPackager.cpp");
    const QString pack = QString::fromUtf8(pp.readAll());
    pp.close();
    QVERIFY2(pack.contains(QStringLiteral("pack(const QList<FlowScene *> &scenes")),
             "打包器必须有多场景重载");

    // ④ 权限类旁路收口：逐函数体查角色闸/收窗/逐场景执行器（非全文字符串命中）
    auto bodyOf = [&main](const QString &fn) {
        const int p = main.indexOf(fn);
        if (p < 0)
            return QString();
        const int q = main.indexOf(QStringLiteral("\nvoid MainWindow::"), p + fn.size());
        return q < 0 ? main.mid(p) : main.mid(p, q - p);
    };
    QVERIFY2(bodyOf(QStringLiteral("void MainWindow::onAddFlowTab")).contains(QStringLiteral("canEditScheme")),
             "新增流程入口缺角色闸");
    QVERIFY2(bodyOf(QStringLiteral("void MainWindow::onCloseFlowTab")).contains(QStringLiteral("canEditScheme")),
             "删除流程入口缺角色闸");
    QVERIFY2(bodyOf(QStringLiteral("void MainWindow::showFlowTabContextMenu")).contains(QStringLiteral("canEditScheme")),
             "页签右键写操作缺角色闸");
    QVERIFY2(bodyOf(QStringLiteral("void MainWindow::applyPermissionRestrictions"))
                 .contains(QStringLiteral("m_editors.closeAll()")),
             "权限降级未收已开编辑窗");
    const int pkCheck = main.indexOf(QStringLiteral("等待选择目录期间流程已开始运行"));
    QVERIFY2(pkCheck > 700 && main.mid(pkCheck - 700, 700).contains(QStringLiteral("m_flowExecutors")),
             "打包入口必须逐场景查执行器（原只查当前页）");
}

void UiGuardsTest::moduleEditorRegistryCloseAll()
{
    // 权限降级的收窗机制（MainWindow 不可在 CI 实例化，故只测登记簿本体）：
    // 收窗必须真销毁对话框并清空簿位，destroyed 连接不能访问已析构的簿。
    ModuleEditorRegistry reg;
    NodeBase *a = reinterpret_cast<NodeBase *>(0x1000);   // 只作键值，不解除引用
    NodeBase *b = reinterpret_cast<NodeBase *>(0x2000);
    QDialog *dlgA = reg.editorFor(a, []() { return new QDialog(); }, nullptr);
    QDialog *dlgB = reg.editorFor(b, []() { return new QDialog(); }, nullptr);
    QVERIFY(dlgA && dlgB);
    QPointer<QDialog> watchA(dlgA);
    reg.closeAll();
    QVERIFY2(watchA.isNull(), "权限降级收窗必须真销毁编辑窗");
    QVERIFY2(reg.lookup(a) == nullptr && reg.lookup(b) == nullptr, "收窗后簿位应清空");
}

void UiGuardsTest::csvBomHeaderIsWritten()
{
    // P10 存疑项裁决：QTextStream(Utf8) << "\xEF\xBB\xBF" 与 << QChar(0xFEFF) 谁真写出 BOM？
    QTemporaryFile f;
    QVERIFY(f.open());
    {
        QTextStream s(&f);
        s.setEncoding(QStringConverter::Utf8);
        s << QChar(0xFEFF);   // 与 ReportDialog 同形
        s << QStringLiteral("中文\n");
    }
    f.flush();
    f.seek(0);
    const QByteArray head = f.read(9);
    QVERIFY2(head.startsWith(QByteArray::fromHex("EFBBBF")),
             qPrintable(QStringLiteral("UTF-8 CSV 头三字节必须是 EF BB BF，实测=%1")
                            .arg(QString::fromLatin1(head.toHex()))));
    QVERIFY2(head.contains(QByteArray::fromHex("E4B8AD")),
             "中文必须以 UTF-8 写入（BOM 存在才有意义）");

    // 对照裁决：性能面板原写法 stream << "\xEF\xBB\xBF" 实测只写出正文（无 BOM），
    // 现已统一改为 QChar(0xFEFF)。守卫钉住两处导出同形，防回退到 char* 写法。
    QFile pf(QStringLiteral(SRC_DIR "/src/PerformancePanel.cpp"));
    QVERIFY2(pf.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 PerformancePanel.cpp");
    const QString perf = QString::fromUtf8(pf.readAll());
    pf.close();
    QFile rd(QStringLiteral(SRC_DIR "/src/ReportDialog.cpp"));
    QVERIFY2(rd.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 ReportDialog.cpp");
    const QString rep = QString::fromUtf8(rd.readAll());
    rd.close();
    QVERIFY2(perf.contains(QStringLiteral("stream << QChar(0xFEFF)"))
             && rep.contains(QStringLiteral("out << QChar(0xFEFF)"))
             && !perf.contains(QStringLiteral("\"\\xEF\\xBB\\xBF\"")),
             "两处 CSV 导出必须用同一正确的 BOM 写法");
}

void UiGuardsTest::csvFieldEscapesFormulaInjection()
{
    // P10 修复的行为腿：以 = + - @ 或制表/回车开头的现场输入（算子名/流程名可改）
    // 在 Excel/WPS 打开导出 CSV 时会被当公式执行，必须置成文本再做列结构转义。
    QCOMPARE(csvField(QStringLiteral("part")), QStringLiteral("part"));
    QCOMPARE(csvField(QStringLiteral("=HYPERLINK(\"x\")")),
             QStringLiteral("\"'=HYPERLINK(\"\"x\"\")\""));
    QCOMPARE(csvField(QStringLiteral("+cmd")), QStringLiteral("'+cmd"));
    QCOMPARE(csvField(QStringLiteral("-cmd")), QStringLiteral("'-cmd"));
    QCOMPARE(csvField(QStringLiteral("@SUM(A1)")), QStringLiteral("'@SUM(A1)"));
    QCOMPARE(csvField(QStringLiteral("a,b")), QStringLiteral("\"a,b\""));
    QCOMPARE(csvField(QStringLiteral("a\r\nb")), QStringLiteral("\"a\r\nb\""));
}

void UiGuardsTest::p10CrossCutGuards()
{
    // ① 会话/写闸信号必须有消费者（此前有 emit 无连接）
    QFile mw(QStringLiteral(SRC_DIR "/src/MainWindow.cpp"));
    QVERIFY2(mw.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 MainWindow.cpp");
    const QString main = QString::fromUtf8(mw.readAll());
    mw.close();
    QVERIFY2(main.contains(QStringLiteral("&SessionManager::sessionChanged"))
             && main.contains(QStringLiteral("&SessionManager::writesLockChanged")),
             "会话/写闸信号必须有消费者，否则动态权限变更不生效");
    QVERIFY2(!main.contains(QStringLiteral("setWindowTitle(QStringLiteral(\"视觉方案工作台")),
             "窗口标题必须 tr()，英文模式不能冒中文");

    // ② 裸指针兜底
    QFile cm(QStringLiteral(SRC_DIR "/include/CommunicationManagerDialog.h"));
    QVERIFY2(cm.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 CommunicationManagerDialog.h");
    const QString comm = QString::fromUtf8(cm.readAll());
    cm.close();
    QVERIFY2(comm.contains(QStringLiteral("QPointer<GlobalTriggerDialog> m_triggerDialog")),
             "内嵌触发对话框成员必须 QPointer 兜底");

    QFile er(QStringLiteral(SRC_DIR "/include/ModuleEditorRegistry.h"));
    QVERIFY2(er.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 ModuleEditorRegistry.h");
    const QString reg = QString::fromUtf8(er.readAll());
    er.close();
    QVERIFY2(reg.contains(QStringLiteral("QPointer<QDialog>> m_editors")),
             "编辑器登记簿必须弱引用兜底");

    // ③ 右键转发信号（FlowScene::request*）的 emit 信号必须有消费者
    // 这些是普通转发方法，真正 emit 的是 nodeHelpRequested 等
    QVERIFY2(main.contains(QStringLiteral("&FlowScene::nodeHelpRequested"))
             && main.contains(QStringLiteral("&FlowScene::nodeOutputDataRequested"))
             && main.contains(QStringLiteral("&FlowScene::executeToHereRequested")),
             "右键菜单转发的信号必须有消费者，否则菜单项点了没反应");

    // ④ 摘除残留：onImageRead 死槽已删、G2 探针 A5 臂随算子摘除退役
    QVERIFY2(!main.contains(QStringLiteral("void MainWindow::onImageRead")),
             "ImageReadNode 摘除后 onImageRead 槽无连接者，应删除");
    QFile mwh(QStringLiteral(SRC_DIR "/include/MainWindow.h"));
    QVERIFY2(mwh.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 MainWindow.h");
    const QString mhead = QString::fromUtf8(mwh.readAll());
    mwh.close();
    QVERIFY2(!mhead.contains(QStringLiteral("onImageRead")), "MainWindow.h 不得残留死槽声明");
    QFile gp(QStringLiteral(SRC_DIR "/tools/probes/G2_ci_host_exit_probe.py"));
    QVERIFY2(gp.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 G2 探针");
    const QString probe = QString::fromUtf8(gp.readAll());
    gp.close();
    QVERIFY2(probe.contains(QStringLiteral("RETIRED"))
             && probe.contains(QStringLiteral("MvsImageSourceNode.cpp")),
             "G2 探针 A5 必须标记退役，不得再指向已删的 ImageReadNode.cpp");

    // ⑤ 测试有效性守卫：HelpViewer 腿须真 close()（非 delete）+ 源码钉生产持有/复用；
    //    soak 须有逐样本单调判据（原只末轮数>=10，停摆可绿）
    QFile ug(QStringLiteral(SRC_DIR "/tests/ui_guards_test.cpp"));
    QVERIFY2(ug.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 ui_guards_test.cpp");
    const QString ugSrc = QString::fromUtf8(ug.readAll());
    ug.close();
    // 字面量在守卫生效文本、注释、真正定义处各出现一次；取以 \n 开头且后跟 \r 的真正定义
    const QString marker = QStringLiteral("\nvoid UiGuardsTest::helpViewerDeletesOnClose()");
    const int hvDef = ugSrc.indexOf(marker);
    QVERIFY(hvDef > 0);
    const int hvEnd = ugSrc.indexOf(QStringLiteral("QTEST_MAIN"), hvDef);
    const QString hvBody = ugSrc.mid(hvDef, hvEnd - hvDef);
    QVERIFY2(hvBody.contains(QStringLiteral("viewer->close()"))
             && hvBody.contains(QStringLiteral("viewer->show()"))
             && hvBody.contains(QStringLiteral("destroyedCount")),
             "HelpViewer 腿必须 show→close + destroyed 断言，不再用 delete 测局部指针");
    QFile sk(QStringLiteral(SRC_DIR "/tests/soak_test.cpp"));
    QVERIFY2(sk.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 soak_test.cpp");
    const QString soak = QString::fromUtf8(sk.readAll());
    sk.close();
    QVERIFY2(soak.contains(QStringLiteral("samples.at(i).rounds >= samples.at(i - 1).rounds"))
             && soak.contains(QStringLiteral("grewRounds")),
             "soak 必须有逐样本 rounds 单调判据与后段增长判据，不能只看末轮数");

    // ⑥ 判断条件编辑态不被自动重算的全量重建吞掉
    QFile mw2(QStringLiteral(SRC_DIR "/src/MainWindow.cpp"));
    QVERIFY2(mw2.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 MainWindow.cpp");
    const QString mw2src = QString::fromUtf8(mw2.readAll());
    mw2.close();
    const int ar = mw2src.indexOf(QStringLiteral("&ModuleEditorDialog::autoRecomputeRequested"));
    QVERIFY(ar > 0);
    const QString arBody = mw2src.mid(ar, 700);
    QVERIFY2(arBody.contains(QStringLiteral("isJudgeTableEditing"))
             && arBody.indexOf(QStringLiteral("isJudgeTableEditing"))
                < arBody.indexOf(QStringLiteral("dlg->reloadFromNode()")),
             "自动重算必须先看条件表是否编辑态，是则跳过 dlg->reloadFromNode() 全表重建");
    // ⑦ DataCode2D 判据抽成可注入纯函数（行为腿有三组输入）
    QFile dc(QStringLiteral(SRC_DIR "/src/DataCode2DNode.cpp"));
    QVERIFY2(dc.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 DataCode2DNode.cpp");
    const QString dcSrc = QString::fromUtf8(dc.readAll());
    dc.close();
    QVERIFY2(dcSrc.contains(QStringLiteral("decodedContentIsValid(decodedLines)")),
             "空结果判红必须走可注入纯函数，不能只数 decodedStrings 个数");

    // ⑧ 参数按钮假脏：QPushButton 钩子只给带 _fileParamName/_refParamName 的按钮，
    //    且取消对话框（参数未变）不标脏
    QFile me(QStringLiteral(SRC_DIR "/src/ModuleEditorDialog.cpp"));
    QVERIFY2(me.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 ModuleEditorDialog.cpp");
    const QString meSrc = QString::fromUtf8(me.readAll());
    me.close();
    const int ir = meSrc.indexOf(QStringLiteral("void ModuleEditorDialog::installParamHooks"));
    const int irEnd = meSrc.indexOf(QStringLiteral("void ModuleEditorDialog::"), ir + 5);
    const QString irBody = meSrc.mid(ir, irEnd - ir);
    QVERIFY2(irBody.contains(QStringLiteral("_fileParamName"))
             && irBody.contains(QStringLiteral("_refParamName"))
             && irBody.contains(QStringLiteral("after == m_paramButtonBefore"))
             && irBody.contains(QStringLiteral("== 0 && !paramButton)")),
             "按钮钩子必须只给改参按钮、过滤无关按钮，且取消对话框参数未变时不标脏");
    // ⑨ 命名序号用尽不得覆盖已有文件
    QFile oi(QStringLiteral(SRC_DIR "/src/OutputImageNode.cpp"));
    QVERIFY2(oi.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 OutputImageNode.cpp");
    const QString oiSrc = QString::fromUtf8(oi.readAll());
    oi.close();
    QVERIFY2(oiSrc.count(QStringLiteral("序号已用尽")) >= 2,
             "%n 与自动加序两条路径都必须在序号用尽时判红返回，不覆盖已有文件");
}

void UiGuardsTest::f1DataPanelFollowupGuards()
{
    QFile lf(QStringLiteral(SRC_DIR "/src/LargeFileImageStore.cpp"));
    QVERIFY2(lf.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 LargeFileImageStore.cpp");
    const QString store = QString::fromUtf8(lf.readAll());
    lf.close();
    QVERIFY2(store.contains(QStringLiteral("FlushResult::NothingToFlush"))
             && store.contains(QStringLiteral("FlushResult::Failed")),
             "flush 必须区分无数据/成功/失败三态");
    QVERIFY2(store.contains(QStringLiteral("acq/largeFileBatchSeq")),
             "批次序号必须持久化，重启同秒不撞名");
    QVERIFY2(store.contains(QStringLiteral("kMaxEntryBytes")),
             "坏文件声明超大长度必须在分配前拒绝");

    // 查询上限提示（三处）
    for (const char *file : {"src/AlarmHistoryDialog.cpp", "src/InspectionResultDialog.cpp"}) {
        QFile f(QStringLiteral(SRC_DIR "/") + QString::fromLatin1(file));
        QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), "打不开查询对话框");
        const QString src = QString::fromUtf8(f.readAll());
        f.close();
        QVERIFY2(src.contains(QStringLiteral("truncated")) && src.contains(QStringLiteral("500")),
                 "查询对话框必须提示 500 条截断");
    }
    QFile rp(QStringLiteral(SRC_DIR "/src/ReportDialog.cpp"));
    QVERIFY2(rp.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 ReportDialog.cpp");
    QVERIFY2(QString::fromUtf8(rp.readAll()).contains(QStringLiteral("truncated")),
             "检测报表 10 万条截断必须提示");

    // 性能面板耗时历史有界
    QFile pf(QStringLiteral(SRC_DIR "/src/PerformancePanel.cpp"));
    QVERIFY2(pf.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 PerformancePanel.cpp");
    const QString perf = QString::fromUtf8(pf.readAll());
    pf.close();
    QVERIFY2(perf.contains(QStringLiteral("kMaxFlowTimes")),
             "流程耗时历史必须有界，防止连续运行无界增长");

    // P10 存储健壮性：写侧单张上限、序号同域、两处 flush 告知
    QVERIFY2(store.contains(QStringLiteral("encoded.size()) > kMaxEntryBytes")),
             "写侧单张 PNG 必须与读侧共用上限，超限拒写");
    QVERIFY2(store.contains(QStringLiteral("m_batchSeq = (m_batchSeq + 1) % 10000")),
             "序号写出取模须与加载钳制 [0,9999] 同域，往返一致");
    QFile mw2(QStringLiteral(SRC_DIR "/src/MainWindow.cpp"));
    QVERIFY2(mw2.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 MainWindow.cpp");
    const QString mwsrc = QString::fromUtf8(mw2.readAll());
    mw2.close();
    QVERIFY2(mwsrc.count(QStringLiteral("flush(&flushErr)")) >= 2
             && mwsrc.count(QStringLiteral("FlushResult::Failed")) >= 2,
             "退出前与停止时的 flush 失败都必须告知，不能静默丢数据");

    // UI 细节三处（P10 复核）：split 分支可达 / 截断筛选下推 / 改名保留选中
    const int splitAt = mwsrc.indexOf(QStringLiteral("splitDockWidget(ui->imageDock"));
    QVERIFY2(splitAt > 0 && mwsrc.mid(splitAt - 120, 120).contains(QStringLiteral("if (ui->imageDock)")),
             "结果区 split 必须由 imageDock 存在与否决定，不再依赖构造期为 false 的 isVisible()");
    QFile id(QStringLiteral(SRC_DIR "/src/InspectionResultDialog.cpp"));
    QVERIFY2(id.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 InspectionResultDialog.cpp");
    const QString insp = QString::fromUtf8(id.readAll());
    id.close();
    QVERIFY2(insp.contains(QStringLiteral("queryResults(from, to, 501, flowFilter)"))
             && insp.indexOf(QStringLiteral("const bool truncated")) < insp.indexOf(QStringLiteral("for (const auto &r : results)")),
             "截断判定必须在流程名筛选之后（筛选下推 SQL）");
    QFile dbh(QStringLiteral(SRC_DIR "/include/AppDatabase.h"));
    QVERIFY2(dbh.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 AppDatabase.h");
    const QString dbhead = QString::fromUtf8(dbh.readAll());
    dbh.close();
    QVERIFY2(dbhead.contains(QStringLiteral("flowNameFilter")), "queryResults 必须支持流程名下推");
    QFile ds(QStringLiteral(SRC_DIR "/src/RuntimeInterfaceDesigner.cpp"));
    QVERIFY2(ds.open(QIODevice::ReadOnly | QIODevice::Text), "打不开设计器");
    const QString design = QString::fromUtf8(ds.readAll());
    ds.close();
    const int ren = design.indexOf(QStringLiteral("ctrl->columns[row].header = name.trimmed()"));
    QVERIFY2(ren > 0 && design.mid(ren, 500).contains(QStringLiteral("setCurrentRow")),
             "改名重建列表后必须恢复选中行");

    // ⑩ 审批握手丢唤醒：GUI 空闲时可能在 worker wait 前置 taken+wakeAll，
    //    wait() 返回值不能当状态用——必须是 while (!taken) 谓词循环
    QFile sp(QStringLiteral(SRC_DIR "/src/ScriptSecurityPolicy.cpp"));
    QVERIFY2(sp.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 ScriptSecurityPolicy.cpp");
    const QString spSrc = QString::fromUtf8(sp.readAll());
    sp.close();
    QVERIFY2(spSrc.contains(QStringLiteral("while (!hs->taken && !hs->abandoned)"))
             && !spSrc.contains(QStringLiteral("wait(&hs->mutex, kGuiTakeoverTimeoutMs) ||")),
             "GUI 接管等待必须用谓词循环，不能拿 wait() 返回值判状态（丢唤醒误拒绝）");
    // ⑪ 通信寄存器转发连接：新增两条不得退回 context=this + 裸 node（UAF）
    QFile cm(QStringLiteral(SRC_DIR "/src/CommunicationManager.cpp"));
    QVERIFY2(cm.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 CommunicationManager.cpp");
    const QString cmSrc = QString::fromUtf8(cm.readAll());
    cm.close();
    QVERIFY2(!cmSrc.contains(QStringLiteral("registerValueChanged, this,"))
             && !cmSrc.contains(QStringLiteral("plcRegisterChanged, this,")),
             "寄存器转发连接 context 必须是 node，节点删除时排队回调一并取消，避免裸 node UAF");
    // ⑫ 相机刷新按钮 lambda：selfGuard 必须在节点存活时建好并按值捕获，lambda 首行判空
    QFile hi(QStringLiteral(SRC_DIR "/src/HalconImageSourceNode.cpp"));
    QVERIFY2(hi.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 HalconImageSourceNode.cpp");
    const QString hiSrc = QString::fromUtf8(hi.readAll());
    hi.close();
    const int hc = hiSrc.indexOf(QStringLiteral("auto refreshCameras ="));
    QVERIFY2(hc > 0 && hiSrc.mid(hc - 120, 120).contains(QStringLiteral("QPointer<HalconImageSourceNode> selfGuard(this)"))
             && hiSrc.mid(hc, 200).contains(QStringLiteral("if (!selfGuard || !comboGuard) return;")),
             "相机刷新 lambda 必须在节点存活时建 QPointer 并按值捕获，入口判空防 UAF");

    // ⑬ 圆拟合选组：剔除点不能进选组评分，输出残差仍含全集
    QFile fc(QStringLiteral(SRC_DIR "/src/OpencvFitCircleNode.cpp"));
    QVERIFY2(fc.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 OpencvFitCircleNode.cpp");
    const QString fcSrc = QString::fromUtf8(fc.readAll());
    fc.close();
    QVERIFY2(fcSrc.contains(QStringLiteral("candidate.selectionScore < best.selectionScore"))
             && !fcSrc.contains(QStringLiteral("candidate.residual < best.residual"))
             && fcSrc.contains(QStringLiteral("fitResidual\")] = choice.residual")),
             "局部最优必须用保留点 RMS 选组，输出残差仍用含剔除点的全集 RMS");
    // X2：拟合/卡尺/边缘的失败口径守卫
    const auto x2src = [](const char *rel) {
        QFile f(QStringLiteral(SRC_DIR "/") + QString::fromLatin1(rel));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return QString();
        return QString::fromUtf8(f.readAll());
    };
    QVERIFY2(x2src("include/FitRobust.h").contains(QStringLiteral("int minPoints = 2;"))
             && x2src("src/FitRobust.cpp").contains(QStringLiteral("usable.size()) - minKept"))
             && x2src("src/OpencvFitLineNode.cpp").contains(QStringLiteral("fitParams.minPoints = minPoints;")),
             "直线剔除下限必须走 minPoints，不能把点集剔到只剩 2 点还判绿");
    QVERIFY2(x2src("src/OpencvCaliperNode.cpp").contains(QStringLiteral("nSig < 4"))
             && x2src("src/OpencvCaliperNode.cpp").contains(QStringLiteral("qBound(3, static_cast<int>(L / step), 2048)"))
             && x2src("src/OpencvCaliperNode.cpp").contains(QStringLiteral("卡尺：输入图像无法转换为 OpenCV 矩阵"))
             && x2src("src/OpencvEdgeNode.cpp").contains(QStringLiteral("边缘检测：输入图像无法转换为 OpenCV 矩阵")),
             "卡尺信号太短/转换失败与边缘图转换失败必须判红并写原因，不能静默 0 结果");
    QVERIFY2(x2src("src/OpencvFitCircleNode.cpp").contains(QStringLiteral("移除该点后最小外接圆半径缩小量超过该值视为离群点")),
             "圆拟合剔除距离的帮助文本须与实际『半径缩小量』判据同口径");
    // Y1：图像运算第二输入槽、中值滤波 8 位门闸
    QVERIFY2(x2src("src/OpencvImageArithNode.cpp").contains(QStringLiteral("addInputSlot(QStringLiteral(\"图像2\"), PortDataType::Image)"))
             && x2src("src/OpencvImageArithNode.cpp").contains(QStringLiteral("除数图像含 0 像素")),
             "图像运算必须是可订阅的逻辑槽；缺输入/零除数须判红并写原因");
    QVERIFY2(x2src("src/MedianFilterNode.cpp").contains(QStringLiteral("OpencvUtil::requireGray8(mat, &depthWhy)"))
             && x2src("src/MedianFilterNode.cpp").contains(QStringLiteral("中值滤波：输入图像无法转换为 OpenCV 矩阵")),
             "中值滤波必须显式拒绝非 8 位与空图，防止低字节垃圾与未初始化输出");
    // Y2：失败原因与裁剪回显
    QVERIFY2(x2src("src/OpencvAdaptiveThresholdNode.cpp").contains(QStringLiteral("OpencvUtil::requireGray8(mat, &depthWhy)"))
             && x2src("src/ColorConversionNode.cpp").contains(QStringLiteral("颜色转换：未提供输入图像"))
             && x2src("src/OpencvCropNode.cpp").contains(QStringLiteral("裁剪：输入图像无法转换为 OpenCV 矩阵")),
             "图像族早退分支必须写可读原因，且自适应阈值与同族同口径拒绝非 8 位");
    QVERIFY2(x2src("src/OpencvCropNode.cpp").contains(QStringLiteral("roiEffectiveWidth"))
             && x2src("src/OpencvCropNode.cpp").contains(QStringLiteral("m_params.value(QStringLiteral(\"roiEffectiveCol\")")),
             "裁剪结果表回显的 ROI 必须是钳制后实际生效的区域");
    // ⑭ 通信内联参数：IP 键必须按设备类型分发（serverIp 只有 TCP 消费）
    QFile cd(QStringLiteral(SRC_DIR "/src/CommunicationManagerDialog.cpp"));
    QVERIFY2(cd.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 CommunicationManagerDialog.cpp");
    const QString cdSrc = QString::fromUtf8(cd.readAll());
    cd.close();
    const int ia = cdSrc.indexOf(QStringLiteral("QString CommunicationManagerDialog::inlineIpKey"));
    const QString iaBody = cdSrc.mid(ia, 1400);
    QVERIFY2(iaBody.contains(QStringLiteral("QStringLiteral(\"host\")"))
             && iaBody.contains(QStringLiteral("QStringLiteral(\"targetIp\")"))
             && iaBody.contains(QStringLiteral("QStringLiteral(\"remoteIp\")"))
             && cdSrc.contains(QStringLiteral("QString CommunicationManagerDialog::inlinePortKey"))
             && cdSrc.contains(QStringLiteral("inlineIpKey(type)")),
             "内联 IP/端口键映射必须按设备类型分发（host/targetIp/remoteIp/serverIp），"
             "回显与写回同用一套映射");
    const int se = cdSrc.indexOf(QStringLiteral("void CommunicationManagerDialog::syncInlineParamsFromSelection"));
    QVERIFY2(cdSrc.mid(se, 2200).contains(QStringLiteral("info.config.value(ipKey)"))
             && cdSrc.mid(se, 2200).contains(QStringLiteral("info.config.value(portKey)")),
             "回显必须读与写回相同的键（ipKey/portKey），否则切内联开关会把非 TCP 设备地址覆盖成 127.0.0.1:502");
    // ⑮ 单窗口创建设备：必填校验不能丢（四段 IP 齐全 + 串口名非空），且改动即时重算按钮
    QFile vc(QStringLiteral(SRC_DIR "/src/VmDeviceCreateDialog.cpp"));
    QVERIFY2(vc.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 VmDeviceCreateDialog.cpp");
    const QString vcSrc = QString::fromUtf8(vc.readAll());
    vc.close();
    const int uc = vcSrc.indexOf(QStringLiteral("void VmDeviceCreateDialog::updateCreateButton"));
    const int ucEnd = vcSrc.indexOf(QStringLiteral("void VmDeviceCreateDialog::applyTypeVisibility"), uc);
    const QString ucBody = vcSrc.mid(uc, ucEnd - uc);
    QVERIFY2(ucBody.contains(QStringLiteral("m_ipEdits[i]->text().trimmed().isEmpty()"))
             && ucBody.contains(QStringLiteral("m_serialPort")),
             "创建按钮必须校验四段 IP 齐全与串口名非空，不能只判设备名");
    // ⑯ 空白行判据：文本单元格形态的寄存器表必须注入自己的判据（地址列为空），
    //    否则默认 cellWidget 判据会把所有已配置行隐藏
    QFile rs(QStringLiteral(SRC_DIR "/src/RegisterSpaceBar.cpp"));
    QVERIFY2(rs.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 RegisterSpaceBar.cpp");
    const QString rsSrc = QString::fromUtf8(rs.readAll());
    rs.close();
    QVERIFY2(rsSrc.contains(QStringLiteral("m_blankPredicate(m_table, r)")),
             "RegisterSpaceBar 的隐藏空白行必须实际调用注入判据，不能写死 cellWidget 判据");
    QFile cm2(QStringLiteral(SRC_DIR "/src/CommunicationManagerDialog.cpp"));
    QVERIFY2(cm2.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 CommunicationManagerDialog.cpp");
    const QString cm2Src = QString::fromUtf8(cm2.readAll());
    cm2.close();
    QVERIFY2(cm2Src.contains(QStringLiteral("addr->text().trimmed().isEmpty()"))
             && cm2Src.contains(QStringLiteral("outsideWindow"))
             && cm2Src.contains(QStringLiteral("addr > 65535")),
             "主对话框必须按地址列判空白，并对窗口外地址提示、单元格钳制 65535");
    // ⑰ PLC 轮询：锁只包住查表+改队列+推进，不跨 Modbus I/O 与失败 emit
    QFile pc(QStringLiteral(SRC_DIR "/src/PlcCommNode.cpp"));
    QVERIFY2(pc.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 PlcCommNode.cpp");
    const QString pcSrc = QString::fromUtf8(pc.readAll());
    pc.close();
    const int pp = pcSrc.indexOf(QStringLiteral("void PlcCommNode::onPollTimeout"));
    const int ppEnd = pcSrc.indexOf(QStringLiteral("bool PlcCommNode::readRegister"), pp);
    const QString ppBody = pcSrc.mid(pp, ppEnd - pp);
    QVERIFY2(ppBody.contains(QStringLiteral("if (!dispatch)"))
             && ppBody.contains(QStringLiteral("readRegister"))
             && ppBody.contains(QStringLiteral("reportReadFailure"))
             && ppBody.indexOf(QStringLiteral("if (!dispatch)"))
                < ppBody.indexOf(QStringLiteral("readRegister"))
             && ppBody.count(QStringLiteral("m_pollMutex")) == 3,
             "PLC 轮询只两处短锁引用（出锁后 I/O 与回滚各一），不持锁跨网络/下游回调");
    // ⑱ 常量时间比较：长度差不得截断到 8 位
    QFile ri(QStringLiteral(SRC_DIR "/src/RuntimeInterface.cpp"));
    QVERIFY2(ri.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 RuntimeInterface.cpp");
    const QString riSrc = QString::fromUtf8(ri.readAll());
    ri.close();
    QVERIFY2(riSrc.contains(QStringLiteral("static_cast<unsigned>(a.size() ^ b.size())")),
             "constantTimeEquals 的长度差必须用足够宽的 unsigned，不能截断到 8 位");
    // ⑲ ChannelSplit/Merge 失败路径必须写 lastError（现场红节点要有原因）
    for (const char *file : { "src/ChannelSplitNode.cpp", "src/ChannelMergeNode.cpp" }) {
        QFile f(QStringLiteral(SRC_DIR "/") + QString::fromLatin1(file));
        QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), "打不开通道节点源码");
        const QString s = QString::fromUtf8(f.readAll());
        f.close();
        QVERIFY2(s.count(QStringLiteral("lastError")) >= 3,
                 "通道拆/合节点的校验失败与 catch 分支都必须写 lastError");
    }
    // ⑳ 发送失败记账归属：sendData 传 context + 三处用 sendFlowContext
    QFile cb(QStringLiteral(SRC_DIR "/src/CommunicationNodeBase.cpp"));
    QVERIFY2(cb.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 CommunicationNodeBase.cpp");
    const QString cbSrc = QString::fromUtf8(cb.readAll());
    cb.close();
    QVERIFY2(cbSrc.count(QStringLiteral("sendFlowContext()")) >= 1,
             "同步寄存器写入仍须有归属回退；异步路径改用入队时冻结的 key");
    // AH1：归属 key 必须在入队时冻结并随事件传递，异步结算不得重读可变成员
    const QString cbHead = []() {
        QFile f(QStringLiteral(SRC_DIR "/include/CommunicationNodeBase.h"));
        f.open(QIODevice::ReadOnly | QIODevice::Text);
        const QString s = QString::fromUtf8(f.readAll());
        f.close();
        return s;
    }();
    QVERIFY2(cbHead.contains(QStringLiteral("emit sendRequested(data, key)"))
             && cbSrc.contains(QStringLiteral("void CommunicationNodeBase::handleSendRequested(const QByteArray &data, const void *flowKey)"))
             && cbSrc.contains(QStringLiteral("dropSendPending(flowKey)"))
             && cbSrc.contains(QStringLiteral("activeSendFlow()")),
             "异步发送的失败与 pending 必须记到入队时冻结的流程 key");
    const QString cmSrc2 = []() {
        QFile f(QStringLiteral(SRC_DIR "/src/CommunicationManager.cpp"));
        f.open(QIODevice::ReadOnly | QIODevice::Text);
        const QString s = QString::fromUtf8(f.readAll());
        f.close();
        return s;
    }();
    QVERIFY2(cmSrc2.contains(QStringLiteral("node->requestSend(data, flow)"))
             && !cmSrc2.contains(QStringLiteral("node->setSendFlowContext(flow)")),
             "sendData 必须把流程上下文随请求显式传入，不能先写共享成员再入队");
    const auto ahHead = [](const char *path) {
        QFile f(QStringLiteral(SRC_DIR "/") + QString::fromLatin1(path));
        f.open(QIODevice::ReadOnly | QIODevice::Text);
        const QString s = QString::fromUtf8(f.readAll());
        f.close();
        return s;
    };
    QVERIFY2(ahHead("include/CommunicationNodeBase.h").contains(
                 QStringLiteral("void requestSend(const QByteArray &data, const std::shared_ptr<FlowRunContext> &ctx)"))
             && !ahHead("include/CommunicationNodeBase.h").contains(
                 QStringLiteral("const void *key = sendFlowContext();")),
             "异步发送不得从共享成员读取归属 key");
    // AH1c：排队请求必须持有上下文强引用，完成时释放
    QVERIFY2(ahHead("include/CommunicationNodeBase.h").contains(QStringLiteral("retainSendContext(key, effective)"))
             && cbSrc.contains(QStringLiteral("releaseSendContext(flowKey)"))
             && cbSrc.contains(QStringLiteral("s_liveSendContexts[flow] = ctx")),
             "排队期间必须保持流程上下文存活，防止关闭流程后旧指针悬空/地址复用");
    QVERIFY2(ahHead("include/CommunicationNodeBase.h").contains(
                 QStringLiteral("const std::shared_ptr<FlowRunContext> effective = ctx ? ctx : ownerContext()"))
             && cbSrc.contains(QStringLiteral("CommunicationNodeBase::~CommunicationNodeBase()"))
             && cbSrc.contains(QStringLiteral("m_heldSendKeys.clear()")),
             "缺省 ctx 也要保活 ownerContext；接收方销毁时须释放排队引用");
    QVERIFY2(ahHead("include/CommunicationNodeBase.h").contains(QStringLiteral("m_heldKeysMutex"))
             && cbSrc.contains(QStringLiteral("QMutexLocker heldLocker(&m_heldKeysMutex)")),
             "本地持有表由多流程线程与节点线程并发读写，必须加锁");
    QVERIFY2(cbSrc.contains(QStringLiteral("for (int i = 0; i < it.value(); ++i)"))
             && cbSrc.contains(QStringLiteral("dropSendPending(it.key());")),
             "接收节点销毁时须替未执行的排队请求销掉 pending，避免排空永久超时");
    QVERIFY2(cbSrc.indexOf(QStringLiteral("++m_heldSendKeys[flow];")) >= 0
             && cbSrc.indexOf(QStringLiteral("++m_heldSendKeys[flow];"))
                < cbSrc.indexOf(QStringLiteral("if (!flow || !ctx)"))
             && cbSrc.contains(QStringLiteral("if (!flow)")),
             "无流程上下文的空 key 请求也要计入本地持有表，析构时一并销 pending");
    // ㉑ 轮末排空 + 写回校验记账
    QFile fe(QStringLiteral(SRC_DIR "/src/FlowExecutor.cpp"));
    QVERIFY2(fe.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 FlowExecutor.cpp");
    const QString feSrc = QString::fromUtf8(fe.readAll());
    fe.close();
    QVERIFY2(feSrc.contains(QStringLiteral("waitSendDrain(m_runContext")),
             "轮末判红取差值前必须先有界排空");
    // AH2：排空超时必须拦下本轮，不能继续写 passed=true
    QVERIFY2(feSrc.contains(QStringLiteral("const bool sendDrained = CommunicationNodeBase::waitSendDrain(m_runContext.get(), 2000)"))
             && feSrc.contains(QStringLiteral("if (!sendDrained)"))
             && feSrc.contains(QStringLiteral("轮末发送排空超时")),
             "轮末排空超时必须判 NG，不能把未完成的异步写入记成通过");
    QVERIFY2(ahHead("include/FlowExecutor.h").contains(QStringLiteral("m_sendFailureBaselineDeferred"))
             && feSrc.contains(QStringLiteral("if (sendDrained) {"))
             && feSrc.contains(QStringLiteral("m_sendFailureBaselineDeferred = false;")),
             "排空超时不得推进失败基线，迟到失败不能重复计入下一轮");
    QVERIFY2(ahHead("include/FlowExecutor.h").contains(
                 QStringLiteral("reconcileSendFailureBaseline(bool deferred, bool drained, int currentCount"))
             && feSrc.contains(QStringLiteral("reconcileSendFailureBaseline(m_sendFailureBaselineDeferred, startDrained")),
             "deferred 标记必须被真正读取并参与基线重算");
    QVERIFY2(feSrc.contains(QStringLiteral("bool FlowExecutor::drainDeferredSendBeforeRound()"))
             && feSrc.contains(QStringLiteral("if (!m_sendFailureBaselineDeferred)")),
             "deferred 收口集中到 helper，连续轮次与 startExecution 共用");
    QVERIFY2(!feSrc.contains(QStringLiteral("&GlobalTriggerManager::triggerFired,"))
             && feSrc.contains(QStringLiteral("AH2m：triggerFired 只是")),
             "triggerFired 不得在 requestExternalRound 之外二次启动流程，避免一次触发双跑");
    QVERIFY2(ahHead("include/FlowExecutor.h").contains(QStringLiteral("StartOutcome startExecution()"))
             && ahHead("include/FlowExecutor.h").contains(QStringLiteral("enum class StartOutcome"))
             && feSrc.contains(QStringLiteral("outcome == StartOutcome::AlreadyRunning"))
             && !feSrc.contains(QStringLiteral("if (false && outcome == StartOutcome::AlreadyRunning)"))
             && feSrc.contains(QStringLiteral("outcome == StartOutcome::DeferredRejected"))
             && feSrc.contains(QStringLiteral("outcome = startExecution();")),
             "起轮须区分已运行与 deferred 拒绝；状态回转时重试，避免外部触发丢失");
    QVERIFY2(feSrc.contains(QStringLiteral("QThread::currentThread() == QCoreApplication::instance()->thread()"))
             && !feSrc.contains(QStringLiteral("if (false && QCoreApplication"))
             && feSrc.contains(QStringLiteral("pendingSendCount(m_runContext.get()) > 0")),
             "GUI 线程不得同步等待排空，避免饿死通信节点的 QueuedConnection 槽");
    QVERIFY2(feSrc.contains(QStringLiteral("Q_UNUSED(deferred);"))
             && feSrc.contains(QStringLiteral("Q_UNUSED(drained);")),
             "轮首不得以旧基线继续记账，即使排空仍未完成也须重取当前计数");
    QVERIFY2(feSrc.contains(QStringLiteral("拒绝/跳过新轮以避免错账"))
             && feSrc.contains(QStringLiteral("waitSendDrain(m_runContext.get(), 2000)"))
             && feSrc.count(QStringLiteral("drainDeferredSendBeforeRound()")) >= 3
             && !ahHead("include/FlowExecutor.h").contains(QStringLiteral("m_suppressSendAccounting")),
             "startExecution 与连续轮次必须共用 deferred 收口，旧轮未排空时拒绝/跳过新轮");
    for (const char *file : { "src/ModbusNode.cpp", "src/PlcCommNode.cpp" }) {
        QFile f(QStringLiteral(SRC_DIR "/") + QString::fromLatin1(file));
        QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), "打不开寄存器节点源码");
        const QString s = QString::fromUtf8(f.readAll());
        f.close();
        QVERIFY2(s.count(QStringLiteral("noteSendWriteFailure")) >= 2,
                 "写回校验失败必须走 noteSendWriteFailure 记账，不能只 emit communicationError");
    }
    // ㉒ gtd 导入失败整组回滚（不能清空现场还报完成）
    QVERIFY2(cdSrc.contains(QStringLiteral("const QJsonObject backupComm = cm->toJson()"))
             && cdSrc.contains(QStringLiteral("cm->fromJson(backupComm)"))
             && cdSrc.contains(QStringLiteral("deviceLoadFailed"))
             && cdSrc.contains(QStringLiteral("kKnownTypes")),
             "gtd 导入必须预校验 device 条目并备份现场配置，失败时整组回滚并报失败");
    // ㉓ 创建设备产出节点消费的键（connectionType TCP/RTU、三分支 dataUpload）
    QFile vcd(QStringLiteral(SRC_DIR "/src/VmDeviceCreateDialog.cpp"));
    QVERIFY2(vcd.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 VmDeviceCreateDialog.cpp");
    const QString vcdSrc = QString::fromUtf8(vcd.readAll());
    vcd.close();
    QVERIFY2(vcdSrc.contains(QStringLiteral("QStringLiteral(\"RTU\") : QStringLiteral(\"TCP\")"))
             && vcdSrc.count(QStringLiteral("c[QStringLiteral(\"dataUpload\")]")) >= 4
             && vcdSrc.count(QStringLiteral("c[QStringLiteral(\"bindAddress\")]")) >= 2
             && vcdSrc.contains(QStringLiteral("vmRowBind")),
             "创建设备必须产出节点消费的 connectionType（TCP/RTU）、各分支写 dataUpload、"
             "UDP/Modbus Server 写 bindAddress");
    // ㉔ 编辑设备配置页也必须有绑定地址（两处口径一致）
    QFile cdd(QStringLiteral(SRC_DIR "/src/CommDeviceConfigDialog.cpp"));
    QVERIFY2(cdd.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 CommDeviceConfigDialog.cpp");
    const QString cddSrc = QString::fromUtf8(cdd.readAll());
    cdd.close();
    QVERIFY2(cddSrc.count(QStringLiteral("m_config[QStringLiteral(\"bindAddress\")]")) >= 2
             && cddSrc.contains(QStringLiteral("服务端必须填写监听地址")),
             "编辑设备配置页 TCP/UDP 必须写 bindAddress，且 Server 空地址必须拒收");
    // ㉕ 内联寄存器写回保留协议专属字段 + FINS 类型大小写同口径
    QVERIFY2(cdSrc.contains(QStringLiteral("const QJsonArray oldRegs = cfg.value"))
             && cdSrc.contains(QStringLiteral("o = oo")),
             "内联寄存器写回必须从原表按地址取回整条，否则 FINS area/Modbus byteOrder 被抹掉");
    QVERIFY2(cdSrc.count(QStringLiteral("startsWith(QStringLiteral(\"FINS\"), Qt::CaseInsensitive)")) >= 3,
             "FINS 类型判定在地址空间、配置分发、内联键三处必须大小写同口径");
    // ㉖ 轮 D：状态机/边界记账
    const QString tcpSrc = []() {
        QFile f(QStringLiteral(SRC_DIR "/src/TcpCommNode.cpp"));
        f.open(QIODevice::ReadOnly | QIODevice::Text);
        const QString s = QString::fromUtf8(f.readAll());
        f.close();
        return s;
    }();
    QVERIFY2(tcpSrc.contains(QStringLiteral("QTcpSocket *old = m_socket"))
             && tcpSrc.count(QStringLiteral("emit connectionClosed()")) >= 4,
             "TCP Server 替换客户端必须先摘旧 socket 并发 connectionClosed 清半帧");
    const auto readSrc = [](const char *rel) {
        QFile f(QStringLiteral(SRC_DIR "/") + QString::fromLatin1(rel));
        f.open(QIODevice::ReadOnly | QIODevice::Text);
        const QString s = QString::fromUtf8(f.readAll());
        f.close();
        return s;
    };
    const QString mbNodeSrc = readSrc("src/ModbusNode.cpp");
    const QString plcNodeSrc = readSrc("src/PlcCommNode.cpp");
    QVERIFY2(mbNodeSrc.count(QStringLiteral("noteSendWriteFailure")) >= 7
             && mbNodeSrc.contains(QStringLiteral("地址越界")),
             "Modbus 写失败与 65535 越界必须记账");
    QVERIFY2(plcNodeSrc.count(QStringLiteral("noteSendWriteFailure")) >= 5,
             "PLC 写失败必须记账");
    // ㉗ 轮 E：焦点守卫按设备切换放行 + 最大化沿父子链隐藏兄弟
    QVERIFY2(cdSrc.contains(QStringLiteral("const bool deviceChanged = (selectedName != m_lastEchoedDevice)"))
             && cdSrc.contains(QStringLiteral("if (focus && !deviceChanged)"))
             && cdSrc.contains(QStringLiteral("m_lastEchoedDevice = selectedName")),
             "切换设备时必须绕过焦点守卫回显新设备参数");
    const QString rsBarSrc = readSrc("src/RegisterSpaceBar.cpp");
    QVERIFY2(rsBarSrc.contains(QStringLiteral("QWidget *cur = m_space"))
             && rsBarSrc.contains(QStringLiteral("const auto siblings = parent->findChildren")),
             "最大化必须沿父子链逐层隐藏兄弟，不能藏掉地址空间的承载容器");
    // ㉙ 轮 F：三个专用配置对话框保留原配置键 + 内联开关同步节点参数
    for (const char *file : { "src/ModbusConfigDialog.cpp", "src/PlcConfigDialog.cpp",
                              "src/FinsConfigDialog.cpp" }) {
        const QString src = readSrc(file);
        QVERIFY2(src.contains(QStringLiteral("m_originalConfig = "))
                 && src.contains(QStringLiteral("QJsonObject cfg = m_originalConfig")),
                 "三个专用配置对话框的 config() 必须从原配置起步，保住 dataUpload");
    }
    QVERIFY2(cdSrc.contains(QStringLiteral("node->setParam(QStringLiteral(\"dataUpload\"), m_inlineDataUpload->isChecked())")),
             "内联上传开关必须同步节点参数，否则上传闸仍按旧值放行");
    // ㉚ 轮 G：FINS/TCP 握手必须有超时，不能永久挂在半连接
    const QString finsNodeSrc = readSrc("src/FinsCommNode.cpp");
    QVERIFY2(finsNodeSrc.contains(QStringLiteral("节点地址握手超时"))
             && finsNodeSrc.contains(QStringLiteral("m_requestTimer->start(qMax(100"))
             && finsNodeSrc.count(QStringLiteral("m_requestTimer->stop()")) >= 1,
             "FINS/TCP 握手必须启动定时器，超时报错断链，握手成功后停表");
    // ㉛ 流程域 H：waitForFinish 必须自己驱动收尾（不能空等尚未创建的信号量）
    const QString tokSrc = readSrc("src/FlowExecutorToken.cpp");
    QVERIFY2(tokSrc.contains(QStringLiteral("bool FlowExecutorToken::waitForFinish"))
             && tokSrc.contains(QStringLiteral("ex->joinForDestroy(inlineMs)"))
             && tokSrc.contains(QStringLiteral("startBackgroundCleanup(ex)"))
             && tokSrc.contains(QStringLiteral("m_teardownStarted")),
             "waitForFinish 必须在 reset() 前真正收尾：内联等待 + 后台兜底 + 防重复启动");
    // ㉜ 流程域 I：后台流程编辑锁按自身场景更新、局部执行收集子流程定义
    const QString mwSrc = readSrc("src/MainWindow.cpp");
    QVERIFY2(mwSrc.contains(QStringLiteral("refreshEditLockForExecutor"))
             && mwSrc.count(QStringLiteral("refreshEditLockForExecutor();")) >= 3,
             "启动/停止/完成的编辑锁重算不得按 m_executor 过滤，后台流程也要更新");
    const QString feLocalSrc = readSrc("src/FlowExecutor.cpp");
    QVERIFY2(feLocalSrc.count(QStringLiteral("captureViaAliveBlock(blockCopy, &subFlowDefs)")) >= 2
             && feLocalSrc.count(QStringLiteral("collectSubFlowBodies(subFlowDefs, nodes)")) >= 2,
             "executeUpTo/executeFrom 必须像 run() 一样带出并收集子流程定义");
    // ㉝ 流程域 J1：缓存锁纪律、兜底时间戳、空算子文本兼容
    const QString nbSrc = readSrc("src/NodeBase.cpp");
    const int rib = feLocalSrc.indexOf(QStringLiteral("void FlowExecutor::releaseRoundOutputsOverBudget()"));
    QVERIFY2(rib >= 0
             && feLocalSrc.mid(rib, 400).contains(QStringLiteral("\n    QMutexLocker cacheLock(&m_graphCacheMutex);"))
             && feLocalSrc.contains(QStringLiteral("r.timestamp.isValid()"))
             && feLocalSrc.contains(QStringLiteral("QDateTime::currentDateTimeUtc()")),
             "releaseRoundOutputsOverBudget 必须入缓存锁，JSONL 无效时间戳须回退当前 UTC");
    QVERIFY2(nbSrc.contains(QStringLiteral("op.isEmpty() || op == QLatin1String(\"==\") || op == QLatin1String(\"!=\")")),
             "空运算符的文本分支必须与数值分支同口径按 == 处理");
    // ㉞ 流程域 J2：轮末启动不被吞、单轮超时按轮重启、错误态不被 finished 覆盖
    const int runStart = feLocalSrc.indexOf(QStringLiteral("void FlowExecutor::run()"));
    QVERIFY2(runStart >= 0
             && feLocalSrc.mid(runStart, 500).contains(QStringLiteral("for (;;)"))
             && feLocalSrc.contains(QStringLiteral("刚被重新启动：回到主循环执行它")),
             "run() 必须在退出窗口复核新启动请求，不能无条件写 Idle 后退出线程");
    QVERIFY2(mwSrc.contains(QStringLiteral("restartFlowTimeoutForCurrentRound"))
             && mwSrc.count(QStringLiteral("m_flowTimeoutTimer->start(timeoutMs)")) >= 2,
             "连续模式每轮结束必须重启单轮超时计时器");
    const QString escSrc = readSrc("src/ExecutionStatusController.cpp");
    QVERIFY2(escSrc.count(QStringLiteral("m_errorPending")) >= 4,
             "同轮 error→finished 不得覆盖错误态（粘滞标记由 started/finished 清除）");
    // ㉟ 相机域 K：IO 节点 consumer 门闸、检测在飞计数、面板切相机排空与读参
    const QString cioSrc = readSrc("src/CameraIoNode.cpp");
    QVERIFY2(cioSrc.contains(QStringLiteral("syncCameraConsumer"))
             && cioSrc.contains(QStringLiteral("markCameraUsed"))
             && cioSrc.contains(QStringLiteral("markCameraUnused"))
             && cioSrc.contains(QStringLiteral("Camera1")),
             "相机 IO 节点必须登记/撤销 consumer，提示名用真实 Camera1");
    const QString hisSrc = readSrc("src/HalconImageSourceNode.cpp");
    QVERIFY2(hisSrc.contains(QStringLiteral("++selfGuard->m_detectInFlight;"))
             && !hisSrc.contains(QStringLiteral("if (selfGuard->m_detectInFlight == 0)")),
             "每个检测任务都必须无条件计入在飞计数");
    const QString mvsSrc = readSrc("src/MvsImageSourceNode.cpp");
    const int sgIndex = mvsSrc.indexOf(QStringLiteral("void MvsImageSourceNode::setGlobalCameraName"));
    QVERIFY2(sgIndex >= 0
             && mvsSrc.mid(sgIndex, 900).contains(QStringLiteral("setParam(QStringLiteral(\"globalCameraName\"), name)"))
             && mvsSrc.mid(sgIndex, 900).contains(QStringLiteral("getCameraHandle(name)"))
             && mvsSrc.mid(sgIndex, 900).contains(QStringLiteral("readCameraParams();")),
             "面板切相机必须走 setParam 排空路径，并用新句柄加锁读参数");
    const int spIndex = mvsSrc.indexOf(QStringLiteral("void MvsImageSourceNode::setParam"));
    QVERIFY2(spIndex >= 0 && mvsSrc.mid(spIndex, 700).contains(QStringLiteral("QMutexLocker locker(&m_mutex);")),
             "切全局相机的状态快照必须在锁内取，不能与取图线程并发读写");
    // ㊱ 相机域 L：imageReady 末节点、RGB 行距、图像预算释放
    const int lastExecIndex = feLocalSrc.indexOf(QStringLiteral("NodeBase *lastExecuted = nullptr;"));
    QVERIFY2(lastExecIndex >= 0
             && feLocalSrc.contains(QStringLiteral("executeNode(node, node == lastExecuted)"))
             && !feLocalSrc.contains(QStringLiteral("executeNode(node, i == sortedNodes.size() - 1)")),
             "imageReady 的 isLastNode 必须取本轮实际最后执行的节点，而非拓扑下标");
    const QString hwSrc = readSrc("src/HalconWindow.cpp");
    QVERIFY2(hwSrc.contains(QStringLiteral("const int srcStride = widthByte.I();"))
             && hwSrc.contains(QStringLiteral("uchar *out = qImage.scanLine(y);")),
             "RGB 转换必须按 HALCON 行距取源行、按 Qt 行距写目标行");
    const int budgetIdx = feLocalSrc.indexOf(QStringLiteral("void FlowExecutor::releaseRoundOutputsOverBudget()"));
    QVERIFY2(budgetIdx >= 0 && feLocalSrc.mid(budgetIdx, 1800).contains(QStringLiteral("obj->getHImage()"))
             && !feLocalSrc.mid(budgetIdx, 1800).contains(QStringLiteral("v.canConvert<HImage>()")),
             "图像预算统计必须走 getHImage()，不能走永远为空的 getData()/QVariant");
    // ㊲ 相机域 M：句柄锁串行化所有 SDK 调用 + 方案改绑失效旧句柄
    const QString gcmSrc = readSrc("src/GlobalCameraManager.cpp");
    QVERIFY2(gcmSrc.count(QStringLiteral("sdkMutexFor(")) >= 7,
             "句柄锁须覆盖参数/IO/启停/拆句柄全部 SDK 调用");
    const int fjIdx = gcmSrc.indexOf(QStringLiteral("void GlobalCameraManager::fromJson"));
    QVERIFY2(fjIdx >= 0 && gcmSrc.mid(fjIdx).contains(QStringLiteral("reboundOpen"))
             && gcmSrc.mid(fjIdx, 2200).contains(QStringLiteral("closeCamera(name, true)")),
             "方案改绑已打开的相机时必须强制拆掉旧句柄，不能让新场景用旧设备采图");
    QVERIFY2(mvsSrc.count(QStringLiteral("sdkMutexFor(")) >= 9,
             "MVS 节点本地拆句柄/触发配置/参数应用/像素格式刷新也须纳入同一把句柄锁");
    QVERIFY2(readSrc("src/GlobalCameraDialog.cpp").contains(QStringLiteral("sdkMutexFor(hCamera)")),
             "面板写像素格式也必须持同一把句柄锁");
    // ㊳ 数据库/落库域：待落库缓冲跨线程保护 + 同秒多轮按 id 定序
    const QString feHdrSrc = readSrc("include/FlowExecutor.h");
    QVERIFY2(feHdrSrc.contains(QStringLiteral("m_pendingResultsMutex"))
             && feLocalSrc.contains(QStringLiteral("appendPendingResult("))
             && feLocalSrc.count(QStringLiteral("QMutexLocker locker(&m_pendingResultsMutex);")) >= 2,
             "m_pendingResults 被工作线程追加、UI 线程局部执行 flush，必须跨线程同步");
    QVERIFY2(readSrc("src/AppDatabase.cpp").contains(QStringLiteral("ORDER BY timestamp DESC, id DESC"))
             && readSrc("src/YieldMonitor.cpp").contains(QStringLiteral("a.id < b.id")),
             "同秒多轮须按 id 定序，最近 N 轮的窗口边界才不会取到更老批次");
    // ㊴ 方案域 O：失败恢复回报、通信设备门禁与全局回滚、注记回滚、布局写盘留痕
    QVERIFY2(mwSrc.contains(QStringLiteral("bool MainWindow::loadProjectFile"))
             && mwSrc.contains(QStringLiteral("if (!loadProjectFile(restorePath"))
             && mwSrc.contains(QStringLiteral("pm->setLastIntegrityNote(prevIntegrityNote)")),
             "崩溃恢复失败必须保留临时件/改锚只发生在成功路径，中止要回滚注记");
    const QString pmSrc = readSrc("src/ProjectManager.cpp");
    QVERIFY2(pmSrc.contains(QStringLiteral("rejectedDevices > 0"))
             && pmSrc.contains(QStringLiteral("restoreGlobals(globalsBefore)"))
             && pmSrc.contains(QStringLiteral("layoutFile.commit())")),
             "通信设备建不出来要拒收并回滚全局，布局写盘失败要留痕");
    // ㊵ 方案域 P：辅助窗随权限降级真销毁、用户管理落库 fail-closed、配方身份剥离与排序
    const QString auxSrc = readSrc("src/AuxPanelManager.cpp");
    QVERIFY2(auxSrc.contains(QStringLiteral("void AuxPanelManager::closeAllDialogs()"))
             && mwSrc.count(QStringLiteral("closeAllDialogs();")) >= 2
             && mwSrc.contains(QStringLiteral("actionRecipeManager->setEnabled(canEdit)")),
             "权限降级/注销必须真销毁辅助窗，配方入口按 Engineer 收口");
    const QString umSrc = readSrc("src/UserManagementDialog.cpp");
    QVERIFY2(umSrc.count(QStringLiteral("canManageUsers()")) >= 2
             && readSrc("src/AppDatabase.cpp").count(QStringLiteral("canManageUsers()")) >= 2,
             "用户管理动作与落库层都要按当前角色 fail-closed");
    const QString rmSrc = readSrc("src/RecipeManager.cpp");
    QVERIFY2(rmSrc.contains(QStringLiteral("payload.remove(QStringLiteral(\"moduleId\"))"))
             && rmSrc.contains(QStringLiteral("qRound(pa.y() / 0.5)"))
             && rmSrc.contains(QStringLiteral("recipeWriteAllowed()")),
             "配方加载须剥离模块身份、排序须严格弱序、写入须过权限闸");
    // ㊶ 方案域 Q：全局配置延迟提交（旧执行器停稳后），改绑相机清缓存句柄
    QVERIFY2(pmSrc.contains(QStringLiteral("bool ProjectManager::commitLoadedGlobals()"))
             && pmSrc.count(QStringLiteral("m_lastLoadedRoot = doc.object();")) >= 2
             && mwSrc.contains(QStringLiteral("pm->commitLoadedGlobals()")),
             "loadProject 不得提前应用全局配置；调用方停稳旧执行器后才 commit");
    const int ccIndex = mvsSrc.indexOf(QStringLiteral("&GlobalCameraManager::cameraChanged"));
    QVERIFY2(ccIndex >= 0 && mvsSrc.mid(ccIndex, 400).contains(QStringLiteral("m_globalCameraName == cameraName"))
             && !mvsSrc.mid(ccIndex, 400).contains(QStringLiteral("deviceName.isEmpty()")),
             "相机解绑与改绑都要清节点缓存句柄，不能留下悬垂值");
    // ㊷ 触发/心跳域 R1：同源触发覆盖清幽灵、方案流程名去重、心跳定时器收敛
    const QString gtmSrc = readSrc("src/GlobalTriggerManager.cpp");
    QVERIFY2(gtmSrc.count(QStringLiteral("m_allTriggers.remove(occupied.value().id);")) >= 2,
             "重命名与 fromJson 同源覆盖都要把被覆盖条目 id 从 allTriggers 撤下");
    QVERIFY2(mwSrc.contains(QStringLiteral("usedFlowNames.contains(flowName)")),
             "载入方案须去重持久化 flowName，避免触发广播双跑");
    const QString hbSrc = readSrc("src/HeartbeatManager.cpp");
    QVERIFY2(hbSrc.contains(QStringLiteral("qBound(500"))
             && hbSrc.contains(QStringLiteral("bool anyActive")),
             "心跳 fromJson 须钳制间隔并按 active 收敛定时器");
    // ㊸ 触发/心跳域 R2：响应状态机生命周期、延迟响应代次、空设备显式项
    QVERIFY2(mwSrc.contains(QStringLiteral("reportIdleForRound"))
             && mwSrc.contains(QStringLiteral("busyFlowName")),
             "轮末不得误报 IDLE；BUSY/IDLE 必须复用同一份捕获的流程名");
    const QString respSrc = readSrc("src/CommResponseManager.cpp");
    QVERIFY2(respSrc.contains(QStringLiteral("clearBusyFlows"))
             && respSrc.contains(QStringLiteral("m_schemeLoadTimer")),
             "方案切换须清 BUSY 集合并取消上一份延迟响应");
    const QString cmdSrc = readSrc("src/CommunicationManagerDialog.cpp");
    QVERIFY2(cmdSrc.contains(QStringLiteral("（未选择）"))
             && cmdSrc.count(QStringLiteral("currentData().toString()")) >= 3,
             "响应设备下拉须有显式空项，写回用 data 而非显示文本");
    // ㊹ 触发/心跳域 R3：字节组包真正使用 offset/length，事件类型下拉覆盖支持的类型
    const QString seSrc = readSrc("src/SendEvent.cpp");
    QVERIFY2(seSrc.contains(QStringLiteral("result.replace(pos, width, encoded)"))
             && seSrc.contains(QStringLiteral("field.absoluteOffset"))
             && seSrc.contains(QStringLiteral("field.length > 0")),
             "组包字段 offset/length 必须真正生效，不能只按字段顺序追加");
    const QString cedSrc = readSrc("src/CommEventEditDialogs.cpp");
    QVERIFY2(cedSrc.contains(QStringLiteral("uint16")) && cedSrc.contains(QStringLiteral("byte")),
             "事件类型下拉须覆盖底层支持的类型，未识别类型原样保留");
    // ㊺ 节点核心域 S1：模块变量引用清理与解析口径
    QVERIFY2(mwSrc.count(QStringLiteral("removeModuleVars(")) >= 2
             && mwSrc.contains(QStringLiteral("\n    m_lastModuleVars.remove(node->moduleId());")),
             "删除节点与切页签都要撤销该模块的变量引用，不能留下永不解析的陈旧引用");
    QVERIFY2(readSrc("include/VariablePanel.h").contains(QStringLiteral("removeModuleVars")),
             "变量面板须提供与 setModuleVars 对称的按模块撤销接口");
    QVERIFY2(seSrc.contains(QStringLiteral("out += m_template.mid(last, m.capturedStart() - last)")),
             "发送模板须单次扫描拼接，插入值不得被当作后续占位符二次替换");
    const QString feSrcRef = readSrc("src/FlowExecutor.cpp");
    QVERIFY2(feSrcRef.count(QStringLiteral("QString::number(v.toDouble(), 'g', 10)")) >= 2
             && feSrcRef.contains(QStringLiteral(R"(\{(\d+)(?:\.([^{}]+))?\})")),
             "引用解析须与面板同精度，并支持非 ASCII 参数名");
    // ㊻ 节点核心域 S2：失败轮状态收口、Enum 钳制、模块号校验、非聚合端口枚举
    const QString nbCoreSrc = readSrc("src/NodeBase.cpp");
    QVERIFY2(nbCoreSrc.contains(QStringLiteral("if (!m_executionSuccess && !isResultNg())")),
             "失败轮必须在 execute 唯一入口收口 moduleStatus，不能沿用上一轮 true");
    const QString hnSrc = readSrc("src/HalconNode.cpp");
    QVERIFY2(hnSrc.count(QStringLiteral("ParamType::Enum")) >= 4
             && hnSrc.contains(QStringLiteral("setModuleId(storedId)"))
             && hnSrc.contains(QStringLiteral("if (!aggregatesOutputPorts())")),
             "Enum 索引须钳制；fromJson 模块号须走校验；非聚合节点须按全部端口枚举");
    // ㊼ 节点核心域 S3：类型推断、uint32 取整、端口计数、float 容差、句柄锁
    const QString dobjSrc = readSrc("src/DataObject.cpp");
    QVERIFY2(dobjSrc.contains(QStringLiteral("if (m_type == DataType::Unknown)"))
             && dobjSrc.count(QStringLiteral("QMutexLocker locker(&m_mutex);")) >= 6,
             "setData 须推断 Unknown 类型；Region/XLD setter 须纳入递归锁");
    const QString rboSrc = readSrc("include/registerbyteorder.h");
    QVERIFY2(rboSrc.count(QStringLiteral("std::llround")) >= 2
             && rboSrc.contains(QStringLiteral("expected > 4294967295.0")),
             "uint32 写入与校验须用 64 位取整并拒绝越界");
    QVERIFY2(readSrc("include/HalconNode.h").contains(QStringLiteral("aggregatesOutputPorts() ? 1 + m_logicalOutputs.size()")),
             "非聚合节点逻辑端口数须按真实端口计");
    QVERIFY2(readSrc("src/ReceiveEvent.cpp").contains(QStringLiteral("diff <= 1e-6 * scale")),
             "字节匹配 float 等于判定须用相对容差");
    // ㊾ 加密打包/恢复域 T1：自动保存失败可见、恢复锚点、空方案全局改动
    QVERIFY2(mwSrc.contains(QStringLiteral("自动保存失败：当前改动尚未形成恢复文件"))
             && mwSrc.contains(QStringLiteral("loadProjectFile(restorePath, /*recoveryRestore=*/true, info.originalPath)"))
             && mwSrc.contains(QStringLiteral("m_recovery.markSaved(m_projectManager->buildProjectJson(m_flowScenes))")),
             "自动保存失败必须可见；恢复须以原方案路径为门禁锚点；启动空方案须建立基线");
    QVERIFY2(pmSrc.contains(QStringLiteral("const QString gatePath = schemeAnchorPath.isEmpty()")),
             "loadProject 须接受独立锚点，恢复时不能用临时导出目录判路径内外");
    // ㊺ 加密打包/恢复域 T2：打包按入参锚点、补扫 filePath、不可信路径 fail-closed
    const QString pkgSrc = readSrc("src/ProjectPackager.cpp");
    QVERIFY2(pkgSrc.contains(QStringLiteral("ProjectPaths::resolveSchemePath(raw, schemeFilePath)"))
             && pkgSrc.contains(QStringLiteral("schemeRealForPack.isEmpty()"))
             && pkgSrc.contains(QStringLiteral("filePathSpecSeen")),
             "打包须按入参方案锚点解析、真实路径不可信时跳过、补扫无规格 filePath");
    QVERIFY2(mwSrc.contains(QStringLiteral("r.skipped")),
             "打包界面须展示跳过项，不能把全部跳过说成没有依赖");
    // ㊻ 加密打包/恢复域 T3：探测面版本一致、解压上限
    const QString spEncSrc = readSrc("src/SchemePackage.cpp");
    QVERIFY2(spEncSrc.contains(QStringLiteral("if (version != kFormatVersion)"))
             && spEncSrc.contains(QStringLiteral("declared > 64u * 1024u * 1024u")),
             "加密探测须校验版本；legacy 解压须设硬上限防 zip-bomb");
    // ㊼ UI 布局/面板域 U1：场景窗随页签销毁、dock 几何恢复、图标回退
    QVERIFY2(mwSrc.count(QStringLiteral("m_auxPanels->closeAllDialogs();")) >= 3
             && mwSrc.contains(QStringLiteral("m_auxPanels->createAllDocks();"))
             && mwSrc.contains(QStringLiteral("节点图标加载失败，已回退到分类图标")),
             "切页签要销毁场景相关辅助窗；主窗口几何恢复前要创建全部 dock；图标失败要回退");
    const QString apmSrc = readSrc("src/AuxPanelManager.cpp");
    QVERIFY2(apmSrc.contains(QStringLiteral("setObjectName(QStringLiteral(\"performanceDock\"))"))
             && apmSrc.contains(QStringLiteral("setObjectName(QStringLiteral(\"outputDataDock\"))"))
             && apmSrc.contains(QStringLiteral("void AuxPanelManager::createAllDocks()")),
             "辅助 dock 须有稳定 objectName 且在 restoreState 前创建，几何/浮窗态才能恢复");
    QVERIFY2(!readSrc("ui/MainWindow.ui").contains(QStringLiteral(":/icons/start.png")),
             "ui 中未注册的 qrc 图标引用应移除，避免静默空 QIcon");
    // ㊽ UI 布局/面板域 U2：翻译目录补齐、语言重启先释放单实例锁
    QVERIFY2(readSrc("src/main.cpp").contains(QStringLiteral("setSingleInstanceLock(&instanceLock)"))
             && mwSrc.contains(QStringLiteral("releaseSingleInstanceLock();"))
             && readSrc("CMakeLists.txt").contains(QStringLiteral("src/SingleInstanceLock.cpp")),
             "语言切换重启前须释放单实例锁，释放点须在 vfp_core 内可链接");
    const QString tsSrc = readSrc("translations/visionflow_en.ts");
    for (const QString &key : { QStringLiteral("视觉方案工作台 - %1 [%2]"),
                                QStringLiteral("方案：%1"),
                                QStringLiteral("导出统计报表"),
                                QStringLiteral("返回设计模式") }) {
        QVERIFY2(tsSrc.contains(QStringLiteral("<source>") + key + QStringLiteral("</source>")),
                 qPrintable(QStringLiteral("翻译目录缺少核心文案: %1").arg(key)));
    }
    // ㊾ 认证/权限域 V1：Viewer 不发跑、预览不解闸、方案写入/新建不走旁路
    QVERIFY2(readSrc("src/SessionManager.cpp").contains(QStringLiteral("hasRoleLevel(QStringLiteral(\"Operator\"))")),
             "canRunFlow 须有 Operator 角色门槛，不能让 Viewer 只读预览发跑");
    QVERIFY2(mwSrc.contains(QStringLiteral("if (m_previewOnly) {"))
             && mwSrc.contains(QStringLiteral("当前权限不允许发起运行"))
             && mwSrc.contains(QStringLiteral("当前角色无权保存方案"))
             && mwSrc.contains(QStringLiteral("当前角色无权打开或新建方案")),
             "预览态不得解闸；本地图运行全部/保存/新建入口都要有角色判据");
    // ㊿ 认证/权限域 V2：审计失败可见、日志过滤下推、改密 fail-closed、CSV 注入防护
    const QString dbSrc = readSrc("src/AppDatabase.cpp");
    QVERIFY2(dbSrc.contains(QStringLiteral("qCWarning(vfpCat) << snapshot"))
             && readSrc("include/AppDatabase.h").contains(QStringLiteral("bool logOperation"))
             && dbSrc.contains(QStringLiteral("factory-recovery")),
             "审计写失败默认可见；logOperation 回报成败；无会话改密审计记系统主体");
    QVERIFY2(dbSrc.contains(QStringLiteral("? = '' OR user = ?"))
             && dbSrc.contains(QStringLiteral("ORDER BY timestamp DESC, id DESC LIMIT ?"))
             && readSrc("src/OperationLogDialog.cpp").contains(QStringLiteral("\\u6E05\\u7A7A\\u663E\\u793A")),
             "操作日志用户过滤须下推 SQL，清除按钮文案须与行为一致");
    QVERIFY2(dbSrc.contains(QStringLiteral("bool privileged"))
             && readSrc("src/FactoryPasswordGuard.cpp").contains(QStringLiteral("privileged=*/true")),
             "changeUserPassword 须补写闸/角色判据，退役路径显式走特权");
    QVERIFY2(readSrc("src/StatisticsReport.cpp").contains(QStringLiteral("#include \"CsvEscape.h\"")),
             "统计 CSV 须走统一转义（含公式注入防护）");
    // ㊿+ 脚本/AI 域 W1：DNN 输出几何/判红、缓存键、topK
    QVERIFY2(readSrc("src/DnnSegmentNode.cpp").contains(QStringLiteral("b.x = (b.x - x0) / s")),
             "实例分割框须按 letterbox 几何反映射回原图");
    const QString adSrc = readSrc("src/AnomalyDetectNode.cpp");
    QVERIFY2(adSrc.contains(QStringLiteral("setResultNg(ng)"))
             && adSrc.contains(QStringLiteral("modelStamp(path)")),
             "异常判 NG 须保留结果；同路径换模型须识别");
    QVERIFY2(readSrc("src/DnnInferNode.cpp").contains(QStringLiteral("topNames.append"))
             && readSrc("src/DnnInferNode.cpp").contains(QStringLiteral("DnnDetectNode::loadClassNames")),
             "topK 须输出 K 个类；类别表须走缓存");
    QVERIFY2(readSrc("src/DnnNetCache.cpp").contains(QStringLiteral("sizeBytes")),
             "DNN 缓存键须含文件大小，防同 mtime 换型沿用旧模型");
    // W2：审批失败原因如实上报、超时配置钳制
    const QString spHeader = readSrc("include/ScriptSecurityPolicy.h");
    QVERIFY2(spHeader.contains(QStringLiteral("QString *reason = nullptr"))
             && spHeader.contains(QStringLiteral("qBound(1000, v, 3600000)"))
             && readSrc("src/ScriptNode.cpp").contains(QStringLiteral("confirmReason")),
             "审批失败须回传具体原因；maxExecutionMs 须钳制，不能两种沙箱语义相反");
    // W3：自检判据/CLI 校验/报告产物门禁
    const QString ocrSelfSrc = readSrc("src/DeepOcrSelfTest.cpp");
    QVERIFY2(ocrSelfSrc.contains(QStringLiteral("if (r.ok && mode != 1 && rawWordCount <= 0)"))
             && ocrSelfSrc.contains(QStringLiteral("out.resize(maxCount)"))
             && ocrSelfSrc.contains(QStringLiteral("csvField(r.text)")),
             "自检零词不得判绿；--limit 须先排序再截断；CSV 须走统一转义");
    QVERIFY2(readSrc("src/main.cpp").contains(QStringLiteral("allOk && csvOk"))
             && readSrc("src/NodeSelfTest.cpp").contains(QStringLiteral("reportOk"))
             && readSrc("tools/deploy.ps1").contains(QStringLiteral("DeepOCR 自检未执行/未通过")),
             "CSV/报告写盘失败须影响退出码；部署脚本未跑成 OCR 不得放行");
    // Z1：CLI CSV 必须查 flush/status；部署脚本的异步流读取不能裸取 Result；license 可选。
    // 这里同时钉住部署脚本失败路径必须有退出码，不能因 stderr 句柄未关而永久挂住。
    QVERIFY2(readSrc("src/main.cpp").contains(QStringLiteral("ts.status() == QTextStream::Ok"))
             && readSrc("src/main.cpp").contains(QStringLiteral("const bool flushOk = csv.flush()")),
             "自检 CSV 只查 open() 不够，flush/status 失败也必须让退出码为 1");
    QVERIFY2(!readSrc("tools/deploy.ps1").contains(QStringLiteral("$errText = $errTask.Result"))
             && readSrc("tools/deploy.ps1").contains(QStringLiteral("if ($errTask.Wait(2000))")),
             "部署脚本超时分支不得裸取异步流 Result，否则 stderr 句柄未关时永久挂住");
    QVERIFY2(!readSrc("tools/deploy.ps1").contains(QStringLiteral("'tessdata', 'dl', 'license'")),
             "license 在部署说明中可选，不能列为出包硬条件");
    // Z2：代码导出不得按中文显示名猜 HALCON 算子或输出非法标识符
    QVERIFY2(!readSrc("src/CodeExportDialog.cpp").contains(QStringLiteral("read_image (Image"))
             && !readSrc("src/CodeExportDialog.cpp").contains(QStringLiteral("threshold (Image"))
             && !readSrc("src/CodeExportDialog.cpp").contains(QStringLiteral(" (Image, Result)")),
             "导出不得拼接不存在的 HALCON 算子名或中文函数名");
    QVERIFY2(readSrc("src/CodeExportDialog.cpp").contains(QStringLiteral("metaObject()->className()"))
             && readSrc("src/CodeExportDialog.cpp").contains(QStringLiteral("getAllParamNames()")),
             "导出须按节点真实类型与真实参数生成结构参考");
    QVERIFY2(readSrc("src/CodeExportDialog.cpp").contains(QStringLiteral("connect(m_langCombo"))
             && readSrc("src/CodeExportDialog.cpp").contains(QStringLiteral("setDefaultSuffix")),
             "语言切换必须重生成，保存过滤器与后缀必须跟随当前语言");
    // AA1：训练评估集与类名路径
    QVERIFY2(readSrc("include/ClassifierTrainer.h").contains(QStringLiteral("validationSplit(files.size(), validationRatio, seed, ci)"))
             && !readSrc("include/ClassifierTrainer.h").contains(QStringLiteral("std::mt19937 rng(seed + (unsigned)ci)")),
             "评估必须与训练共用同一划分函数，不能另写 seed+ci");
    QVERIFY2(readSrc("src/ObjectCountingNode.cpp").contains(
                 QStringLiteral("const QString clsPath = ProjectPaths::resolveExecutionPath(")),
             "目标计数的类名路径必须锚定方案目录，与其余 DNN 节点同口径");
    QVERIFY2(readSrc("src/TrainingWizardDialog.cpp").count(QStringLiteral("m_lastTrainOk")) >= 4
             && readSrc("src/TrainingWizardDialog.cpp").contains(QStringLiteral("本轮尚未训练成功")),
             "训练失败后不得发布/评估上一次的模型（发布与评估两处都要守）");
    // AA2：失败帧/失败轮的结果字段不得残留上一轮读数
    QVERIFY2(readSrc("src/OpencvTrainClassifierNode.cpp").contains(QStringLiteral("m_params[QStringLiteral(\"sampleCount\")] = 0;"))
             && readSrc("src/DnnInferNode.cpp").contains(QStringLiteral("m_params[QStringLiteral(\"topClassName\")] = QString();")),
             "训练与推理失败轮必须清空上一轮的结果读数");
    QVERIFY2(readSrc("src/ObjectCountingNode.cpp").contains(QStringLiteral("m_params[QStringLiteral(\"addedThisFrame\")] = 0;"))
             && readSrc("src/ObjectCountingNode.cpp").contains(QStringLiteral("本帧未完成计数")),
             "计数失败帧必须清本帧新增/在跟，累计计数保留跨帧语义");
    // AB2：翻译资源缺失不得在构建/部署/发布三道闸静默放行
    QVERIFY2(readSrc("CMakeLists.txt").contains(QStringLiteral("缺少英文翻译构建条件")),
             "构建机缺 lrelease 必须配置失败，不能静默跳过 .qm 生成");
    QVERIFY2(readSrc("tools/deploy.ps1").contains(QStringLiteral("'translations/visionflow_en.qm'"))
             && readSrc("tools/deploy.ps1").contains(QStringLiteral("$problems += \"找不到 $QmSrcDir")),
             "部署必检项必须含 .qm，缺翻译必须计入 problems 而不是只告警");
    QVERIFY2(readSrc("src/I18n.cpp").contains(QStringLiteral("英文翻译加载失败"))
             && readSrc("src/I18n.cpp").contains(QStringLiteral("return false;")),
             "英文 .qm 加载失败必须留痕，不再静默返回成功");
    // AC1：CI 门禁链自身不得 fail-open
    QVERIFY2(readSrc("tools/ci.ps1").contains(QStringLiteral("Get-Command pwsh"))
             && readSrc("tools/ci.ps1").contains(QStringLiteral("$docRan")),
             "doc 闸必须探测解释器并校验结果行，不能靠陈旧 LASTEXITCODE 判 OK");
    QVERIFY2(readSrc("tools/release_manifest_check.py").contains(QStringLiteral("git 读不出来时上面已经记了 finding")),
             "release auto SKIP 不得抹掉已记录的 git 读取失败");
    QVERIFY2(readSrc("tools/doc_anchors.py").contains(QStringLiteral("发布声明找不到时该断言本应判红")),
             "锚点基线文件缺失必须判红，不能当作表已核对放行");
    // AC2：两条门禁不得把"读不到/缺依赖"降级成通过
    QVERIFY2(readSrc("tools/version_header_check.py").contains(QStringLiteral("git_hash_unreadable"))
             && !readSrc("tools/version_header_check.py").contains(QStringLiteral("return \"unknown\"")),
             "git 哈希不可读必须判红，不能用与刷新脚本同值的 unknown 哨兵");
    QVERIFY2(readSrc("tools/repo_hygiene.py").contains(QStringLiteral("FAILURES.append(\"pyyaml missing"))
             && !readSrc("tools/repo_hygiene.py").contains(QStringLiteral("NOTES.append(\"pyyaml missing")),
             "缺 pyyaml 导致 workflow 语法检查无法运行必须判红，不能降级为 NOTE");
    // AD1：exe 不是当前 HEAD 编出来的，部署必须直接拒收，不能只告警后还写出 manifest
    QVERIFY2(readSrc("tools/deploy.ps1").contains(QStringLiteral("请先 cmake --build 再部署；禁止出包"))
             && !readSrc("tools/deploy.ps1").contains(QStringLiteral("exe 内嵌版本哈希未匹配 HEAD {0} —— 这份 exe")),
             "部署必须拒绝陈旧 exe，避免产出提交号不可信的发版件");
    // AE1：关键写操作审计 + 操作日志导出
    QVERIFY2(mwSrc.count(QStringLiteral("logOperation(")) >= 3
             && mwSrc.contains(QStringLiteral("tr(\"保存方案: %1\")"))
             && mwSrc.contains(QStringLiteral("tr(\"删除流程: %1\")")),
             "方案保存与流程删除必须写入持久审计日志");
    QVERIFY2(readSrc("src/OperationLogDialog.cpp").contains(QStringLiteral("void OperationLogDialog::onExport()"))
             && readSrc("src/OperationLogDialog.cpp").contains(QStringLiteral("ExportWriteCheck::finishTextExport"))
             && readSrc("src/OperationLogDialog.cpp").contains(QStringLiteral("csvField(")),
             "操作日志导出须复用统一 CSV 转义与写盘收尾检查");
    // AE2：本地绘制图标必须走 DPR 画布，不能在逻辑像素上直接画
    int dprCanvasFiles = 0;
    for (const char *path : { "src/NodeIconPalette.cpp", "src/MainWindow.cpp",
                              "src/CommunicationManagerDialog.cpp",
                              "src/ModuleEditorDialog.cpp", "src/TemplateConfigDialog.cpp" }) {
        if (readSrc(path).contains(QStringLiteral("DprIcon::Canvas")))
            ++dprCanvasFiles;
    }
    QVERIFY2(readSrc("include/DprIcon.h").contains(QStringLiteral("setDevicePixelRatio"))
             && dprCanvasFiles == 5,
             "所有本地绘制图标必须按设备像素比放大物理画布并保留逻辑尺寸");
    // AF1：流程耗时信号必须真发射，性能面板的接线才不是死线
    QVERIFY2(readSrc("src/FlowExecutor.cpp").contains(QStringLiteral("emit flowExecutionTime(roundMs)"))
             && readSrc("src/PerformancePanel.cpp").contains(QStringLiteral("onFlowExecutionTime")),
             "性能面板的流程总耗时信号必须有发射点");
    // AH3/AH4：跨线程信号只传值；关页先解绑；轮末强制刷新末批统计
    QVERIFY2(readSrc("include/FlowExecutor.h").contains(
                 QStringLiteral("void nodeExecutionTime(const QString &nodeName, int nodeType, int moduleId, qint64 elapsedMs)"))
             && readSrc("src/PerformancePanel.cpp").contains(
                 QStringLiteral("void PerformancePanel::onNodeExecutionTime(const QString &nodeName, int nodeType,")),
             "性能统计跨线程不得传递裸 NodeBase*");
    QVERIFY2(readSrc("include/PerformancePanel.h").contains(QStringLiteral("boundExecutor() const"))
             && readSrc("src/MainWindow.cpp").contains(QStringLiteral("ppBoundBeforeClose == sceneEx && pp->boundExecutor() == sceneEx"))
             && !readSrc("src/MainWindow.cpp").contains(QStringLiteral("removePostedEvents(pp, QEvent::MetaCall)")),
             "关闭流程页只解绑即将销毁的执行器，不得清空面板全部排队信号");
    // AH3c：重绑必须清空旧统计，并用代次过滤旧执行器的迟到信号
    QVERIFY2(readSrc("include/PerformancePanel.h").contains(QStringLiteral("quint64 m_bindGeneration"))
             && readSrc("src/PerformancePanel.cpp").contains(QStringLiteral("generation == m_bindGeneration"))
             && readSrc("src/PerformancePanel.cpp").contains(QStringLiteral("clearStats();\n    m_boundExecutor = executor;")),
             "切换执行器须清空旧流程统计并忽略旧代次迟到信号");
    QVERIFY2(readSrc("src/PerformancePanel.cpp").contains(QStringLiteral("QTimer::singleShot(0, this"))
             && readSrc("src/PerformancePanel.cpp").contains(QStringLiteral("m_uiRefreshTimer.invalidate();")),
             "轮末必须安排一次合并刷新，确保末批统计可见且仍保留节流");
    // AG1：端到端流水线基线必须可复现、可自证判红，而不是只报数的死工具
    QVERIFY2(readSrc("tests/soak_test.cpp").contains(QStringLiteral("BENCH-RESULT"))
             && readSrc("tools/measure_pipeline_bench.py").contains(
                 QStringLiteral("findings.append(\"ms_per_round_regressed\")"))
             && readSrc("tools/measure_pipeline_bench.py").contains(QStringLiteral("--selfcheck")),
             "流水线基准须输出可解析读数，并提供相对回归判据与自测");
    // AG2：轮末排空必须条件等待，不能 2ms 忙轮询
    QVERIFY2(readSrc("src/CommunicationNodeBase.cpp").contains(QStringLiteral("s_pendingDrained.wait"))
             && !readSrc("src/CommunicationNodeBase.cpp").contains(QStringLiteral("QThread::msleep(2)")),
             "发送排空须由写完事件唤醒，不得每 2ms 空转");
    // AG3：性能面板 UI 刷新必须节流，不能每节点信号都重建全表
    QVERIFY2(readSrc("include/PerformancePanel.h").contains(QStringLiteral("QElapsedTimer m_uiRefreshTimer"))
             && readSrc("src/PerformancePanel.cpp").contains(QStringLiteral("m_uiRefreshTimer.elapsed() >= 100")),
             "高帧率下性能表须节流刷新，统计仍逐节点累计");
    // AG4a：采集解耦的纯数据面须有可测契约（当前不接入产品抓图路径）
    QVERIFY2(readSrc("include/LatestFrameQueue.h").contains(QStringLiteral("与本地覆盖分开记"))
             && readSrc("include/LatestFrameQueue.h").contains(QStringLiteral("takeLatest"))
             && readSrc("tests/latest_frame_queue_test.cpp").contains(
                 QStringLiteral("testFrameNumberGapsAndDuplicates")),
             "最新帧队列须可单测帧号跳变/重复、本地覆盖与 SDK 丢包分离");
    // AG5：性能基线必须真接进 CI，不能是写了没人调用的死工具
    QVERIFY2(readSrc("tools/ci.ps1").contains(QStringLiteral("Write-Step \"Pipeline performance baseline\""))
             && readSrc("tools/ci.ps1").contains(QStringLiteral("exit 20"))
             && readSrc("tools/ci_exit_code_check.py").contains(
                 QStringLiteral("(\"Pipeline performance baseline\", \"20\")")),
             "流水线性能基线须接入 CI 退出码并登记冻结表");
    QVERIFY2(readSrc("docs/performance_baseline.md").contains(QStringLiteral("BENCH-RESULT"))
             && readSrc("docs/performance_baseline.md").contains(QStringLiteral("--selfcheck")),
             "性能基线的测量口径与重冻流程必须随源码留存");
    // AH5：README 不得继续声称服务端缺省全网卡监听（现为必填、留空拒启动）
    QVERIFY2(!readSrc("README.md").contains(QStringLiteral("服务端通信缺省绑定全部网卡"))
             && readSrc("README.md").contains(QStringLiteral("服务端通信监听地址必填")),
             "README 的监听地址口径须与代码一致");
    // X1：标定模型一致、多相机键隔离、方案切换清空、单例加锁
    QVERIFY2(readSrc("include/CalibrationManager.h").contains(QStringLiteral("QRecursiveMutex m_mutex")),
             "标定单例被多个工作线程与 GUI 并发读写，必须加锁");
    QVERIFY2(readSrc("src/OpencvCalibNode.cpp").contains(QStringLiteral("flags |= cv::CALIB_FIX_K3"))
             && readSrc("src/OpencvCalibNode.cpp").contains(QStringLiteral("calibKey")),
             "标定模型须与读侧四系数一致；多相机须可分配不同标定键");
    QVERIFY2(readSrc("src/ProjectManager.cpp").contains(QStringLiteral("CalibrationManager::instance()->clear();"))
             && mwSrc.contains(QStringLiteral("CalibrationManager::instance()->clear();")),
             "载入/回滚/新建方案须无条件清标定单例，防止跨方案串台");
    // X3：失败轮读数收口 + 内参分辨率核对
    QVERIFY2(readSrc("src/OpencvCalibNode.cpp").contains(QStringLiteral("m_params[QStringLiteral(\"calibFx\")] = 0.0;"))
             && readSrc("src/OpencvCalibNode.cpp").contains(QStringLiteral("标定：输入图像无法转换为 OpenCV 矩阵")),
             "标定失败轮须清上一轮内参读数；图转换失败须写原因");
    QVERIFY2(readSrc("src/OpencvUndistortNode.cpp").contains(QStringLiteral("calibExpectedWidth"))
             && readSrc("src/OpencvUndistortNode.cpp").contains(QStringLiteral("内参标定分辨率")),
             "去畸变须能核对标定分辨率，不一致判红并写原因");
}

void UiGuardsTest::f2ValidationGuards()
{
    // ① 模板搜索参数告警
    QFile t(QStringLiteral(SRC_DIR "/src/OpencvTemplateMatchNode.cpp"));
    QVERIFY2(t.open(QIODevice::ReadOnly | QIODevice::Text), "打不开模板匹配节点");
    const QString tmpl = QString::fromUtf8(t.readAll());
    t.close();
    QVERIFY2(tmpl.contains(QStringLiteral("下限大于上限，已自动交换"))
             && tmpl.contains(QStringLiteral("步长非法")),
             "搜索参数被静默纠正必须有告警");
    // P7 复核：X/Y 尺度与横切角三路漏传 warnings，且 note 写在前导致补传也进不去
    const int cw = tmpl.indexOf(QStringLiteral("QStringList rangesWarnings;"));
    const int cwEnd = tmpl.indexOf(QStringLiteral("struct PosePair"), cw);
    const QString calls = tmpl.mid(cw, cwEnd - cw);
    QVERIFY2(calls.count(QStringLiteral("collectSearchValues(")) >= 5
             && calls.count(QStringLiteral("rangesWarnings)")) >= 5,
             "角度/整体尺度/X尺度/Y尺度/横切角五路都必须传告警表，不能丢进 ignored");
    QVERIFY2(calls.contains(QStringLiteral("if (!xyMode)"))
             && calls.contains(QStringLiteral("m_params[QStringLiteral(\"searchNote\")] = note")),
             "XY 模式不查无关的整体尺度，且所有告警收齐后再落 searchNote");

    // ② 特征库同名拒绝
    QFile fs(QStringLiteral(SRC_DIR "/src/FeatureSearchNode.cpp"));
    QVERIFY2(fs.open(QIODevice::ReadOnly | QIODevice::Text), "打不开特征节点");
    const QString feat = QString::fromUtf8(fs.readAll());
    fs.close();
    QVERIFY2(feat.contains(QStringLiteral("loadFeatureSampleByName"))
             && feat.contains(QStringLiteral("同名特征样本")),
             "特征入库必须拒绝同名样本，不能静默取最新");

    // ③ VOC classes.txt 写失败不静默
    QFile y(QStringLiteral(SRC_DIR "/include/YoloAnnotation.h"));
    QVERIFY2(y.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 YoloAnnotation.h");
    const QString yolo = QString::fromUtf8(y.readAll());
    y.close();
    QVERIFY2(yolo.contains(QStringLiteral("classes.txt 写入失败")),
             "VOC 导入新增类别写 classes.txt 失败必须提示");

    // ④ 设计器表头/系列名可编辑 + 预览红字
    QFile d(QStringLiteral(SRC_DIR "/src/RuntimeInterfaceDesigner.cpp"));
    QVERIFY2(d.open(QIODevice::ReadOnly | QIODevice::Text), "打不开设计器");
    const QString des = QString::fromUtf8(d.readAll());
    d.close();
    QVERIFY2(des.contains(QStringLiteral("编辑表头")) && des.contains(QStringLiteral("编辑系列名")),
             "表头与系列名有序列化但无编辑入口必须补齐");
    const int setName = des.indexOf(QStringLiteral("view->setKnownNodeNames(m_nodeNames)"));
    const int setLayout = des.indexOf(QStringLiteral("view->setInterface(m_layout)"));
    QVERIFY2(setName >= 0 && setLayout >= 0 && setLayout < setName,
             "预览必须先装布局再给节点名（体检不能算在空布局上）");
}

void UiGuardsTest::dockDefaultVisibilityGuards()
{
    QFile mw(QStringLiteral(SRC_DIR "/src/MainWindow.cpp"));
    QVERIFY2(mw.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 MainWindow.cpp");
    const QString src = QString::fromUtf8(mw.readAll());
    mw.close();

    // ① 工具箱默认展开（不能构造后又 setToolBrowserExpanded(false) 覆盖）
    const int init = src.indexOf(QStringLiteral("setupToolCategoryBar();"));
    const int after = src.indexOf(QStringLiteral("setToolBrowserExpanded("), init);
    QVERIFY2(src.mid(after, 60).contains(QStringLiteral("true")),
             "工具箱构造时必须默认展开，48px 竖条视觉上等于没显示");
    QFile h(QStringLiteral(SRC_DIR "/include/MainWindow.h"));
    QVERIFY2(h.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 MainWindow.h");
    const QString header = QString::fromUtf8(h.readAll());
    h.close();
    QVERIFY2(header.contains(QStringLiteral("m_toolBrowserExpanded = true")),
             "工具箱展开标志默认必须 true");

    // ② 结果表用 splitDockWidget 放到图像下方，不能再 addDockWidget(Right) 与图像 tabbed
    QVERIFY2(src.contains(QStringLiteral("splitDockWidget(ui->imageDock, resultDock, Qt::Vertical)")),
             "结果表必须 split 到图像下方，不能与图像显示 tabbed 把图像页压到后面");

    // ③ 最后再 raise 图像页
    const int tail = src.indexOf(QStringLiteral("VFP_DEBUG << \"setupDockWidgets completed\""));
    QVERIFY2(src.mid(tail - 400, 400).contains(QStringLiteral("ui->imageDock->raise()")),
             "setupDockWidgets 收尾必须 raise 图像页，确保默认可见");
}

void UiGuardsTest::mvsLocalSourceGuards()
{
    // ① ImageReadNode 彻底摘除：类文件与注册均已移除
    QFile hdr(QStringLiteral(SRC_DIR "/include/ImageReadNode.h"));
    QVERIFY2(!hdr.exists(), "ImageReadNode.h 必须已删除");
    QFile impl(QStringLiteral(SRC_DIR "/src/ImageReadNode.cpp"));
    QVERIFY2(!impl.exists(), "ImageReadNode.cpp 必须已删除");
    QFile reg(QStringLiteral(SRC_DIR "/src/NodeRegistry.cpp"));
    QVERIFY2(reg.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 NodeRegistry.cpp");
    QVERIFY2(!QString::fromUtf8(reg.readAll()).contains(QStringLiteral("VFP_REG(ImageReadNode")),
             "注册表不得再注册 ImageReadNode");

    // ② MVS 本地模式能力：源类型切换刷新列表、单文件、自动切换、结果回读、持久化
    QFile mvs(QStringLiteral(SRC_DIR "/src/MvsImageSourceNode.cpp"));
    QVERIFY2(mvs.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 MvsImageSourceNode.cpp");
    const QString src = QString::fromUtf8(mvs.readAll());
    mvs.close();
    QVERIFY2(src.contains(QStringLiteral("m_isDirectory = (m_sourceType == LOCAL_FILE)")),
             "切本地模式必须置位目录标志（原致命 bug）");
    QVERIFY2(src.contains(QStringLiteral("info.isFile()")),
             "必须支持单文件路径");
    QVERIFY2(src.contains(QStringLiteral("m_autoSwitch && m_allowAutoAdvance")),
             "自动切换必须可循环且不误伤缩略图点选");
    QVERIFY2(src.contains(QStringLiteral("localImageFiles")) && src.contains(QStringLiteral("thumbnailUpdated")),
             "本地清单与缩略图通知必须齐备");
    QVERIFY2(src.contains(QStringLiteral("m_params[QStringLiteral(\"lastError\")]")),
             "读图失败必须写 lastError（结果表判红）");
}

void UiGuardsTest::deleteShortcutIgnoredWhileEditingText()
{
    // 离屏环境下 setFocus() 不生效，所以测纯判定 isTextEditingWidget；
    // isTextEditingFocus 只是它加一层 QApplication::focusWidget()。
    QLineEdit edit;
    QVERIFY2(FocusGuard::isTextEditingWidget(&edit), "输入框里按 Delete 不能删节点");

    QTextEdit text;
    QVERIFY2(FocusGuard::isTextEditingWidget(&text), "文本域同理");

    QPlainTextEdit plainText;
    QVERIFY2(FocusGuard::isTextEditingWidget(&plainText), "纯文本域同理");

    QSpinBox spin;
    QVERIFY2(FocusGuard::isTextEditingWidget(&spin), "数值框同理");

    QComboBox combo;
    combo.setEditable(true);
    QVERIFY2(FocusGuard::isTextEditingWidget(&combo), "可编辑下拉框同理");

    QWidget plain;
    QVERIFY2(!FocusGuard::isTextEditingWidget(&plain),
             "普通控件里按 Delete 应正常删节点，不能一律拦截");
    QVERIFY2(!FocusGuard::isTextEditingWidget(nullptr),
             "空指针不应崩，也不应算作文本编辑");
}

// VM4.4 官方手册 9.1 / 9.2.2（本机 PDF 页 182-183）：
// 控件库可拖入预览编辑区；复制粘贴仅 Ctrl+C / Ctrl+V。
// 这三条结构性事实不用真拖鼠标也能锁住，防止后续重构把开关关掉。
void UiGuardsTest::runtimeDesignerHasOfficialDragAndClipboard()
{
    RuntimeInterfaceDesigner dlg(QStringList{QStringLiteral("模块1")});

    bool paletteDraggable = false;
    for (QListWidget *lw : dlg.findChildren<QListWidget *>()) {
        if (lw->count() >= 7 && lw->dragEnabled()
            && lw->dragDropMode() == QAbstractItemView::DragOnly) {
            paletteDraggable = true;
            break;
        }
    }
    QVERIFY2(paletteDraggable, "控件库必须 dragEnabled + DragOnly（官方 9.2.2 拖放添加）");

    bool canvasAcceptsDrop = false;
    for (QWidget *w : dlg.findChildren<QWidget *>()) {
        if (w->acceptDrops() && w->size() == QSize(1200, 760)) {
            canvasAcceptsDrop = true;
            break;
        }
    }
    QVERIFY2(canvasAcceptsDrop, "预览编辑区画布必须 acceptDrops()");

    bool hasCopy = false, hasPaste = false;
    for (QShortcut *s : dlg.findChildren<QShortcut *>()) {
        if (s->key() == QKeySequence(QKeySequence::Copy)) hasCopy = true;
        if (s->key() == QKeySequence(QKeySequence::Paste)) hasPaste = true;
    }
    QVERIFY2(hasCopy && hasPaste, "官方复制粘贴仅 Ctrl+C / Ctrl+V，两个快捷键都要在");
}

// 官方实机（第三张截图）：属性区是 标识/数据源/布局/外观 四个分组；
// 工具条对齐 6 种 + 分布 2 种，图层 4 档。
void UiGuardsTest::runtimeDesignerHasOfficialSectionsAndAlign()
{
    RuntimeInterfaceDesigner dlg(QStringList{QStringLiteral("模块1")});
    QStringList sections;
    for (QGroupBox *gb : dlg.findChildren<QGroupBox *>())
        sections << gb->title();
    for (const QString &s : {QStringLiteral("标识"), QStringLiteral("数据源"),
                             QStringLiteral("布局"), QStringLiteral("外观")}) {
        QVERIFY2(sections.contains(s),
                 qPrintable(QStringLiteral("属性区缺官方分组：%1").arg(s)));
    }

    QStringList labels;
    for (QPushButton *b : dlg.findChildren<QPushButton *>())
        labels << b->text();
    for (const QString &s : {QStringLiteral("左对齐"), QStringLiteral("水平居中"),
                             QStringLiteral("右对齐"), QStringLiteral("顶对齐"),
                             QStringLiteral("垂直居中"), QStringLiteral("底对齐"),
                             QStringLiteral("水平分布"), QStringLiteral("垂直分布")}) {
        QVERIFY2(labels.contains(s),
                 qPrintable(QStringLiteral("缺官方对齐/分布按钮：%1").arg(s)));
    }
    for (const QString &s : {QStringLiteral("置于顶层"), QStringLiteral("上移一层"),
                             QStringLiteral("下移一层"), QStringLiteral("置于底层")}) {
        QVERIFY2(labels.contains(s),
                 qPrintable(QStringLiteral("缺官方图层档位：%1").arg(s)));
    }
}

// 新增的水平居中 / 右对齐要按「选中区域」为基准（官方 9.1），不能只做左对齐。
void UiGuardsTest::runtimeDesignerAlignHCenterAndRight()
{
    RuntimeInterfacePage page;
    page.addControl(RuntimeControlType::Button, QRect(0, 0, 100, 50));
    page.addControl(RuntimeControlType::Button, QRect(200, 40, 60, 50));
    RuntimeDesignerCanvas canvas;
    canvas.setPage(&page);
    canvas.selectIndex(0);
    canvas.selectIndex(1, true);

    canvas.alignSelected(RuntimeDesignerCanvas::AlignHCenter);
    const QRect a = page.controls.at(0).geometry;
    const QRect b = page.controls.at(1).geometry;
    QCOMPARE(a.center().x(), b.center().x());
    // 选中区域 [0,260] 的中心约 130；QRect::right() = left+width-1 会带来 1px 约定差
    QVERIFY2(qAbs(a.center().x() - 130) <= 2,
             qPrintable(QStringLiteral("水平居中应对齐选中区域中心，实得 %1")
                            .arg(a.center().x())));

    canvas.alignSelected(RuntimeDesignerCanvas::AlignRight);
    QCOMPARE(page.controls.at(0).geometry.right(), page.controls.at(1).geometry.right());
}

// 官方外观分组（2026-11-09 实机截图）：显示文本 / 字体（大小+加粗）/ 字体颜色 /
// 背景 / 边框颜色 / 点击颜色。新字段必须能往返，且旧布局（无这些键）加载后外观不变。
void UiGuardsTest::runtimeControlAppearanceRoundTripsAndStaysCompatible()
{
    RuntimeInterfacePage page;
    RuntimeControl *c = page.addControl(RuntimeControlType::Button, QRect(10, 20, 100, 40));
    QVERIFY(c);
    c->bold = true;
    c->bgColor = QStringLiteral("#FF565657");
    c->borderColor = QStringLiteral("#FF272727");
    c->pressedColor = QStringLiteral("#FF769000");

    const QJsonObject json = page.toJson();
    RuntimeInterfacePage back;
    QVERIFY(back.fromJson(json));
    QCOMPARE(back.controls.size(), 1);
    const RuntimeControl &r = back.controls.first();
    QVERIFY2(r.bold, "字形加粗要能往返");
    QCOMPARE(r.bgColor, QStringLiteral("#FF565657"));
    QCOMPARE(r.borderColor, QStringLiteral("#FF272727"));
    QCOMPARE(r.pressedColor, QStringLiteral("#FF769000"));

    // 模拟旧布局：把这些键删掉，加载后应回到「不绘制/不加粗」的默认外观
    QJsonObject legacy = json;
    QJsonArray arr = legacy[QStringLiteral("controls")].toArray();
    QJsonObject ctl = arr.at(0).toObject();
    ctl.remove(QStringLiteral("bold"));
    ctl.remove(QStringLiteral("bgColor"));
    ctl.remove(QStringLiteral("borderColor"));
    ctl.remove(QStringLiteral("pressedColor"));
    arr.replace(0, ctl);
    legacy[QStringLiteral("controls")] = arr;

    RuntimeInterfacePage old;
    QVERIFY(old.fromJson(legacy));
    const RuntimeControl &o = old.controls.first();
    QVERIFY2(!o.bold, "旧布局不该被补成加粗");
    QVERIFY2(o.bgColor.isEmpty() && o.borderColor.isEmpty() && o.pressedColor.isEmpty(),
             "旧布局的三个颜色必须为空（空 = 不绘制，外观与旧版一致）");
}

// 官方 9.3 控件区按「基础 / 布局 / 图表 / 其它」分组；组标题不可选、不可拖，
// 控件类型靠 UserRole 而不是行号（行号里有组标题）。
void UiGuardsTest::runtimeDesignerPaletteGroupedByOfficialSections()
{
    RuntimeInterfaceDesigner dlg(QStringList{QStringLiteral("模块1")});
    QListWidget *palette = nullptr;
    for (QListWidget *lw : dlg.findChildren<QListWidget *>()) {
        if (lw->dragEnabled() && lw->count() >= 7) {
            palette = lw;
            break;
        }
    }
    QVERIFY2(palette, "没找到控件库");

    QStringList headers, controls;
    for (int i = 0; i < palette->count(); ++i) {
        QListWidgetItem *it = palette->item(i);
        if (!it->data(Qt::UserRole).isValid())
            headers << it->text();
        else
            controls << it->text();
    }
    for (const QString &h : {QStringLiteral("基础"), QStringLiteral("图表"), QStringLiteral("其它")})
        QVERIFY2(headers.contains(h), qPrintable(QStringLiteral("缺控件分组：%1").arg(h)));
    for (const QString &c : {QStringLiteral("图像"), QStringLiteral("按钮"),
                             QStringLiteral("表格"), QStringLiteral("IO状态")})
        QVERIFY2(controls.contains(c), qPrintable(QStringLiteral("缺控件条目：%1").arg(c)));

    int headerCount = 0;
    for (int i = 0; i < palette->count(); ++i) {
        QListWidgetItem *it = palette->item(i);
        if (it->data(Qt::UserRole).isValid()) continue;
        ++headerCount;
        QVERIFY2(!(it->flags() & Qt::ItemIsSelectable),
                 "组标题必须不可选，否则会被当成控件添加");
        QVERIFY2(!(it->flags() & Qt::ItemIsDragEnabled), "组标题必须不可拖");
    }
    QVERIFY2(headerCount == headers.size(), "组标题计数应与识别到的一致");

    QStringList labels;
    for (QPushButton *b : dlg.findChildren<QPushButton *>())
        labels << b->text();
    for (const QString &t : {QStringLiteral("存为模板"), QStringLiteral("应用模板")})
        QVERIFY2(labels.contains(t), qPrintable(QStringLiteral("缺模板入口：%1").arg(t)));
}

// 官方按钮「数据源配置」：动作表可多行（类型/方法/参数）。
// 项目未上线，不做旧 bindKey 单动作的迁移（2026-11-09 用户口径）。
void UiGuardsTest::buttonActionsRoundTrip()
{
    // 1) 多行动作往返
    {
        RuntimeInterfacePage page;
        RuntimeControl *c = page.addControl(RuntimeControlType::Button, QRect(0, 0, 100, 40));
        QVERIFY(c);
        RuntimeAction a1;
        a1.commandType = QStringLiteral("exec");
        a1.method = QStringLiteral("single");
        RuntimeAction a2;
        a2.commandType = QStringLiteral("param");
        a2.method = QStringLiteral("曝光");
        a2.param = QStringLiteral("12.5");
        c->actions = {a1, a2};

        RuntimeInterfacePage back;
        QVERIFY(back.fromJson(page.toJson()));
        const RuntimeControl &r = back.controls.first();
        QCOMPARE(r.actions.size(), 2);
        QCOMPARE(r.actions.at(1).commandType, QStringLiteral("param"));
        QCOMPARE(r.actions.at(1).method, QStringLiteral("曝光"));
        QCOMPARE(r.actions.at(1).param, QStringLiteral("12.5"));
    }

    // 2) 设计器里按钮必须露出「配置」入口
    {
        RuntimeInterfaceDesigner dlg(QStringList{QStringLiteral("模块1")});
        QStringList labels;
        for (QPushButton *b : dlg.findChildren<QPushButton *>())
            labels << b->text();
        QVERIFY2(labels.contains(QStringLiteral("配置")),
                 "按钮的数据源应是「配置」按钮（官方实机口径）");
    }
}

// B2-2：currentPageIndex 生效、showPage 按页名切页、动作表逐行上报。
void UiGuardsTest::runtimeViewHonorsCurrentPageAndActionTable()
{
    RuntimeInterface layout;
    layout.ensurePage();
    layout.page(0)->pageName = QStringLiteral("页面1");
    layout.addPage();
    layout.page(1)->pageName = QStringLiteral("页面2");
    layout.currentPageIndex = 1;

    RuntimeInterfaceView view;
    view.setInterface(layout);
    auto *tabs = view.findChild<QTabWidget *>();
    QVERIFY2(tabs, "多页应建出 QTabWidget");
    QCOMPARE(tabs->count(), 2);
    QVERIFY2(tabs->currentIndex() == 1,
             "currentPageIndex 必须真正生效（此前只存不用）");

    view.showPage(QStringLiteral("页面1"));
    QCOMPARE(tabs->currentIndex(), 0);
    view.showPage(QStringLiteral("不存在的页"));   // 找不到应静默忽略
    QCOMPARE(tabs->currentIndex(), 0);

    // 动作表：点击按钮应逐行上报
    RuntimeControl *c = layout.page(0)->addControl(RuntimeControlType::Button,
                                                   QRect(10, 10, 120, 40));
    QVERIFY(c);
    c->title = QStringLiteral("测试按钮");
    RuntimeAction a1;
    a1.commandType = QStringLiteral("scheme");
    a1.method = QStringLiteral("save");
    RuntimeAction a2;
    a2.commandType = QStringLiteral("reset");
    a2.method = QStringLiteral("counter");
    c->actions = {a1, a2};
    view.setInterface(layout);

    QSignalSpy spy(&view, &RuntimeInterfaceView::runtimeActionTriggered);
    QPushButton *btn = nullptr;
    for (QPushButton *b : view.findChildren<QPushButton *>()) {
        if (b->text() == QStringLiteral("测试按钮")) {
            btn = b;
            break;
        }
    }
    QVERIFY2(btn, "没找到运行视图里的按钮");
    btn->click();
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("scheme"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("save"));
    QCOMPARE(spy.at(1).at(0).toString(), QStringLiteral("reset"));
}

// B3-1 参数设置控件（官方 9.3）：订阅节点 int/float 参数。
void UiGuardsTest::paramSettingControlRoundTripsAndSeedsWithoutWriteBack()
{
    RuntimeInterfacePage page;
    RuntimeControl *c = page.addControl(RuntimeControlType::ParamSetting,
                                        QRect(0, 0, 200, 60));
    QVERIFY(c);
    c->bindKey = QStringLiteral("曝光模块");
    c->paramKey = QStringLiteral("exposure");
    c->paramMin = 0.5;
    c->paramMax = 50.0;

    RuntimeInterfacePage back;
    QVERIFY(back.fromJson(page.toJson()));
    const RuntimeControl &r = back.controls.first();
    QCOMPARE(r.paramKey, QStringLiteral("exposure"));
    QCOMPARE(r.paramMin, 0.5);
    QCOMPARE(r.paramMax, 50.0);

    RuntimeInterface layout;
    layout.ensurePage();
    layout.page(0)->controls.append(r);
    RuntimeInterfaceView view;
    QSignalSpy spy(&view, &RuntimeInterfaceView::parameterChangeRequested);
    view.setInterface(layout);

    auto *spin = view.findChild<QDoubleSpinBox *>();
    QVERIFY2(spin, "参数设置控件应渲染出数值框");
    QCOMPARE(spin->minimum(), 0.5);
    QCOMPARE(spin->maximum(), 50.0);

    // 回填当前值：不应对节点产生写回
    QHash<QString, double> seeds;
    seeds.insert(QStringLiteral("曝光模块|exposure"), 12.5);
    view.setParameterValues(seeds);
    QCOMPARE(spin->value(), 12.5);
    QCOMPARE(spy.count(), 0);

    // 用户改值才触发一次写入请求
    spin->setValue(20.0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("曝光模块"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("exposure"));
    QCOMPARE(spy.at(0).at(2).toDouble(), 20.0);
}

// B3-2 开关控制（官方 9.3）：订阅节点 bool 参数，控制其是否启用。
void UiGuardsTest::switchControlRoundTripsAndSeedsWithoutWriteBack()
{
    RuntimeInterfacePage page;
    RuntimeControl *c = page.addControl(RuntimeControlType::SwitchControl,
                                        QRect(0, 0, 200, 60));
    QVERIFY(c);
    c->bindKey = QStringLiteral("预处理模块");
    c->paramKey = QStringLiteral("enable");

    RuntimeInterfacePage back;
    QVERIFY(back.fromJson(page.toJson()));
    const RuntimeControl &r = back.controls.first();
    QCOMPARE(r.type, RuntimeControlType::SwitchControl);
    QCOMPARE(r.paramKey, QStringLiteral("enable"));

    RuntimeInterface layout;
    layout.ensurePage();
    layout.page(0)->controls.append(r);
    RuntimeInterfaceView view;
    QSignalSpy spy(&view, &RuntimeInterfaceView::switchChangeRequested);
    view.setInterface(layout);

    auto *box = view.findChild<QCheckBox *>();
    QVERIFY2(box, "开关控制应渲染出勾选框");
    QVERIFY2(!box->isChecked(), "无回填时默认应为关");

    // 回填「开」：不应产生写回
    QHash<QString, double> seeds;
    seeds.insert(QStringLiteral("预处理模块|enable"), 1.0);
    view.setParameterValues(seeds);
    QVERIFY2(box->isChecked(), "回填 1.0 应显示为开");
    QCOMPARE(spy.count(), 0);

    // 用户拨动才触发
    box->setChecked(false);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("预处理模块"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("enable"));
    QCOMPARE(spy.at(0).at(2).toBool(), false);
}

// B3-3 组合框（官方 9.3）：在候选值之间切换，按字符串写入参数。
void UiGuardsTest::comboBoxRoundTripsAndSeedsWithoutWriteBack()
{
    RuntimeInterfacePage page;
    RuntimeControl *c = page.addControl(RuntimeControlType::ComboBox, QRect(0, 0, 220, 60));
    QVERIFY(c);
    c->bindKey = QStringLiteral("配方模块");
    c->paramKey = QStringLiteral("recipe");
    c->comboOptions = QStringList{QStringLiteral("配方A"), QStringLiteral("配方B"),
                                  QStringLiteral("配方C")};

    RuntimeInterfacePage back;
    QVERIFY(back.fromJson(page.toJson()));
    const RuntimeControl &r = back.controls.first();
    QCOMPARE(r.type, RuntimeControlType::ComboBox);
    QCOMPARE(r.paramKey, QStringLiteral("recipe"));
    QCOMPARE(r.comboOptions.size(), 3);
    QCOMPARE(r.comboOptions.at(2), QStringLiteral("配方C"));

    RuntimeInterface layout;
    layout.ensurePage();
    layout.page(0)->controls.append(r);
    RuntimeInterfaceView view;
    QSignalSpy spy(&view, &RuntimeInterfaceView::comboChangeRequested);
    view.setInterface(layout);

    auto *combo = view.findChild<QComboBox *>();
    QVERIFY2(combo, "组合框控件应渲染出下拉框");
    QCOMPARE(combo->count(), 3);

    // 回填「配方B」：应选中且不触发写回
    QHash<QString, QString> sel;
    sel.insert(QStringLiteral("配方模块|recipe"), QStringLiteral("配方B"));
    view.setComboSelections(sel);
    QCOMPARE(combo->currentText(), QStringLiteral("配方B"));
    QCOMPARE(spy.count(), 0);

    // 用户切换才触发
    combo->setCurrentText(QStringLiteral("配方C"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("配方模块"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("recipe"));
    QCOMPARE(spy.at(0).at(2).toString(), QStringLiteral("配方C"));
}

// B2-2 补口：param 动作的目标解析（method=节点全名，param="参数键=值"）。
void UiGuardsTest::paramActionTargetParsesNodeKeyValue()
{
    QString node, key;
    QVariant value;

    // 数值参数
    QVERIFY(parseParamActionTarget(QStringLiteral("曝光模块"),
                                   QStringLiteral("exposure=12.5"), &node, &key, &value));
    QCOMPARE(node, QStringLiteral("曝光模块"));
    QCOMPARE(key, QStringLiteral("exposure"));
    QCOMPARE(value.userType(), QMetaType::Double);
    QCOMPARE(value.toDouble(), 12.5);

    // 字符串参数（枚举/模式）
    QVERIFY(parseParamActionTarget(QStringLiteral("配方模块"),
                                   QStringLiteral("mode=配方B"), &node, &key, &value));
    QCOMPARE(value.userType(), QMetaType::QString);
    QCOMPARE(value.toString(), QStringLiteral("配方B"));

    // 前后空白应被裁掉
    QVERIFY(parseParamActionTarget(QStringLiteral("  模块1  "),
                                   QStringLiteral("  thr = 80 "), &node, &key, &value));
    QCOMPARE(node, QStringLiteral("模块1"));
    QCOMPARE(key, QStringLiteral("thr"));
    QCOMPARE(value.toDouble(), 80.0);

    // 格式错误：无 '=' / 参数键为空 / 节点为空
    QVERIFY2(!parseParamActionTarget(QStringLiteral("模块1"), QStringLiteral("exposure"),
                                     &node, &key, &value),
             "缺 '=' 必须拒绝");
    QVERIFY2(!parseParamActionTarget(QStringLiteral("模块1"), QStringLiteral("=12"),
                                     &node, &key, &value),
             "参数键为空必须拒绝");
    QVERIFY2(!parseParamActionTarget(QStringLiteral("  "), QStringLiteral("a=1"),
                                     &node, &key, &value),
             "节点为空必须拒绝");
}

// B3-4 图片 / 世界时钟（官方 9.3）：纯展示控件，序列化往返 + 渲染形态。
void UiGuardsTest::imageAndClockControlsRoundTripAndRender()
{
    RuntimeInterfacePage page;
    RuntimeControl *img = page.addControl(RuntimeControlType::ImageAsset,
                                          QRect(0, 0, 200, 100));
    QVERIFY(img);
    img->imagePath = QStringLiteral("/no/such/file.png");
    RuntimeControl *clk = page.addControl(RuntimeControlType::WorldClock,
                                          QRect(0, 110, 260, 60));
    QVERIFY(clk);
    clk->timeZoneOffsetMinutes = 540;   // UTC+9

    RuntimeInterfacePage back;
    QVERIFY(back.fromJson(page.toJson()));
    QCOMPARE(back.controls.size(), 2);
    QCOMPARE(back.controls.at(0).imagePath, QStringLiteral("/no/such/file.png"));
    QCOMPARE(back.controls.at(1).timeZoneOffsetMinutes, 540);

    RuntimeInterface layout;
    layout.ensurePage();
    layout.page(0)->controls = back.controls;
    RuntimeInterfaceView view;
    view.setInterface(layout);

    bool sawBrokenImageHint = false;
    bool sawClockText = false;
    const QRegularExpression clockRe(
        QStringLiteral("^\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}$"));
    for (QLabel *l : view.findChildren<QLabel *>()) {
        if (l->text().contains(QStringLiteral("图片无法加载"))) sawBrokenImageHint = true;
        if (clockRe.match(l->text()).hasMatch()) sawClockText = true;
    }
    QVERIFY2(sawBrokenImageHint, "图片路径无效时应给出可读提示，而不是空白");
    QVERIFY2(sawClockText, "世界时钟应显示 yyyy-MM-dd HH:mm:ss 形式的时间");
}

// B2-3a 画布配置与固定栏（官方 9.3.1 画布可配置 + 9.1 四向固定栏）。
void UiGuardsTest::canvasConfigAndBarsRoundTrip()
{
    RuntimeInterface layout;
    layout.ensurePage();
    layout.canvas.width = 800;
    layout.canvas.height = 500;
    layout.canvas.showGrid = true;
    layout.canvas.gridColor = QStringLiteral("#112233");
    layout.canvas.bgColor = QStringLiteral("#445566");
    layout.page(0)->topBarHeight = 60;
    layout.page(0)->bottomBarHeight = 40;
    layout.page(0)->leftBarWidth = 120;
    layout.page(0)->rightBarWidth = 0;

    RuntimeInterface back;
    QVERIFY(back.fromJson(layout.toJson()));
    QCOMPARE(back.canvas.width, 800);
    QCOMPARE(back.canvas.height, 500);
    QVERIFY(back.canvas.showGrid);
    QCOMPARE(back.canvas.gridColor, QStringLiteral("#112233"));
    QCOMPARE(back.canvas.bgColor, QStringLiteral("#445566"));
    QCOMPARE(back.page(0)->topBarHeight, 60);
    QCOMPARE(back.page(0)->bottomBarHeight, 40);
    QCOMPARE(back.page(0)->leftBarWidth, 120);
    QCOMPARE(back.page(0)->rightBarWidth, 0);

    // 默认值：新建布局仍是 1200×760、无固定栏
    RuntimeInterface fresh;
    fresh.ensurePage();
    QCOMPARE(fresh.canvas.width, 1200);
    QCOMPARE(fresh.canvas.height, 760);
    QCOMPARE(fresh.page(0)->topBarHeight, 0);

    // 异常值钳制：过小的画布被抬到可编辑下限
    QJsonObject tiny = layout.toJson();
    QJsonObject cv = tiny[QStringLiteral("canvas")].toObject();
    cv[QStringLiteral("width")] = 10;
    cv[QStringLiteral("height")] = 10;
    tiny[QStringLiteral("canvas")] = cv;
    RuntimeInterface clamped;
    QVERIFY(clamped.fromJson(tiny));
    QVERIFY2(clamped.canvas.width >= 200 && clamped.canvas.height >= 150,
             "画布尺寸过小应被钳制到可编辑下限");
}

// B2-3b 区域归属（官方 9.1 固定栏）：画布区随窗口缩放，固定栏 1:1 不缩放。
void UiGuardsTest::zoneGeometryScalesCanvasButNotBars()
{
    RuntimeInterface layout;
    layout.ensurePage();
    layout.canvas.width = 1000;
    layout.canvas.height = 600;
    layout.page(0)->topBarHeight = 60;
    layout.page(0)->leftBarWidth = 100;
    layout.page(0)->topBarSlots = 3;

    // zone / 格数往返
    RuntimeControl bar;
    bar.type = RuntimeControlType::TextLabel;
    bar.geometry = QRect(10, 10, 80, 40);
    bar.zone = RuntimeZone::TopBar;
    layout.page(0)->controls.append(bar);

    RuntimeInterface back;
    QVERIFY(back.fromJson(layout.toJson()));
    QCOMPARE(back.page(0)->topBarSlots, 3);
    QCOMPARE(back.page(0)->controls.first().zone, RuntimeZone::TopBar);

    // 与设计画布同尺寸：画布区 1:1
    RuntimeInterfaceView view;
    view.resize(1000, 600);
    view.setInterface(layout);
    const QRect barRect = view.controlGeometry(back.page(0)->controls.first());
    QCOMPARE(barRect, QRect(10, 10, 80, 40));   // 固定栏 1:1

    // 窗口缩到一半：固定栏尺寸不变，画布区控件减半
    RuntimeControl canvasCtrl = bar;
    canvasCtrl.zone = RuntimeZone::Canvas;
    canvasCtrl.geometry = QRect(100, 100, 200, 100);
    view.resize(600, 400);   // 画布区 = 600-100 × 400-60 = 500×340
    const QRect scaled = view.controlGeometry(canvasCtrl);
    // 设计画布区 = 1000-100 × 600-60 = 900×540；缩放 = 500/900 ≈ 0.5556
    QVERIFY2(scaled.x() > canvasCtrl.geometry.x() / 2,
             qPrintable(QStringLiteral("画布区控件应缩到约 55%%，实得 x=%1").arg(scaled.x())));
    QVERIFY2(scaled.width() < canvasCtrl.geometry.width(),
             "画布区控件宽度应随窗口缩小");
    const QRect barAgain = view.controlGeometry(bar);
    QCOMPARE(barAgain.size(), QSize(80, 40));   // 固定栏控件尺寸不随窗口变
}

// B2-3c：落点 → 所属区域 + 区域局部坐标。横条优先（角落归上/下栏），
// 画布区坐标要扣掉栏厚（与运行视图 controlGeometry 的 Canvas 分支同口径）。
void UiGuardsTest::zoneAtCanvasPointMapsBarsAndCanvas()
{
    RuntimeInterfacePage page;
    page.topBarHeight = 60;
    page.bottomBarHeight = 40;
    page.leftBarWidth = 100;
    page.rightBarWidth = 80;
    const QSize cs(1000, 600);
    QPoint local;

    QVERIFY(zoneAtCanvasPoint(page, cs, QPoint(500, 10), &local) == RuntimeZone::TopBar);
    QCOMPARE(local, QPoint(500, 10));

    QVERIFY(zoneAtCanvasPoint(page, cs, QPoint(500, 590), &local) == RuntimeZone::BottomBar);
    QCOMPARE(local, QPoint(500, 30));      // 590 - (600-40)

    QVERIFY(zoneAtCanvasPoint(page, cs, QPoint(10, 300), &local) == RuntimeZone::LeftBar);
    QCOMPARE(local, QPoint(10, 240));      // 300 - 60

    QVERIFY(zoneAtCanvasPoint(page, cs, QPoint(990, 300), &local) == RuntimeZone::RightBar);
    QCOMPARE(local, QPoint(70, 240));      // 990 - (1000-80)

    QVERIFY(zoneAtCanvasPoint(page, cs, QPoint(500, 300), &local) == RuntimeZone::Canvas);
    QCOMPARE(local, QPoint(400, 240));     // 扣掉 leftW/topH

    // 左上角落：横条优先，归顶部栏而不是左栏
    QVERIFY2(zoneAtCanvasPoint(page, cs, QPoint(10, 10), &local) == RuntimeZone::TopBar,
             "四个角落应归横向固定栏（与视觉一致）");
    QCOMPARE(local, QPoint(10, 10));

    // 没有固定栏时一律归画布，且局部坐标 = 绝对坐标
    RuntimeInterfacePage bare;
    QVERIFY(zoneAtCanvasPoint(bare, cs, QPoint(0, 0), &local) == RuntimeZone::Canvas);
    QCOMPARE(local, QPoint(0, 0));

    // 入参 local 可为空（只要区域判定时不该崩）
    QVERIFY(zoneAtCanvasPoint(page, cs, QPoint(500, 10), nullptr) == RuntimeZone::TopBar);
}

// B2-3c：设计器里控件存储坐标（区域局部）与画布绝对坐标必须能互逆，
// 否则固定栏里的控件一拖动就会写坏 geometry。
void UiGuardsTest::designerCanvasBarCoordinatesRoundTrip()
{
    RuntimeInterfacePage page;
    page.topBarHeight = 60;
    page.leftBarWidth = 100;

    RuntimeDesignerCanvas canvas;
    canvas.setCanvasSize(QSize(1000, 600));
    canvas.setPage(&page);

    RuntimeControl top;
    top.type = RuntimeControlType::TextLabel;
    top.zone = RuntimeZone::TopBar;
    top.geometry = QRect(10, 5, 80, 40);
    QCOMPARE(canvas.toCanvasRect(top), QRect(10, 5, 80, 40));   // 顶栏原点即画布原点
    QCOMPARE(canvas.toStoredRect(top, QRect(10, 5, 80, 40)), QRect(10, 5, 80, 40));

    RuntimeControl left = top;
    left.zone = RuntimeZone::LeftBar;
    left.geometry = QRect(3, 4, 50, 30);
    QCOMPARE(canvas.toCanvasRect(left), QRect(3, 64, 50, 30));  // 平移 (0, topH)
    QCOMPARE(canvas.toStoredRect(left, QRect(3, 64, 50, 30)), QRect(3, 4, 50, 30));

    RuntimeControl body = top;
    body.zone = RuntimeZone::Canvas;
    body.geometry = QRect(7, 8, 100, 50);
    QCOMPARE(canvas.toCanvasRect(body), QRect(107, 68, 100, 50));   // 平移 (leftW, topH)
    QCOMPARE(canvas.toStoredRect(body, QRect(107, 68, 100, 50)), QRect(7, 8, 100, 50));

    // 写回时不允许负坐标（拖到栏外应停在 0，而不是存负数）
    const QRect clamped = canvas.toStoredRect(left, QRect(-50, -50, 30, 20));
    QVERIFY2(clamped.left() >= 0 && clamped.top() >= 0,
             "局部坐标被钳到非负，避免拖到栏外后存负数");
}

// 官方 9.1 工具条注：删除/撤销/重做/图层位置支持鼠标右键。
// 这里锁住两件容易做错的事：右键必须上报**被点的那个**控件索引，
// 且必须先把它设为当前选中（否则菜单会作用到上一个选中的控件上）。
void UiGuardsTest::designerControlRightClickEmitsContextMenu()
{
    RuntimeInterfacePage page;
    RuntimeDesignerCanvas canvas;
    canvas.setCanvasSize(QSize(600, 400));
    canvas.setPage(&page);
    page.addControl(RuntimeControlType::Button, QRect(10, 10, 100, 40));
    page.addControl(RuntimeControlType::TextLabel, QRect(200, 200, 100, 40));
    canvas.rebuild();

    const QList<DesignerControlFrame *> frames = canvas.findChildren<DesignerControlFrame *>();
    QCOMPARE(frames.size(), 2);

    DesignerControlFrame *target = nullptr;
    for (DesignerControlFrame *f : frames) {
        if (f->geometry() == QRect(200, 200, 100, 40)) {
            target = f;
            break;
        }
    }
    QVERIFY2(target, "没找到第二个控件的框");

    QSignalSpy spy(&canvas, &RuntimeDesignerCanvas::controlContextMenu);
    QContextMenuEvent ev(QContextMenuEvent::Mouse, QPoint(5, 5), QPoint(500, 500));
    QApplication::sendEvent(target, &ev);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 1);            // 第二个控件，不是第一个
    QCOMPARE(spy.at(0).at(1).toPoint(), QPoint(500, 500));
    QCOMPARE(canvas.selectedIndex(), 1);             // 右键即选中
}

// B2-3e 页内二级 Tab（官方 9.3「Tab页」，页级实现）：
// 序列化往返 + 越界归属退回画布。后者是页级方案相对"容器控件"的关键优势：
// 删标签不会留下指向已删容器的悬空引用，退化行为是可定义的。
void UiGuardsTest::innerTabsRoundTripAndClampOrphans()
{
    RuntimeInterfacePage page;
    page.innerTabNames = QStringList{QStringLiteral("参数"), QStringLiteral("结果")};
    page.innerTabHeight = 40;
    page.defaultInnerTab = 1;
    RuntimeControl *a = page.addControl(RuntimeControlType::TextLabel, QRect(0, 0, 100, 40));
    QVERIFY(a);
    a->innerTab = 1;
    RuntimeControl *b = page.addControl(RuntimeControlType::TextLabel, QRect(0, 100, 100, 40));
    QVERIFY(b);
    b->innerTab = -1;

    RuntimeInterfacePage back;
    QVERIFY(back.fromJson(page.toJson()));
    QCOMPARE(back.innerTabNames.size(), 2);
    QCOMPARE(back.innerTabNames.at(1), QStringLiteral("结果"));
    QCOMPARE(back.innerTabHeight, 40);
    QCOMPARE(back.defaultInnerTab, 1);
    QCOMPARE(back.controls.at(0).innerTab, 1);
    QCOMPARE(back.controls.at(1).innerTab, -1);

    // 手改 JSON 把归属改成越界值（模拟"标签被删少"）：加载时必须退回 -1
    QJsonObject json = page.toJson();
    QJsonArray arr = json[QStringLiteral("controls")].toArray();
    QJsonObject c0 = arr.at(0).toObject();
    c0[QStringLiteral("innerTab")] = 9;
    arr.replace(0, c0);
    json[QStringLiteral("controls")] = arr;

    RuntimeInterfacePage clamped;
    QVERIFY(clamped.fromJson(json));
    QVERIFY2(clamped.controls.at(0).innerTab == -1,
             "越界的 Tab 归属必须退回画布(-1)，不能留下指向不存在标签的控件");
}

// 标签条占据画布区顶部一条：设计器与运行视图的坐标都要让出这个高度，
// 否则 Tab 里的控件会压在标签条上。
void UiGuardsTest::innerTabStripOffsetsDesignerAndRuntime()
{
    RuntimeInterfacePage page;
    page.innerTabNames = QStringList{QStringLiteral("A"), QStringLiteral("B")};
    page.innerTabHeight = 40;

    // 设计器：Tab 内控件显示时下移一条标签高度，写回时再减回来
    RuntimeDesignerCanvas canvas;
    canvas.setCanvasSize(QSize(600, 400));
    canvas.setPage(&page);
    RuntimeControl c;
    c.type = RuntimeControlType::TextLabel;
    c.zone = RuntimeZone::Canvas;
    c.innerTab = 0;
    c.geometry = QRect(10, 10, 100, 40);
    QCOMPARE(canvas.toCanvasRect(c), QRect(10, 50, 100, 40));            // y + 40
    QCOMPARE(canvas.toStoredRect(c, QRect(10, 50, 100, 40)), QRect(10, 10, 100, 40));

    // 不在 Tab 内的画布控件同样下移：标签条占的是整个画布区顶部，
    // 设计器与运行态必须同口径（否则同一控件两端差一条标签高度）
    c.innerTab = -1;
    QCOMPARE(canvas.toCanvasRect(c), QRect(10, 50, 100, 40));
    QCOMPARE(canvas.toStoredRect(c, QRect(10, 50, 100, 40)), QRect(10, 10, 100, 40));

    // 运行视图：画布区顶部让出标签条，1:1 时控件 y 应是 40
    RuntimeInterface layout;
    layout.ensurePage();
    layout.canvas.width = 800;
    layout.canvas.height = 600;
    RuntimeInterfacePage *pg = layout.page(0);
    pg->innerTabNames = page.innerTabNames;
    pg->innerTabHeight = 40;
    RuntimeControl body;
    body.type = RuntimeControlType::TextLabel;
    body.geometry = QRect(0, 0, 100, 50);
    body.zone = RuntimeZone::Canvas;
    pg->controls.append(body);

    RuntimeInterfaceView view;
    view.resize(800, 600);
    view.setInterface(layout);
    const QRect g = view.controlGeometry(pg->controls.first());
    QCOMPARE(g.y(), 40);
}

// B2-3 回归：窗口缩放必须仍走区域/内层 Tab 映射。
// 旧实现 resizeEvent 用纯画布缩放，固定栏控件会被放大、Tab 内容控件会丢掉标签条偏移。
void UiGuardsTest::runtimeViewResizeKeepsZoneAndInnerTabGeometry()
{
    RuntimeInterface layout;
    layout.ensurePage();
    layout.canvas.width = 800;
    layout.canvas.height = 600;
    RuntimeInterfacePage *pg = layout.page(0);
    pg->topBarHeight = 60;
    pg->innerTabNames = QStringList{QStringLiteral("A"), QStringLiteral("B")};
    pg->innerTabHeight = 40;
    RuntimeControl top;
    top.type = RuntimeControlType::Button;
    top.title = QStringLiteral("顶栏按钮");
    top.zone = RuntimeZone::TopBar;
    top.geometry = QRect(10, 10, 100, 40);
    pg->controls.append(top);
    RuntimeControl body;
    body.type = RuntimeControlType::Button;
    body.title = QStringLiteral("画布按钮");
    body.zone = RuntimeZone::Canvas;
    body.innerTab = 0;
    body.geometry = QRect(0, 0, 100, 50);
    pg->controls.append(body);

    RuntimeInterfaceView view;
    view.resize(800, 600);
    view.setInterface(layout);

    QPushButton *topBtn = nullptr;
    QPushButton *bodyBtn = nullptr;
    for (QPushButton *b : view.findChildren<QPushButton *>()) {
        if (b->text() == QStringLiteral("顶栏按钮")) topBtn = b;
        else if (b->text() == QStringLiteral("画布按钮")) bodyBtn = b;
    }
    QVERIFY(topBtn && bodyBtn);
    QCOMPARE(topBtn->geometry(), view.controlGeometry(top));
    QCOMPARE(bodyBtn->geometry(), view.controlGeometry(body));

    // 放大窗口后仍须与 controlGeometry 一致（固定栏 1:1、画布扣栏+标签条缩放）
    view.resize(1600, 1200);
    QResizeEvent ev(QSize(1600, 1200), QSize(800, 600));
    QApplication::sendEvent(&view, &ev);
    QCOMPARE(topBtn->geometry(), view.controlGeometry(top));
    QCOMPARE(bodyBtn->geometry(), view.controlGeometry(body));
    QCOMPARE(topBtn->geometry().topLeft(), QPoint(10, 10));   // 固定栏不缩放
    QCOMPARE(bodyBtn->geometry().y(), 100);                   // top60 + 标签条40
}

// B2-3 回归：多页切换时按「该页自己」的内层 Tab 建条/显隐，并记住每页的选择。
// 旧实现只在 setInterface 的当前页建条，切页不刷新，m_innerTab 还是全局单值。
void UiGuardsTest::runtimeViewPageSwitchKeepsPerPageInnerTab()
{
    RuntimeInterface layout;
    layout.ensurePage();
    layout.page(0)->pageName = QStringLiteral("页一");
    layout.addPage();
    layout.page(1)->pageName = QStringLiteral("页二");
    layout.currentPageIndex = 0;
    for (int pi = 0; pi < 2; ++pi) {
        RuntimeInterfacePage *pg = layout.page(pi);
        pg->innerTabNames = QStringList{QStringLiteral("甲"), QStringLiteral("乙")};
        pg->innerTabHeight = 30;
        pg->defaultInnerTab = pi;          // 页一默认甲(0)，页二默认乙(1)
        RuntimeControl *a = pg->addControl(RuntimeControlType::TextLabel, QRect(0, 0, 80, 30));
        QVERIFY(a);
        a->title = QStringLiteral("控件甲");
        a->innerTab = 0;
        RuntimeControl *b = pg->addControl(RuntimeControlType::TextLabel, QRect(0, 40, 80, 30));
        QVERIFY(b);
        b->title = QStringLiteral("控件乙");
        b->innerTab = 1;
    }

    RuntimeInterfaceView view;
    view.resize(800, 600);
    view.setInterface(layout);
    auto *tabs = view.findChild<QTabWidget *>();
    QVERIFY(tabs);
    QCOMPARE(tabs->currentIndex(), 0);

    auto barOf = [&](int i) -> QWidget * {
        return tabs->widget(i)->findChild<QWidget *>(QStringLiteral("RuntimeInnerTabBar"));
    };
    auto labelVisible = [&](int i, const QString &text) -> bool {
        // 显隐作用在宿主控件上；view 未 show，isVisible 会因祖先隐藏恒 false，
        // 故看宿主自身 isHidden（applyInnerTabVisibility 正是 setVisible 宿主）
        const QList<QLabel *> labels = tabs->widget(i)->findChildren<QLabel *>();
        for (QLabel *l : labels)
            if (l->text() == text)
                return l->parentWidget() && !l->parentWidget()->isHidden();
        return false;
    };

    // 初始页一：默认甲可见、乙隐藏
    QVERIFY(barOf(0));
    QVERIFY(labelVisible(0, QStringLiteral("控件甲")));
    QVERIFY(!labelVisible(0, QStringLiteral("控件乙")));

    // 切到页二：必须建出页二自己的标签条，并按页二默认(乙)显隐
    tabs->setCurrentIndex(1);
    QVERIFY2(barOf(1), "切页后必须为该页建/刷新内层标签条");
    QVERIFY(!labelVisible(1, QStringLiteral("控件甲")));
    QVERIFY(labelVisible(1, QStringLiteral("控件乙")));

    // 在页二手动选「甲」，切走再切回应记住页二自己的选择（不被页一覆盖）
    QPushButton *tabA = nullptr;
    for (QPushButton *b : barOf(1)->findChildren<QPushButton *>())
        if (b->property("vfpTabIdx").toInt() == 0) tabA = b;
    QVERIFY(tabA);
    tabA->click();
    QVERIFY(labelVisible(1, QStringLiteral("控件甲")));
    QVERIFY(!labelVisible(1, QStringLiteral("控件乙")));

    tabs->setCurrentIndex(0);
    QVERIFY(labelVisible(0, QStringLiteral("控件甲")));   // 页一仍是自己的默认甲
    tabs->setCurrentIndex(1);
    QVERIFY(labelVisible(1, QStringLiteral("控件甲")));   // 页二记住手动选的甲
    QVERIFY(!labelVisible(1, QStringLiteral("控件乙")));
    QCOMPARE(tabs->widget(1)->findChildren<QWidget *>(
                 QStringLiteral("RuntimeInnerTabBar")).size(), 1);   // 不重复建条
}

// 多页几何回归：页容器因标签条占位而小于视图时，画布映射必须以页容器尺寸为基，
// 否则控件按视图尺寸缩放会整体偏移/越界（单页容器铺满视图，行为不变）。
void UiGuardsTest::multiPageGeometryUsesPageContainerBase()
{
    RuntimeInterface layout;
    layout.ensurePage();
    layout.page(0)->pageName = QStringLiteral("页一");
    layout.addPage();
    layout.page(1)->pageName = QStringLiteral("页二");
    layout.currentPageIndex = 0;
    layout.canvas.width = 800;
    layout.canvas.height = 600;
    RuntimeControl ctrl;
    ctrl.type = RuntimeControlType::TextLabel;
    ctrl.zone = RuntimeZone::Canvas;
    ctrl.geometry = QRect(0, 560, 100, 40);
    layout.page(0)->controls.append(ctrl);
    RuntimeInterfaceView view;
    view.resize(800, 600);
    view.setInterface(layout);
    auto *tabs = view.findChild<QTabWidget *>();
    QVERIFY(tabs);
    QWidget *c0 = tabs->widget(0);
    QVERIFY(c0);
    c0->resize(700, 500);   // 模拟多页页容器因标签条占位而小于视图
    const QRect g = view.controlGeometry(ctrl);
    QVERIFY2(g.bottom() <= c0->height(),
             "多页画布映射必须以页容器尺寸为基，控件不得超出页容器");
}

// 线图阵列（官方 9.3 图表）：每系列一张线图，JSON 形状与钳制都要能往返。
void UiGuardsTest::lineChartRoundTripsSeriesAndClamps()
{
    RuntimeInterfacePage page;
    RuntimeControl c;
    c.type = RuntimeControlType::LineChart;
    c.title = QStringLiteral("趋势");
    c.chartHistoryLimit = 40;
    c.chartTitle = QStringLiteral("执行趋势");
    c.showChartTitle = true;
    c.chartBgColor = QStringLiteral("#101018");
    c.chartAxisColor = QStringLiteral("#c0c0d0");
    ChartSeries a;
    a.name = QStringLiteral("压力");
    a.bindType = QStringLiteral("global");
    a.bindKey = QStringLiteral("pressure");
    a.color = QStringLiteral("#ff8800");
    a.showLimits = true;
    a.lower = 5.0;
    a.upper = 9.0;
    c.chartSeries.append(a);
    ChartSeries b;
    b.name = QStringLiteral("结果");
    b.bindType = QStringLiteral("node");
    b.bindKey = QStringLiteral("流程/结果");
    c.chartSeries.append(b);
    page.controls.append(c);

    RuntimeInterfacePage back;
    QVERIFY(back.fromJson(page.toJson()));
    const RuntimeControl &bc = back.controls.at(0);
    QCOMPARE(bc.type, RuntimeControlType::LineChart);
    QCOMPARE(bc.chartSeries.size(), 2);
    QCOMPARE(bc.chartSeries.at(0).name, QStringLiteral("压力"));
    QCOMPARE(bc.chartSeries.at(0).bindType, QStringLiteral("global"));
    QCOMPARE(bc.chartSeries.at(0).bindKey, QStringLiteral("pressure"));
    QCOMPARE(bc.chartSeries.at(0).color, QStringLiteral("#ff8800"));
    QVERIFY(bc.chartSeries.at(0).showLimits);
    QCOMPARE(bc.chartSeries.at(0).lower, 5.0);
    QCOMPARE(bc.chartSeries.at(0).upper, 9.0);
    QCOMPARE(bc.chartSeries.at(1).bindType, QStringLiteral("node"));
    QCOMPARE(bc.chartSeries.at(1).bindKey, QStringLiteral("流程/结果"));
    QCOMPARE(bc.chartTitle, QStringLiteral("执行趋势"));
    QVERIFY(bc.showChartTitle);
    QCOMPARE(bc.chartBgColor, QStringLiteral("#101018"));
    QCOMPARE(bc.chartAxisColor, QStringLiteral("#c0c0d0"));
    QCOMPARE(bc.chartHistoryLimit, 40);

    // 手改 JSON：超界历史条数钳到 1000；upper<=lower 自动拉开；
    // 空绑定的系列没有数据来源，载入时丢弃而不是留一张空图。
    QJsonObject json = page.toJson();
    QJsonArray controls = json[QStringLiteral("controls")].toArray();
    QJsonObject co = controls.at(0).toObject();
    co[QStringLiteral("chartHistoryLimit")] = 5000;
    QJsonArray seriesArr = co[QStringLiteral("chartSeries")].toArray();
    QJsonObject s0 = seriesArr.at(0).toObject();
    s0[QStringLiteral("upper")] = 1.0;
    seriesArr.replace(0, s0);
    QJsonObject orphan;
    orphan[QStringLiteral("name")] = QStringLiteral("空系列");
    orphan[QStringLiteral("bindType")] = QStringLiteral("global");
    orphan[QStringLiteral("bindKey")] = QString();
    seriesArr.append(orphan);
    co[QStringLiteral("chartSeries")] = seriesArr;
    controls.replace(0, co);
    json[QStringLiteral("controls")] = controls;

    RuntimeInterfacePage clamped;
    QVERIFY(clamped.fromJson(json));
    const RuntimeControl &cc = clamped.controls.at(0);
    QCOMPARE(cc.chartHistoryLimit, 1000);
    QCOMPARE(cc.chartSeries.size(), 2);
    QCOMPARE(cc.chartSeries.at(0).upper, 6.0);
}

static QLineSeries *mainLineSeries(QChartView *view)
{
    if (!view) return nullptr;
    const QList<QAbstractSeries *> all = view->chart()->series();
    return all.isEmpty() ? nullptr : qobject_cast<QLineSeries *>(all.first());
}

// 运行期数据入口：全局变量/节点输出按 bindKey 精确匹配，非数值忽略，
// 历史条数裁剪后横轴保留绝对执行次数（官方：执行次数 vs 当前值）。
void UiGuardsTest::lineChartRuntimeAppendsBoundSamplesAndTrims()
{
    RuntimeInterface layout;
    layout.ensurePage();
    layout.canvas.width = 900;
    layout.canvas.height = 600;
    RuntimeInterfacePage *pg = layout.page(0);
    QVERIFY(pg);
    RuntimeControl chart;
    chart.type = RuntimeControlType::LineChart;
    chart.geometry = QRect(10, 10, 420, 300);
    chart.chartHistoryLimit = 3;
    chart.chartTitle = QStringLiteral("监控趋势");
    chart.showChartTitle = true;
    ChartSeries g;
    g.name = QStringLiteral("压力");
    g.bindType = QStringLiteral("global");
    g.bindKey = QStringLiteral("pressure");
    g.color = QStringLiteral("#ff0000");
    chart.chartSeries.append(g);
    ChartSeries n;
    n.name = QStringLiteral("节点值");
    n.bindType = QStringLiteral("node");
    n.bindKey = QStringLiteral("流程/节点");
    n.showLimits = true;
    n.lower = 10.0;
    n.upper = 20.0;
    chart.chartSeries.append(n);
    ChartSeries other;
    other.name = QStringLiteral("其他");
    other.bindType = QStringLiteral("global");
    other.bindKey = QStringLiteral("other");
    chart.chartSeries.append(other);
    pg->controls.append(chart);

    RuntimeInterfaceView view;
    view.resize(900, 600);
    view.setInterface(layout);

    QVERIFY2(view.findChild<QWidget *>(QStringLiteral("RuntimeLineChart")),
             "运行态必须建出线图阵列面板");
    QChartView *v0 = view.findChild<QChartView *>(QStringLiteral("RuntimeLineChartView0"));
    QChartView *v1 = view.findChild<QChartView *>(QStringLiteral("RuntimeLineChartView1"));
    QChartView *v2 = view.findChild<QChartView *>(QStringLiteral("RuntimeLineChartView2"));
    QVERIFY(v0 && v1 && v2);
    QLabel *title = view.findChild<QLabel *>(QStringLiteral("RuntimeChartTitle"));
    QVERIFY2(title, "showChartTitle=true 时要有标题行");
    QCOMPARE(title->text(), QStringLiteral("监控趋势"));
    QLabel *name0 = view.findChild<QLabel *>(QStringLiteral("RuntimeLineChartName0"));
    QVERIFY(name0);
    QCOMPARE(name0->text(), QStringLiteral("压力"));

    QLineSeries *s0 = mainLineSeries(v0);
    QLineSeries *s1 = mainLineSeries(v1);
    QLineSeries *s2 = mainLineSeries(v2);
    QVERIFY(s0 && s1 && s2);

    view.updateVariable(QStringLiteral("pressure"), 5.0);
    view.updateVariable(QStringLiteral("pressure"), 7.0);
    view.updateVariable(QStringLiteral("other"), 99.0);
    view.updateNodeOutput(QStringLiteral("流程/节点"), 15.0);
    view.updateVariable(QStringLiteral("pressure"), QVariant(QStringLiteral("abc")));
    view.updateNodeOutput(QStringLiteral("流程/节点"), true);

    const QList<QPointF> p0 = s0->points();
    QCOMPARE(p0.size(), 2);
    QCOMPARE(p0.at(1).y(), 7.0);
    const QList<QPointF> p1 = s1->points();
    QCOMPARE(p1.size(), 2);
    QCOMPARE(p1.at(0).y(), 15.0);
    QCOMPARE(p1.at(1).y(), 1.0);   // bool 以 1/0 进入图表
    const QList<QPointF> p2 = s2->points();
    QCOMPARE(p2.size(), 1);
    QCOMPARE(p2.at(0).y(), 99.0);

    // 上下限参与纵轴范围：即使数据 15 在限内，上限 20 也必须可见
    const QList<QAbstractAxis *> axesY = v1->chart()->axes(Qt::Vertical);
    QVERIFY(!axesY.isEmpty());
    auto *axisY = qobject_cast<QValueAxis *>(axesY.first());
    QVERIFY(axisY);
    QVERIFY(axisY->min() <= 10.0);
    QVERIFY(axisY->max() >= 20.0);

    // 历史条数：超过上限裁掉最旧点，横轴保留绝对执行次数
    for (int i = 0; i < 5; ++i)
        view.updateVariable(QStringLiteral("pressure"), 100.0 + i);
    const QList<QPointF> trimmed = s0->points();
    QCOMPARE(trimmed.size(), 3);
    QCOMPARE(trimmed.at(0).x(), 4.0);
    QCOMPARE(trimmed.at(2).x(), 6.0);
    QCOMPARE(trimmed.at(2).y(), 104.0);
    const QList<QAbstractAxis *> axesX = v0->chart()->axes(Qt::Horizontal);
    QVERIFY(!axesX.isEmpty());
    auto *axisX = qobject_cast<QValueAxis *>(axesX.first());
    QVERIFY(axisX);
    QCOMPARE(axisX->min(), 4.0);
    QCOMPARE(axisX->max(), 6.0);
}

// 多数据线图（官方 9.3 图表）：多系列同绘一张图，各系列独立累计执行次数。
void UiGuardsTest::multiLineChartSharesOneChartAndAppends()
{
    RuntimeInterface layout;
    layout.ensurePage();
    RuntimeControl chart;
    chart.type = RuntimeControlType::MultiLineChart;
    chart.geometry = QRect(10, 10, 420, 300);
    ChartSeries g;
    g.name = QStringLiteral("甲");
    g.bindType = QStringLiteral("global");
    g.bindKey = QStringLiteral("a");
    chart.chartSeries.append(g);
    ChartSeries n;
    n.name = QStringLiteral("乙");
    n.bindType = QStringLiteral("node");
    n.bindKey = QStringLiteral("n");
    chart.chartSeries.append(n);
    layout.page(0)->controls.append(chart);

    RuntimeInterfaceView view;
    view.resize(900, 600);
    view.setInterface(layout);

    QWidget *panel = view.findChild<QWidget *>(QStringLiteral("RuntimeMultiLineChart"));
    QVERIFY2(panel, "运行态必须建出多数据线图面板");
    QCOMPARE(panel->findChildren<QChartView *>().size(), 1);   // 只有一张共享图
    QChartView *v = view.findChild<QChartView *>(QStringLiteral("RuntimeMultiLineChartView0"));
    QVERIFY(v);
    QCOMPARE(v->chart()->series().size(), 2);
    QLineSeries *s0 = mainLineSeries(v);
    auto *s1 = qobject_cast<QLineSeries *>(v->chart()->series().at(1));
    QVERIFY(s0 && s1);

    view.updateVariable(QStringLiteral("a"), 3.0);
    view.updateNodeOutput(QStringLiteral("n"), 5.0);
    QCOMPARE(s0->points().size(), 1);
    QCOMPARE(s0->points().at(0).y(), 3.0);
    QCOMPARE(s1->points().size(), 1);
    QCOMPARE(s1->points().at(0).y(), 5.0);

    view.updateVariable(QStringLiteral("a"), 7.0);
    QCOMPARE(s0->points().size(), 2);
    QCOMPARE(s0->points().at(1).y(), 7.0);
    QCOMPARE(s1->points().size(), 1);   // 未绑定的系列不受影响
}

// 饼图（官方 9.3 图表）：每个数据源一个扇区，喂数更新最新当前值（占比随之变化）。
void UiGuardsTest::pieChartUpdatesSliceValues()
{
    RuntimeInterface layout;
    layout.ensurePage();
    RuntimeControl chart;
    chart.type = RuntimeControlType::PieChart;
    chart.geometry = QRect(10, 10, 420, 300);
    ChartSeries a;
    a.name = QStringLiteral("甲");
    a.bindType = QStringLiteral("global");
    a.bindKey = QStringLiteral("a");
    chart.chartSeries.append(a);
    ChartSeries b;
    b.name = QStringLiteral("乙");
    b.bindType = QStringLiteral("global");
    b.bindKey = QStringLiteral("b");
    chart.chartSeries.append(b);
    layout.page(0)->controls.append(chart);

    RuntimeInterfaceView view;
    view.resize(900, 600);
    view.setInterface(layout);

    QChartView *v = view.findChild<QChartView *>(QStringLiteral("RuntimePieChartView0"));
    QVERIFY2(v, "运行态必须建出饼图视图");
    QVERIFY(!v->chart()->series().isEmpty());
    auto *pie = qobject_cast<QPieSeries *>(v->chart()->series().first());
    QVERIFY(pie);
    QCOMPARE(pie->slices().size(), 2);

    view.updateVariable(QStringLiteral("a"), 30.0);
    view.updateVariable(QStringLiteral("b"), 10.0);
    QVERIFY(qFuzzyCompare(pie->slices().at(0)->value(), 30.0));
    QVERIFY(qFuzzyCompare(pie->slices().at(1)->value(), 10.0));
}

// 新增两类图表的 JSON 往返：类型与数据系列/标题都要原样保留。
void UiGuardsTest::chartTypesRoundTrip()
{
    const RuntimeControlType types[] = {RuntimeControlType::MultiLineChart,
                                        RuntimeControlType::PieChart};
    for (RuntimeControlType type : types) {
        RuntimeInterfacePage page;
        RuntimeControl c;
        c.type = type;
        c.chartTitle = QStringLiteral("图表标题");
        ChartSeries s;
        s.name = QStringLiteral("系列甲");
        s.bindType = QStringLiteral("global");
        s.bindKey = QStringLiteral("a");
        s.color = QStringLiteral("#ff8800");
        c.chartSeries.append(s);
        ChartSeries t;
        t.name = QStringLiteral("系列乙");
        t.bindType = QStringLiteral("node");
        t.bindKey = QStringLiteral("流程/结果");
        c.chartSeries.append(t);
        page.controls.append(c);

        RuntimeInterfacePage back;
        QVERIFY(back.fromJson(page.toJson()));
        QCOMPARE(back.controls.size(), 1);
        const RuntimeControl &bc = back.controls.at(0);
        QCOMPARE(bc.type, type);
        QCOMPARE(bc.chartTitle, QStringLiteral("图表标题"));
        QCOMPARE(bc.chartSeries.size(), 2);
        QCOMPARE(bc.chartSeries.at(0).bindKey, QStringLiteral("a"));
        QCOMPARE(bc.chartSeries.at(0).color, QStringLiteral("#ff8800"));
        QCOMPARE(bc.chartSeries.at(1).bindType, QStringLiteral("node"));
        QCOMPARE(bc.chartSeries.at(1).bindKey, QStringLiteral("流程/结果"));
    }
}

// 设计器内层 Tab 编辑聚焦：画布只显示选中 Tab 的控件 + 不归属 Tab 的画布控件；
// -1（全部画布）恢复全显。画布未 show，显隐要看 isHidden 而非 isVisible。
void UiGuardsTest::designerInnerTabEditFilter()
{
    RuntimeInterfacePage page;
    page.innerTabNames = QStringList{QStringLiteral("A"), QStringLiteral("B")};
    page.innerTabHeight = 30;
    RuntimeControl *a = page.addControl(RuntimeControlType::TextLabel, QRect(0, 0, 80, 30));
    a->innerTab = 0;
    RuntimeControl *b = page.addControl(RuntimeControlType::TextLabel, QRect(0, 60, 80, 30));
    b->innerTab = 1;
    RuntimeControl *c = page.addControl(RuntimeControlType::TextLabel, QRect(0, 120, 80, 30));
    c->innerTab = -1;
    RuntimeDesignerCanvas canvas;
    canvas.setCanvasSize(QSize(600, 400));
    canvas.setPage(&page);
    canvas.rebuild();
    const QList<DesignerControlFrame *> frames = canvas.findChildren<DesignerControlFrame *>();
    QCOMPARE(frames.size(), 3);
    // rebuild() 销毁旧框重建新框：每次取画布当前子控件计数（旧框已脱离画布）
    auto hiddenCount = [&]() {
        int n = 0;
        const QList<DesignerControlFrame *> cur = canvas.findChildren<DesignerControlFrame *>();
        for (DesignerControlFrame *f : cur) if (f->isHidden()) ++n;
        return n;
    };
    QCOMPARE(hiddenCount(), 0);          // 默认全部可见
    canvas.setEditInnerTab(0);
    QCOMPARE(hiddenCount(), 1);          // 只隐藏归属 B 的那个
    canvas.setEditInnerTab(1);
    QCOMPARE(hiddenCount(), 1);          // 只隐藏归属 A 的那个
    canvas.setEditInnerTab(-1);
    QCOMPARE(hiddenCount(), 0);          // 恢复全部

    // 对话框必须暴露「编辑内层Tab」选择器（objectName 定位）
    RuntimeInterfaceDesigner dlg(QStringList{QStringLiteral("模块1")});
    QVERIFY(dlg.findChild<QComboBox *>(QStringLiteral("designerEditInnerTabCombo")));
}

// B1：画布尺寸「按比例适配」——只缩放画布区控件，固定栏控件 1:1 不动；
// 尺寸非正或相等时不改任何几何（返回 false）。
void UiGuardsTest::canvasScalingHelperScalesCanvasControls()
{
    RuntimeInterfacePage page;
    RuntimeControl a;
    a.type = RuntimeControlType::TextLabel;
    a.zone = RuntimeZone::Canvas;
    a.geometry = QRect(100, 100, 200, 100);
    page.controls.append(a);
    RuntimeControl b;
    b.type = RuntimeControlType::TextLabel;
    b.zone = RuntimeZone::TopBar;
    b.geometry = QRect(10, 10, 50, 20);
    page.controls.append(b);

    QVERIFY(scaleCanvasControls(page, QSize(800, 600), QSize(1600, 1200)));
    QCOMPARE(page.controls.at(0).geometry, QRect(200, 200, 400, 200));
    QCOMPARE(page.controls.at(1).geometry, QRect(10, 10, 50, 20));   // 固定栏 1:1

    RuntimeInterfacePage same = page;
    QVERIFY(!scaleCanvasControls(same, QSize(1600, 1200), QSize(1600, 1200)));
    QCOMPARE(same.controls.at(0).geometry, page.controls.at(0).geometry);  // 无改动
    QVERIFY(!scaleCanvasControls(same, QSize(0, 0), QSize(1600, 1200)));
}

// B1：设计器必须暴露画布尺寸输入 + 「按比例适配」勾选 + 应用按钮 + 四向固定栏厚度
void UiGuardsTest::designerExposesCanvasAndBarConfig()
{
    RuntimeInterfaceDesigner dlg(QStringList{QStringLiteral("模块1")});
    QVERIFY(dlg.findChild<QSpinBox *>(QStringLiteral("designerCanvasWidthSpin")));
    QVERIFY(dlg.findChild<QSpinBox *>(QStringLiteral("designerCanvasHeightSpin")));
    QVERIFY(dlg.findChild<QCheckBox *>(QStringLiteral("designerCanvasScaleCheck")));
    QVERIFY(dlg.findChild<QPushButton *>(QStringLiteral("designerCanvasApplyBtn")));
    QVERIFY(dlg.findChild<QSpinBox *>(QStringLiteral("designerBarTopSpin")));
    QVERIFY(dlg.findChild<QSpinBox *>(QStringLiteral("designerBarBottomSpin")));
    QVERIFY(dlg.findChild<QSpinBox *>(QStringLiteral("designerBarLeftSpin")));
    QVERIFY(dlg.findChild<QSpinBox *>(QStringLiteral("designerBarRightSpin")));
}

// B1 末项：几何编辑（拖动 / 右下角缩放 / 方向键微调）此前绕过撤销栈。
// 这里锁住三件事：一次手势只入栈一枚「改前快照」、只点选不挪动不入栈、
// 方向键首次按键入栈（自动重复不算新手势）。
void UiGuardsTest::designerGeometryEditPushesUndo()
{
    RuntimeInterfaceDesigner dlg(QStringList{QStringLiteral("模块1")});

    // 控件库：挑带 Qt::UserRole（真控件条目，不是分组标题）的第一行并选中
    QListWidget *palette = nullptr;
    for (QListWidget *lw : dlg.findChildren<QListWidget *>()) {
        if (lw->dragEnabled() && lw->count() >= 7) {
            palette = lw;
            break;
        }
    }
    QVERIFY2(palette, "没找到控件库");
    int row = -1;
    for (int i = 0; i < palette->count(); ++i) {
        if (palette->item(i)->data(Qt::UserRole).isValid()) {
            row = i;
            break;
        }
    }
    QVERIFY2(row >= 0, "控件库里没有可添加的控件条目");
    palette->setCurrentRow(row);

    // 私有槽按名调用：走设计器自己的添加流程（含 pushUndo / rebuild / selectIndex）
    QVERIFY2(QMetaObject::invokeMethod(&dlg, "addControlAt", Qt::DirectConnection,
                                       Q_ARG(QPoint, QPoint(120, 120))),
             "addControlAt 调用失败");

    auto *canvas = dlg.findChild<RuntimeDesignerCanvas *>();
    QVERIFY(canvas);
    const QList<DesignerControlFrame *> frames = canvas->findChildren<DesignerControlFrame *>();
    QVERIFY2(!frames.isEmpty(), "画布上没有控件框");
    DesignerControlFrame *frame = frames.first();

    QSignalSpy startedSpy(canvas, &RuntimeDesignerCanvas::geometryEditStarted);
    QSignalSpy finishedSpy(canvas, &RuntimeDesignerCanvas::geometryEditFinished);
    QVERIFY2(startedSpy.isValid() && finishedSpy.isValid(), "几何手势信号未声明");

    const int depth0 = dlg.undoDepth();
    const QRect g0 = frame->geometry();

    // 1) 真拖动：按下 → 移动 → 松开 = 恰好一枚撤销点
    QTest::mousePress(frame, Qt::LeftButton, Qt::NoModifier, QPoint(6, 6));
    QTest::mouseMove(frame, QPoint(66, 46));
    bool manualMove = false;
    if (frame->geometry() == g0) {
        // 本环境下 QTest::mouseMove 没把「按住左键」的移动送到控件：手动补一条
        manualMove = true;
        const QPoint to(66, 46);
        QMouseEvent move(QEvent::MouseMove, to, frame->mapToGlobal(to),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(frame, &move);
    }
    QTest::mouseRelease(frame, Qt::LeftButton, Qt::NoModifier, QPoint(66, 46));
    QVERIFY2(frame->geometry() != g0, "拖动没有改变控件几何（模拟未生效）");
    QCOMPARE(dlg.undoDepth(), depth0 + 1);
    QCOMPARE(startedSpy.count(), 1);    // 一次拖动 = 一次 started + 一次 finished
    QCOMPARE(finishedSpy.count(), 1);
    qInfo("designerGeometryEditPushesUndo: drag path = %s",
          manualMove ? "manual QMouseEvent" : "QTest::mouseMove");

    // 2) 只点选不挪动：不得新增撤销点
    QTest::mousePress(frame, Qt::LeftButton, Qt::NoModifier, QPoint(6, 6));
    QTest::mouseRelease(frame, Qt::LeftButton, Qt::NoModifier, QPoint(6, 6));
    QCOMPARE(dlg.undoDepth(), depth0 + 1);

    // 3) 方向键微调 1px：首次按键建一枚撤销点
    frame->setFocus();
    const QRect g1 = frame->geometry();
    QTest::keyClick(frame, Qt::Key_Right);
    QCOMPARE(dlg.undoDepth(), depth0 + 2);
    QVERIFY2(frame->geometry().left() >= g1.left() + 1, "方向键没有移动控件");

    // 4) 撤销逐枚回退：证明入栈的是「改前」快照而不是改后
    //    第一枚 = 方向键之前（拖动后），第二枚 = 拖动之前
    QVERIFY(QMetaObject::invokeMethod(&dlg, "undo", Qt::DirectConnection));
    QCOMPARE(dlg.undoDepth(), depth0 + 1);
    QList<DesignerControlFrame *> afterUndo = canvas->findChildren<DesignerControlFrame *>();
    QVERIFY2(!afterUndo.isEmpty(), "撤销后画布上没有控件框");
    QCOMPARE(afterUndo.first()->geometry(), g1);
    QVERIFY(QMetaObject::invokeMethod(&dlg, "undo", Qt::DirectConnection));
    QCOMPARE(dlg.undoDepth(), depth0);
    afterUndo = canvas->findChildren<DesignerControlFrame *>();
    QVERIFY2(!afterUndo.isEmpty(), "撤销后画布上没有控件框");
    QCOMPARE(afterUndo.first()->geometry(), g0);
}

// B1 回归：属性编辑也走 pushUndo() + 改前抓到的控件指针写回。QList 写时复制下，
// 快照若与 m_layout 共享页数组，写回会同时改到快照 ⇒ 撤销「撤不掉」这次编辑。
// 这里用真实的标题输入框走一遍：改标题 → 撤销必须回到旧标题 → 重做必须再回来。
void UiGuardsTest::designerPropertyEditUndoRestoresValue()
{
    RuntimeInterfaceDesigner dlg(QStringList{QStringLiteral("模块1")});

    QListWidget *palette = nullptr;
    for (QListWidget *lw : dlg.findChildren<QListWidget *>()) {
        if (lw->dragEnabled() && lw->count() >= 7) {
            palette = lw;
            break;
        }
    }
    QVERIFY2(palette, "没找到控件库");
    int row = -1;
    for (int i = 0; i < palette->count(); ++i) {
        if (palette->item(i)->data(Qt::UserRole).isValid()) {
            row = i;
            break;
        }
    }
    QVERIFY2(row >= 0, "控件库里没有可添加的控件条目");
    palette->setCurrentRow(row);
    QVERIFY2(QMetaObject::invokeMethod(&dlg, "addControlAt", Qt::DirectConnection,
                                       Q_ARG(QPoint, QPoint(120, 120))),
             "addControlAt 调用失败");

    auto *canvas = dlg.findChild<RuntimeDesignerCanvas *>();
    QVERIFY(canvas);
    const QList<DesignerControlFrame *> frames = canvas->findChildren<DesignerControlFrame *>();
    QVERIFY2(!frames.isEmpty(), "画布上没有控件框");
    DesignerControlFrame *frame = frames.first();

    // 点选（不挪动，不建撤销点）→ 属性面板填上该控件
    QTest::mousePress(frame, Qt::LeftButton, Qt::NoModifier, QPoint(6, 6));
    QTest::mouseRelease(frame, Qt::LeftButton, Qt::NoModifier, QPoint(6, 6));

    auto *titleEdit = dlg.findChild<QLineEdit *>(QStringLiteral("designerTitleEdit"));
    QVERIFY2(titleEdit, "没找到属性面板的标题输入框");

    auto titleOf = [&dlg]() {
        const RuntimeInterface &layout = dlg.resultInterface();
        if (layout.pages.isEmpty() || layout.pages.first().controls.isEmpty())
            return QString();
        return layout.pages.first().controls.first().title;
    };
    const QString before = titleOf();
    const int depth0 = dlg.undoDepth();
    QVERIFY2(!before.isEmpty(), "控件初始标题为空，无法区分改前改后");

    const QString edited = QStringLiteral("联调标题");
    titleEdit->setText(edited);
    QCOMPARE(titleOf(), edited);
    QCOMPARE(dlg.undoDepth(), depth0 + 1);

    QVERIFY(QMetaObject::invokeMethod(&dlg, "undo", Qt::DirectConnection));
    QCOMPARE(titleOf(), before);          // 撤销必须回到改前，不能停在 edited
    QCOMPARE(dlg.undoDepth(), depth0);

    QVERIFY(QMetaObject::invokeMethod(&dlg, "redo", Qt::DirectConnection));
    QCOMPARE(titleOf(), edited);
}

// B3-2：控件库条目必须都能走「添加控件」路径。旧实现 paletteTypeOf 的上界停在
// IoStatus，参数设置/图表等后加类型全被静默丢弃；这里点名新 4 类 + 两个旧的后加类型。
void UiGuardsTest::designerPaletteAddsNewAndPreviouslyBlockedControls()
{
    RuntimeInterfaceDesigner dlg(QStringList{QStringLiteral("模块1")});
    QListWidget *palette = nullptr;
    for (QListWidget *lw : dlg.findChildren<QListWidget *>()) {
        if (lw->dragEnabled() && lw->count() >= 7) {
            palette = lw;
            break;
        }
    }
    QVERIFY2(palette, "没找到控件库");

    auto rowOf = [palette](const QString &label) {
        for (int i = 0; i < palette->count(); ++i) {
            QListWidgetItem *it = palette->item(i);
            if (it->data(Qt::UserRole).isValid() && it->text() == label) return i;
        }
        return -1;
    };
    const QStringList names = {QStringLiteral("设置变量"), QStringLiteral("运行控制"),
                               QStringLiteral("OK/NG"), QStringLiteral("条件信号灯"),
                               QStringLiteral("多图像"), QStringLiteral("运行状态"),
                               QStringLiteral("界面日志"), QStringLiteral("文本框组合"),
                               QStringLiteral("子界面"),
                               QStringLiteral("参数设置"), QStringLiteral("线图阵列")};
    for (const QString &name : names) {
        const int row = rowOf(name);
        QVERIFY2(row >= 0, qPrintable(QStringLiteral("控件库缺条目：%1").arg(name)));
        palette->setCurrentRow(row);
        const int before = dlg.resultInterface().totalControlCount();
        QVERIFY2(QMetaObject::invokeMethod(&dlg, "addCurrentPaletteControl",
                                           Qt::DirectConnection),
                 qPrintable(QStringLiteral("添加控件调用失败：%1").arg(name)));
        QCOMPARE(dlg.resultInterface().totalControlCount(), before + 1);
    }
}

void UiGuardsTest::runtimeSetVariableWritesTypedGlobal()
{
    auto *gv = GlobalVariableManager::instance();
    const QString name = QStringLiteral("UT_B3_SET_INT");
    gv->removeVariable(name);
    QVERIFY(gv->addVariable(name, GlobalVariableManager::IntType, 0));

    RuntimeInterface layout;
    auto *ctrl = layout.currentPage()->addControl(
        RuntimeControlType::SetVariable, QRect(20, 20, 300, 96));
    ctrl->bindType = QStringLiteral("global");
    ctrl->bindKey = name;

    RuntimeInterfaceView view;
    view.setInterface(layout);
    auto *edit = view.findChild<QLineEdit *>(QStringLiteral("RuntimeSetterEdit"));
    auto *apply = view.findChild<QPushButton *>(QStringLiteral("RuntimeSetterApply"));
    auto *current = view.findChild<QLabel *>(QStringLiteral("RuntimeSetterCurrent"));
    QVERIFY2(edit && apply && current, "设置变量控件缺输入/按钮/回显");

    edit->setText(QStringLiteral("42"));
    apply->click();
    QCOMPARE(gv->getVariable(name).toInt(), 42);
    QVERIFY2(current->text().contains(QStringLiteral("42")), "写入后当前值未回显");

    // 非法整数：不得写入，且给出可读提示
    edit->setText(QStringLiteral("abc"));
    apply->click();
    QCOMPARE(gv->getVariable(name).toInt(), 42);
    QVERIFY2(current->text().contains(QStringLiteral("输入无效")), "非法输入未提示");

    gv->removeVariable(name);
}

void UiGuardsTest::runtimeRunControlEmitsActions()
{
    RuntimeInterface layout;
    layout.currentPage()->addControl(RuntimeControlType::RunControl,
                                     QRect(20, 20, 320, 56));
    RuntimeInterfaceView view;
    view.setInterface(layout);

    QSignalSpy spy(&view, &RuntimeInterfaceView::actionTriggered);
    QVERIFY(spy.isValid());
    auto *start = view.findChild<QPushButton *>(QStringLiteral("RuntimeRunStart"));
    auto *stop = view.findChild<QPushButton *>(QStringLiteral("RuntimeRunStop"));
    auto *single = view.findChild<QPushButton *>(QStringLiteral("RuntimeRunSingle"));
    QVERIFY2(start && stop && single, "运行控制缺开始/停止/单次按钮");
    start->click();
    stop->click();
    single->click();
    QCOMPARE(spy.count(), 3);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("start"));
    QCOMPARE(spy.at(1).at(0).toString(), QStringLiteral("stop"));
    QCOMPARE(spy.at(2).at(0).toString(), QStringLiteral("single"));
}

void UiGuardsTest::runtimeOkNgAndConditionalLightUpdate()
{
    auto *gv = GlobalVariableManager::instance();
    const QString okName = QStringLiteral("UT_B3_OK");
    const QString ngName = QStringLiteral("UT_B3_NG");
    const QString scoreName = QStringLiteral("UT_B3_SCORE");
    gv->removeVariable(okName);
    gv->removeVariable(ngName);
    gv->removeVariable(scoreName);
    QVERIFY(gv->addVariable(okName, GlobalVariableManager::IntType, 3));
    QVERIFY(gv->addVariable(ngName, GlobalVariableManager::IntType, 2));
    QVERIFY(gv->addVariable(scoreName, GlobalVariableManager::FloatType, 5.0));

    RuntimeInterface layout;
    auto *okng = layout.currentPage()->addControl(RuntimeControlType::OkNgCount,
                                                  QRect(20, 20, 320, 110));
    okng->bindType = QStringLiteral("global");
    okng->bindKey = okName;
    okng->bindKey2 = ngName;
    auto *cond = layout.currentPage()->addControl(RuntimeControlType::ConditionalLight,
                                                  QRect(20, 150, 260, 80));
    cond->bindType = QStringLiteral("global");
    cond->bindKey = scoreName;
    LightCondition pass;
    pass.op = 0;
    pass.threshold = 10.0;
    pass.color = QStringLiteral("#3db97a");
    pass.text = QStringLiteral("PASS");
    LightCondition fail;
    fail.op = 3;
    fail.threshold = 10.0;
    fail.color = QStringLiteral("#e74c4c");
    fail.text = QStringLiteral("FAIL");
    cond->lightConditions << pass << fail;

    RuntimeInterfaceView view;
    view.setInterface(layout);
    auto *okLabel = view.findChild<QLabel *>(QStringLiteral("RuntimeOkValue"));
    auto *ngLabel = view.findChild<QLabel *>(QStringLiteral("RuntimeNgValue"));
    auto *condLight = view.findChild<QLabel *>(QStringLiteral("RuntimeCondLight"));
    auto *condText = view.findChild<QLabel *>(QStringLiteral("RuntimeCondText"));
    QVERIFY2(okLabel && ngLabel && condLight && condText, "OK/NG 或条件信号灯控件缺失");

    // 初值播种：OK=3、NG=2、score=5 → FAIL/红
    QVERIFY2(okLabel->text().contains(QStringLiteral("3")), "OK 初值未播种");
    QVERIFY2(ngLabel->text().contains(QStringLiteral("2")), "NG 初值未播种");
    QCOMPARE(condText->text(), QStringLiteral("FAIL"));
    QVERIFY2(condLight->styleSheet().contains(QStringLiteral("#e74c4c")), "低值未取红");

    view.updateVariable(scoreName, 12.5);
    QCOMPARE(condText->text(), QStringLiteral("PASS"));
    QVERIFY2(condLight->styleSheet().contains(QStringLiteral("#3db97a")), "高值未取绿");

    view.updateVariable(okName, 7);
    QVERIFY2(okLabel->text().contains(QStringLiteral("7")), "OK 更新未生效");

    gv->removeVariable(okName);
    gv->removeVariable(ngName);
    gv->removeVariable(scoreName);
}

void UiGuardsTest::runtimeMultiImageRunStatusAndUiLog()
{
    // B3-3：多图像（每画面一个窗口）、运行状态（空闲文本 + 轮询入口）、界面日志（订阅日志总线）
    RuntimeInterface layout;
    auto *multi = layout.currentPage()->addControl(RuntimeControlType::MultiImage,
                                                   QRect(20, 20, 440, 300));
    multi->multiImageLayout = 0;
    MultiImagePanel p1;
    p1.name = QStringLiteral("画面甲");
    p1.bindKey = QStringLiteral("模块1.图像源");
    MultiImagePanel p2;
    p2.name = QStringLiteral("画面乙");
    multi->imagePanels << p1 << p2;
    auto *status = layout.currentPage()->addControl(RuntimeControlType::RunStatus,
                                                    QRect(20, 340, 240, 80));
    status->statusSource = QStringLiteral("all");
    auto *uilog = layout.currentPage()->addControl(RuntimeControlType::UiLog,
                                                   QRect(20, 440, 440, 220));
    uilog->logShowTitle = true;
    uilog->logMaxLines = 50;

    RuntimeInterfaceView view;
    view.setInterface(layout);
    auto *m0 = view.findChild<HalconWindow *>(QStringLiteral("RuntimeMultiImage_0"));
    auto *m1 = view.findChild<HalconWindow *>(QStringLiteral("RuntimeMultiImage_1"));
    QVERIFY2(m0 && m1, "多图像画面未按数量创建");
    auto *statusLabel = view.findChild<QLabel *>(QStringLiteral("RuntimeRunStatusLabel"));
    QVERIFY2(statusLabel, "运行状态控件缺失");
    QCOMPARE(statusLabel->text(), QStringLiteral("停止运行"));
    // 轮询入口确实登记了控件：人为改字后触发一次刷新必须被刷回
    statusLabel->setText(QStringLiteral("STALE"));
    QVERIFY(QMetaObject::invokeMethod(&view, "updateRunStatusLabels", Qt::DirectConnection));
    QCOMPARE(statusLabel->text(), QStringLiteral("停止运行"));

    auto *logTable = view.findChild<QTableWidget *>(QStringLiteral("RuntimeUiLogTable"));
    QVERIFY2(logTable, "界面日志控件缺失");
    const int before = logTable->rowCount();
    UiLogBus::instance()->publish(QStringLiteral("UT_B3_UI_LOG_LINE"));
    QCOMPARE(logTable->rowCount(), before + 1);
    QVERIFY2(logTable->item(logTable->rowCount() - 1, 0)->text().contains(
                 QStringLiteral("UT_B3_UI_LOG_LINE")), "日志未实时追加");

    // 绑定体检：多图像画面的图像源也要参与失效判定
    const QStringList stale = layout.staleBindings({}, {QStringLiteral("别的模块")});
    QVERIFY2(stale.contains(QStringLiteral("node:模块1.图像源")),
             "多图像画面绑定未参与体检");

    // 边界：空画面表 / 未命名画面都要退回占位名，不能越界读
    const QList<MultiImagePanel> emptyPanels;
    QCOMPARE(multiImagePanelName(emptyPanels, 0), QStringLiteral("画面1"));
    QCOMPARE(multiImagePanelName(emptyPanels, 3), QStringLiteral("画面4"));
    QCOMPARE(multiImagePanelName(emptyPanels, -1), QStringLiteral("画面1"));
    MultiImagePanel unnamed;
    QCOMPARE(multiImagePanelName(QList<MultiImagePanel>{unnamed}, 0), QStringLiteral("画面1"));
    RuntimeInterface emptyMulti;
    emptyMulti.currentPage()->addControl(RuntimeControlType::MultiImage, QRect(0, 0, 240, 160));
    RuntimeInterfaceView emptyView;
    emptyView.setInterface(emptyMulti);
    QVERIFY2(emptyView.findChild<HalconWindow *>(QStringLiteral("RuntimeMultiImage_0")),
             "空画面表未退化出占位画面");
}

void UiGuardsTest::runtimeTextEntryComboFormatsAndColors()
{
    // 官方格式化语法：string=%s；int=%0d、%5d 按位补零；float=%1.Nf 小数位
    TextEntryItem s;
    s.text = QStringLiteral("状态：");
    QCOMPARE(formatTextEntry(s, QVariant(QStringLiteral("OK")), true),
             QStringLiteral("状态：OK"));
    s.text.clear();
    s.format = QStringLiteral("值=%s");
    QCOMPARE(formatTextEntry(s, QStringLiteral("OK"), true), QStringLiteral("值=OK"));
    TextEntryItem i;
    i.dataType = 1;
    QCOMPARE(formatTextEntry(i, 42, true), QStringLiteral("42"));
    i.format = QStringLiteral("%5d");
    QCOMPARE(formatTextEntry(i, 600, true), QStringLiteral("00600"));
    TextEntryItem f;
    f.dataType = 2;
    QCOMPARE(formatTextEntry(f, 3.14159, true), QStringLiteral("3.142"));
    f.format = QStringLiteral("%1.1f");
    QCOMPARE(formatTextEntry(f, 3.16, true), QStringLiteral("3.2"));
    // 未绑定：永远显示静态文本
    QCOMPARE(formatTextEntry(f, 1.0, false), QString());

    // 颜色条件：按顺序首个命中；string 只认 == / !=
    TextEntryItem c;
    c.dataType = 0;
    c.color = QStringLiteral("#112233");
    LightCondition ng;
    ng.op = 4;
    ng.text = QStringLiteral("NG");
    ng.color = QStringLiteral("#ff0000");
    LightCondition ok;
    ok.op = 4;
    ok.text = QStringLiteral("OK");
    ok.color = QStringLiteral("#00ff00");
    c.colorConditions << ng << ok;
    QCOMPARE(textEntryDisplayColor(c, QStringLiteral("NG"), true), QStringLiteral("#ff0000"));
    QCOMPARE(textEntryDisplayColor(c, QStringLiteral("OK"), true), QStringLiteral("#00ff00"));
    QCOMPARE(textEntryDisplayColor(c, QStringLiteral("??"), true), QStringLiteral("#112233"));

    // 运行视图：整数行按 %5d 补零 + 阈值变红；节点行按字符串条件变红
    auto *gv = GlobalVariableManager::instance();
    const QString varName = QStringLiteral("UT_B3_TE_VALUE");
    gv->removeVariable(varName);
    QVERIFY(gv->addVariable(varName, GlobalVariableManager::IntType, 0));

    RuntimeInterface layout;
    auto *te = layout.currentPage()->addControl(RuntimeControlType::TextEntryCombo,
                                                QRect(10, 10, 360, 160));
    te->textEntries.clear();
    TextEntryItem e0;
    e0.text = QStringLiteral("计数：");
    e0.bindType = QStringLiteral("global");
    e0.bindKey = varName;
    e0.dataType = 1;
    e0.format = QStringLiteral("%5d");
    LightCondition big;
    big.op = 0;
    big.threshold = 10.0;
    big.color = QStringLiteral("#ff0000");
    e0.colorConditions << big;
    TextEntryItem e1;
    e1.text = QStringLiteral("状态：");
    e1.bindType = QStringLiteral("node");
    e1.bindKey = QStringLiteral("模块1.状态");
    e1.dataType = 0;
    LightCondition bad;
    bad.op = 4;
    bad.text = QStringLiteral("NG");
    bad.color = QStringLiteral("#ff0000");
    e1.colorConditions << bad;
    te->textEntries << e0 << e1;

    RuntimeInterfaceView view;
    view.setInterface(layout);
    auto *r0 = view.findChild<QLabel *>(QStringLiteral("RuntimeTextEntry_0"));
    auto *r1 = view.findChild<QLabel *>(QStringLiteral("RuntimeTextEntry_1"));
    QVERIFY2(r0 && r1, "文本框组合行标签未按数量创建");
    // 初值播种：变量创建时是 0，所以建面即按 %5d 显示 00000
    QCOMPARE(r0->text(), QStringLiteral("计数：00000"));
    view.updateVariable(varName, 42);
    QCOMPARE(r0->text(), QStringLiteral("计数：00042"));
    QVERIFY2(r0->styleSheet().contains(QStringLiteral("#ff0000")), "数值条件未生效");
    view.updateVariable(varName, 7);
    QCOMPARE(r0->text(), QStringLiteral("计数：00007"));
    QVERIFY2(!r0->styleSheet().contains(QStringLiteral("#ff0000")), "数值条件未撤销");

    view.updateNodeOutput(QStringLiteral("模块1.状态"), QStringLiteral("OK"));
    QCOMPARE(r1->text(), QStringLiteral("状态：OK"));
    view.updateNodeOutput(QStringLiteral("模块1.状态"), QStringLiteral("NG"));
    QCOMPARE(r1->text(), QStringLiteral("状态：NG"));
    QVERIFY2(r1->styleSheet().contains(QStringLiteral("#ff0000")), "字符串条件未生效");

    // 失效绑定体检覆盖每一行
    const QStringList stale = layout.staleBindings({}, {QStringLiteral("别的模块")});
    QVERIFY2(stale.contains(QStringLiteral("global:") + varName),
             "文本框组合全局绑定未参与体检");
    QVERIFY2(stale.contains(QStringLiteral("node:模块1.状态")),
             "文本框组合节点绑定未参与体检");

    gv->removeVariable(varName);
}

void UiGuardsTest::subInterfacePagesRoundTripAndDepth()
{
    // 模型：子界面页存 subPages，控件只持 id；深度 BFS 从根页算，带 visited 防环
    RuntimeInterface iface;
    auto *root = iface.currentPage();
    const QString subA = iface.createSubPage(QStringLiteral("明细页"));
    QVERIFY(!subA.isEmpty());
    auto *rootCtrl = root->addControl(RuntimeControlType::SubInterface, QRect(10, 10, 200, 80));
    rootCtrl->subPageId = subA;
    rootCtrl->subPassword = QStringLiteral("1234");
    rootCtrl->subIconVisible = false;
    QCOMPARE(iface.subPageDepth(subA), 1);
    QVERIFY(iface.subPage(subA));

    auto *inner = iface.subPage(subA);
    auto *innerText = inner->addControl(RuntimeControlType::ValueDisplay, QRect(0, 0, 100, 40));
    innerText->bindType = QStringLiteral("global");
    innerText->bindKey = QStringLiteral("UT_SUB_MISS");
    const QString subB = iface.createSubPage(QStringLiteral("二级"));
    auto *c2 = inner->addControl(RuntimeControlType::SubInterface, QRect(0, 50, 200, 80));
    c2->subPageId = subB;
    QCOMPARE(iface.subPageDepth(subB), 2);
    const QString subC = iface.createSubPage(QStringLiteral("三级"));
    auto *c3 = iface.subPage(subB)->addControl(RuntimeControlType::SubInterface,
                                              QRect(0, 50, 200, 80));
    c3->subPageId = subC;
    QCOMPARE(iface.subPageDepth(subC), 3);
    QCOMPARE(iface.subPageDepth(QStringLiteral("nope")), -1);

    // 共享引用：还有其它控件引用时删页必须被拒
    auto *dup = root->addControl(RuntimeControlType::SubInterface, QRect(10, 100, 200, 80));
    dup->subPageId = subA;
    QVERIFY(iface.subPageReferenced(subA));
    root->removeControl(1);   // 删共享引用之一
    QVERIFY2(!iface.removeSubPageIfUnused(subA), "仍被引用时子页不得删除");
    root->removeControl(0);   // 最后一个引用
    QVERIFY(iface.removeSubPageIfUnused(subA));
    QVERIFY(!iface.subPage(subA));
    QVERIFY2(!iface.subPage(subB), "子页删除后其下级子页未被级联清理");
    QVERIFY(!iface.subPage(subC));

    // JSON：整包往返保留子页内容与控件字段
    RuntimeInterface j;
    const QString jid = j.createSubPage(QStringLiteral("子页"));
    auto *jc = j.currentPage()->addControl(RuntimeControlType::SubInterface,
                                           QRect(0, 0, 200, 80));
    jc->subPageId = jid;
    jc->subPassword = QStringLiteral("pw");
    jc->subIconVisible = false;
    auto *jv = j.subPage(jid)->addControl(RuntimeControlType::ValueDisplay,
                                          QRect(0, 0, 100, 40));
    jv->bindType = QStringLiteral("global");
    jv->bindKey = QStringLiteral("UT_SUB_MISS");
    RuntimeInterface j2;
    QVERIFY(j2.fromJson(j.toJson()));
    QVERIFY(j2.subPage(jid));
    QCOMPARE(j2.subPage(jid)->controls.size(), 1);
    QCOMPARE(j2.subPageDepth(jid), 1);
    QCOMPARE(j2.currentPage()->controls.first().subPageId, jid);
    // 密码改为摘要存储（2026-11-11）：不再比对明文，而是比对「摘要可校验」
    QVERIFY2(isHashedSubPassword(j2.currentPage()->controls.first().subPassword),
             "子界面密码必须存成摘要，不能落明文");
    QVERIFY2(verifySubPassword(QStringLiteral("pw"),
                               j2.currentPage()->controls.first().subPassword),
             "摘要必须能被原密码校验通过");
    QCOMPARE(j2.currentPage()->controls.first().subIconVisible, false);
    QVERIFY2(j2.staleBindings({}, {}).contains(QStringLiteral("global:UT_SUB_MISS")),
             "子界面内的绑定未参与体检");

    // 运行视图：点击子界面按钮弹出新窗口，层级 +1；无密码时不再拦
    j2.currentPage()->controls.first().subPassword.clear();
    RuntimeInterfaceView view;
    view.setInterface(j2);
    auto *btn = view.findChild<QPushButton *>(QStringLiteral("RuntimeSubInterfaceButton"));
    QVERIFY2(btn, "子界面按钮缺失");
    btn->click();
    RuntimeInterfaceView *subView = nullptr;
    for (QWidget *w : QApplication::topLevelWidgets()) {
        auto *v = w->findChild<RuntimeInterfaceView *>(QStringLiteral("RuntimeInterfaceView"));
        if (v && v != &view) {
            subView = v;
            break;
        }
    }
    QVERIFY2(subView, "点击子界面未弹出子窗口");
    QCOMPARE(subView->subLevel(), 1);
    subView->window()->close();
    QCoreApplication::processEvents();

    // 广播注册表：主视图与子窗口共享同一数据流（MainWindow 只调广播入口）
    RuntimeInterface bcast;
    auto *bctrl = bcast.currentPage()->addControl(RuntimeControlType::ValueDisplay,
                                                  QRect(0, 0, 120, 40));
    bctrl->bindType = QStringLiteral("global");
    bctrl->bindKey = QStringLiteral("UT_BCAST");
    RuntimeInterfaceView bv1;
    RuntimeInterfaceView bv2;
    bv1.setInterface(bcast);
    bv2.setInterface(bcast);
    RuntimeInterfaceView::broadcastVariable(QStringLiteral("UT_BCAST"), 5);
    auto *bl1 = bv1.findChild<QLabel *>(QStringLiteral("RuntimeValueLabel"));
    auto *bl2 = bv2.findChild<QLabel *>(QStringLiteral("RuntimeValueLabel"));
    QVERIFY2(bl1 && bl2, "数值控件缺失");
    QCOMPARE(bl1->text(), QStringLiteral("5"));
    QCOMPARE(bl2->text(), QStringLiteral("5"));

    // ── B3 运行期牙 ①：subLevel=3 时再点击子界面按钮必须弹 warning 并拒开第 4 层
    // （覆盖 RuntimeInterfaceView.cpp:1356 的 m_subLevel >= 3 分支；原先只测到 level=1 弹出）
    // 子页深度从根页（currentPage）算：D1=1, D2=2, D3=3, D4=4；validateStructure 会在
    // 保存时拒绝 depth>3，但**运行期点击**走的是 RuntimeInterfaceView 的 m_subLevel>=3
    // 闸——必须有一个挂在 depth=3 页上的按钮指向 D4，才有得点。
    {
        RuntimeInterface depthIf;
        const QString d1 = depthIf.createSubPage(QStringLiteral("D1"));
        auto *c1 = depthIf.currentPage()->addControl(RuntimeControlType::SubInterface,
                                                     QRect(0, 0, 200, 80));
        c1->subPageId = d1;
        const QString d2 = depthIf.createSubPage(QStringLiteral("D2"));
        depthIf.subPage(d1)->addControl(RuntimeControlType::SubInterface, QRect(0, 0, 200, 80))
            ->subPageId = d2;
        const QString d3 = depthIf.createSubPage(QStringLiteral("D3"));
        depthIf.subPage(d2)->addControl(RuntimeControlType::SubInterface, QRect(0, 0, 200, 80))
            ->subPageId = d3;
        // D4 挂在 D3 下：按钮位于 subLevel=3 的视图里，目标 depth=4（超过 validateStructure
        // 上界，但 m_subLevel>=3 闸必须在 validateStructure 之前拦住 → 不依赖 validateStructure）。
        const QString d4 = depthIf.createSubPage(QStringLiteral("D4"));
        depthIf.subPage(d3)->addControl(RuntimeControlType::SubInterface, QRect(0, 0, 200, 80))
            ->subPageId = d4;

        RuntimeInterfaceView dv;
        dv.setInterface(depthIf);
        auto *b1 = dv.findChild<QPushButton *>(QStringLiteral("RuntimeSubInterfaceButton"));
        QVERIFY(b1);
        b1->click();
        QCoreApplication::processEvents();
        RuntimeInterfaceView *lv1 = nullptr;
        for (QWidget *w : QApplication::topLevelWidgets()) {
            lv1 = w->findChild<RuntimeInterfaceView *>(QStringLiteral("RuntimeInterfaceView"));
            if (lv1 && lv1 != &dv) break;
        }
        QVERIFY2(lv1 && lv1->subLevel() == 1, "第 1 层未弹出");
        auto *b2 = lv1->findChild<QPushButton *>(QStringLiteral("RuntimeSubInterfaceButton"));
        QVERIFY(b2);
        b2->click();
        QCoreApplication::processEvents();
        RuntimeInterfaceView *lv2 = nullptr;
        for (QWidget *w : QApplication::topLevelWidgets()) {
            lv2 = w->findChild<RuntimeInterfaceView *>(QStringLiteral("RuntimeInterfaceView"));
            if (lv2 && lv2 != &dv && lv2 != lv1) break;
        }
        QVERIFY2(lv2 && lv2->subLevel() == 2, "第 2 层未弹出");
        auto *b3 = lv2->findChild<QPushButton *>(QStringLiteral("RuntimeSubInterfaceButton"));
        QVERIFY(b3);
        b3->click();
        QCoreApplication::processEvents();
        RuntimeInterfaceView *lv3 = nullptr;
        for (QWidget *w : QApplication::topLevelWidgets()) {
            lv3 = w->findChild<RuntimeInterfaceView *>(QStringLiteral("RuntimeInterfaceView"));
            if (lv3 && lv3 != &dv && lv3 != lv1 && lv3 != lv2) break;
        }
        QVERIFY2(lv3 && lv3->subLevel() == 3, "第 3 层未弹出");

        // 第 4 次点击：必须被 m_subLevel>=3 分支拦截；任何时刻都不得存在 subLevel==4 的视图
        auto *b4 = lv3->findChild<QPushButton *>(QStringLiteral("RuntimeSubInterfaceButton"));
        QVERIFY2(b4, "第 3 层里必须有指向第 4 层的按钮");
        // 异步关掉即将弹出的 QMessageBox：QMessageBox::warning 是 modal，会让 click() 阻塞；
        // 用 QTimer 在主事件循环里把它拒掉，按钮调用栈才能 return。
        QTimer::singleShot(100, lv3, [lv3]() {
            for (QWidget *w : QApplication::topLevelWidgets()) {
                auto *mb = qobject_cast<QMessageBox *>(w);
                if (mb && mb->isVisible()) {
                    mb->defaultButton()->click();
                    return;
                }
            }
        });
        b4->click();
        QCoreApplication::processEvents();
        bool foundLv4 = false;
        for (QWidget *w : QApplication::topLevelWidgets()) {
            auto *v = w->findChild<RuntimeInterfaceView *>(QStringLiteral("RuntimeInterfaceView"));
            if (v && v->subLevel() == 4) { foundLv4 = true; break; }
        }
        QVERIFY2(!foundLv4, "第 4 层子界面被弹出：m_subLevel>=3 闸失效");

        // 收口（DeleteOnClose + 级联）
        lv1->window()->close();
        QCoreApplication::processEvents();
    }

    // ── B3 运行期牙 ②：密码保护子界面必须弹 QInputDialog 并走 verifySubPassword
    // （覆盖 RuntimeInterfaceView.cpp:1367-1373；原先密码路径没被测试覆盖）
    {
        RuntimeInterface pwIf;
        const QString ps = pwIf.createSubPage(QStringLiteral("Secret"));
        auto *pc = pwIf.currentPage()->addControl(RuntimeControlType::SubInterface,
                                                  QRect(0, 0, 200, 80));
        pc->subPageId = ps;
        pc->subPassword = hashSubPassword(QStringLiteral("secret7"));   // 摘要存储
        // 把布局喂给视图
        RuntimeInterface pwv2;
        QVERIFY(pwv2.fromJson(pwIf.toJson()));
        RuntimeInterfaceView pv;
        pv.setInterface(pwv2);
        auto *pbtn = pv.findChild<QPushButton *>(QStringLiteral("RuntimeSubInterfaceButton"));
        QVERIFY2(pbtn, "密码子界面按钮缺失");
        QVERIFY2(isHashedSubPassword(pwv2.currentPage()->controls.first().subPassword),
                 "载入后子界面密码必须是摘要");

        // ── 错密码：弹输入框 → 输错 → 不得弹出子视图
        std::atomic<bool> wrongDone{false};
        QThread *wrongWorker = QThread::create([&wrongDone]() {
            QTest::qWait(200);
            for (QWidget *w : QApplication::topLevelWidgets()) {
                auto *dlg = qobject_cast<QInputDialog *>(w);
                if (dlg && dlg->isVisible()) {
                    dlg->setTextValue(QStringLiteral("wrong"));
                    dlg->findChild<QPushButton *>("QDialogButtonBox") ? void() : void();
                    // 通过 accept 按钮点击：先找 QDialogButtonBox 再点它的 OK
                    auto *box = dlg->findChild<QDialogButtonBox *>();
                    if (box) {
                        for (QAbstractButton *b : box->buttons()) {
                            if (box->buttonRole(b) == QDialogButtonBox::AcceptRole) {
                                b->click();
                                break;
                            }
                        }
                    }
                    wrongDone.store(true);
                    return;
                }
            }
            wrongDone.store(true);
        });
        wrongWorker->start();
        pbtn->click();
        wrongWorker->wait();
        delete wrongWorker;
        QCoreApplication::processEvents();
        QVERIFY(wrongDone.load());
        bool foundPwSub = false;
        for (QWidget *w : QApplication::topLevelWidgets()) {
            auto *v = w->findChild<RuntimeInterfaceView *>(QStringLiteral("RuntimeInterfaceView"));
            if (v && v != &pv) { foundPwSub = true; break; }
        }
        QVERIFY2(!foundPwSub, "错密码后子视图仍被打开：verifySubPassword 闸失效");

        // ── 对密码：弹输入框 → 输对 → 子视图必须弹出
        std::atomic<bool> rightDone{false};
        QThread *rightWorker = QThread::create([&rightDone]() {
            QTest::qWait(200);
            for (QWidget *w : QApplication::topLevelWidgets()) {
                auto *dlg = qobject_cast<QInputDialog *>(w);
                if (dlg && dlg->isVisible()) {
                    dlg->setTextValue(QStringLiteral("secret7"));
                    auto *box = dlg->findChild<QDialogButtonBox *>();
                    if (box) {
                        for (QAbstractButton *b : box->buttons()) {
                            if (box->buttonRole(b) == QDialogButtonBox::AcceptRole) {
                                b->click();
                                break;
                            }
                        }
                    }
                    rightDone.store(true);
                    return;
                }
            }
            rightDone.store(true);
        });
        rightWorker->start();
        pbtn->click();
        rightWorker->wait();
        delete rightWorker;
        QCoreApplication::processEvents();
        QVERIFY(rightDone.load());
        RuntimeInterfaceView *pwSub = nullptr;
        for (QWidget *w : QApplication::topLevelWidgets()) {
            auto *v = w->findChild<RuntimeInterfaceView *>(QStringLiteral("RuntimeInterfaceView"));
            if (v && v != &pv) { pwSub = v; break; }
        }
        QVERIFY2(pwSub, "对密码后子视图未弹出：密码验证路径失效");
        QCOMPARE(pwSub->subLevel(), 1);
        pwSub->window()->close();
        QCoreApplication::processEvents();
    }
}


void UiGuardsTest::b3NewControlsRoundTrip()
{
    // 类型名映射：枚举每一项都必须能按名字往返（新增类型漏映射会在这里判红）
    for (int i = 0; i <= int(RuntimeControlType::SubInterface); ++i) {
        const auto type = RuntimeControlType(i);
        RuntimeControlType back = RuntimeControlType::TextLabel;
        QVERIFY2(runtimeControlTypeFromName(runtimeControlTypeName(type), back),
                 qPrintable(QStringLiteral("类型名无法回读：%1").arg(i)));
        QCOMPARE(int(back), i);
    }

    RuntimeInterfacePage page;
    auto *setter = page.addControl(RuntimeControlType::SetVariable, QRect(0, 0, 300, 96));
    setter->bindType = QStringLiteral("global");
    setter->bindKey = QStringLiteral("v_target");
    auto *run = page.addControl(RuntimeControlType::RunControl, QRect(0, 100, 320, 56));
    run->title = QStringLiteral("产线控制");
    auto *okng = page.addControl(RuntimeControlType::OkNgCount, QRect(0, 160, 320, 110));
    okng->bindKey = QStringLiteral("cnt_ok");
    okng->bindKey2 = QStringLiteral("cnt_ng");
    auto *cond = page.addControl(RuntimeControlType::ConditionalLight, QRect(0, 280, 260, 80));
    cond->bindType = QStringLiteral("node");
    cond->bindKey = QStringLiteral("模块1.检测");
    LightCondition c1;
    c1.op = 1;
    c1.threshold = 10.0;
    c1.color = QStringLiteral("#112233");
    c1.text = QStringLiteral("PASS");
    LightCondition c2;
    c2.op = 2;
    c2.threshold = 0.0;
    c2.color = QStringLiteral("#445566");
    c2.text = QStringLiteral("LOW");
    cond->lightConditions << c1 << c2;

    // B3-3：多图像 / 运行状态 / 界面日志（官方「控件配置参考」）
    auto *multi = page.addControl(RuntimeControlType::MultiImage, QRect(0, 380, 440, 300));
    multi->multiImageLayout = 2;
    MultiImagePanel mp1;
    mp1.name = QStringLiteral("相机A");
    mp1.bindKey = QStringLiteral("模块1.图像源");
    MultiImagePanel mp2;
    mp2.name = QStringLiteral("相机B");
    multi->imagePanels << mp1 << mp2;
    auto *status = page.addControl(RuntimeControlType::RunStatus, QRect(0, 700, 240, 80));
    status->statusSource = QStringLiteral("flow");
    status->statusFlow = QStringLiteral("流程1");
    status->cornerRadius = 8;
    auto *uilog = page.addControl(RuntimeControlType::UiLog, QRect(0, 800, 440, 220));
    uilog->logShowTitle = false;
    uilog->logMaxLines = 200;
    uilog->logAltRowBgColor = QStringLiteral("#123456");

    // B3-4a：文本框组合（每行数据源/类型/格式/颜色条件）
    auto *te = page.addControl(RuntimeControlType::TextEntryCombo, QRect(0, 1040, 360, 160));
    te->textEntryOrientation = 1;
    te->textEntrySpacing = 12;
    te->showTextEntryTitle = false;
    te->textEntryBgColor = QStringLiteral("#101010");
    TextEntryItem teA;
    teA.text = QStringLiteral("计数：");
    teA.bindType = QStringLiteral("global");
    teA.bindKey = QStringLiteral("v_target");
    teA.dataType = 1;
    teA.format = QStringLiteral("%5d");
    teA.color = QStringLiteral("#abcdef");
    LightCondition teRule;
    teRule.op = 0;
    teRule.threshold = 10.0;
    teRule.color = QStringLiteral("#ff0000");
    teA.colorConditions << teRule;
    TextEntryItem teB;
    teB.text = QStringLiteral("状态：");
    teB.bindType = QStringLiteral("node");
    teB.bindKey = QStringLiteral("模块1.检测");
    teB.dataType = 0;
    te->textEntries << teA << teB;

    // B3-4b：子界面（控件只存 id/密码/显示图标；页表在 RuntimeInterface 级）
    auto *sub = page.addControl(RuntimeControlType::SubInterface, QRect(0, 1240, 200, 80));
    sub->subPageId = QStringLiteral("sub7");
    sub->subPassword = QStringLiteral("pw7");
    sub->subIconVisible = false;

    RuntimeInterfacePage page2;
    QVERIFY(page2.fromJson(page.toJson()));
    QCOMPARE(page2.controls.size(), 9);
    QCOMPARE(page2.controls.at(0).type, RuntimeControlType::SetVariable);
    QCOMPARE(page2.controls.at(0).bindKey, QStringLiteral("v_target"));
    QCOMPARE(page2.controls.at(1).type, RuntimeControlType::RunControl);
    QCOMPARE(page2.controls.at(1).title, QStringLiteral("产线控制"));
    QCOMPARE(page2.controls.at(2).type, RuntimeControlType::OkNgCount);
    QCOMPARE(page2.controls.at(2).bindKey2, QStringLiteral("cnt_ng"));
    QCOMPARE(page2.controls.at(3).type, RuntimeControlType::ConditionalLight);
    QCOMPARE(page2.controls.at(3).bindType, QStringLiteral("node"));
    QCOMPARE(page2.controls.at(3).lightConditions.size(), 2);
    QCOMPARE(page2.controls.at(3).lightConditions.at(0).op, 1);
    QCOMPARE(page2.controls.at(3).lightConditions.at(0).threshold, 10.0);
    QCOMPARE(page2.controls.at(3).lightConditions.at(0).color, QStringLiteral("#112233"));
    QCOMPARE(page2.controls.at(3).lightConditions.at(1).text, QStringLiteral("LOW"));
    QCOMPARE(page2.controls.at(4).type, RuntimeControlType::MultiImage);
    QCOMPARE(page2.controls.at(4).multiImageLayout, 2);
    QCOMPARE(page2.controls.at(4).imagePanels.size(), 2);
    QCOMPARE(page2.controls.at(4).imagePanels.at(0).name, QStringLiteral("相机A"));
    QCOMPARE(page2.controls.at(4).imagePanels.at(0).bindKey, QStringLiteral("模块1.图像源"));
    QCOMPARE(page2.controls.at(5).type, RuntimeControlType::RunStatus);
    QCOMPARE(page2.controls.at(5).statusSource, QStringLiteral("flow"));
    QCOMPARE(page2.controls.at(5).statusFlow, QStringLiteral("流程1"));
    QCOMPARE(page2.controls.at(5).cornerRadius, 8);
    QCOMPARE(page2.controls.at(6).type, RuntimeControlType::UiLog);
    QCOMPARE(page2.controls.at(6).logShowTitle, false);
    QCOMPARE(page2.controls.at(6).logMaxLines, 200);
    QCOMPARE(page2.controls.at(6).logAltRowBgColor, QStringLiteral("#123456"));
    QCOMPARE(page2.controls.at(7).type, RuntimeControlType::TextEntryCombo);
    QCOMPARE(page2.controls.at(7).textEntryOrientation, 1);
    QCOMPARE(page2.controls.at(7).textEntrySpacing, 12);
    QCOMPARE(page2.controls.at(7).showTextEntryTitle, false);
    QCOMPARE(page2.controls.at(7).textEntryBgColor, QStringLiteral("#101010"));
    QCOMPARE(page2.controls.at(7).textEntries.size(), 2);
    QCOMPARE(page2.controls.at(7).textEntries.at(0).bindKey, QStringLiteral("v_target"));
    QCOMPARE(page2.controls.at(7).textEntries.at(0).dataType, 1);
    QCOMPARE(page2.controls.at(7).textEntries.at(0).format, QStringLiteral("%5d"));
    QCOMPARE(page2.controls.at(7).textEntries.at(0).color, QStringLiteral("#abcdef"));
    QCOMPARE(page2.controls.at(7).textEntries.at(0).colorConditions.size(), 1);
    QCOMPARE(page2.controls.at(7).textEntries.at(0).colorConditions.at(0).threshold, 10.0);
    QCOMPARE(page2.controls.at(7).textEntries.at(1).bindType, QStringLiteral("node"));
    QCOMPARE(page2.controls.at(7).textEntries.at(1).bindKey, QStringLiteral("模块1.检测"));
    QCOMPARE(page2.controls.at(8).type, RuntimeControlType::SubInterface);
    QCOMPARE(page2.controls.at(8).subPageId, QStringLiteral("sub7"));
    // 同上：只断言摘要可校验，不断言明文
    QVERIFY2(isHashedSubPassword(page2.controls.at(8).subPassword),
             "子界面密码必须存成摘要");
    QVERIFY2(verifySubPassword(QStringLiteral("pw7"), page2.controls.at(8).subPassword),
             "摘要必须能被原密码校验通过");
    QCOMPARE(page2.controls.at(8).subIconVisible, false);
}

// B3-2 设计器属性面板：条件信号灯条件表可增删、OK/NG 第二绑定可写回，且两组按类型互斥显示。
void UiGuardsTest::designerB3PropertyPanelsWork()
{
    auto *gv = GlobalVariableManager::instance();
    const QString ngName = QStringLiteral("UT_B3_NG_BIND");
    gv->removeVariable(ngName);
    QVERIFY(gv->addVariable(ngName, GlobalVariableManager::IntType, 0));

    RuntimeInterfaceDesigner dlg(QStringList{QStringLiteral("模块1")});
    QListWidget *palette = nullptr;
    for (QListWidget *lw : dlg.findChildren<QListWidget *>()) {
        if (lw->dragEnabled() && lw->count() >= 7) {
            palette = lw;
            break;
        }
    }
    QVERIFY2(palette, "没找到控件库");
    auto addByName = [&dlg, palette](const QString &name) {
        for (int i = 0; i < palette->count(); ++i) {
            QListWidgetItem *it = palette->item(i);
            if (it->data(Qt::UserRole).isValid() && it->text() == name) {
                palette->setCurrentRow(i);
                return QMetaObject::invokeMethod(&dlg, "addCurrentPaletteControl",
                                                 Qt::DirectConnection);
            }
        }
        return false;
    };

    QVERIFY(addByName(QStringLiteral("条件信号灯")));
    auto *table = dlg.findChild<QTableWidget *>(QStringLiteral("designerLightCondTable"));
    auto *addBtn = dlg.findChild<QPushButton *>(QStringLiteral("designerLightCondAdd"));
    auto *delBtn = dlg.findChild<QPushButton *>(QStringLiteral("designerLightCondDel"));
    auto *group = dlg.findChild<QGroupBox *>(QStringLiteral("designerLightCondGroup"));
    QVERIFY2(table && addBtn && delBtn && group, "条件信号灯属性控件缺失");
    QCOMPARE(table->rowCount(), 2);   // 新建时预置 >=1 绿 / <1 红
    QVERIFY2(!group->isHidden(), "选中条件信号灯时条件表应显示");
    addBtn->click();
    QCOMPARE(dlg.resultInterface().pages.first().controls.first().lightConditions.size(), 3);
    table->selectRow(2);
    delBtn->click();
    QCOMPARE(dlg.resultInterface().pages.first().controls.first().lightConditions.size(), 2);

    // 切到 OK/NG：条件表应隐藏，NG 绑定行可用并能写回
    QVERIFY(addByName(QStringLiteral("OK/NG")));
    QVERIFY2(group->isHidden(), "选中 OK/NG 时条件表应隐藏");
    auto *ngCombo = dlg.findChild<QComboBox *>(QStringLiteral("designerBindKey2Combo"));
    QVERIFY2(ngCombo, "没找到 NG 绑定组合框");
    const int idx = ngCombo->findData(ngName);
    QVERIFY2(idx >= 0, "NG 绑定组合框未列出全局变量");
    ngCombo->setCurrentIndex(idx);
    const auto &page = dlg.resultInterface().pages.first();
    QCOMPARE(page.controls.size(), 2);
    QCOMPARE(page.controls.last().bindKey2, ngName);

    // 文本框组合：属性组按类型显示，行表可增行，绑定组合框可写回
    QVERIFY(addByName(QStringLiteral("文本框组合")));
    auto *teGroup = dlg.findChild<QGroupBox *>(QStringLiteral("designerTextEntryGroup"));
    auto *teTable = dlg.findChild<QTableWidget *>(QStringLiteral("designerTextEntryTable"));
    auto *teAdd = dlg.findChild<QPushButton *>(QStringLiteral("designerTextEntryAdd"));
    QVERIFY2(teGroup && teTable && teAdd, "文本框组合属性控件缺失");
    QVERIFY2(!teGroup->isHidden(), "选中文本框组合时属性组应显示");
    QCOMPARE(teTable->rowCount(), 2);
    teAdd->click();
    QCOMPARE(dlg.resultInterface().pages.first().controls.last().textEntries.size(), 3);
    auto *teBind = qobject_cast<QComboBox *>(teTable->cellWidget(0, 1));
    QVERIFY2(teBind, "文本框组合绑定组合框缺失");
    const int teVi = teBind->findData(QStringLiteral("global:") + ngName);
    QVERIFY2(teVi >= 0, "文本框组合绑定组合框未列出全局变量");
    teBind->setCurrentIndex(teVi);
    QCOMPARE(dlg.resultInterface().pages.first().controls.last().textEntries.at(0).bindKey,
             ngName);

    // 子界面：属性组显示、编辑态提示条与页管理禁用、返回恢复、删除后清理孤儿子页
    QVERIFY(addByName(QStringLiteral("子界面")));
    auto *subGroup = dlg.findChild<QGroupBox *>(QStringLiteral("designerSubInterfaceGroup"));
    auto *subEdit = dlg.findChild<QPushButton *>(QStringLiteral("designerSubEditBtn"));
    auto *subBanner = dlg.findChild<QWidget *>(QStringLiteral("designerSubEditBanner"));
    auto *subPageGroup = dlg.findChild<QGroupBox *>(QStringLiteral("designerPageGroup"));
    QVERIFY2(subGroup && subEdit && subBanner && subPageGroup, "子界面属性控件缺失");
    QVERIFY2(!subGroup->isHidden(), "选中子界面时属性组应显示");
    const QString subId = dlg.resultInterface().pages.first().controls.last().subPageId;
    QVERIFY(!subId.isEmpty());
    QVERIFY(dlg.resultInterface().subPages.contains(subId));
    auto *subBack = dlg.findChild<QPushButton *>(QStringLiteral("designerSubBackBtn"));
    QVERIFY2(subBack, "子界面返回按钮缺失");
    subEdit->click();
    QVERIFY2(!subBanner->isHidden(), "编辑子界面时未显示提示条");
    QVERIFY2(!subPageGroup->isEnabled(), "编辑子界面时页管理应禁用");
    subBack->click();
    QVERIFY2(subBanner->isHidden(), "返回主界面后提示条未隐藏");
    QVERIFY(subPageGroup->isEnabled());
    auto *canvas = dlg.findChild<RuntimeDesignerCanvas *>();
    QVERIFY(canvas);
    canvas->selectIndex(dlg.resultInterface().pages.first().controls.size() - 1);
    QVERIFY(QMetaObject::invokeMethod(&dlg, "deleteSelected", Qt::DirectConnection));
    QVERIFY2(!dlg.resultInterface().subPages.contains(subId), "删除子界面后孤儿子页未清理");

    gv->removeVariable(ngName);
}

// 2026-11-10 审核：设计器可编辑子界面圆角、运行视图会用它，但 toJson/fromJson
// 只在 RunStatus 分支读写 cornerRadius ⇒ 子界面圆角保存后变回 0。
void UiGuardsTest::subInterfaceCornerRadiusRoundTrips()
{
    RuntimeInterfacePage page;
    RuntimeControl *sub = page.addControl(RuntimeControlType::SubInterface,
                                          QRect(0, 0, 200, 100));
    QVERIFY(sub);
    sub->cornerRadius = 12;

    RuntimeInterfacePage back;
    QVERIFY(back.fromJson(page.toJson()));
    QVERIFY2(back.controls.first().cornerRadius == 12,
             qPrintable(QStringLiteral("子界面圆角保存后丢失，实得 %1")
                            .arg(back.controls.first().cornerRadius)));

    // 运行状态仍保持原有钳制口径
    RuntimeInterfacePage page2;
    RuntimeControl *status = page2.addControl(RuntimeControlType::RunStatus,
                                              QRect(0, 0, 200, 60));
    QVERIFY(status);
    QJsonObject j = page2.toJson();
    QJsonArray arr = j[QStringLiteral("controls")].toArray();
    QJsonObject c = arr.at(0).toObject();
    c[QStringLiteral("cornerRadius")] = 999;
    arr.replace(0, c);
    j[QStringLiteral("controls")] = arr;
    RuntimeInterfacePage clamped;
    QVERIFY(clamped.fromJson(j));
    QCOMPARE(clamped.controls.first().cornerRadius, 40);
}

// 2026-11-10 审核：对齐/分布原本不压撤销快照，一键排完就没法回退；
// 且少于 2 个选中时不该白压一枚空快照。
void UiGuardsTest::designerAlignCanBeUndone()
{
    RuntimeInterfaceDesigner dlg(QStringList{QStringLiteral("模块1")});
    auto *canvas = dlg.findChild<RuntimeDesignerCanvas *>();
    QVERIFY(canvas);

    QListWidget *palette = nullptr;
    for (QListWidget *lw : dlg.findChildren<QListWidget *>()) {
        if (lw->dragEnabled() && lw->count() >= 7) { palette = lw; break; }
    }
    QVERIFY(palette);
    int row = -1;
    for (int i = 0; i < palette->count(); ++i) {
        if (palette->item(i)->data(Qt::UserRole).isValid()) { row = i; break; }
    }
    QVERIFY(row >= 0);
    palette->setCurrentRow(row);

    QVERIFY(QMetaObject::invokeMethod(&dlg, "addControlAt", Qt::DirectConnection,
                                      Q_ARG(QPoint, QPoint(30, 30))));
    QVERIFY(QMetaObject::invokeMethod(&dlg, "addControlAt", Qt::DirectConnection,
                                      Q_ARG(QPoint, QPoint(300, 200))));

    const QList<DesignerControlFrame *> frames = canvas->findChildren<DesignerControlFrame *>();
    QVERIFY2(frames.size() >= 2, "画布上需要两个控件");
    const QRect firstBefore = frames.at(0)->geometry();
    const QRect secondBefore = frames.at(1)->geometry();
    QVERIFY2(firstBefore.left() != secondBefore.left(), "两个控件初始 left 必须不同");

    // 单选：对齐不生效、几何不动
    canvas->selectIndex(0);
    QVERIFY(QMetaObject::invokeMethod(&dlg, "onAlignLeft", Qt::DirectConnection));
    QCOMPARE(frames.at(0)->geometry(), firstBefore);

    // 多选：左对齐生效
    canvas->selectIndex(0);
    canvas->selectIndex(1, true);
    QVERIFY(QMetaObject::invokeMethod(&dlg, "onAlignLeft", Qt::DirectConnection));
    QCOMPARE(frames.at(0)->geometry().left(), frames.at(1)->geometry().left());
    QVERIFY2(frames.at(1)->geometry().left() != secondBefore.left(),
             "对齐没有改变第二个控件位置，本腿会空转通过");

    // 撤销恢复到对齐前
    QVERIFY(QMetaObject::invokeMethod(&dlg, "undo", Qt::DirectConnection));
    const QList<DesignerControlFrame *> afterUndo =
        canvas->findChildren<DesignerControlFrame *>();
    QVERIFY2(afterUndo.size() >= 2, "撤销后控件数量不对");
    QCOMPARE(afterUndo.at(1)->geometry(), secondBefore);
}

// 2026-11-10 框架契约：方案文件早有 schemaVersion + 高版本拒收，运行界面布局
// 却只检查 contains("pages")。这里钉住布局文件的版本契约与子界面引用图校验：
// 高版本/环/超深必须拒收，悬空引用可修复但要留痕。
void UiGuardsTest::layoutSchemaContractRejectsFutureVersionsAndBadSubPageGraphs()
{
    RuntimeInterface base;
    base.ensurePage();
    base.page(0)->pageName = QStringLiteral("保留页");
    QCOMPARE(base.toJson().value(QStringLiteral("schemaVersion")).toInt(),
             RuntimeInterface::kLayoutSchemaVersion);

    // ① 高版本文件：拒收且**不改现有状态**（版本检查在清空之前）
    QJsonObject future = base.toJson();
    future[QStringLiteral("schemaVersion")] = RuntimeInterface::kLayoutSchemaVersion + 99;
    QVERIFY2(!base.fromJson(future), "高于支持版本的布局必须拒收");
    QCOMPARE(base.page(0)->pageName, QStringLiteral("保留页"));
    QVERIFY2(base.lastLoadWarning().contains(QStringLiteral("高于")),
             "拒收原因要能诊断");

    // ② pages 不是数组：拒收
    QJsonObject notArray = base.toJson();
    notArray[QStringLiteral("pages")] = QJsonValue(42);
    QVERIFY(!base.fromJson(notArray));

    // ③ 子界面引用成环：拒收（否则运行期递归展开永不终止）
    RuntimeInterface ring;
    ring.ensurePage();
    const QString a = ring.createSubPage(QStringLiteral("A"));
    const QString b = ring.createSubPage(QStringLiteral("B"));
    RuntimeControl ref;
    ref.type = RuntimeControlType::SubInterface;
    ref.subPageId = a;
    ring.page(0)->controls.append(ref);
    RuntimeControl refB = ref;
    refB.subPageId = b;
    QVERIFY(ring.subPage(a));
    ring.subPage(a)->controls.append(refB);
    RuntimeControl refBack = ref;
    QVERIFY(ring.subPage(b));
    ring.subPage(b)->controls.append(refBack);   // B -> A，成环
    RuntimeInterface loadedRing;
    QVERIFY2(!loadedRing.fromJson(ring.toJson()), "引用成环必须拒收");
    QVERIFY2(loadedRing.lastLoadWarning().contains(QStringLiteral("成环")),
             "环的拒收原因要能诊断");

    // ④ 嵌套超深：超过 kMaxSubPageDepth 拒收
    RuntimeInterface deep;
    deep.ensurePage();
    QString prev;
    for (int i = 0; i <= RuntimeInterface::kMaxSubPageDepth; ++i) {
        const QString id = deep.createSubPage(QStringLiteral("P%1").arg(i));
        RuntimeControl link;
        link.type = RuntimeControlType::SubInterface;
        link.subPageId = id;
        if (prev.isEmpty())
            deep.page(0)->controls.append(link);
        else
            deep.subPage(prev)->controls.append(link);
        prev = id;
    }
    RuntimeInterface loadedDeep;
    QVERIFY2(!loadedDeep.fromJson(deep.toJson()), "超深嵌套必须拒收");
    QVERIFY2(loadedDeep.lastLoadWarning().contains(QStringLiteral("超过")),
             "超深的拒收原因要能诊断");

    // ⑤ 悬空 subPageId：可修复 —— 清空并留 warning，不删用户控件
    RuntimeInterface dangling;
    dangling.ensurePage();
    RuntimeControl bad;
    bad.type = RuntimeControlType::SubInterface;
    bad.subPageId = QStringLiteral("no_such_sub");
    dangling.page(0)->controls.append(bad);
    RuntimeInterface fixed;
    QVERIFY(fixed.fromJson(dangling.toJson()));
    QCOMPARE(fixed.page(0)->controls.size(), 1);
    QVERIFY2(fixed.page(0)->controls.first().subPageId.isEmpty(),
             "悬空引用必须被清空");
    QVERIFY2(fixed.lastLoadWarning().contains(QStringLiteral("已清理")),
             "自动修复也要留痕，不能静默改用户布局");

    // ⑥ 正常文件往返无 warning
    RuntimeInterface clean;
    clean.ensurePage();
    RuntimeInterface reload;
    QVERIFY(reload.fromJson(clean.toJson()));
    QVERIFY2(reload.lastLoadWarning().isEmpty(),
             "正常布局不该产生诊断信息");
}

// 子界面密码（2026-11-11）：布局里只存派生摘要，不存明文；校验走常量时间比较。
// 项目未上线，格式直接改，但读入时要把已存的明文就地升级——否则用户自己已有的
// 布局会打不开子界面。
void UiGuardsTest::subPasswordIsHashedAndVerified()
{
    // ① 摘要格式：不以明文出现、可识别为摘要
    const QString h = hashSubPassword(QStringLiteral("abc"));
    QVERIFY(!h.isEmpty());
    QVERIFY2(isHashedSubPassword(h), "hashSubPassword 必须产出可识别的摘要格式");
    QVERIFY2(h != QStringLiteral("abc"), "不能把明文当摘要存回去");
    // 「摘要里不含明文」要用**不含十六进制字符**的密码来断言：
    // 摘要串本身是 hex，像 "abc" 这种合法 hex 子串完全可能偶然出现，
    // 用它断言会变成概率性红（写这条腿时就撞上过）。
    const QString plainNonHex = QStringLiteral("zzz-not-hex");
    const QString hNonHex = hashSubPassword(plainNonHex);
    QVERIFY2(!hNonHex.contains(plainNonHex), "摘要里不能出现明文密码");

    // P3 复核：长度差截断到 8 位时，摘要 hex 后追加 512 个字符 '0'（=256 个零字节）
    // 仍会通过（32^288=256→0）。用公开的 verifySubPassword 直接验证。
    QVERIFY2(verifySubPassword(plainNonHex, hNonHex), "正确明文+摘要必须通过");
    const QString stretched = hNonHex + QString(512, QLatin1Char('0'));
    QVERIFY2(!verifySubPassword(plainNonHex, stretched),
             "摘要后追加 256 个零字节必须判不相等（长度差不得截断到 8 位）");

    // ② 随机盐：同一密码两次哈希不相同
    const QString h2 = hashSubPassword(QStringLiteral("abc"));
    QVERIFY2(h != h2, "两次哈希必须不同（随机盐），否则相同密码可被识别");

    // ③ 正确/错误密码
    QVERIFY(verifySubPassword(QStringLiteral("abc"), h));
    QVERIFY(!verifySubPassword(QStringLiteral("abd"), h));
    QVERIFY(!verifySubPassword(QString(), h));
    QVERIFY(!verifySubPassword(QStringLiteral("abcd"), h));

    // ④ 未设置密码：任何输入都放行
    QVERIFY(verifySubPassword(QStringLiteral("whatever"), QString()));

    // ⑤ 摘要被篡改（改一位）必须失败
    QString tampered = h;
    tampered[tampered.size() - 1] =
        (tampered.at(tampered.size() - 1) == QLatin1Char('0')) ? QLatin1Char('1')
                                                              : QLatin1Char('0');
    QVERIFY2(!verifySubPassword(QStringLiteral("abc"), tampered),
             "摘要被改动后仍校验通过，说明没有真正比对");

    // ⑤b B3 加固：摘要被**截短/拉长**也必须失败（constantTimeEquals 必须按 qMax 扫完，
    // 不得用 qMin 让长度差漏过；这条牙专门咬"长度不齐"这条边，防止有人篡改摘要长度绕过）。
    QString shortened = h.left(h.size() - 4);   // 截短 4 字节
    QString lengthened = h + QStringLiteral("0000");   // 拉长 4 字节
    QVERIFY2(!verifySubPassword(QStringLiteral("abc"), shortened),
             "摘要被截短后仍校验通过：constantTimeEquals 的 qMax 失效");
    QVERIFY2(!verifySubPassword(QStringLiteral("abc"), lengthened),
             "摘要被拉长后仍校验通过：constantTimeEquals 的 qMax 失效");

    // ⑥ 迭代次数被改成天文数字：拒绝而不是卡死
    const QString bomb =
        QStringLiteral("pbkdf2$1000000000$00112233445566778899aabbccddeeff$"
                       "00112233445566778899aabbccddeeff");
    QVERIFY2(!verifySubPassword(QStringLiteral("abc"), bomb),
             "迭代次数超上界必须直接拒绝，否则打开子界面会卡死");

    // ⑦ 明文迁移：老布局里存的是明文，fromJson 之后应转成摘要且旧密码仍可用
    RuntimeInterfacePage page;
    RuntimeControl *sub = page.addControl(RuntimeControlType::SubInterface,
                                          QRect(0, 0, 200, 100));
    QVERIFY(sub);
    QJsonObject json = page.toJson();
    QJsonArray arr = json[QStringLiteral("controls")].toArray();
    QJsonObject c = arr.at(0).toObject();
    c[QStringLiteral("subPassword")] = QStringLiteral("legacyPwd");
    arr.replace(0, c);
    json[QStringLiteral("controls")] = arr;

    RuntimeInterfacePage migrated;
    QVERIFY(migrated.fromJson(json));
    const QString stored = migrated.controls.first().subPassword;
    QVERIFY2(isHashedSubPassword(stored), "读入时必须把明文密码升级为摘要");
    QVERIFY2(!stored.contains(QStringLiteral("legacyPwd")),
             "迁移后文件里不能残留明文");
    QVERIFY2(verifySubPassword(QStringLiteral("legacyPwd"), stored),
             "迁移后旧密码必须仍然可用，否则用户自己的布局会打不开");
    QVERIFY(!verifySubPassword(QStringLiteral("wrong"), stored));

    // ⑧ 布局往返：摘要串原样保留
    RuntimeInterfacePage back;
    QVERIFY(back.fromJson(migrated.toJson()));
    QCOMPARE(back.controls.first().subPassword, stored);
    QVERIFY(verifySubPassword(QStringLiteral("legacyPwd"),
                              back.controls.first().subPassword));
}

// 属性面板里 15 处 lambda 直接改控件字段却不进撤销栈（2026-11-11 审核）。
// 这里用「子界面圆角」这个最典型的漏网点验证：面板改完必须能撤销回去。
void UiGuardsTest::designerPanelEditsAreUndoable()
{
    RuntimeInterfaceDesigner dlg(QStringList{QStringLiteral("模块1")});

    QListWidget *palette = nullptr;
    for (QListWidget *lw : dlg.findChildren<QListWidget *>()) {
        if (lw->dragEnabled() && lw->count() >= 7) { palette = lw; break; }
    }
    QVERIFY(palette);

    int row = -1;
    for (int i = 0; i < palette->count(); ++i) {
        QListWidgetItem *it = palette->item(i);
        if (!it->data(Qt::UserRole).isValid()) continue;   // 组标题不是控件
        if (it->text().contains(QStringLiteral("子界面"))) { row = i; break; }
    }
    QVERIFY2(row >= 0, "控件库缺少「子界面」条目，本腿会空转");
    palette->setCurrentRow(row);
    QVERIFY(QMetaObject::invokeMethod(&dlg, "addControlAt", Qt::DirectConnection,
                                      Q_ARG(QPoint, QPoint(20, 20))));

    auto *canvas = dlg.findChild<RuntimeDesignerCanvas *>();
    QVERIFY(canvas);
    canvas->selectIndex(0);
    QVERIFY2(dlg.controlsForTest().size() == 1, "应已添加一个控件");
    // 基准取控件自身默认值（子界面默认圆角不是 0），不能写死 0
    const int baseRadius = dlg.controlsForTest().first().cornerRadius;

    auto *spin = dlg.findChild<QSpinBox *>(QStringLiteral("designerSubRadius"));
    QVERIFY2(spin, "找不到子界面圆角输入框");
    const int target = (baseRadius == 24) ? 8 : 24;   // 保证与基准不同且落在 0..40
    spin->setValue(target);
    QVERIFY2(dlg.controlsForTest().first().cornerRadius == target,
             "面板改圆角没有写回控件，本腿会空转通过");

    // 撤销：必须回到基准值。若 markPropertyEdit 没在改前压快照，这里仍是 target。
    QVERIFY(QMetaObject::invokeMethod(&dlg, "undo", Qt::DirectConnection));
    const QList<RuntimeControl> after = dlg.controlsForTest();
    QVERIFY2(!after.isEmpty(), "撤销后控件不该消失");
    QCOMPARE(after.first().cornerRadius, baseRadius);
}

// 2026-11-17 P0-1：帮助窗关窗即销毁，MainWindow 侧用 QPointer 持有。
void UiGuardsTest::helpViewerDeletesOnClose()
{
    // 不能用栈对象测 WA_DeleteOnClose：close 时会 delete this 造成二次析构。
    QPointer<HelpViewer> viewer = new HelpViewer;
    QVERIFY2(viewer->testAttribute(Qt::WA_DeleteOnClose),
             "帮助窗必须关窗即销毁，否则主窗口一直握着旧实例");
    // P0-1 复核：原腿用 delete 测局部 QPointer，判据置死也全绿。改为真走 close()，
    // 断言 destroyed 触发且观察者指针置空（WA_DeleteOnClose 的契约）。
    int destroyedCount = 0;
    connect(viewer.data(), &QObject::destroyed, this, [&destroyedCount]() { ++destroyedCount; });
    // WA_DeleteOnClose 只对"曾显示过"的窗口生效（Qt 语义），先 show 再 close
    viewer->show();
    QCoreApplication::processEvents();
    viewer->close();
    QCoreApplication::processEvents();
    QCOMPARE(destroyedCount, 1);
    QVERIFY(viewer.isNull());

    // 生产侧守卫：MainWindow 必须用 QPointer 持有且两处复用前判空重建
    QFile mw(QStringLiteral(SRC_DIR "/src/MainWindow.cpp"));
    QVERIFY2(mw.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 MainWindow.cpp");
    const QString src = QString::fromUtf8(mw.readAll());
    mw.close();
    QFile mh(QStringLiteral(SRC_DIR "/include/MainWindow.h"));
    QVERIFY2(mh.open(QIODevice::ReadOnly | QIODevice::Text), "打不开 MainWindow.h");
    const QString head = QString::fromUtf8(mh.readAll());
    mh.close();
    QVERIFY2(head.contains(QStringLiteral("QPointer<HelpViewer> m_helpViewer")),
             "MainWindow 必须用 QPointer 持有帮助窗，裸指针在关窗销毁后悬垂");
    const int reuse = src.indexOf(QStringLiteral("if (!m_helpViewer)"));
    QVERIFY2(reuse >= 0 && src.indexOf(QStringLiteral("if (!m_helpViewer)"), reuse + 5) > reuse,
             "两处帮助入口都必须在复用前判空重建（原只测测试自建指针，漏掉生产接线）");
}

QTEST_MAIN(UiGuardsTest)
#include "ui_guards_test.moc"
