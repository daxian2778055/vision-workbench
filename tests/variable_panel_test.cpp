// 变量面板验证：引用表达式与 resolveParamRefs 语法一致、同一模块原地更新、全局变量可见、
// 引用可插入到编辑控件光标处。不 show() 窗口，可在无桌面的 CI 上稳定运行。
#include <QtTest>
#include <QLineEdit>
#include <QTextEdit>

#include "VariablePanel.h"
#include "GlobalVariableManager.h"

class VariablePanelTest : public QObject
{
    Q_OBJECT

private slots:
    void testRefTextSyntax();
    void testModuleRowsInPlaceUpdate();
    void testInsertIntoEditors();
};

void VariablePanelTest::testRefTextSyntax()
{
    // 语法必须与 FlowExecutor::resolveParamRefs 的解析式一致，否则粘贴到参数里不会被替换
    QCOMPARE(VariablePanel::moduleRefText(3, QStringLiteral("foregroundPixels")),
             QStringLiteral("{3.foregroundPixels}"));
    QCOMPARE(VariablePanel::globalRefText(QStringLiteral("triggerCount")),
             QStringLiteral("{global.triggerCount}"));
}

void VariablePanelTest::testModuleRowsInPlaceUpdate()
{
    VariablePanel panel;
    QCOMPARE(panel.moduleCount(), 0);

    QVariantMap first;
    first.insert(QStringLiteral("value"), 3600.0);
    first.insert(QStringLiteral("foregroundPixels"), 3600);
    panel.setModuleVars(3, QStringLiteral("OpenCV二值化"), first);
    QCOMPARE(panel.moduleCount(), 1);

    QVariantMap second;
    second.insert(QStringLiteral("value"), 2500.0);
    panel.setModuleVars(3, QStringLiteral("OpenCV二值化"), second);   // 同一模块再次执行
    QCOMPARE(panel.moduleCount(), 1);                                 // 不新增模块行

    panel.setModuleVars(4, QStringLiteral("找边"), second);
    QCOMPARE(panel.moduleCount(), 2);

    // 全局变量分组应能读到单例（含测试自建变量）
    const QString varName = QStringLiteral("vfp_test_var");
    GlobalVariableManager::instance()->addVariable(varName, GlobalVariableManager::IntType, 7);
    panel.refreshGlobalVariables();
    QVERIFY2(panel.globalVariableCount() >= 1, "全局变量未进入变量面板");
    GlobalVariableManager::instance()->removeVariable(varName);

    panel.clearAll();
    QCOMPARE(panel.moduleCount(), 0);
}

void VariablePanelTest::testInsertIntoEditors()
{
    QLineEdit edit;
    edit.setText(QStringLiteral("基准 + "));
    edit.setCursorPosition(edit.text().size());
    QVERIFY(VariablePanel::insertReferenceInto(&edit, QStringLiteral("{3.value}")));
    QCOMPARE(edit.text(), QStringLiteral("基准 + {3.value}"));

    QTextEdit text;
    text.setPlainText(QStringLiteral("x="));
    text.moveCursor(QTextCursor::End);
    QVERIFY(VariablePanel::insertReferenceInto(&text, QStringLiteral("{global.a}")));
    QCOMPARE(text.toPlainText(), QStringLiteral("x={global.a}"));

    // 不可编辑的控件应返回 false（调用方据此给出提示，而不是静默失败）
    QWidget plain;
    QVERIFY(!VariablePanel::insertReferenceInto(&plain, QStringLiteral("{1.value}")));
}

QTEST_MAIN(VariablePanelTest)

// 源文件内定义 Q_OBJECT 类时，AUTOMOC 要求显式包含生成的 moc
#include "variable_panel_test.moc"
