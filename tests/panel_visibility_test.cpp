// 面板显隐持久化验证：键集中定义、写入后能跨"重启"读回、未记录过默认关闭。
// 用独立 ini 文件而非默认配置（避免污染注册表），且不需要 Widgets。
#include <QtTest>
#include <QTemporaryDir>

#include "PanelVisibilityStore.h"

class PanelVisibilityTest : public QObject
{
    Q_OBJECT

private slots:
    void testKeysAreStable();
    void testRoundTripAcrossRestart();
};

void PanelVisibilityTest::testKeysAreStable()
{
    const QStringList keys = PanelVisibilityStore::knownKeys();
    QCOMPARE(keys.size(), 4);
    for (const QString &k : keys) {
        QVERIFY2(!k.isEmpty(), "面板键不能为空");
        // 键名规则集中在一处，写入与恢复必然一致
        QCOMPARE(PanelVisibilityStore::settingsKey(k), QStringLiteral("panels/%1").arg(k));
    }
    QVERIFY(keys.contains(QStringLiteral("resultTable")));
    QVERIFY(keys.contains(QStringLiteral("variable")));
}

void PanelVisibilityTest::testRoundTripAcrossRestart()
{
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "无法创建临时目录");
    const QString ini = dir.filePath(QStringLiteral("panel-visibility.ini"));

    // 第一次运行：默认全部关闭，只打开其中两个
    {
        PanelVisibilityStore store(ini);
        const QStringList keys = PanelVisibilityStore::knownKeys();
        for (const QString &k : keys)
            QVERIFY2(!store.isVisible(k), "未记录过的面板应为关闭");
        store.setVisible(QStringLiteral("resultTable"), true);
        store.setVisible(QStringLiteral("performance"), true);
        store.setVisible(QStringLiteral("variable"), false);
    }

    // 重启后：仍记得上次打开的面板（这正是"每次重启都要重新打开面板"的解药）
    {
        PanelVisibilityStore store(ini);
        QVERIFY2(store.isVisible(QStringLiteral("resultTable")), "未记住打开的结果表");
        QVERIFY2(store.isVisible(QStringLiteral("performance")), "未记住打开的性能统计");
        QVERIFY2(!store.isVisible(QStringLiteral("variable")), "已关闭的面板不应被恢复");
        QVERIFY2(!store.isVisible(QStringLiteral("outputData")), "未打开过的面板应保持关闭");
    }

    // 全部键都能往返：防止将来新增面板时写入端与恢复端键名不一致而静默失效
    {
        PanelVisibilityStore store(ini);
        const QStringList keys = PanelVisibilityStore::knownKeys();
        for (const QString &k : keys)
            store.setVisible(k, true);
    }
    {
        PanelVisibilityStore store(ini);
        const QStringList keys = PanelVisibilityStore::knownKeys();
        for (const QString &k : keys)
            QVERIFY2(store.isVisible(k), qPrintable(QStringLiteral("键 %1 未能往返").arg(k)));
    }

    // 空键既不写也不读（否则会写成 "panels/"，永远匹配不上）
    {
        PanelVisibilityStore store(ini);
        store.setVisible(QString(), true);
        QVERIFY(!store.isVisible(QString()));
    }
}

QTEST_MAIN(PanelVisibilityTest)

// 源文件内定义 Q_OBJECT 类时，AUTOMOC 要求显式包含生成的 moc
#include "panel_visibility_test.moc"
