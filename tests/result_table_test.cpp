// 结果数据表验证：同一模块重复执行只原地更新（不新增行、不残留旧值）、失败模块可见、失败行显示
// 算子留下的原因（U-18）、CSV 导出包含模块/输出项/值/状态/耗时。不 show() 窗口，可在无桌面的 CI 上稳定运行。
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
    void testReportText();
    void testFailureRowShowsReason();   // U-18：失败行必须显示算子留下的原因
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

void ResultTableTest::testReportText()
{
    ResultTablePanel panel;

    // 无数据时明确提示（不会生成一份"看起来正常但全空"的报告）
    const QString empty = panel.toReportText(QStringLiteral("TestFlow"));
    QVERIFY2(empty.contains(QStringLiteral("TestFlow")), "报告缺少流程名");
    QVERIFY2(empty.contains(QStringLiteral("暂无结果")), "空面板应提示暂无结果");

    QVariantMap ok;
    ok.insert(QStringLiteral("foregroundPixels"), 3600);
    ok.insert(QStringLiteral("value"), 3600.0);
    panel.setModuleResult(3, QStringLiteral("OpenCV二值化"), true, 12, ok);
    panel.setModuleResult(4, QStringLiteral("找边"), false, 5, QVariantMap());

    const QString report = panel.toReportText(QStringLiteral("TestFlow"));
    QVERIFY2(report.contains(QStringLiteral("TestFlow")), "报告缺少流程名");
    QVERIFY2(report.contains(QStringLiteral("2 个模块")), "报告缺少模块汇总");
    QVERIFY2(report.contains(QStringLiteral("失败 1 个")), "报告未统计失败模块");
    QVERIFY2(report.contains(QStringLiteral("OpenCV二值化")), "报告缺少成功模块");
    QVERIFY2(report.contains(QStringLiteral("foregroundPixels = 3600")), "报告缺少测量值明细");
    QVERIFY2(report.contains(QStringLiteral("找边")), "报告缺少失败模块");
    QVERIFY2(report.contains(QStringLiteral("失败")), "报告未标注失败状态");
}

void ResultTableTest::testFailureRowShowsReason()
{
    // U-18：判红轮面板不能只剩"(执行失败，无输出)"。算子把原因写进结果字段
    // （lastError / *Note），执行器失败轮把它随快照推过来 ⇒ 面板必须显示原文。
    ResultTablePanel panel;
    auto *tree = panel.findChild<QTreeWidget *>();
    QVERIFY2(tree != nullptr, "未找到结果树");

    auto rowOf = [tree](const QString &name) -> QTreeWidgetItem * {
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            if (tree->topLevelItem(i)->text(0) == name)
                return tree->topLevelItem(i);
        }
        return nullptr;
    };
    auto childDump = [](const QTreeWidgetItem *row) {
        QString joined;
        for (int c = 0; c < row->childCount(); ++c) {
            const QTreeWidgetItem *child = row->child(c);
            joined += child->text(0);
            if (!child->text(1).isEmpty())
                joined += QStringLiteral(" = ") + child->text(1);
            joined += QLatin1Char('|');
        }
        return joined;
    };

    // 1) 有原因：原文进子项（字段名 + 值），状态列仍是"失败"
    QVariantMap reason;
    reason.insert(QStringLiteral("lastError"), QStringLiteral("标定失败: #8403 描述文件不存在"));
    panel.setModuleResult(3, QStringLiteral("标定板"), false, 6, reason);
    QTreeWidgetItem *withReason = rowOf(QStringLiteral("标定板"));
    QVERIFY2(withReason != nullptr, "失败模块未进入表格");
    QCOMPARE(withReason->text(2), QStringLiteral("失败"));
    QVERIFY2(childDump(withReason).contains(QStringLiteral("#8403")),
             qPrintable(QStringLiteral("失败行没显示原因原文：%1").arg(childDump(withReason))));
    QVERIFY2(childDump(withReason).contains(QStringLiteral("lastError")),
             qPrintable(QStringLiteral("失败行只显示原因文本、没带字段名（现场无法对上参数）：%1")
                        .arg(childDump(withReason))));
    QVERIFY2(!childDump(withReason).contains(QStringLiteral("执行失败，无输出")),
             qPrintable(QStringLiteral("已有原因却仍显示占位：%1").arg(childDump(withReason))));

    // 2) 无原因：保留占位（不得凭空造原因，也不得留空白行）
    panel.setModuleResult(4, QStringLiteral("无原因算子"), false, 5, QVariantMap());
    QTreeWidgetItem *noReason = rowOf(QStringLiteral("无原因算子"));
    QVERIFY2(noReason != nullptr, "失败模块未进入表格");
    QCOMPARE(noReason->childCount(), 1);
    QCOMPARE(noReason->child(0)->text(0), QStringLiteral("(执行失败，无输出)"));

    // 3) 同一模块换一轮：新原因替换旧原因，不得残留
    QVariantMap reason2;
    reason2.insert(QStringLiteral("calibNote"), QStringLiteral("标定点对数不足（至少 2 对）"));
    panel.setModuleResult(3, QStringLiteral("标定板"), false, 4, reason2);
    withReason = rowOf(QStringLiteral("标定板"));
    QVERIFY2(childDump(withReason).contains(QStringLiteral("点对数不足")),
             qPrintable(QStringLiteral("新一轮原因未显示：%1").arg(childDump(withReason))));
    QCOMPARE(withReason->child(0)->text(0), QStringLiteral("calibNote"));
    QVERIFY2(!childDump(withReason).contains(QStringLiteral("#8403")),
             qPrintable(QStringLiteral("上一轮原因残留：%1").arg(childDump(withReason))));

    // 4) CSV 与报告同样带原因（现场留档的是面板内容）
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "无法创建临时目录");
    const QString csvPath = dir.filePath(QStringLiteral("reason.csv"));
    QString err;
    QVERIFY2(panel.exportCsv(csvPath, &err), qPrintable(err));
    QFile csv(csvPath);
    QVERIFY(csv.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString csvText = QString::fromUtf8(csv.readAll());
    csv.close();
    QVERIFY2(csvText.contains(QStringLiteral("点对数不足")),
             qPrintable(QStringLiteral("CSV 未包含失败原因：%1").arg(csvText)));
    QVERIFY2(csvText.contains(QStringLiteral("calibNote")),
             qPrintable(QStringLiteral("CSV 的原因行没有字段名（对不上是哪个参数）：%1").arg(csvText)));

    const QString report = panel.toReportText(QStringLiteral("U18Flow"));
    QVERIFY2(report.contains(QStringLiteral("点对数不足")),
             qPrintable(QStringLiteral("报告未包含失败原因：%1").arg(report)));
    QVERIFY2(report.contains(QStringLiteral("calibNote")),
             qPrintable(QStringLiteral("报告的原因行没有字段名：%1").arg(report)));
}

QTEST_MAIN(ResultTableTest)

// 源文件内定义 Q_OBJECT 类时，AUTOMOC 要求显式包含生成的 moc
#include "result_table_test.moc"
