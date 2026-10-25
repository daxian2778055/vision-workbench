// G-P1-5 国际化：端到端验证英文翻译文件（visionflow_en.qm）可被加载，
// 且关键源文本（菜单标题 / 运行控制）翻译正确。证明 tr() → .ts → .qm 管线闭环。
#include <QObject>
#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QTranslator>
#include <QString>
#include <QtTest/QtTest>

class I18nTest : public QObject {
    Q_OBJECT

private slots:
    void englishQmLoadsAndTranslates();
    void tsHasNoUnfinishedEmptyOrMirrorTranslations();
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

    // 脚本确认开关（2026-10-25 复核：新增的 6 条串此前整条缺失，英文界面下仍是中文）
    QCOMPARE(QCoreApplication::translate("MainWindow", "关闭脚本执行前确认"),
             QStringLiteral("Turn off script confirmation"));
    QCOMPARE(QCoreApplication::translate("MainWindow", "仍要关闭"),
             QStringLiteral("Turn off anyway"));
    // 这一条在 ProjectManager 上下文（同源文在别的上下文另有条目，别按 MainWindow 查）
    QCOMPARE(QCoreApplication::translate("ProjectManager", "写操作已被禁止"),
             QStringLiteral("Write operations are blocked"));

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

    // 脚本执行前确认相关的 6 条全钉住（2026-10-25 复核：这批此前整条没进 .ts）
    QCOMPARE(QCoreApplication::translate("MainWindow", "关闭脚本执行前确认"),
             QStringLiteral("Turn off script confirmation"));
    QCOMPARE(QCoreApplication::translate("MainWindow", "仍要关闭"),
             QStringLiteral("Turn off anyway"));
    QCOMPARE(QCoreApplication::translate(
                 "MainWindow",
                 "关闭后，方案里的脚本将以当前用户权限直接执行，不再弹窗确认。\n"
                 "该设置按 Windows 账户保存，对本账户下的全部登录会话生效。\n\n确定要关闭吗？"),
             QStringLiteral("Once disabled, scripts in the scheme run directly with the "
                            "current user's privileges, without confirmation.\nThe setting is "
                            "stored per Windows account and applies to all sign-in sessions of "
                            "this account.\n\nTurn it off?"));
    QCOMPARE(QCoreApplication::translate(
                 "MainWindow",
                 "脚本执行前确认：已关闭——脚本将以当前用户权限直接执行"),
             QStringLiteral("Script confirmation: OFF - scripts will run directly with the "
                            "current user's privileges"));
    QCOMPARE(QCoreApplication::translate(
                 "MainWindow",
                 "脚本执行前确认：已开启（同一脚本每次启动后首次执行前确认一次）"),
             QStringLiteral("Script confirmation: ON (a given script is confirmed once per "
                            "app start)"));
    QCOMPARE(QCoreApplication::translate(
                 "MainWindow",
                 "注意：脚本执行前确认处于关闭状态（脚本将直接运行）——"
                 "可在「系统 → 脚本安全：执行前确认脚本」恢复"),
             QStringLiteral("Warning: script confirmation is OFF (scripts run directly) - "
                            "restore it under System -> Script safety: confirm scripts before "
                            "running"));

    // 未翻译的源文（如本测试未抽取到的字符串）应保持源文，不影响中文模式
    QCOMPARE(QCoreApplication::translate("MainWindow", "未抽取的占位文本123"),
             QStringLiteral("未抽取的占位文本123"));
}

// 静态闸（2026-10-25 复核：39 条译文此前只有 3 条运行期腿，lupdate 新增未翻串没人报红）。
// .ts 里不得出现：type="unfinished"、空译文、或**中文源文**的译文等于源文。
// 英文源文等值是允许的（"New"/"Save"/"Vision Flow Platform" 这类本来就是英文）。
void I18nTest::tsHasNoUnfinishedEmptyOrMirrorTranslations()
{
#ifndef VFP_TS_PATH
#error "VFP_TS_PATH must be defined by CMake"
#endif
    QFile f(QString::fromUtf8(VFP_TS_PATH));
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("打不开 .ts：%1").arg(f.fileName())));
    const QString ts = QString::fromUtf8(f.readAll());

    auto hasCjk = [](const QString &s) {
        for (const QChar &ch : s) {
            const ushort u = ch.unicode();
            if ((u >= 0x4E00 && u <= 0x9FFF) || (u >= 0x3400 && u <= 0x4DBF))
                return true;
        }
        return false;
    };

    int checked = 0;
    QStringList problems;
    const QRegularExpression msgRx(QStringLiteral("<message>(.*?)</message>"),
                                   QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpression srcRx(QStringLiteral("<source>(.*?)</source>"),
                                   QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpression trRx(
        QStringLiteral("<translation(?:\\s[^>]*)?>(.*?)</translation>"),
        QRegularExpression::DotMatchesEverythingOption);
    for (auto it = msgRx.globalMatch(ts); it.hasNext();) {
        const QString body = it.next().captured(1);
        const auto srcM = srcRx.match(body);
        const auto trM = trRx.match(body);
        if (!srcM.hasMatch() || !trM.hasMatch())
            continue;
        ++checked;
        const QString src = srcM.captured(1);
        const QString en = trM.captured(1);
        const QString label = src.left(28).replace(QLatin1Char('\n'), QLatin1Char(' '));
        if (body.contains(QStringLiteral("type=\"unfinished\"")))
            problems << QStringLiteral("unfinished: %1").arg(label);
        else if (en.trimmed().isEmpty())
            problems << QStringLiteral("empty: %1").arg(label);
        else if (hasCjk(src) && en == src)
            problems << QStringLiteral("mirrored Chinese source: %1").arg(label);
    }
    QVERIFY(checked > 200);   // 防解析退化：条目数掉了要报红，不能"空跑判绿"
    QVERIFY2(problems.isEmpty(),
             qPrintable(QStringLiteral("译文表有 %1 处问题：%2")
                            .arg(problems.size())
                            .arg(problems.join(QStringLiteral(" | ")))));
}

QTEST_GUILESS_MAIN(I18nTest)
#include "i18n_test.moc"
