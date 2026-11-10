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
#include "YoloAnnotation.h"
#include "RuntimeInterface.h"
#include "RuntimeInterfaceView.h"
#include "HalconWindow.h"
#include "UiLogBus.h"
#include "ExportWriteCheck.h"
#include "FocusGuard.h"
#include "RuntimeInterfaceDesigner.h"
#include "GlobalVariableManager.h"

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
void b3NewControlsRoundTrip();
void designerB3PropertyPanelsWork();
    void staleBindingsIgnoresValidAndUnbound();
    void staleBindingsTreatsUnknownNodeSetAsUnknown();
    // r15 P1-9：切图保存必须写到「上一张」
    void annotationDialogDoesNotTruncateOnOpen();
    void annotationDialogSwitchSavesToPreviousImage();
    void annotationDialogRollsBackWhenSaveFails();
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

// 复核（扫描清单「设计器 Delete 快捷键劫持文本编辑」）：主窗口 Delete 用
// QShortcut 默认的 WindowShortcut 上下文，只要窗口激活、任何子控件收到 Delete 都会
// 触发——在参数面板输入框里按 Delete 想删字符，画布上选中的节点却被删了。
// 这条腿锁住焦点判定：可编辑控件里必须拦，普通控件里必须放行。
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

void UiGuardsTest::b3NewControlsRoundTrip()
{
    // 类型名映射：枚举每一项都必须能按名字往返（新增类型漏映射会在这里判红）
    for (int i = 0; i <= int(RuntimeControlType::TextEntryCombo); ++i) {
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

    RuntimeInterfacePage page2;
    QVERIFY(page2.fromJson(page.toJson()));
    QCOMPARE(page2.controls.size(), 8);
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

    gv->removeVariable(ngName);
}

QTEST_MAIN(UiGuardsTest)
#include "ui_guards_test.moc"
