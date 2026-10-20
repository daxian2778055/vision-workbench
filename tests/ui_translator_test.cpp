// UITranslatorTest（词条切片 B 就地链首抽）：UI 文本表应用器的纯表面座。
// 目标用裸 QAction 与 QLabel（离屏构造）、设文回调全走窗侧实书；不进 MainWindow。
// 红臂破坏位 = 语言分派取反（apply 的三元两文互换）——双语文本往返腿当场红，
// 序偶腿与空表/空回调腿臂内原样绿。
#include <QAction>
#include <QLabel>
#include <QtTest/QtTest>

#include "UITranslator.h"

class UITranslatorTest : public QObject
{
    Q_OBJECT

private slots:
    void testApplyBothLanguagesRoundTrip();   // apply(Zh) 全中、apply(En) 全英、再回中（红臂靶腿）
    void testApplyOrderMatchesRegistration(); // 应用序 = 注册序（序偶性）
    void testApplyEmptyTableAndEmptySetter(); // 空表无事、空回调跳过、两文同文允许
};

void UITranslatorTest::testApplyBothLanguagesRoundTrip()
{
    QAction newAction;
    QAction saveAction;
    QLabel title;

    UITranslator table;
    table.add([&newAction](const QString &text) { newAction.setText(text); },
              QStringLiteral("新建方案"), QStringLiteral("New Scheme"));
    table.add([&saveAction](const QString &text) { saveAction.setText(text); },
              QStringLiteral("保存方案"), QStringLiteral("Save Scheme"));
    table.add([&title](const QString &text) { title.setText(text); },
              QStringLiteral("视觉方案工作台"), QStringLiteral("Workbench"));

    table.apply(UITranslator::Lang::Zh);
    QCOMPARE(newAction.text(), QStringLiteral("新建方案"));
    QCOMPARE(saveAction.text(), QStringLiteral("保存方案"));
    QCOMPARE(title.text(), QStringLiteral("视觉方案工作台"));

    table.apply(UITranslator::Lang::En);
    QCOMPARE(newAction.text(), QStringLiteral("New Scheme"));
    QCOMPARE(saveAction.text(), QStringLiteral("Save Scheme"));
    QCOMPARE(title.text(), QStringLiteral("Workbench"));

    table.apply(UITranslator::Lang::Zh);
    QCOMPARE(newAction.text(), QStringLiteral("新建方案"));
    QCOMPARE(saveAction.text(), QStringLiteral("保存方案"));
    QCOMPARE(title.text(), QStringLiteral("视觉方案工作台"));
}

void UITranslatorTest::testApplyOrderMatchesRegistration()
{
    QAction first;
    QAction second;
    QAction third;
    QVector<QString> applied;

    UITranslator table;
    table.add([&first, &applied](const QString &text) { first.setText(text); applied.append("action1"); },
              QStringLiteral("一中一"), QStringLiteral("action1-en"));
    table.add([&second, &applied](const QString &text) { second.setText(text); applied.append("action2"); },
              QStringLiteral("二中二"), QStringLiteral("action2-en"));
    table.add([&third, &applied](const QString &text) { third.setText(text); applied.append("action3"); },
              QStringLiteral("三中三"), QStringLiteral("action3-en"));

    table.apply(UITranslator::Lang::En);
    QCOMPARE(applied, QVector<QString>({ "action1", "action2", "action3" }));
    QCOMPARE(first.text(), QStringLiteral("action1-en"));
    QCOMPARE(third.text(), QStringLiteral("action3-en"));   // 序同文对
}

void UITranslatorTest::testApplyEmptyTableAndEmptySetter()
{
    QAction alias;

    UITranslator empty;
    empty.apply(UITranslator::Lang::Zh);   // 空表应用：无事不崩
    empty.apply(UITranslator::Lang::En);

    UITranslator table;
    table.add(UITranslator::Setter(), QStringLiteral("空回调"), QStringLiteral("empty"));   // 空回调跳过
    table.add([&alias](const QString &text) { alias.setText(text); },
              QStringLiteral("粘贴"), QStringLiteral("Paste"));

    table.apply(UITranslator::Lang::En);
    QCOMPARE(alias.text(), QStringLiteral("Paste"));       // 空回跳过、后条照常应用

    // 两文同文允许（别名对语义，表不去重）
    UITranslator twin;
    twin.add([&alias](const QString &text) { alias.setText(text); },
             QStringLiteral("Paste"), QStringLiteral("Paste"));
    twin.apply(UITranslator::Lang::Zh);
    QCOMPARE(alias.text(), QStringLiteral("Paste"));
}

QTEST_MAIN(UITranslatorTest)
#include "ui_translator_test.moc"
