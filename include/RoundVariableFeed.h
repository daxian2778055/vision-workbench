#pragma once

#include <QHash>
#include <QMap>
#include <QStringList>
#include <QVariant>

#include "GlobalVariableManager.h"   // Variable 结构（域型；随类而来的 QObject/QMutex 余重无碍）

/// 轮变量供给面（词条切片 B 变量族首抽，从 MainWindow 抽出）：
/// 本轮变量域两源（全局变量与模块输出缓存）→ 两出口纯装配。
///
/// 契约钉（与原体逐行同语义）：
///  · 纯变换不取数：两源由调用方直给（全局变量由单例取得后传入、模块输出缓存即窗侧
///    m_lastModuleVars），本面不碰单例线程与通信；
///  · payload 出口 = 机器键："global.名"（取 Value.value）与"模块号.参数名"（十进制号），
///    同键覆盖语义（QVariantMap insert）原样；发送事件文本模板以此把本轮结果格式化进报文；
///  · 引用出口 = 人读文本："模块号.参数名"的 braces 形经 VariablePanel 双静态（引用菜单
///    原貌，不另立字面），终排序兜序（菜单顺序稳定便于按位置选择）。
class RoundVariableFeed final {
public:
    /// 上报注入载荷（原 MainWindow::fireSendEventsForRound 中段）。
    static QVariantMap roundPayload(const QMap<QString, GlobalVariableManager::Variable> &globals,
                                    const QHash<int, QVariantMap> &moduleVars);

    /// 「变量引用」菜单文本列表（原 MainWindow::buildVariableReferences 本体）。
    static QStringList roundReferenceTexts(
        const QMap<QString, GlobalVariableManager::Variable> &globals,
        const QHash<int, QVariantMap> &moduleVars);
};
