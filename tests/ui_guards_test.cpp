// 两项 UI 侧守卫的自动化腿（2026-10-26 复核要求补腿）：
//   1) 标注类别名白名单——`..\..\x` 这类名字能把样本写到标注目录之外（活路径，P2 修复）；
//   2) 导出写检查——open() 成功不代表写成功，写完必须 flush + 查状态。
#include "AnnotationLabelRule.h"
#include "BoxAnnotatorDialog.h"
#include "YoloAnnotation.h"
#include "RuntimeInterface.h"
#include "RuntimeInterfaceView.h"
#include "ExportWriteCheck.h"
#include "FocusGuard.h"
#include "RuntimeInterfaceDesigner.h"

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
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QProcess>
#include <QTextStream>
#include <QtTest/QtTest>

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

QTEST_MAIN(UiGuardsTest)
#include "ui_guards_test.moc"
