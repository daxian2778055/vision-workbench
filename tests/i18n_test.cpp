// G-P1-5 国际化：端到端验证英文翻译文件（visionflow_en.qm）可被加载，
// 且关键源文本（菜单标题 / 运行控制）翻译正确。证明 tr() → .ts → .qm 管线闭环。
#include <QObject>
#include <QCoreApplication>
#include <QTranslator>
#include <QString>
#include <QtTest/QtTest>

class I18nTest : public QObject {
    Q_OBJECT

private slots:
    void englishQmLoadsAndTranslates();
};

void I18nTest::englishQmLoadsAndTranslates()
{
    QTranslator translator;
    const QString qm = QCoreApplication::applicationDirPath() +
                      QStringLiteral("/translations/visionflow_en.qm");
    QVERIFY2(translator.load(qm), qPrintable(QStringLiteral("无法加载 ") + qm));
    QCoreApplication::installTranslator(&translator);

    // 主菜单标题（MainWindow.ui 经 uic 编译进 MainWindow 上下文）
    QCOMPARE(QCoreApplication::translate("MainWindow", "文件"), QStringLiteral("File"));
    QCOMPARE(QCoreApplication::translate("MainWindow", "编辑"), QStringLiteral("Edit"));
    QCOMPARE(QCoreApplication::translate("MainWindow", "系统"), QStringLiteral("System"));

    // 运行控制（ExecutionStatusController 上下文）
    QCOMPARE(QCoreApplication::translate("ExecutionStatusController", "继续"),
             QStringLiteral("Continue"));
    QCOMPARE(QCoreApplication::translate("ExecutionStatusController", "暂停"),
             QStringLiteral("Pause"));

    // 主窗口运行期消息（MainWindow 上下文）
    QCOMPARE(QCoreApplication::translate("MainWindow", "新建方案"),
             QStringLiteral("New Scheme"));
    QCOMPARE(QCoreApplication::translate("MainWindow", "只读方案：禁止覆盖保存（可另存为新方案）"),
             QStringLiteral("Read-only scheme: overwriting is forbidden "
                            "(you may save as a new scheme)"));

    // 第二轮包裹的可见控件标签（原先硬编码 QStringLiteral，现已 tr）
    QCOMPARE(QCoreApplication::translate("MainWindow", "流程控制"),
             QStringLiteral("Flow Control"));
    QCOMPARE(QCoreApplication::translate("MainWindow", "单次执行"),
             QStringLiteral("Single Run"));
    QCOMPARE(QCoreApplication::translate("MainWindow", "连续模式"),
             QStringLiteral("Continuous Mode"));
    QCOMPARE(QCoreApplication::translate("MainWindow", "关于"),
             QStringLiteral("About"));

    // FR15.10 运行期子流程的菜单文本（本轮补抽取：上一批交付漏跑 lupdate，
    // 这 14 条当时根本没进 .ts，英文模式下这些菜单只会是中文）
    QCOMPARE(QCoreApplication::translate("MainWindow", "定义为子流程…"),
             QStringLiteral("Define as Subflow..."));
    QCOMPARE(QCoreApplication::translate("MainWindow", "删除子流程定义…"),
             QStringLiteral("Delete Subflow Definition..."));

    // 未翻译的源文（如本测试未抽取到的字符串）应保持源文，不影响中文模式
    QCOMPARE(QCoreApplication::translate("MainWindow", "未抽取的占位文本123"),
             QStringLiteral("未抽取的占位文本123"));
}

QTEST_GUILESS_MAIN(I18nTest)
#include "i18n_test.moc"
