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
    void tsGateCatchesSelfClosingAndMissingTranslations();   // 自闭合夹具牙
};

/// 译文表静态闸（纯函数，可对合成文本自测）。
/// 返回 problem 条目；checked 为**全部** <message> 数（不是"解析成功的条数"）——
/// 2026-10-25 复核：原实现只统计 regex 能匹配的条目，自闭合 <translation .../> 会被整条
/// 跳过（既不进 problems 也不进 checked），闸对它完全失明。
static void checkTsText(const QString &ts, QStringList &problems, int &messageCount,
                        int &ignoredCount)
{
    auto hasCjk = [](const QString &s) {
        for (const QChar &ch : s) {
            const ushort u = ch.unicode();
            if ((u >= 0x4E00 && u <= 0x9FFF) || (u >= 0x3400 && u <= 0x4DBF))
                return true;
        }
        return false;
    };

    problems.clear();
    messageCount = 0;
    ignoredCount = 0;
    // 消息标签允许带属性（2026-10-25 复核两轮）：
    //   · 第一轮：原只认精确 <message>，<message numerus="yes"> 既不进检查也不进计数；
    //   · 第二轮：计数正则 <message(?:\s[^>]*)?> 又漏了**无空格自闭合** <message/>。
    // 现在：计数用 <message\b[^>]*?>（含自闭合），正文用"必须有成对 </message>"的写法——
    // 自闭合条目会被计进总数却进不了正文检查 ⇒ 计数不等判红（这是机制，不是"空 body"）。
    const QRegularExpression msgRx(QStringLiteral("<message\\b[^>]*?>(.*?)</message>"),
                                   QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpression srcRx(QStringLiteral("<source>(.*?)</source>"),
                                   QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpression trRx(
        QStringLiteral("<translation(?:\\s[^>]*)?>(.*?)</translation>"),
        QRegularExpression::DotMatchesEverythingOption);
    for (auto it = msgRx.globalMatch(ts); it.hasNext();) {
        const QString body = it.next().captured(1);
        ++messageCount;
        const auto srcM = srcRx.match(body);
        const auto trM = trRx.match(body);
        // 一条 <message> 必须同时有 <source> 与**成对**的 <translation>；缺任一都判红，
        // 否则自闭合/畸形标签会让整条从检查里消失（2026-10-25 实测：自闭合形式改坏后仍绿）。
        if (!srcM.hasMatch()) {
            problems << QStringLiteral("message without <source>");
            continue;
        }
        const QString src = srcM.captured(1);
        const QString label = src.left(28).replace(QLatin1Char('\n'), QLatin1Char(' '));
        if (!trM.hasMatch()) {
            problems << QStringLiteral("missing/closed <translation>: %1").arg(label);
            continue;
        }
        // 已废弃条目（vanished/obsolete）不参与"活动译文"的判据（2026-10-25 复核）：
        // 它们的译文本就会随源码淘汰而陈旧，把空/等值判红是误报。只保证标签成对即可。
        // 判定看 **<translation> 标签的 type 属性**，不是对整个 message body 做子串匹配——
        // 源文里若出现 'type="vanished"' 字样会被整条误认成废弃条目（2026-10-25 复核第二轮）。
        const QRegularExpression trTagRx(QStringLiteral("<translation([^>]*)>"));
        const auto trTag = trTagRx.match(body);
        const QString trAttrs = trTag.hasMatch() ? trTag.captured(1) : QString();
        if (trAttrs.contains(QLatin1String("vanished")) || trAttrs.contains(QLatin1String("obsolete"))) {
            ++ignoredCount;
            continue;
        }
        const QString en = trM.captured(1);
        if (body.contains(QStringLiteral("type=\"unfinished\"")))
            problems << QStringLiteral("unfinished: %1").arg(label);
        else if (en.trimmed().isEmpty())
            problems << QStringLiteral("empty: %1").arg(label);
        else if (hasCjk(src) && en == src)
            problems << QStringLiteral("mirrored Chinese source: %1").arg(label);
    }
}

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

void I18nTest::tsHasNoUnfinishedEmptyOrMirrorTranslations()
{
#ifndef VFP_TS_PATH
#error "VFP_TS_PATH must be defined by CMake"
#endif
    QFile f(QString::fromUtf8(VFP_TS_PATH));
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("\u6253\u4e0d\u5f00 .ts\uff1a%1").arg(f.fileName())));
    const QString ts = QString::fromUtf8(f.readAll());

    QStringList problems;
    int messageCount = 0;
    int ignoredCount = 0;
    checkTsText(ts, problems, messageCount, ignoredCount);

    // 每条 <message> 都必须被检查过：进检查的条数 == 文本里的 <message> 总数，
    // 而不是只看"总数大于某个阈值"（2026-10-25 复核：原 checked>200 防不住单条被跳过）
    int rawMessageCount = 0;
    for (auto it = QRegularExpression(QStringLiteral("<message\\b[^>]*?>")).globalMatch(ts);
         it.hasNext();) {
        it.next();
        ++rawMessageCount;
    }
    QCOMPARE(messageCount, rawMessageCount);
    QVERIFY2(messageCount > 200, "条目数异常偏低，解析可能退化");
    // ignoredCount 必须与文本里真实的废弃标签数对账（2026-10-25 复核第二轮：原实现算了没人看，
    // 空 vanished 注入后条数变了也照样绿）。对不上就是"有条目被悄悄跳过/误判"。
    int rawIgnored = 0;
    for (auto it = QRegularExpression(QStringLiteral("<translation\\s+type=\"(?:vanished|obsolete)\""))
                     .globalMatch(ts);
         it.hasNext();) {
        it.next();
        ++rawIgnored;
    }
    QCOMPARE(ignoredCount, rawIgnored);
    qInfo("译文表：%d 条活动条目受检，%d 条已废弃条目豁免", messageCount - ignoredCount, ignoredCount);
    QVERIFY2(problems.isEmpty(),
             qPrintable(QStringLiteral("\u8bd1\u6587\u8868\u6709 %1 \u5904\u95ee\u9898\uff1a%2")
                            .arg(problems.size())
                            .arg(problems.join(QStringLiteral(" | ")))));
}

