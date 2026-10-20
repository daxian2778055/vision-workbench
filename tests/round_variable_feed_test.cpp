// RoundVariableFeedTest（词条切片 B 变量族首抽）：变量域两出口装配的纯核座。
// 两源两类全部以普通容器直给（无单例/无线程/无价面）；引用文本与 VariablePanel 双静态
// 同源（菜单显示文本原貌在案）。不进 MainWindow 与通信层。
// 红臂破坏位 = payload 全局键前缀 "global." 收空（裸名键）——全局注入腿当场红，
// 模块注入腿与引用文本两腿臂内原样绿。
#include <QHash>
#include <QMap>
#include <QStringList>
#include <QVariant>
#include <QtTest>

#include "RoundVariableFeed.h"

namespace {

GlobalVariableManager::Variable makeGlobalVar(const QString &name, const QVariant &value)
{
    GlobalVariableManager::Variable var;
    var.name = name;
    var.type = GlobalVariableManager::StringType;
    var.value = value;
    var.description = QStringLiteral("seat");
    return var;
}

}   // namespace

class RoundVariableFeedTest : public QObject
{
    Q_OBJECT

private slots:
    void testPayloadInjectsGlobals();       // 上报载荷：global.名 键（红臂靶腿）
    void testPayloadInjectsModuleVars();    // 上报载荷：模块号.参数名 键
    void testReferenceTextsBracedSorted();  // 引用菜单：braces 文本与终排序兜序
    void testEmptySourcesProduceEmpty();    // 空源 两出口皆空
};

void RoundVariableFeedTest::testPayloadInjectsGlobals()
{
    QMap<QString, GlobalVariableManager::Variable> globals;
    globals.insert(QStringLiteral("温度"), makeGlobalVar(QStringLiteral("温度"), 23.5));
    globals.insert(QStringLiteral("在位"), makeGlobalVar(QStringLiteral("在位"), true));

    const QVariantMap payload = RoundVariableFeed::roundPayload(globals, QHash<int, QVariantMap>());

    QCOMPARE(payload.keys(), QStringList({ QStringLiteral("global.在位"), QStringLiteral("global.温度") }));
    QCOMPARE(payload.value(QStringLiteral("global.温度")), QVariant(23.5));
    QCOMPARE(payload.value(QStringLiteral("global.在位")), QVariant(true));
}

void RoundVariableFeedTest::testPayloadInjectsModuleVars()
{
    QHash<int, QVariantMap> moduleVars;
    moduleVars.insert(12, QVariantMap{
        { QStringLiteral("半径"), 5.0 },
        { QStringLiteral("OK率"), 0.98 },
    });
    moduleVars.insert(7, QVariantMap{
        { QStringLiteral("裕度"), 2.25 },
    });

    const QVariantMap payload = RoundVariableFeed::roundPayload(QMap<QString, GlobalVariableManager::Variable>(), moduleVars);

    QCOMPARE(payload.keys(), QStringList({ QStringLiteral("12.OK率"), QStringLiteral("12.半径"), QStringLiteral("7.裕度") }));
    QCOMPARE(payload.value(QStringLiteral("12.半径")), QVariant(5.0));
    QCOMPARE(payload.value(QStringLiteral("12.OK率")), QVariant(0.98));
    QCOMPARE(payload.value(QStringLiteral("7.裕度")), QVariant(2.25));
}

void RoundVariableFeedTest::testReferenceTextsBracedSorted()
{
    QMap<QString, GlobalVariableManager::Variable> globals;
    globals.insert(QStringLiteral("在线判定"), makeGlobalVar(QStringLiteral("在线判定"), true));
    QHash<int, QVariantMap> moduleVars;
    moduleVars.insert(3, QVariantMap{
        { QStringLiteral("取图时长"), 12 },
    });

    // 模块输出文本与全局文本与终排兜序：'3'(0x33) < 'g'(0x67)，模块文本在前
    const QStringList refs = RoundVariableFeed::roundReferenceTexts(globals, moduleVars);
    QCOMPARE(refs, QStringList({ QStringLiteral("{3.取图时长}"), QStringLiteral("{global.在线判定}") }));
}

void RoundVariableFeedTest::testEmptySourcesProduceEmpty()
{
    QVERIFY(RoundVariableFeed::roundReferenceTexts(
                QMap<QString, GlobalVariableManager::Variable>(), QHash<int, QVariantMap>()).isEmpty());
    QVERIFY(RoundVariableFeed::roundPayload(
                QMap<QString, GlobalVariableManager::Variable>(), QHash<int, QVariantMap>()).isEmpty());
}

QTEST_MAIN(RoundVariableFeedTest)
#include "round_variable_feed_test.moc"
