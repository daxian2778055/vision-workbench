// 结果数据表验证：同一模块重复执行只原地更新（不新增行、不残留旧值）、失败模块可见、失败行显示
// 算子留下的原因（U-18）、CSV 导出包含模块/输出项/值/状态/耗时。不 show() 窗口，可在无桌面的 CI 上稳定运行。
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QTreeWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QTableWidget>
#include <QCheckBox>

#include "ResultTablePanel.h"

class ResultTableTest : public QObject
{
    Q_OBJECT

private slots:
    void testInPlaceUpdateAndCsv();
    void testSortingAndFilter();
    void testReportText();
    void testFailureRowShowsReason();   // U-18：失败行必须显示算子留下的原因
    void testResultFieldSpecDisplay();  // VM「模块结果」：只显示声明字段 + 分组/极性/单位/精度
    void testHistoryOnlyTabs();         // 采集/逻辑工具选中时只展示历史结果（VM 结果显示区域）
    void testFollowSelectionFilter();   // 当前/历史结果默认只显示画布选中的模块
    void testPageVisibilitySplit();     // 图像区「模块结果」/ 底部结果区共用面板的页签裁剪
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

void ResultTableTest::testResultFieldSpecDisplay()
{
    ResultTablePanel panel;
    auto *tree = panel.findChild<QTreeWidget *>();
    QVERIFY2(tree != nullptr, "未找到结果树");

    // vars 里同时放配置参数与结果字段：配置参数不得出现在「当前结果」
    QVariantMap vars;
    vars.insert(QStringLiteral("row1"), 50.0);
    vars.insert(QStringLiteral("threshold"), 30.0);
    vars.insert(QStringLiteral("elapsedMs"), 7.0);
    vars.insert(QStringLiteral("moduleStatus"), true);
    vars.insert(QStringLiteral("edgeCount"), 2);
    vars.insert(QStringLiteral("edgeScores"), QStringLiteral("0.9512,0.4231"));
    vars.insert(QStringLiteral("edgePolarities"), QStringLiteral("1,2"));
    vars.insert(QStringLiteral("edgePositions"), QStringLiteral("1.25,-3.50"));
    vars.insert(QStringLiteral("edgePoints"), QStringLiteral("100.00,50.00;120.00,50.00"));
    vars.insert(QStringLiteral("edgeGrays"), QStringLiteral("180.0,90.0"));
    vars.insert(QStringLiteral("lastError"), QStringLiteral("示例原因"));

    // 与卡尺 resultDisplayFields() 同构的一小段规范
    QVector<NodeResultField> fields;
    fields << resultValue(QStringLiteral("elapsedMs"), QStringLiteral("耗时"),
                          QStringLiteral("ms"), 0)
           << resultStatus(QStringLiteral("moduleStatus"), QStringLiteral("模块状态"))
           << resultValue(QStringLiteral("edgeCount"), QStringLiteral("边缘个数"), QString(), 0)
           << resultList(QStringLiteral("edgeScores"), QStringLiteral("边缘%1"),
                         QStringLiteral("分数"), QString(), 4)
           << resultPolarity(QStringLiteral("edgePolarities"), QStringLiteral("边缘%1"),
                             QStringLiteral("极性"))
           << resultList(QStringLiteral("edgePositions"), QStringLiteral("边缘%1"),
                         QStringLiteral("位置"), QStringLiteral("px"), 2)
           << resultPoint(QStringLiteral("edgePoints"), QStringLiteral("边缘%1"),
                          QStringLiteral("边缘点"), 2)
           << resultList(QStringLiteral("edgeGrays"), QStringLiteral("边缘%1"),
                         QStringLiteral("灰度"), QString(), 1);

    panel.setModuleResult(5, QStringLiteral("卡尺测量"), true, 7, vars, fields);
    QTreeWidgetItem *row = tree->topLevelItem(0);
    QVERIFY2(row != nullptr, "结果行未进入表格");

    auto childNamed = [](QTreeWidgetItem *parent, const QString &name) -> QTreeWidgetItem * {
        for (int i = 0; i < parent->childCount(); ++i) {
            if (parent->child(i)->text(0) == name)
                return parent->child(i);
        }
        return nullptr;
    };

    // ① 模块级字段顺序 = 规范顺序；配置参数（row1/threshold）不出现
    //    3 个单值 + 边缘0/边缘1 两个分组 + 规范外的判红原因 = 6
    QCOMPARE(row->childCount(), 6);
    QCOMPARE(row->child(0)->text(0), QStringLiteral("耗时(ms)"));
    QCOMPARE(row->child(0)->text(1), QStringLiteral("7"));
    QCOMPARE(row->child(1)->text(0), QStringLiteral("模块状态"));
    QCOMPARE(row->child(1)->text(1), QStringLiteral("OK"));
    QCOMPARE(row->child(2)->text(0), QStringLiteral("边缘个数"));
    QCOMPARE(row->child(2)->text(1), QStringLiteral("2"));
    QVERIFY2(childNamed(row, QStringLiteral("row1")) == nullptr, "配置参数混进了结果表");
    QVERIFY2(childNamed(row, QStringLiteral("threshold")) == nullptr, "配置参数混进了结果表");

    // ② 逐结果分组：同一序号的多字段合并到「边缘0/边缘1」
    QTreeWidgetItem *edge0 = childNamed(row, QStringLiteral("边缘0"));
    QVERIFY2(edge0 != nullptr, "缺少边缘0分组");
    QCOMPARE(edge0->childCount(), 5);
    QCOMPARE(edge0->child(0)->text(0), QStringLiteral("分数"));
    QCOMPARE(edge0->child(0)->text(1), QStringLiteral("0.9512"));
    QCOMPARE(edge0->child(1)->text(0), QStringLiteral("极性"));
    QCOMPARE(edge0->child(1)->text(1), QStringLiteral("从黑到白"));   // 1
    QCOMPARE(edge0->child(2)->text(0), QStringLiteral("位置(px)"));
    QCOMPARE(edge0->child(2)->text(1), QStringLiteral("1.25"));
    QCOMPARE(edge0->child(3)->text(1), QStringLiteral("X=100.00, Y=50.00"));
    QCOMPARE(edge0->child(4)->text(1), QStringLiteral("180.0"));
    QTreeWidgetItem *edge1 = childNamed(row, QStringLiteral("边缘1"));
    QVERIFY2(edge1 != nullptr, "缺少边缘1分组");
    QCOMPARE(edge1->child(1)->text(1), QStringLiteral("从白到黑"));   // 2

    // ③ 判红原因即便不在规范里也要显示（U-18 口径不能因规范渲染而丢）
    QCOMPARE(row->child(5)->text(0), QStringLiteral("lastError"));
    QCOMPARE(row->child(5)->text(1), QStringLiteral("示例原因"));

    // ④ CSV / 报告按「分组 子项」扁平化，现场留档能还原
    QTemporaryDir dir;
    QVERIFY2(dir.isValid(), "无法创建临时目录");
    const QString csvPath = dir.filePath(QStringLiteral("spec.csv"));
    QString err;
    QVERIFY2(panel.exportCsv(csvPath, &err), qPrintable(err));
    QFile csv(csvPath);
    QVERIFY(csv.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString csvText = QString::fromUtf8(csv.readAll());
    csv.close();
    QVERIFY2(csvText.contains(QStringLiteral("边缘0 分数")), qPrintable(csvText));
    QVERIFY2(csvText.contains(QStringLiteral("从黑到白")), qPrintable(csvText));
    const QString report = panel.toReportText(QStringLiteral("SpecFlow"));
    QVERIFY2(report.contains(QStringLiteral("[边缘0]")), qPrintable(report));
    QVERIFY2(report.contains(QStringLiteral("X=100.00, Y=50.00")), qPrintable(report));
}

// VM 4.4「结果显示区域」：选中采集 / 逻辑工具模块时只展示历史结果
// （采集输出是图像、逻辑工具只产出流程走向，均无“需要实时关注的当前值”）。
void ResultTableTest::testHistoryOnlyTabs()
{
    ResultTablePanel panel;
    auto *tabs = panel.findChild<QTabWidget *>();
    QVERIFY2(tabs != nullptr, "未找到结果区页签容器");
    QCOMPARE(tabs->count(), 3);
    QVERIFY2(tabs->isTabVisible(0), "默认应显示「当前结果」");
    QVERIFY2(tabs->isTabVisible(1) && tabs->isTabVisible(2),
             "「历史结果」「帮助信息」应常驻可见");

    QVariantMap v;
    v.insert(QStringLiteral("value"), 1.0);
    panel.setModuleResult(7, QStringLiteral("采集模块"), true, 3, v);
    QCOMPARE(panel.moduleCount(), 1);

    // 选中采集/逻辑工具模块：隐藏当前结果并自动切到历史页
    panel.setHistoryOnlySelection(true);
    QVERIFY(panel.historyOnlySelection());
    QVERIFY2(!tabs->isTabVisible(0), "采集/逻辑工具不应展示「当前结果」页");
    QCOMPARE(tabs->currentIndex(), 1);
    QVERIFY2(tabs->isTabVisible(2), "帮助页不应被隐藏");

    // 切回普通模块：恢复双页并落在当前结果
    panel.setHistoryOnlySelection(false);
    QVERIFY(!panel.historyOnlySelection());
    QVERIFY2(tabs->isTabVisible(0), "普通模块应恢复「当前结果」页");
    QCOMPARE(tabs->currentIndex(), 0);
    QCOMPARE(panel.moduleCount(), 1);   // 数据未丢

    // 幂等：同一个模式下重复设置不把用户从帮助页拽走
    tabs->setCurrentIndex(2);
    panel.setHistoryOnlySelection(false);
    QCOMPARE(tabs->currentIndex(), 2);
}

// VM 4.4 右侧分区：同一 ResultTablePanel 被两处复用——
// 图像区「模块结果」只留当前结果（并隐藏内部页签栏），底部结果区只留历史/帮助。
// 关键回归点：「只展示历史结果」的选中开关不能把底部被配置隐藏的当前页翻出来。
void ResultTableTest::testPageVisibilitySplit()
{
    // ① 图像区「模块结果」：当前结果铺满，内部页签栏不占高度
    {
        ResultTablePanel panel;
        auto *tabs = panel.findChild<QTabWidget *>();
        QVERIFY2(tabs != nullptr, "未找到结果区页签容器");
        panel.setPageVisibility(true, false, false);
        panel.setEmbeddedMode(true);
        QVERIFY2(tabs->isTabVisible(0), "模块结果页应保留当前结果");
        QVERIFY2(!tabs->isTabVisible(1) && !tabs->isTabVisible(2),
                 "图像区不应展示历史/帮助页");
        QCOMPARE(tabs->currentIndex(), 0);
        QVERIFY2(tabs->tabBar()->isHidden(), "嵌入模式应隐藏内部页签栏");
    }

    // ② 底部结果区：只留历史/帮助；采集/逻辑工具的「仅历史」开关不改变配置
    {
        ResultTablePanel panel;
        auto *tabs = panel.findChild<QTabWidget *>();
        QVERIFY2(tabs != nullptr, "未找到结果区页签容器");
        panel.setPageVisibility(false, true, true);
        QVERIFY2(!tabs->isTabVisible(0), "底部结果区不展示当前结果");
        QVERIFY2(tabs->isTabVisible(1) && tabs->isTabVisible(2),
                 "底部结果区应保留历史与帮助");
        QCOMPARE(tabs->currentIndex(), 1);   // 当前页被裁剪 → 落到第一个可见页

        panel.setHistoryOnlySelection(true);
        QVERIFY2(!tabs->isTabVisible(0), "「仅历史」开关不得把配置隐藏的当前页翻出来");
        QCOMPARE(tabs->currentIndex(), 1);
        panel.setHistoryOnlySelection(false);
        QVERIFY2(!tabs->isTabVisible(0), "恢复双页时也不得把配置隐藏的当前页翻出来");
        QVERIFY2(tabs->isTabVisible(1), "历史页应保持可见");

        // 数据照常可喂：被裁剪的只是显示，不影响结果/历史记录
        QVariantMap v;
        v.insert(QStringLiteral("value"), 2.0);
        panel.setModuleResult(11, QStringLiteral("ModuleC"), true, 5, v);
        QCOMPARE(panel.moduleCount(), 1);
        QCOMPARE(panel.findChild<QTableWidget *>()->rowCount(), 1);
    }
}

// VM 4.4：「当前结果 / 历史结果」随画布选中模块过滤（默认开启）。
// 关闭后回到全部模块总览；导出 CSV / 报告始终包含全部模块。
void ResultTableTest::testFollowSelectionFilter()
{
    ResultTablePanel panel;
    auto *tree = panel.findChild<QTreeWidget *>();
    auto *history = panel.findChild<QTableWidget *>();
    auto *check = panel.findChild<QCheckBox *>(QStringLiteral("followSelectionCheck"));
    QVERIFY2(tree && history && check, "结果面板缺少结果树/历史表/跟随选中开关");
    QVERIFY2(panel.followSelectionEnabled(), "默认应按 VM 口径只显示选中模块");
    QVERIFY(check->isChecked());

    QVariantMap v;
    v.insert(QStringLiteral("value"), 1.0);
    panel.setModuleResult(8, QStringLiteral("ModuleA"), true, 10, v);
    panel.setModuleResult(9, QStringLiteral("ModuleB"), true, 10, v);
    auto rowOf = [tree](const QString &name) -> QTreeWidgetItem * {
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            if (tree->topLevelItem(i)->text(0) == name)
                return tree->topLevelItem(i);
        }
        return nullptr;
    };
    QTreeWidgetItem *rowA = rowOf(QStringLiteral("ModuleA"));
    QTreeWidgetItem *rowB = rowOf(QStringLiteral("ModuleB"));
    QVERIFY(rowA && rowB);

    // 未选中：全部可见
    panel.setCurrentModule(-1);
    QVERIFY2(!rowA->isHidden() && !rowB->isHidden(), "无选中时应显示全部模块");

    // 选中 A：仅 A 可见（数据仍在，导出/报告不受影响）
    panel.setCurrentModule(8);
    QVERIFY2(!rowA->isHidden(), "选中模块自身应可见");
    QVERIFY2(rowB->isHidden(), "未选中的模块行应隐藏");
    QCOMPARE(panel.moduleCount(), 2);

    // 历史页同样按选中模块过滤
    QCOMPARE(history->rowCount(), 2);
    auto historyVisible = [history](const QString &name) {
        for (int r = 0; r < history->rowCount(); ++r) {
            QTableWidgetItem *m = history->item(r, 2);
            if (m && m->text().contains(name))
                return !history->isRowHidden(r);
        }
        return false;
    };
    QVERIFY2(historyVisible(QStringLiteral("ModuleA")), "选中模块的历史应可见");
    QVERIFY2(!historyVisible(QStringLiteral("ModuleB")), "未选中模块的历史应隐藏");

    // 关闭跟随：恢复全部模块总览
    panel.setFollowSelectionEnabled(false);
    QVERIFY(!check->isChecked());
    QVERIFY2(!rowB->isHidden(), "关闭跟随选中后应显示全部模块");
    QVERIFY2(historyVisible(QStringLiteral("ModuleB")), "关闭跟随选中后历史也应恢复");

    // 重新开启 + 取消选中：仍显示全部（没有选中对象时不做过滤）
    panel.setFollowSelectionEnabled(true);
    panel.setCurrentModule(-1);
    QVERIFY2(!rowB->isHidden(), "无选中时不应按模块过滤");
    QVERIFY2(historyVisible(QStringLiteral("ModuleB")), "无选中时历史不应过滤");
}

QTEST_MAIN(ResultTableTest)
// 源文件内定义 Q_OBJECT 类时，AUTOMOC 要求显式包含生成的 moc
#include "result_table_test.moc"
