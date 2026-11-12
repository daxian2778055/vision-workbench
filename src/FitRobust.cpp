#include "FitRobust.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

bool isFinitePoint(const QPointF &p)
{
    return std::isfinite(p.x()) && std::isfinite(p.y());
}

/// 加权总最小二乘：返回单位方向向量与加权重心；点集退化（所有点重合）返回 false
bool weightedTotalLeastSquares(const QVector<QPointF> &points, const QVector<double> &weights,
                               double *vx, double *vy, double *x0, double *y0)
{
    double sumW = 0.0;
    double sumX = 0.0;
    double sumY = 0.0;
    for (int i = 0; i < points.size(); ++i) {
        const double w = weights.at(i);
        if (!(w > 0.0))
            continue;
        sumW += w;
        sumX += w * points.at(i).x();
        sumY += w * points.at(i).y();
    }
    if (!(sumW > 0.0))
        return false;

    const double cx = sumX / sumW;
    const double cy = sumY / sumW;
    double sxx = 0.0;
    double syy = 0.0;
    double sxy = 0.0;
    for (int i = 0; i < points.size(); ++i) {
        const double w = weights.at(i);
        if (!(w > 0.0))
            continue;
        const double dx = points.at(i).x() - cx;
        const double dy = points.at(i).y() - cy;
        sxx += w * dx * dx;
        syy += w * dy * dy;
        sxy += w * dx * dy;
    }
    // 二阶矩全为 0：所有参与点重合，方向不可定
    if (sxx + syy <= 1e-18)
        return false;

    // 主方向：2×2 协方差矩阵最大特征值对应的特征向量，闭式解
    const double theta = 0.5 * std::atan2(2.0 * sxy, sxx - syy);
    double dx = std::cos(theta);
    double dy = std::sin(theta);
    // 方向符号归一：保证 vx>0（竖直时 vy>0），结果可复现
    if (dx < 0.0 || (dx == 0.0 && dy < 0.0)) {
        dx = -dx;
        dy = -dy;
    }
    *vx = dx;
    *vy = dy;
    *x0 = cx;
    *y0 = cy;
    return true;
}

double medianOf(QVector<double> values)
{
    if (values.isEmpty())
        return 0.0;
    const int mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + mid, values.end());
    return values.at(mid);
}

/// Huber/Tukey 的稳健初值：沿主方向把点分成两半，各半取分量中位数连线。
/// 为什么不能直接用 L2 初值做 IRLS：离群点会把 L2 直线拉向自己，导致"离群点残差反而比内点小"，
/// MAD 尺度随之失效（实测 100 内点 + 10 个远端离群点时 L2 倾角 ≈74°，残差内点 46、离群点 11）。
/// 中位数对 <50% 的污染不敏感，连线得到的初值可把 IRLS 拉回主趋势。
/// 返回 false 表示两半中位数重合（点集退化），调用方保留 L2 初值。
bool robustInitialLine(const QVector<QPointF> &points, double l2vx, double l2vy,
                       double *vx, double *vy, double *x0, double *y0)
{
    if (points.size() < 4)
        return false;

    // 主方向由 L2 方向决定：横向为主按 x 分半，纵向为主按 y 分半
    const bool splitByX = std::fabs(l2vx) >= std::fabs(l2vy);
    struct Sample {
        double primary;
        double secondary;
    };
    QVector<Sample> samples;
    samples.reserve(points.size());
    for (const QPointF &p : points)
        samples.append(splitByX ? Sample{p.x(), p.y()} : Sample{p.y(), p.x()});
    std::sort(samples.begin(), samples.end(),
              [](const Sample &a, const Sample &b) { return a.primary < b.primary; });

    const int mid = samples.size() / 2;
    QVector<double> leftPrimary, leftSecondary, rightPrimary, rightSecondary;
    leftPrimary.reserve(mid);
    leftSecondary.reserve(mid);
    rightPrimary.reserve(samples.size() - mid);
    rightSecondary.reserve(samples.size() - mid);
    for (int i = 0; i < samples.size(); ++i) {
        if (i < mid) {
            leftPrimary.append(samples.at(i).primary);
            leftSecondary.append(samples.at(i).secondary);
        } else {
            rightPrimary.append(samples.at(i).primary);
            rightSecondary.append(samples.at(i).secondary);
        }
    }
    const double dp = medianOf(rightPrimary) - medianOf(leftPrimary);
    const double ds = medianOf(rightSecondary) - medianOf(leftSecondary);
    if (std::hypot(dp, ds) < 1e-9)
        return false;

    double dx = splitByX ? dp : ds;
    double dy = splitByX ? ds : dp;
    if (dx < 0.0 || (dx == 0.0 && dy < 0.0)) {
        dx = -dx;
        dy = -dy;
    }
    const double norm = std::hypot(dx, dy);
    *vx = dx / norm;
    *vy = dy / norm;

    // 过点取分量中位数（比均值更抗离群）
    QVector<double> xs, ys;
    xs.reserve(points.size());
    ys.reserve(points.size());
    for (const QPointF &p : points) {
        xs.append(p.x());
        ys.append(p.y());
    }
    *x0 = medianOf(xs);
    *y0 = medianOf(ys);
    return true;
}

