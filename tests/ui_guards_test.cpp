// 两项 UI 侧守卫的自动化腿（2026-10-26 复核要求补腿）：
//   1) 标注类别名白名单——`..\..\x` 这类名字能把样本写到标注目录之外（活路径，P2 修复）；
//   2) 导出写检查——open() 成功不代表写成功，写完必须 flush + 查状态。
#include "AnnotationLabelRule.h"
#include "ExportWriteCheck.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
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
    void finishTextExportSucceedsOnWritableFile();
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
    QVERIFY2(!why.isEmpty(), "拒绝时必须给出原因");

    QString dir2 = QStringLiteral("sentinel");
    QVERIFY2(!annotationClassDir(QStringLiteral("   "), QStringLiteral("划痕"), &dir2, &why),
             "空白标注目录被放行");
    QVERIFY2(dir2.isEmpty(), "拒绝时必须清空 outDir");
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

QTEST_MAIN(UiGuardsTest)
#include "ui_guards_test.moc"
