#include "RoundVariableFeed.h"

#include "VariablePanel.h"

// 轮变量供给面（切片 B 变量族首抽）：变量域两出口纯装配（上报 payload 与引用菜单）。
//  · payload 键法窗侧原形："global.名"键与"模块号.参数名"键（机器形态，无 braces）；
//  · 引用文本走 VariablePanel 双静态（{模块号.参数名} 与 {global.名}），终 sort 兜序。

QVariantMap RoundVariableFeed::roundPayload(
    const QMap<QString, GlobalVariableManager::Variable> &globals,
    const QHash<int, QVariantMap> &moduleVars)
{
    QVariantMap payload;
    for (auto it = globals.constBegin(); it != globals.constEnd(); ++it)
        payload.insert(QStringLiteral("global.%1").arg(it.key()), it.value().value);
    for (auto it = moduleVars.constBegin(); it != moduleVars.constEnd(); ++it) {
        const QVariantMap &vars = it.value();
        for (auto jt = vars.constBegin(); jt != vars.constEnd(); ++jt)
            payload.insert(QStringLiteral("%1.%2").arg(it.key()).arg(jt.key()), jt.value());
    }
    return payload;
}

QStringList RoundVariableFeed::roundReferenceTexts(
    const QMap<QString, GlobalVariableManager::Variable> &globals,
    const QHash<int, QVariantMap> &moduleVars)
{
    QStringList refs;
    for (auto it = moduleVars.constBegin(); it != moduleVars.constEnd(); ++it) {
        const QStringList keys = it.value().keys();
        for (const QString &k : keys)
            refs << VariablePanel::moduleRefText(it.key(), k);
    }
    for (auto it = globals.cbegin(); it != globals.cend(); ++it)
        refs << VariablePanel::globalRefText(it.key());
    refs.sort();   // 菜单顺序稳定（原体口径：便于按位置快速选择）
    return refs;
}
