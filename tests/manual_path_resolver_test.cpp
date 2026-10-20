// ManualPathResolverTest（词条切片 B 手册解析族首抽）：手册候选链与首读者胜出语义的纯核座。
// 候选链腿为纯字符串装配（不触文件系统）；解析腿以注入候选清单走
// QTemporaryDir 真文件（绝对路径，避开工作目录污染）。不进 MainWindow。
// 红臂破坏位 = 首读者胜出失效（即回改作遍历覆写）——首读者胜出腿当场红，
// 候选链与跳读者与全空三腿臂内原样绿。
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "ManualPathResolver.h"

namespace {

bool writeFileUtf8(const QString &path, const QString &text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    return file.write(text.toUtf8()) >= 0;
}

} // namespace

class ManualPathResolverTest : public QObject
{
    Q_OBJECT

private slots:
    void testCandidateChainOrderAndPaths();   // 候选链四式恒序（纯装配）
    void testResolveFirstReadableWins();      // 首个存在且可读者胜出（红臂靶腿）
    void testResolveNotReadableSkips();       // 目录形跳过、后位读者胜出
    void testResolveEmptyWhenNoneReadable();  // 全不可读 → 空串
};

void ManualPathResolverTest::testCandidateChainOrderAndPaths()
{
    const QStringList list = ManualPathResolver::candidatePaths(QStringLiteral("/base/dir"));
    QCOMPARE(list, QStringList({
        QStringLiteral("/base/dir/docs/用户操作手册.md"),
        QStringLiteral("docs/用户操作手册.md"),
        QStringLiteral("/base/dir/用户操作手册.md"),
        QStringLiteral(":/用户操作手册.md"),
    }));
}

void ManualPathResolverTest::testResolveFirstReadableWins()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString missing = dir.path() + QStringLiteral("/missing.md");
    const QString first = dir.path() + QStringLiteral("/z_first.md");
    const QString later = dir.path() + QStringLiteral("/z_later.md");
    QVERIFY(writeFileUtf8(first, QStringLiteral("REAL")));
    QVERIFY(writeFileUtf8(later, QStringLiteral("LATER")));

    const QString markdown = ManualPathResolver::resolveMarkdown({ missing, first, later });
    QCOMPARE(markdown, QStringLiteral("REAL"));   // 首读者胜出：后位不再试
}

void ManualPathResolverTest::testResolveNotReadableSkips()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir().mkpath(dir.path() + QStringLiteral("/sub")));
    const QString subDir = dir.path() + QStringLiteral("/sub");
    QVERIFY(QFileInfo::exists(subDir));   // 目录：exists 真而 open 失败
    const QString missing = dir.path() + QStringLiteral("/missing.md");
    const QString readable = dir.path() + QStringLiteral("/after.md");
    QVERIFY(writeFileUtf8(readable, QStringLiteral("C")));

    const QString markdown = ManualPathResolver::resolveMarkdown(
        { missing, subDir, readable });
    QCOMPARE(markdown, QStringLiteral("C"));
}

void ManualPathResolverTest::testResolveEmptyWhenNoneReadable()
{
    const QStringList paths({
        QStringLiteral("c:/definitely/not/here/manual-one.md"),
        QStringLiteral("c:/definitely/not/here/manual-two.md"),
    });
    QVERIFY(ManualPathResolver::resolveMarkdown(paths).isEmpty());
}

QTEST_MAIN(ManualPathResolverTest)
#include "manual_path_resolver_test.moc"