/// 点到直线的垂直距离（法向投影）
double distanceToLine(const QPointF &p, double x0, double y0, double vx, double vy)
{
    const double dx = p.x() - x0;
    const double dy = p.y() - y0;
    return std::fabs(-vy * dx + vx * dy);   // |n·d|，n=(-vy, vx) 为单位法向
}

/// Huber / Tukey 的 IRLS 权重；LeastSquares 返回全 1
QVector<double> robustWeights(const QVector<QPointF> &points, FitWeight weight,
                              double x0, double y0, double vx, double vy, double maxIterations)
{
    QVector<double> weights(points.size(), 1.0);
    if (weight == FitWeight::LeastSquares)
        return weights;

    for (int iter = 0; iter < std::max(1, int(maxIterations)); ++iter) {
        QVector<double> residuals(points.size());
        for (int i = 0; i < points.size(); ++i)
            residuals[i] = distanceToLine(points.at(i), x0, y0, vx, vy);

        QVector<double> absResiduals = residuals;
        for (double &r : absResiduals)
            r = std::fabs(r);
        // MAD 尺度：正态下 1.4826×MAD ≈ σ；全 0 说明超过半数点精确共线，
        // 用极小尺度把"离线点"压到 0 权重（不能直接 break 成全 1，否则离群点又拉偏直线）
        double sigma = 1.4826 * medianOf(absResiduals);
        if (!(sigma > 1e-12))
            sigma = 1e-6;

        double maxDelta = 0.0;
        QVector<double> next(points.size(), 1.0);
        if (weight == FitWeight::Huber) {
            const double c = 1.345 * sigma;
            for (int i = 0; i < points.size(); ++i) {
                const double r = residuals.at(i);
                next[i] = (r <= c) ? 1.0 : (c / r);
                maxDelta = std::max(maxDelta, std::fabs(next[i] - weights.at(i)));
            }
        } else {   // Tukey biweight
            const double c = 4.685 * sigma;
            for (int i = 0; i < points.size(); ++i) {
                const double r = residuals.at(i);
                const double u = r / c;
                next[i] = (r <= c) ? (1.0 - u * u) * (1.0 - u * u) : 0.0;
                maxDelta = std::max(maxDelta, std::fabs(next[i] - weights.at(i)));
            }
        }
        weights = next;

        double nvx = vx, nvy = vy, nx0 = x0, ny0 = y0;
        if (!weightedTotalLeastSquares(points, weights, &nvx, &nvy, &nx0, &ny0))
            break;   // Tukey 可能把权重全压成 0：保留上一轮结果
        vx = nvx;
        vy = nvy;
        x0 = nx0;
        y0 = ny0;
        if (maxDelta < 1e-6)
            break;
    }
    return weights;
}

} // namespace

double FitRobust::normalizeLineAngle(double angleDeg)
{
    if (!std::isfinite(angleDeg))
        return 0.0;
    double a = std::fmod(angleDeg, 180.0);
    if (a < 0.0)
        a += 180.0;
    if (a >= 180.0)
        a = 0.0;
    return a;
}

double FitRobust::lineAngleDifference(double angleA, double angleB)
{
    const double diff = std::fabs(normalizeLineAngle(angleA) - normalizeLineAngle(angleB));
    return diff > 90.0 ? 180.0 - diff : diff;
}

