// 结果数据表验证：同一模块重复执行只原地更新（不新增行、不残留旧值）、失败模块可见、
// CSV 导出包含模块/输出项/值/状态/耗时。不 show() 窗口，可在无桌面的 CI 上稳定运行。
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>

#include "ResultTablePanel.h"

class ResultTableTest : public QObject
{
    Q_OBJECT

private slots:
    void testInPlaceUpdateAndCsv();
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

QTEST_MAIN(ResultTableTest)

// 源文件内定义 Q_OBJECT 类时，AUTOMOC 要求显式包含生成的 moc
#include "result_table_test.moc"
