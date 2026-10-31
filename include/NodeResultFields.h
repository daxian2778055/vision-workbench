#pragma once

#include <QString>
#include <QLatin1String>

/// 算子「判红原因」字段的唯一口径（U-18）。
///
/// 背景：算子判红时输出端口会被 HalconNode 的失败分支整体清空，原因只能留在参数表里的
/// 结果字段（发布侧见 FlowExecutor 的失败轮快照，显示侧见 ResultTablePanel 的失败行）。
/// 判定必须**单点定义**：发布侧（挑哪些键推给面板）与作废侧（每轮开始清掉上一轮的原因）
/// 用的是同一个谓词，两处各写一遍就会漏项。
///
/// 未知异常（非 std::exception）的统一原因串（2026-10-26）：`catch (...)` 拿不到 what()，
/// 但"红了没有原因"同样不可接受。集中一处，避免二十个节点各写一遍（中文重复闸也会拦）。
inline QString unknownExceptionReason()
{
    return QStringLiteral("未知异常（非 std::exception），详见日志");
}

/// 规则：键 == "lastError" 或以 "Note" 结尾。
/// 依据（2026-10-28 重测的写侧站点表，共 198 处）：lastError 121 / *Note 43 / trainStatus 34。
/// 数字一律以 `python tools/reason_key_sites.py` 的输出为准（`--check` 会与本注释比对，
/// 不一致退出 1）——早先写"一次性脚本、不入库"导致数值无法复现，已改成入库脚本。
/// 旧记（原文照抄）：共 84 处：lastError 50 / calibNote 24 / matchNote 4 /
/// transformNote 3 / correctNote 3；这 5 个键没有一个被声明成参数面板可配项
/// （makeXxxParam / registerParams 里 0 命中）⇒ 不会把用户配置值当成失败原因。
/// 前缀/后缀规则而非手工清单，同 CommunicationManager 的 "last" 前缀保留键判定（先例）。
///
/// 已知边界（不在本口径内，本轮不覆盖）：trainStatus / baselineStatus 这类「状态兼原因」
/// 的混合键——它们既被写也参与状态判断，按名字收进来会把状态位当原因推给面板。
inline bool isNodeReasonKey(const QString &key)
{
    return key == QLatin1String("lastError") || key.endsWith(QLatin1String("Note"));
}