FitLineResult FitRobust::fitLine(const QVector<QPointF> &points, const FitLineParams &params)
{
    FitLineResult result;

    QVector<QPointF> usable;
    QVector<int> usableIndex;   // usable 里每个点在**输入点**中的下标（用于匹配掩码）
    usable.reserve(points.size());
    usableIndex.reserve(points.size());
    for (int i = 0; i < points.size(); ++i) {
        const QPointF &p = points.at(i);
        if (isFinitePoint(p)) {
            usable.append(p);
            usableIndex.append(i);
        }
    }
    if (usable.size() < 2) {
        result.failureReason = QStringLiteral("有效点不足（%1 < 2），无法拟合直线").arg(usable.size());
        return result;
    }

    // ① 初拟合：等权总最小二乘（与 DIST_L2 同口径）
    QVector<double> weights(usable.size(), 1.0);
    double vx = 1.0, vy = 0.0, x0 = 0.0, y0 = 0.0;
    if (!weightedTotalLeastSquares(usable, weights, &vx, &vy, &x0, &y0)) {
        result.failureReason = QStringLiteral("拟合点退化（所有点重合），无法确定直线方向");
        return result;
    }
    // 稳健模式或启用剔除时，改用"中位数分半"初值：L2 初值会被离群点拉向自己，
    // 导致离群点残差反而小于内点（实测 40 内点 + 3 个远端离群点时 L2 倾角 61.8°，
    // 离群点垂距仅 6.6px、内点反而 17.5px），按它剔除会先删掉内点。
    const bool needsRobustInitial = (params.weight != FitWeight::LeastSquares)
                                    || (params.rejectPoints > 0 && params.rejectDistance > 0.0);
    if (needsRobustInitial) {
        double rvx = vx, rvy = vy, rx0 = x0, ry0 = y0;
        if (robustInitialLine(usable, vx, vy, &rvx, &rvy, &rx0, &ry0)) {
            vx = rvx;
            vy = rvy;
            x0 = rx0;
            y0 = ry0;
        }
    }

    // ② 离群点剔除：只做一轮，按残差从大到小最多剔除 rejectPoints 个超阈值点
    int rejected = 0;
    if (params.rejectPoints > 0 && params.rejectDistance > 0.0 && usable.size() > 2) {
        QVector<int> candidates;
        for (int i = 0; i < usable.size(); ++i) {
            if (distanceToLine(usable.at(i), x0, y0, vx, vy) > params.rejectDistance)
                candidates.append(i);
        }
        if (!candidates.isEmpty()) {
            // 残差大的优先剔除；剔除后必须仍剩 >=2 点
            std::sort(candidates.begin(), candidates.end(), [&](int a, int b) {
                return distanceToLine(usable.at(a), x0, y0, vx, vy)
                       > distanceToLine(usable.at(b), x0, y0, vx, vy);
            });
            const int candidatesCount = int(candidates.size());
            const int removableCount = int(usable.size()) - 2;
            const int maxRemove = std::min(params.rejectPoints,
                                           std::min(candidatesCount, removableCount));
            QVector<bool> removed(usable.size(), false);
            for (int k = 0; k < maxRemove; ++k)
                removed[candidates.at(k)] = true;

            QVector<QPointF> kept;
            QVector<int> keptIndex;
            kept.reserve(usable.size() - maxRemove);
            keptIndex.reserve(usable.size() - maxRemove);
            for (int i = 0; i < usable.size(); ++i) {
                if (!removed.at(i)) {
                    kept.append(usable.at(i));
                    keptIndex.append(usableIndex.at(i));
                }
            }
            usable = kept;
            usableIndex = keptIndex;
            rejected = maxRemove;
        }
    }

    // ③ 按权重函数重拟合（LeastSquares 时这一步为空操作，保持初拟合结果）
    weights = QVector<double>(usable.size(), 1.0);
    if (params.weight != FitWeight::LeastSquares) {
        weights = robustWeights(usable, params.weight, x0, y0, vx, vy,
                                std::max(1, params.maxIterations));
    }
    if (!weightedTotalLeastSquares(usable, weights, &vx, &vy, &x0, &y0)) {
        result.failureReason = QStringLiteral("稳健加权后拟合点退化（权重全部为 0）");
        return result;
    }

    // 匹配掩码：与输入点一一对应（非有限点、被硬剔除的点为 0）
    result.usedMask = QVector<bool>(points.size(), false);
    for (int index : usableIndex)
        result.usedMask[index] = true;

    const double angleDeg = std::atan2(vy, vx) * 180.0 / kPi;

    // ④ 角度约束：直线无方向，按 0..90° 最小差判定
    if (params.angleConstrained) {
        const double diff = lineAngleDifference(angleDeg, params.expectedAngleDeg);
        if (diff > std::fabs(params.angleToleranceDeg)) {
            result.failureReason =
                QStringLiteral("拟合角度 %1° 超出期望 %2°±%3°（偏差 %4°）")
                    .arg(angleDeg, 0, 'f', 2)
                    .arg(params.expectedAngleDeg, 0, 'f', 2)
                    .arg(params.angleToleranceDeg, 0, 'f', 2)
                    .arg(diff, 0, 'f', 2);
            result.angleDeg = angleDeg;
            result.usedPoints = usable.size();
            result.rejectedPoints = rejected;
            return result;
        }
    }

    result.valid = true;
    result.angleDeg = angleDeg;
    result.x0 = x0;
    result.y0 = y0;
    result.vx = vx;
    result.vy = vy;
    result.usedPoints = usable.size();
    result.rejectedPoints = rejected;
    return result;
}