// 自闭合/畸形夹具牙（2026-10-25 复核）：改坏复跑实测「闭标签形式能红、自闭合形式仍绿」——
// 原 regex 只认成对闭标签，<translation .../> 会让整条从检查里消失。这里用合成文本把各种
// 畸形钉住，闸再漏就是这条腿先红。
void I18nTest::tsGateCatchesSelfClosingAndMissingTranslations()
{
    QStringList problems;
    int n = 0;
    int ignored = 0;

    // ① 自闭合 unfinished：必须判红（正是上次漏掉的那种）
    checkTsText(QStringLiteral("<message><source>\u6d4b\u8bd5\u4e32</source>"
                               "<translation type=\"unfinished\"/></message>"),
                problems, n, ignored);
    QCOMPARE(n, 1);
    QVERIFY2(!problems.isEmpty(), "自闭合 <translation/> 没被识别为问题（闸有盲区）");

    // ② 缺 <translation> 标签：必须判红
    checkTsText(QStringLiteral("<message><source>\u6d4b\u8bd5\u4e32\u4e8c</source></message>"),
                problems, n, ignored);
    QCOMPARE(n, 1);
    QVERIFY2(!problems.isEmpty(), "缺失 <translation> 标签没被识别为问题");

    // ③ 空译文：必须判红
    checkTsText(QStringLiteral("<message><source>\u6d4b\u8bd5\u4e32\u4e09</source>"
                               "<translation></translation></message>"),
                problems, n, ignored);
    QCOMPARE(n, 1);
    QVERIFY2(!problems.isEmpty(), "空译文没被识别为问题");

    // ④ 中文源文译文等值：必须判红
    checkTsText(QStringLiteral("<message><source>\u6d4b\u8bd5\u4e32\u56db</source>"
                               "<translation>\u6d4b\u8bd5\u4e32\u56db</translation></message>"),
                problems, n, ignored);
    QCOMPARE(n, 1);
    QVERIFY2(!problems.isEmpty(), "中文源文译文等值没被识别为问题");

    // ⑤ 带属性的消息（lupdate 的 numerus 形态）：必须**被检查到**——此前正则只认裸 <message>，
    // 这类条目既不进检查也不进计数，"计数相等"照样绿（2026-10-25 复核）
    checkTsText(QStringLiteral("<message numerus=\"yes\"><source>\u6d4b\u8bd5\u5c5e\u6027</source>"
                               "<translation type=\"unfinished\"/></message>"),
                problems, n, ignored);
    QCOMPARE(n, 1);
    QVERIFY2(!problems.isEmpty(), "带属性的 <message numerus=\"yes\"> 没被检查到（闸仍失明）");

    // ⑥ 已废弃（vanished/obsolete）条目豁免空/等值判据：不得误报
    checkTsText(QStringLiteral("<message><source>\u5e9f\u5f03\u4e00</source>"
                               "<translation type=\"vanished\"></translation></message>"
                               "<message><source>\u5e9f\u5f03\u4e8c</source>"
                               "<translation type=\"obsolete\">\u5e9f\u5f03\u4e8c</translation></message>"),
                problems, n, ignored);
    QCOMPARE(n, 2);
    QCOMPARE(ignored, 2);
    QVERIFY2(problems.isEmpty(),
             qPrintable(QStringLiteral("\u5df2\u5e9f\u5f03\u6761\u76ee\u88ab\u8bef\u5224\uff1a%1")
                            .arg(problems.join(QStringLiteral(" | ")))));

    // ⑥b 无空格自闭合 <message/>：计数正则认它、正文正则不认 ⇒ 本函数 messageCount=0，
    // 而主槽的"进检查条数 == <message 总数>"会不等 ⇒ 判红（机制是计数差，不是"空 body"）
    checkTsText(QStringLiteral("<message/>"),
                problems, n, ignored);
    QCOMPARE(n, 0);
    QVERIFY(problems.isEmpty());   // 本函数按条报问题；这条的红由计数差负责
    {
        int raw = 0;
        for (auto it = QRegularExpression(QStringLiteral("<message\\b[^>]*?>"))
                         .globalMatch(QStringLiteral("<message/>"));
             it.hasNext();) {
            it.next();
            ++raw;
        }
        QCOMPARE(raw, 1);   // 计数认它 ⇒ 主槽里 messageCount(0) != raw(1) ⇒ 红
    }

    // ⑥c 源文里出现 type="vanished" 字样不得整条误判为废弃：译文为空仍要判红
    checkTsText(QStringLiteral("<message><source>type=\"vanished\" 字样</source>"
                               "<translation></translation></message>"),
                problems, n, ignored);
    QCOMPARE(n, 1);
    QCOMPARE(ignored, 0);
    QVERIFY2(!problems.isEmpty(),
             "源文含 type=vanished 字样导致整条被误判为废弃（空译文逃过检查）");

    // ⑦ 合法条目（英文源文等值 + 中文有译文）：必须全绿，防止闸误伤
    checkTsText(QStringLiteral("<message><source>Vision Flow Platform</source>"
                               "<translation>Vision Flow Platform</translation></message>"
                               "<message><source>\u6d4b\u8bd5\u4e32\u4e94</source>"
                               "<translation>Test Five</translation></message>"),
                problems, n, ignored);
    QCOMPARE(n, 2);
    QVERIFY2(problems.isEmpty(),
             qPrintable(QStringLiteral("\u5408\u6cd5\u6761\u76ee\u88ab\u8bef\u5224\uff1a%1")
                            .arg(problems.join(QStringLiteral(" | ")))));
}

QTEST_GUILESS_MAIN(I18nTest)
#include "i18n_test.moc"
