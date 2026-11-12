#pragma once

#include <QPointF>
#include <QString>
#include <QVector>

/// 直线拟合的权重函数（对齐 VM「图形生成 → 直线」的三档：最小二乘 / huber / tukey）
enum class FitWeight {
    LeastSquares,   ///< 普通最小二乘（总最小二乘，单次求解）
    Huber,          ///< Huber 稳健权重（IRLS，按 MAD 定尺度）
    Tukey           ///< Tukey 双权重（IRLS，强离群点权重归零）
};

/// 直线拟合参数（与节点参数一一对应；默认值保持原有行为）
struct FitLineParams {
    FitWeight weight = FitWeight::LeastSquares;
    int maxIterations = 100;        ///< Huber/Tukey 的 IRLS 迭代上限（最小二乘不用）
    int rejectPoints = 0;           ///< 最多剔除的离群点数（0 = 不剔除）
    double rejectDistance = 0.0;    ///< 剔除距离阈值 px（<=0 = 不剔除）
    bool angleConstrained = false;  ///< 是否启用直线角度约束
    double expectedAngleDeg = 0.0;  ///< 期望角度（度）
    double angleToleranceDeg = 10.0;///< 角度容忍范围（度）
};

/// 直线拟合结果（坐标一律为调用方坐标系，纯数据面不做像素取整）
struct FitLineResult {
    bool valid = false;
    double angleDeg = 0.0;   ///< 方向角（-90, 90]，与 atan2(vy, vx) 同口径
    double x0 = 0.0;         ///< 直线过点
    double y0 = 0.0;
    double vx = 1.0;         ///< 单位方向向量
    double vy = 0.0;
    int usedPoints = 0;      ///< 实际参与最终拟合的点数
    int rejectedPoints = 0;  ///< 被剔除的离群点数
    /// 与**输入点一一对应**的匹配掩码（VM「模块结果 → 匹配点」口径）：
    /// 1 = 参与最终拟合，0 = 未参与（非有限点或被剔除）。
    QVector<bool> usedMask;
    QString failureReason;   ///< valid=false 时的可读原因
};

/// 稳健直线拟合（纯函数面，便于单测；不依赖 OpenCV / HALCON）
///
/// 语义：
/// - LeastSquares 用一次总最小二乘（协方差主方向），与 cv::fitLine(DIST_L2) 等价；
/// - Huber / Tukey 以 MAD 定尺度做 IRLS，最大 maxIterations 次，权重收敛即停；
/// - rejectPoints>0 且 rejectDistance>0 时，按初拟合残差剔除最多 rejectPoints 个
///   距离超过阈值的点后重拟合（剔除只做一轮，避免来回震荡）；
/// - angleConstrained 时校验拟合角与期望角的最小差（0..90°），超差即失败并给出原因。
class FitRobust final
{
public:
    /// 归一化到 [0, 180)：直线无方向，0° 与 180° 等价
    static double normalizeLineAngle(double angleDeg);

    /// 两条直线夹角的最小差（0..90°）
    static double lineAngleDifference(double angleA, double angleB);

    /// 稳健拟合；点数不足 / 点集退化 / 角度超差均返回 valid=false 且带原因
    static FitLineResult fitLine(const QVector<QPointF> &points, const FitLineParams &params);
};
