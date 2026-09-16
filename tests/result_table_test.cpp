// 结果数据表验证：同一模块重复执行只原地更新（不新增行、不残留旧值）、失败模块可见、
// CSV 导出包含模块/输出项/值/状态/耗时。不 show() 窗口，可在无桌面的 CI 上稳定运行。
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QTreeWidget>

#include "ResultTablePanel.h"

class ResultTableTest : public QObject
{
    Q_OBJECT

private slots:
    void testInPlaceUpdateAndCsv();
    void testSortingAndFilter();
};

void ResultTableTest::testInPlaceUpdateAndCsv()
{
    ResultTablePanel panel;
    QCOMPARE(panel.moduleCount(), 0);

    QVariantMap first;
    first.insert(QStringLiteral("value"), 3600.0);
    first.insert(QStringLiteral("foregroundPixels"), 3600);
    panel.setModuleResult(3, QStringLiteral("OpenCV二值化"), true, 12, first);
    QCOMPARE(panel.moduleCount(), 1);

    // 同一模块第二次执行：仍是 1 行，且子项已按本轮重建（不得叠加或残留上一轮旧值）
    QVariantMap second;
    second.insert(QStringLiteral("value"), 2500.0);
    panel.setModuleResult(3, QStringLiteral("OpenCV二值化"), true, 8, second);
    QCOMPARE(panel.moduleCount(), 1);

    // 失败模块：单独一行，子项给出明确说明
    panel.setModuleResult(4, QStringLiteral("找边"), false, 5, QVariantMap());
    QCOMPARE(panel.moduleCount(), 2);

    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "无法创建临时目录");
    const QString csv = dir.filePath(QStringLiteral("results.csv"));
    QString err;
    QVERIFY2(panel.exportCsv(csv, &err), qPrintable(err));

    QFile f(csv);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString text = QString::fromUtf8(f.readAll());
    f.close();

    QVERIFY2(text.contains(QStringLiteral("OpenCV二值化")), "CSV 缺少模块名");
    QVERIFY2(text.contains(QStringLiteral("2500")), "CSV 缺少本轮新值");
    QVERIFY2(!text.contains(QStringLiteral("3600")), "CSV 残留了上一轮的旧值");
    QVERIFY2(text.contains(QStringLiteral("失败")), "CSV 未标注失败状态");

    panel.clearResults();
    QCOMPARE(panel.moduleCount(), 0);
}

void ResultTableTest::testSortingAndFilter()
{
    ResultTablePanel panel;
    auto *tree = panel.findChild<QTreeWidget *>();
    QVERIFY2(tree != nullptr, "未找到结果树");

    QVariantMap v;
    v.insert(QStringLiteral("value"), 1.0);
    // 排序已启用：先插入 ModuleB 再插入 ModuleA，面板应按名称把 A 排在前
    panel.setModuleResult(9, QStringLiteral("ModuleB"), true, 20, v);
    panel.setModuleResult(8, QStringLiteral("ModuleA"), true, 10, v);
    QCOMPARE(tree->topLevelItemCount(), 2);
    QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("ModuleA"));
    QCOMPARE(tree->topLevelItem(1)->text(0), QStringLiteral("ModuleB"));

    // 筛选：未命中的模块行整行隐藏，命中的保留
    panel.setFilterText(QStringLiteral("ModuleB"));
    QVERIFY2(tree->topLevelItem(0)->isHidden(), "未命中筛选的模块行应隐藏");
    QVERIFY2(!tree->topLevelItem(1)->isHidden(), "命中筛选的模块行应保留");

    // 筛选生效期间新增的结果也要遵守筛选（否则一跑流程筛选就"失效"）
    panel.setModuleResult(7, QStringLiteral("ModuleC"), true, 5, v);
    QTreeWidgetItem *cRow = nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (tree->topLevelItem(i)->text(0) == QStringLiteral("ModuleC"))
            cRow = tree->topLevelItem(i);
    }
    QVERIFY2(cRow != nullptr, "新增模块未进入表格");
    QVERIFY2(cRow->isHidden(), "筛选生效时新结果不应绕过筛选直接显示");

    // 清空筛选恢复全部
    panel.setFilterText(QString());
    int visible = 0;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (!tree->topLevelItem(i)->isHidden())
            ++visible;
    }
    QCOMPARE(visible, tree->topLevelItemCount());
}

QTEST_MAIN(ResultTableTest)

// 源文件内定义 Q_OBJECT 类时，AUTOMOC 要求显式包含生成的 moc
#include "result_table_test.moc"
