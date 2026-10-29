// 两项 UI 侧守卫的自动化腿（2026-10-26 复核要求补腿）：
//   1) 标注类别名白名单——`..\..\x` 这类名字能把样本写到标注目录之外（活路径，P2 修复）；
//   2) 导出写检查——open() 成功不代表写成功，写完必须 flush + 查状态。
#include "AnnotationLabelRule.h"
#include "BoxAnnotatorDialog.h"
#include "YoloAnnotation.h"
#include "RuntimeInterface.h"
#include "ExportWriteCheck.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QListWidget>
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
    void annotationLabelRejectsTraversalAndSeparators();
    void annotationClassDirStaysUnderRoot();
    void annotationClassDirRejectsEscape();
    void annotationClassDirRejectsEmptyRoot();
    void annotationPathIsInsideBoundaries();
    // 闸"有没有接线"的端到端腿：junction 指向 root 之外必须被拦（臂 D 的缺口）
    void annotationClassDirRejectsJunctionEscape();
    void finishTextExportSucceedsOnWritableFile();
    void staleBindingsFindsMissingTargets();
    void staleBindingsIgnoresValidAndUnbound();
    void staleBindingsTreatsUnknownNodeSetAsUnknown();
    // r15 P1-9：切图保存必须写到「上一张」
    void annotationDialogDoesNotTruncateOnOpen();
    void annotationDialogSwitchSavesToPreviousImage();
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

QTEST_MAIN(UiGuardsTest)
#include "ui_guards_test.moc"
