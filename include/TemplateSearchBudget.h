#pragma once

#include <QString>
#include <QtGlobal>
#include <cmath>

/// 模板匹配搜索预算（r19 训练向导 ③/4：范围体检）。
///
/// 存在理由：现场"为什么这么慢"多半是角度×尺度组合数太大（−30…30/2° = 31 组，
/// 再叠加 5 档尺度就是 155 次匹配）。把组合数与预计耗时算给用户看，比让他猜要好。
///
/// 系数来源：仓库基准腿（1500×1000 + 208px 模板 + 31 角度）实测约 130ms，
/// 即**每个角度/尺度组合约 4.2ms**（粗到细之后）。这是估算，不是承诺——
/// 模板更大、图更大时会偏乐观，所以对外表述写成"约"。
namespace TemplateSearchBudget {

/// 闭区间按步进取值的个数（含端点；步进 <= 0 视为 1 个）
inline int stepsInRange(double lo, double hi, double step)
{
    if (!(hi > lo))
        return 1;
    if (!(step > 0.0))
        return 1;
    return int(std::floor((hi - lo) / step + 1e-9)) + 1;
}

/// 角度 × 尺度的组合数
inline int comboCount(double angleMin, double angleMax, double angleStep,
                      double scaleMin, double scaleMax, double scaleStep)
{
    return stepsInRange(angleMin, angleMax, angleStep)
           * stepsInRange(scaleMin, scaleMax, scaleStep);
}

/// 预计单帧耗时（毫秒，粗到细之后的经验值）
inline double estimateMs(int combos, double msPerCombo = 4.2)
{
    return qMax(0, combos) * msPerCombo;
}

/// 给用户的一句话（组合数、预计耗时、是否偏多）
inline QString describe(int combos, double msPerCombo = 4.2)
{
    const double ms = estimateMs(combos, msPerCombo);
    QString verdict;
    if (combos <= 40)
        verdict = QStringLiteral("组合数正常");
    else if (combos <= 160)
        verdict = QStringLiteral("组合数偏多，建议缩小角度或尺度范围");
    else
        verdict = QStringLiteral("组合数过多，现场会明显卡顿，务必缩小范围");
    return QStringLiteral("角度×尺度 = %1 组，预计单帧约 %2 ms（%3）")
        .arg(combos)
        .arg(ms, 0, 'f', 0)
        .arg(verdict);
}

}   // namespace TemplateSearchBudget
