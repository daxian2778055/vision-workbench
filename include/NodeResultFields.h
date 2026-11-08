#pragma once

#include <QString>
#include <QStringList>
#include <QLatin1String>
#include <QVector>

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
/// 依据（2026-11-08 重测的写侧站点表，共 228 处）：lastError 149 / *Note 45 / trainStatus 34。
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

/// 「当前结果」面板的单字段展示口径（对标 VM 4.4「模块结果」）。
///
/// VM 的结果区只列**结果字段**：配置参数、临时键都不出现在结果表里；字段顺序、名称、
/// 单位、精度以官方「模块结果」表为准（例：卡尺见 docs/VM4.4卡尺工具对齐规范.md §三）。
/// 算子通过 NodeBase::resultDisplayFields() 返回本列表，ResultTablePanel 按顺序渲染；
/// 未声明的键（除判红原因外）一律不显示。
struct NodeResultField
{
    enum Kind {
        Value,         ///< 模块级单值：数值按 decimals 格式化；bool 显示 true/false
        Status,        ///< 模块级状态：true=OK（绿）/ false=NG（红）
        ListItem,      ///< 该键是逗号分隔数组；第 i 项进「group%1」分组
        PolarityItem,  ///< 同 ListItem，元素为官方极性编号（1=从黑到白 / 2=从白到黑 / 3=任意）
        PointItem,     ///< 该键是 "col,row;col,row" 数组；元素渲染成「X=…, Y=…」
        PairLineItem,  ///< 该键是 "c1,r1,c2,r2,w;…" 数组；元素渲染成起点→终点+宽度
    };

    QString key;        ///< 参数表里的结果键
    QString label;      ///< 显示名（分组内为子项名）
    QString group;      ///< 分组标签，含 %1（从 0 起的序号）；空 = 模块级单值
    QString unit;       ///< 单位（空 = 不显示），渲染成「标签(单位)」
    int decimals = -1;  ///< 小数位；-1 = 原样/自动
    Kind kind = Value;
};

/// 模块级单值（数值/文本）
inline NodeResultField resultValue(const QString &key, const QString &label,
                                   const QString &unit = QString(), int decimals = -1)
{
    NodeResultField f;
    f.key = key; f.label = label; f.unit = unit; f.decimals = decimals;
    return f;
}

/// 模块状态（OK/NG）
inline NodeResultField resultStatus(const QString &key, const QString &label)
{
    NodeResultField f;
    f.key = key; f.label = label; f.kind = NodeResultField::Status;
    return f;
}

/// 逐结果分组字段（group 里的 %1 由面板替换成从 0 起的序号）
inline NodeResultField resultList(const QString &key, const QString &group,
                                  const QString &label, const QString &unit = QString(),
                                  int decimals = -1)
{
    NodeResultField f;
    f.key = key; f.group = group; f.label = label; f.unit = unit;
    f.decimals = decimals; f.kind = NodeResultField::ListItem;
    return f;
}

/// 逐结果极性（1=从黑到白 / 2=从白到黑 / 3=任意极性）
inline NodeResultField resultPolarity(const QString &key, const QString &group,
                                      const QString &label)
{
    NodeResultField f;
    f.key = key; f.group = group; f.label = label; f.kind = NodeResultField::PolarityItem;
    return f;
}

/// 逐结果边缘点（"col,row" 数组）
inline NodeResultField resultPoint(const QString &key, const QString &group,
                                   const QString &label, int decimals = 2)
{
    NodeResultField f;
    f.key = key; f.group = group; f.label = label; f.decimals = decimals;
    f.kind = NodeResultField::PointItem;
    return f;
}

/// 逐结果直线（"col1,row1,col2,row2,width" 数组，边对模式）
inline NodeResultField resultPairLine(const QString &key, const QString &group,
                                      const QString &label, int decimals = 2)
{
    NodeResultField f;
    f.key = key; f.group = group; f.label = label; f.decimals = decimals;
    f.kind = NodeResultField::PairLineItem;
    return f;
}
