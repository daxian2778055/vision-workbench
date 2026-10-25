// G-P1-5 国际化：端到端验证英文翻译文件（visionflow_en.qm）可被加载，
// 且关键源文本（菜单标题 / 运行控制）翻译正确。证明 tr() → .ts → .qm 管线闭环。
#include <QObject>
#include <QCoreApplication>
#include <QFile>
#include <QPair>
#include <QRegularExpression>
#include <QVector>
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
/// 返回 problem 条目；messageCount 为**成功配对**的 <message> 数。
/// 三轮复核（2026-10-25）：
///   · 第一轮：regex 成对匹配会整条跳过自闭合 <translation/>，计數相等照样绿；
///   · 第二轮：自闭合 <message/> 会"借"走后面好条目的 </message>，诊断点错对象；
///   · 第三轮：缺闭标签同样会借后面的好 </message>。
/// 现改为**栈式配对**：扫描所有 message 侧标签（开口/自闭合/闭标签），
/// 开口入栈、闭标签弹栈配对；自闭合直接点名；栈里剩下的开口逐个点名
/// "缺 </message>"。诊断由此始终指向真正畸形的那个标签。
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

    const QRegularExpression tagRx("<message\\b[^>]*?>|</message>");
    // DotMatchesEverything 必须开（2026-10-25 实测）：多行 <source>（含换行的
    // 提示/警告长文）此前因 . 不匹配换行而匹配失败，整条被当"无 <source>"跳过——
    // 真 .ts 里有 15 条这样的条目（含 1 条 vanished），未翻/空/镜像对它们全部失明。
    const QRegularExpression srcRx("<source>(.*?)</source>",
                                   QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpression trRx("<translation(?:\\s[^>]*)?>(.*?)</translation>", QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpression trTagRx("<translation([^>]*)>");

    QVector<QPair<int, int>> openStack;   // (tagStart, tagEnd)
    for (auto it = tagRx.globalMatch(ts); it.hasNext();) {
        const auto m = it.next();
        const QString tag = m.captured(0);
        const int pos = m.capturedStart(0);
        const int end = m.capturedEnd(0);

        if (tag.startsWith(QLatin1String("</"))) {
            if (openStack.isEmpty()) {
                problems << QStringLiteral("stray </message> at offset %1").arg(pos);
                continue;
            }
            const QPair<int, int> open = openStack.takeLast();
            ++messageCount;
            const QString body = ts.mid(open.second, pos - open.second);
            const auto srcM = srcRx.match(body);
            const auto trM = trRx.match(body);
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
            // 已废弃条目豁免空/等值判据；判定看 <translation> 的 type 属性，
            // 且 type 前须是行首或空白（防 data-type="…" 误中）。
            const auto trTag = trTagRx.match(body);
            const QString trAttrs = trTag.hasMatch() ? trTag.captured(1) : QString();
            static const QRegularExpression obsoleteRx("(?:^|\\s)type\\s*=\\s*\"(?:vanished|obsolete)\"");
            if (obsoleteRx.match(trAttrs).hasMatch()) {
                ++ignoredCount;
                continue;
            }
            const QString en = trM.captured(1);
            // unfinished 与 obsolete 同一判据：看 <translation> 标签的 type 属性（2026-10-25
            // 复核：body 子串会把源文/译文里出现该字样的正常条目误判成未翻）
            static const QRegularExpression unfinishedRx("(?:^|\\s)type\\s*=\\s*\"unfinished\"");
            if (unfinishedRx.match(trAttrs).hasMatch())
                problems << QStringLiteral("unfinished: %1").arg(label);
            else if (en.trimmed().isEmpty())
                problems << QStringLiteral("empty: %1").arg(label);
            else if (hasCjk(src) && en == src)
                problems << QStringLiteral("mirrored Chinese source: %1").arg(label);
        } else if (tag.endsWith(QLatin1String("/>"))) {
            problems << QStringLiteral("self-closing message tag: %1")
                            .arg(tag.left(40).replace(QLatin1Char('\n'), QLatin1Char(' ')));
        } else {
            openStack.append(qMakePair(pos, end));
        }
    }
    // 栈里剩的开口 = 缺闭标签：逐个点名（不再被"借闭标签"的好条目顶名）
    for (const QPair<int, int> &open : openStack) {
        problems << QStringLiteral("message tag missing </message>: %1")
                        .arg(ts.mid(open.first, 40).replace(QLatin1Char('\n'), QLatin1Char(' ')));
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
    // 计数不等时要把问题清单一起报出来（2026-10-25 复核：原来只报 273 vs 274，
    // 点名诊断被这行先红的断言盖住，现场仍指不到畸形标签）。
    QVERIFY2(messageCount == rawMessageCount,
             qPrintable(QStringLiteral("\u8fdb\u68c0\u67e5\u6761\u6578(%1) != <message> \u603b\u6578(%2)\uff1a%3")
                            .arg(messageCount)
                            .arg(rawMessageCount)
                            .arg(problems.join(QStringLiteral(" | ")))));
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
    // 诊断必须点名这个畸形标签（2026-10-25 复核：原来只有计数差，报错不指位置；
    // 同日第二轮：自闭合走"self-closing"专用诊断，缺闭标签才是"unpaired"）
    QVERIFY2(!problems.isEmpty()
                 && problems.first().contains(QLatin1String("self-closing")),
             qPrintable(QStringLiteral("自闭合 <message/> 没有产生点名诊断：%1")
                            .arg(problems.join(QStringLiteral(" | ")))));
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

    // ⑥d foo="vanished" 这类属性不得被当成废弃标记（2026-10-25 复核：属性子串判定会误认）
    checkTsText(QStringLiteral("<message><source>\u4f2a\u5e9f\u5f03</source>"
                               "<translation foo=\"vanished\"></translation></message>"),
                problems, n, ignored);
    QCOMPARE(n, 1);
    QCOMPARE(ignored, 0);
    QVERIFY2(!problems.isEmpty(),
             "foo=\"vanished\" 被误判成废弃标记，空译文逃过了检查");

    // ⑥e 中间自闭合：不得把后面那条**好**标签点成畸形（2026-10-25 复核实测的点错对象）
    checkTsText(QStringLiteral("<message><source>\u597d\u4e00</source>"
                               "<translation>Good One</translation></message>"
                               "<message/><message><source>\u597d\u4e8c</source>"
                               "<translation>Good Two</translation></message>"),
                problems, n, ignored);
    for (const QString &pr : problems) {
        QVERIFY2(!pr.contains(QLatin1String("Good Two")),
                 qPrintable(QStringLiteral("\u8bca\u65ad\u70b9\u9519\u4e86\u597d\u6807\u7b7e\uff1a%1")
                                .arg(pr)));
    }
    QVERIFY2(!problems.isEmpty()
                 && problems.first().contains(QLatin1String("self-closing")),
             "中间自闭合没有被点名");

    // ⑥f data-type=\"vanished\" 不是废弃标记（\btype 会命中它）：空译文仍要判红
    checkTsText(QStringLiteral("<message><source>data-type \u5b57\u6837</source>"
                               "<translation data-type=\"vanished\"></translation></message>"),
                problems, n, ignored);
    QCOMPARE(n, 1);
    QCOMPARE(ignored, 0);
    QVERIFY2(!problems.isEmpty(),
             "data-type=vanished 被误判成废弃标记，空译文逃过了检查");

    // ⑥g 中间缺闭标签：诊断必须点名**缺闭标签那条**，不得把后面好条目报成 unpaired
    checkTsText(QStringLiteral("<message><source>\u7f3a\u95ed\u6807\u7b7e</source>"
                               "<translation>Missing</translation>"
                               "<message><source>GoodSix</source>"
                               "<translation>Good Six</translation></message>"),
                problems, n, ignored);
    QVERIFY2(!problems.isEmpty(), "中间缺闭标签没有产生诊断");
    {
        bool namesMissing = false;
        bool blamesGood = false;
        for (const QString &pr : problems) {
            if (pr.contains(QLatin1String("missing </message>")))
                namesMissing = true;
            if (pr.contains(QLatin1String("GoodSix")))
                blamesGood = true;
        }
        QVERIFY2(namesMissing, "缺闭标签没有被点名（诊断缺失）");
        QVERIFY2(!blamesGood, qPrintable(QStringLiteral("\u8bca\u65ad\u70b9\u9519\u4e86\u597d\u6761\u76ee GoodSix\uff1a%1")
                                             .arg(problems.join(QStringLiteral(" | ")))));
    }

    // ⑥i 源文/译文里出现 type="unfinished" 字样：不得把正常条目误判成未翻（2026-10-25 复核）
    checkTsText(QStringLiteral("<message><source>type=\"unfinished\" \u5b57\u6837</source>"
                               "<translation>Normal Translation</translation></message>"),
                problems, n, ignored);
    QCOMPARE(n, 1);
    QVERIFY2(problems.isEmpty(),
             qPrintable(QStringLiteral("源文含 type=unfinished 字样导致正常条目被误判：%1")
                            .arg(problems.join(QStringLiteral(" | ")))));
    // ⑥h 多行源文（含换行）：同样要被检查（2026-10-25 实测的更深盲区）
    checkTsText(QStringLiteral("<message><source>\u591a\u884c\u6e90\u6587\n\u7b2c\u4e8c\u884c</source>"
                               "<translation></translation></message>"),
                problems, n, ignored);
    QCOMPARE(n, 1);
    QVERIFY2(!problems.isEmpty(), "多行源文条目被整条跳过（空译文逃过检查）");

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
