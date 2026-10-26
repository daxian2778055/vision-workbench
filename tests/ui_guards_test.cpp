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
