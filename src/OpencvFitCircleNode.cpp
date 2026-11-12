#include "OpencvFitCircleNode.h"
#include "FitPointSource.h"
#include "NodeResultFields.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>
#include <vector>

namespace {

/// 一组点的最小外接圆拟合结果（含可选的离群点剔除与径向残差）
struct CircleChoice
{
    bool valid = false;
    double col = 0.0;
    double row = 0.0;
    double radius = 0.0;
    double residual = 0.0;   ///< 径向残差 RMS（px）
    int usedPoints = 0;
    int rejectedPoints = 0;
    /// 与**本组输入点**一一对应的匹配掩码（未剔除的点为 true）
    QVector<bool> usedMask;
};

/// 用一组点拟合最小外接圆（口径见 §15.5：圆保持 minEnclosingCircle，不换最小二乘）。
///
/// 2026-11-12 修三处与口径无关的真缺陷：
/// ① 剔除基准：外接圆语义下所有点都在圆内/圆上，旧的 |r_p - radius| 对圆内合法
///    采样点系统性偏大，剔除会优先删掉圆内点而不是真正撑大半径的远点。改为
///    「移除该点后外接圆半径缩小多少」的留一法判据。
/// ② 剔除下限：旧代码写死 usable.size()-3，完全绕过 minPoints 约束，能剔到
///    剩 3 点（任意 3 点定一个圆、残差恒 0）报出「完美拟合」的假成功。
/// ③ 残差统计：旧代码只统计保留点，剔得越多残差越小，fitResidual 变成可以
///    自己刷出来的假读数；改为覆盖剔除前的全部有效输入点。
///
/// 默认 0/0 = 不剔除，数值与旧版一致。
CircleChoice fitCircleGroup(const QVector<QPointF> &points, int needPoints,
                            int rejectPoints, double rejectDistance)
{
    CircleChoice choice;
    // 非有限点（NaN/Inf）必须在进入 OpenCV 之前滤掉；usedMask 仍按输入下标对应，
    // 被滤掉的点保持 false，下游可看出未参与拟合。
    std::vector<cv::Point2f> usable;
    QVector<int> usableIndex;   // usable 里每个点在本组输入点中的下标
    usable.reserve(points.size());
    usableIndex.reserve(points.size());
    for (int i = 0; i < points.size(); ++i) {
        const QPointF &p = points.at(i);
        if (!std::isfinite(p.x()) || !std::isfinite(p.y()))
            continue;
        usable.emplace_back(static_cast<float>(p.x()), static_cast<float>(p.y()));
        usableIndex.append(i);
    }
    // 按**有效点数**判退化，而不是原始点数
    if (usable.size() < needPoints || usable.size() < 3)
        return choice;

    // 残差对剔除前的全部有效点计算（③）
    const std::vector<cv::Point2f> evalPoints = usable;
    // 剔除后必须仍满足 minPoints（②）；参数下限是 5，故不会退化到 3 点
    const size_t minKept = static_cast<size_t>(needPoints);

    if (rejectPoints > 0 && rejectDistance > 0.0 && usable.size() > minKept) {
        cv::Point2f initialCenter;
        float initialRadius = 0.0f;
        cv::minEnclosingCircle(usable, initialCenter, initialRadius);
        if (initialRadius > 0.0f) {
            // 只有最外侧的点可能撑大外接圆：先按到圆心距离降序取有限候选，
            // 再逐个做留一拟合，避免 O(n^2) 全扫（①）。
            std::vector<size_t> order(usable.size());
            std::iota(order.begin(), order.end(), size_t(0));
            const size_t candidateCap = std::min<size_t>(
                usable.size(), std::max<size_t>(size_t(rejectPoints) * 3, 8));
            std::partial_sort(order.begin(), order.begin() + candidateCap, order.end(),
                [&](size_t a, size_t b) {
                    return std::hypot(usable[a].x - initialCenter.x,
                                      usable[a].y - initialCenter.y)
                         > std::hypot(usable[b].x - initialCenter.x,
                                      usable[b].y - initialCenter.y);
                });

            std::vector<std::pair<double, size_t>> outliers;
            for (size_t c = 0; c < candidateCap; ++c) {
                const size_t k = order[c];
                std::vector<cv::Point2f> without;
                without.reserve(usable.size() - 1);
                for (size_t j = 0; j < usable.size(); ++j)
                    if (j != k) without.push_back(usable[j]);
                if (without.size() < 3) break;
                cv::Point2f c2;
                float r2 = 0.0f;
                cv::minEnclosingCircle(without, c2, r2);
                const double shrink = double(initialRadius) - double(r2);
                if (shrink > rejectDistance)
                    outliers.emplace_back(shrink, k);
            }
            if (!outliers.empty()) {
                std::sort(outliers.begin(), outliers.end(),
                          [](const auto &a, const auto &b) { return a.first > b.first; });
                const size_t maxRemove = std::min({size_t(rejectPoints), outliers.size(),
                                                   usable.size() - minKept});
                std::vector<bool> removed(usable.size(), false);
                for (size_t k = 0; k < maxRemove; ++k)
                    removed[outliers[k].second] = true;
                std::vector<cv::Point2f> kept;
                QVector<int> keptIndex;
                kept.reserve(usable.size() - maxRemove);
                keptIndex.reserve(static_cast<int>(usable.size() - maxRemove));
                for (size_t k = 0; k < usable.size(); ++k) {
                    if (!removed[k]) {
                        kept.push_back(usable[k]);
                        keptIndex.append(usableIndex.at(static_cast<int>(k)));
                    }
                }
                usable.swap(kept);
                usableIndex = keptIndex;
                choice.rejectedPoints = static_cast<int>(maxRemove);
            }
        }
    }

    // 剔除后重新检查：不能因为剔除而绕过 minPoints 约束
    if (usable.size() < minKept)
        return choice;

    cv::Point2f center;
    float radius = 0.0f;
    cv::minEnclosingCircle(usable, center, radius);
    if (!(radius > 0.0f))
        return choice;

    double sumSquares = 0.0;
    for (const cv::Point2f &point : evalPoints) {
        const double distance = std::hypot(point.x - center.x, point.y - center.y);
        const double residual = distance - radius;
        sumSquares += residual * residual;
    }

    choice.valid = true;
    choice.col = center.x;
    choice.row = center.y;
    choice.radius = radius;
    choice.residual = evalPoints.empty() ? 0.0
                        : std::sqrt(sumSquares / evalPoints.size());
    choice.usedPoints = static_cast<int>(usable.size());
    choice.usedMask = QVector<bool>(points.size(), false);
    for (int index : usableIndex)
        choice.usedMask[index] = true;
    return choice;
}

/// 按初始化类型选点：
///   全局法  = 全部候选组合并成一个点集拟合一个圆；
///   局部最优 = 每个候选组单独拟合，取径向残差最小的圆（默认，兼容旧版"单轮廓"行为）。
CircleChoice chooseCircle(const QVector<QVector<QPointF>> &groups,
                          const QVector<QPointF> &allPoints, int initType, int minPoints,
                          int rejectPoints, double rejectDistance, QString *failureReason)
{
    CircleChoice best;
    if (initType == 0) {
        if (allPoints.size() < minPoints) {
            if (failureReason) {
                *failureReason = QStringLiteral("圆拟合失败（有效边缘点不足，本轮 %1 点）")
                                     .arg(allPoints.size());
            }
            return best;
        }
        best = fitCircleGroup(allPoints, minPoints, rejectPoints, rejectDistance);
        if (!best.valid && failureReason) {
            *failureReason = QStringLiteral("圆拟合失败（有效边缘点不足，本轮 %1 点）")
                                 .arg(allPoints.size());
        }
        return best;
    }

    int largestGroup = 0;
    int offset = 0;
    for (const QVector<QPointF> &group : groups) {
        const int groupOffset = offset;
        offset += group.size();
        largestGroup = std::max(largestGroup, static_cast<int>(group.size()));
        if (group.size() < minPoints)
            continue;
        CircleChoice candidate = fitCircleGroup(group, minPoints, rejectPoints,
                                                rejectDistance);
        if (!candidate.valid)
            continue;
        if (!best.valid || candidate.residual < best.residual) {
            // 候选掩码是本组相对下标：选中最优组后映射到全部候选点上
            QVector<bool> globalMask(allPoints.size(), false);
            for (int i = 0; i < group.size(); ++i)
                globalMask[groupOffset + i] = candidate.usedMask.value(i, false);
            candidate.usedMask = globalMask;
            best = candidate;
        }
    }
    if (!best.valid && failureReason) {
        *failureReason = QStringLiteral("圆拟合失败（有效边缘点不足，本轮 %1 点）")
                             .arg(largestGroup);
    }
    return best;
}

/// 参与拟合点的圆周覆盖：起始角 + 角度范围（官方「圆起始角度 / 圆角度范围」口径）。
/// 坐标系为整图图像坐标（x=列、y=行），角度 atan2(y-cy, x-cx) 归一化到 [0,360)。
/// 用"最大空隙"法：起始角 = 最大空隙之后的角度，范围 = 360 - 最大空隙；
/// 单点范围 0，点覆盖整圈时范围接近 360。
void circleAngularCoverage(const QVector<QPointF> &points, double cx, double cy,
                           double *startDeg, double *rangeDeg)
{
    *startDeg = 0.0;
    *rangeDeg = 0.0;
    if (points.isEmpty())
        return;

    constexpr double kPi = 3.14159265358979323846;
    QVector<double> angles;
    angles.reserve(points.size());
    for (const QPointF &point : points) {
        double angle = std::atan2(point.y() - cy, point.x() - cx) * 180.0 / kPi;
        if (angle < 0.0)
            angle += 360.0;
        angles.append(angle);
    }
    std::sort(angles.begin(), angles.end());
    if (angles.size() == 1) {
        *startDeg = angles.first();
        return;
    }

    double maxGap = -1.0;
    int gapEnd = 0;
    for (int i = 0; i < angles.size(); ++i) {
        const int next = (i + 1) % angles.size();
        double gap = angles.at(next) - angles.at(i);
        if (gap < 0.0)
            gap += 360.0;
        if (gap > maxGap) {
            maxGap = gap;
            gapEnd = next;
        }
    }
    *startDeg = angles.at(gapEnd);
    *rangeDeg = std::max(0.0, 360.0 - maxGap);
}

} // namespace

OpencvFitCircleNode::OpencvFitCircleNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("圆拟合"));
    m_type = SHAPE_ANALYSIS;
}

void OpencvFitCircleNode::init()
{
    HalconNode::init();
    // VM「图形生成 → 圆」的输入点集：走逻辑槽（订阅），不在画布上多长一个输入点。
    // 「输入方式」= 按坐标时用点集X / 点集Y 两个数值序列。
    addInputSlot(FitPointSource::slotName(), PortDataType::Any);
    addInputSlot(FitPointSource::pointXSlotName(), PortDataType::Any);
    addInputSlot(FitPointSource::pointYSlotName(), PortDataType::Any);
    addOutputPort(QStringLiteral("拟合结果"), PortDataType::Measure);
    registerParams({
        makeIntParam(QStringLiteral("minPoints"), 10, 5, 100000,
                     QStringLiteral("参与拟合的最小点数")),
        makeIntParam(QStringLiteral("maxContours"), 3, 1, 1000,
                     QStringLiteral("参与拟合的最大轮廓数")),
        // 输入方式（VM「图形生成 → 圆」）：点集订阅按点还是按坐标
        makeEnumParam(QStringLiteral("inputMode"), 0,
                      {QStringLiteral("按点"), QStringLiteral("按坐标")},
                      QStringLiteral("输入方式")),
        // 初始化类型（VM「图形生成 → 圆」）：默认局部最优，保持旧版"单轮廓拟合"的语义
        makeEnumParam(QStringLiteral("initType"), 1,
                      {QStringLiteral("全局法"), QStringLiteral("局部最优")},
                      QStringLiteral("初始化类型")),
        // 搜索区域（ROI）：模块编辑器里在图上拖矩形即可写回；宽或高为 0 表示全图（原行为）
        makeIntParam(QStringLiteral("roiRow"), 0, 0, 100000, QStringLiteral("ROI 起始行")),
        makeIntParam(QStringLiteral("roiCol"), 0, 0, 100000, QStringLiteral("ROI 起始列")),
        makeIntParam(QStringLiteral("roiWidth"), 0, 0, 100000,
                     QStringLiteral("ROI 宽度（0=全图）")),
        makeIntParam(QStringLiteral("roiHeight"), 0, 0, 100000,
                     QStringLiteral("ROI 高度（0=全图）")),
        // VM「图形生成 → 圆」的剔除参数：先按初拟合圆剔离群轮廓点，再重拟合（默认关闭）
        makeIntParam(QStringLiteral("rejectPoints"), 0, 0, 100000,
                     QStringLiteral("剔除点数")),
        makeDoubleParam(QStringLiteral("rejectDistance"), 0.0, 0.0, 100000.0,
                        QStringLiteral("剔除距离"), QStringLiteral("px")),
    });
    setParamTooltip(QStringLiteral("inputMode"),
                    QStringLiteral("按点：订阅一个点集；按坐标：分别订阅点集X与点集Y的数值序列，"
                                   "数量必须一致，按序配对成圆的输入点"));
    setParamTooltip(QStringLiteral("initType"),
                    QStringLiteral("全局法把全部候选点合并成一个圆；局部最优逐轮廓拟合，"
                                   "取径向残差最小的圆（多轮廓时不再按点数多少先到先得）"));
    setParamTooltip(QStringLiteral("rejectPoints"),
                    QStringLiteral("按初拟合圆的径向距离剔除最多该数量的离群轮廓点（0=不剔除）"));
    setParamTooltip(QStringLiteral("rejectDistance"),
                    QStringLiteral("轮廓点到初拟合圆圆周的径向距离超过该值视为离群点（px）"));
    m_params[QStringLiteral("fitRow")] = 0.0;
    m_params[QStringLiteral("fitCol")] = 0.0;
    m_params[QStringLiteral("fitRadius")] = 0.0;
    m_params[QStringLiteral("fitPointCount")] = 0;
    m_params[QStringLiteral("fitRejectedCount")] = 0;
    m_params[QStringLiteral("fitResidual")] = 0.0;
    // VM「模块结果」逐点/状态字段（官方口径）：拟合状态 1/0、拟合点数（输入）、匹配点数、
    // 圆起始角度/角度范围、逐点结果
    m_params[QStringLiteral("fitStatus")] = 0;
    m_params[QStringLiteral("fitInputCount")] = 0;
    m_params[QStringLiteral("fitMatchedCount")] = 0;
    m_params[QStringLiteral("fitStartAngle")] = 0.0;
    m_params[QStringLiteral("fitAngleRange")] = 0.0;
    m_params[QStringLiteral("fitInputPoints")] = QString();
    m_params[QStringLiteral("fitPointDistances")] = QString();
    m_params[QStringLiteral("fitPointMask")] = QString();
    m_params[QStringLiteral("fitPointHint")] = QString();
}

bool OpencvFitCircleNode::requiresInputImage() const
{
    // 订阅是权威输入：任一点集/坐标槽有绑定就不再要求图像，订阅无效时由 run() 明确判失败，
    // 不能悄悄回退到阈值图（VM 的输入点集与图像是两种互斥输入）。
    return !hasInputBinding(kPointSlot) && !hasInputBinding(kPointXSlot)
           && !hasInputBinding(kPointYSlot);
}

bool OpencvFitCircleNode::process()
{
    // 缺图时基类会在 run() 之前返回，读数必须先作废。
    m_params[QStringLiteral("fitRow")] = 0.0;
    m_params[QStringLiteral("fitCol")] = 0.0;
    m_params[QStringLiteral("fitRadius")] = 0.0;
    m_params[QStringLiteral("fitPointCount")] = 0;
    m_params[QStringLiteral("fitRejectedCount")] = 0;
    m_params[QStringLiteral("fitResidual")] = 0.0;
    m_params[QStringLiteral("fitStatus")] = 0;
    m_params[QStringLiteral("fitInputCount")] = 0;
    m_params[QStringLiteral("fitMatchedCount")] = 0;
    m_params[QStringLiteral("fitStartAngle")] = 0.0;
    m_params[QStringLiteral("fitAngleRange")] = 0.0;
    m_params[QStringLiteral("fitInputPoints")] = QString();
    m_params[QStringLiteral("fitPointDistances")] = QString();
    m_params[QStringLiteral("fitPointMask")] = QString();
    m_params[QStringLiteral("fitPointHint")] = QString();
    return HalconNode::process();
}

void OpencvFitCircleNode::run(bool /*autoSwitch*/)
{
    try {
        const int minPoints =
            std::max(3, m_params.value(QStringLiteral("minPoints"), 10).toInt());
        const int maxContours =
            std::max(1, m_params.value(QStringLiteral("maxContours"), 3).toInt());
        const int initType =
            qBound(0, m_params.value(QStringLiteral("initType"), 1).toInt(), 1);
        const int inputMode =
            qBound(0, m_params.value(QStringLiteral("inputMode"), 0).toInt(), 1);
        const int rejectPoints =
            std::max(0, m_params.value(QStringLiteral("rejectPoints"), 0).toInt());
        const double rejectDistance =
            std::max(0.0, m_params.value(QStringLiteral("rejectDistance"), 0.0).toDouble());

        // 候选点按"组"组织：一条轮廓/一段点集一组。全局法合并，局部最优逐组比较。
        QVector<QVector<QPointF>> groups;
        int roiOffsetX = 0;
        int roiOffsetY = 0;
        FitPointSource::SubscriptionInputs subscription;
        subscription.pointBound = hasInputBinding(kPointSlot);
        subscription.xBound = hasInputBinding(kPointXSlot);
        subscription.yBound = hasInputBinding(kPointYSlot);
        const bool subscribed = subscription.pointBound || subscription.xBound
                                || subscription.yBound;

        if (subscribed) {
            // 订阅优先：点集来自前序模块，不使用图像与 ROI。
            if (subscription.pointBound)
                subscription.point = getInputData(kPointSlot);
            if (subscription.xBound)
                subscription.x = getInputData(kPointXSlot);
            if (subscription.yBound)
                subscription.y = getInputData(kPointYSlot);

            QString reason;
            if (!FitPointSource::collectSubscribedGroups(subscription, inputMode, &groups,
                                                         &reason)) {
                m_params[QStringLiteral("lastError")] =
                    reason.isEmpty() ? QStringLiteral("订阅点集不可用") : reason;
                m_params[QStringLiteral("moduleStatus")] = false;
                setOutputData(1, QSharedPointer<DataObject>());
                m_outputImage.Clear();
                return;
            }
            FitPointSource::limitGroups(&groups, maxContours);
        } else {
            HImage input(m_inputImage);
            if (!input.IsInitialized()) {
                // 正常流程不会到这里（requiresInputImage 已判失败），保留兜底原因
                m_params[QStringLiteral("lastError")] =
                    QStringLiteral("圆拟合：未订阅点集且缺少输入图像");
                m_params[QStringLiteral("moduleStatus")] = false;
                setOutputData(1, QSharedPointer<DataObject>());
                m_outputImage.Clear();
                return;
            }
            cv::Mat mat = OpencvUtil::himageToMat(input);
            if (mat.empty()) {
                m_params[QStringLiteral("lastError")] =
                    QStringLiteral("圆拟合：输入图像无法转换为 OpenCV 矩阵");
                m_params["moduleStatus"] = false;
                return;
            }
            if (mat.channels() == 3) {
                cv::cvtColor(mat, mat, cv::COLOR_BGR2GRAY);
            }

            // ROI：只在用户拖出的矩形内找轮廓（宽或高为 0 = 全图，与原行为一致）。
            // 关键是拟合结果要**加回偏移**（见下面 bestCol/bestRow），否则输出会变成 ROI 局部
            // 坐标——对下游标定/定位是错的，而且这种错很难从数值上看出来。
            {
                const int roiRow = m_params.value(QStringLiteral("roiRow"), 0).toInt();
                const int roiCol = m_params.value(QStringLiteral("roiCol"), 0).toInt();
                const int roiW = m_params.value(QStringLiteral("roiWidth"), 0).toInt();
                const int roiH = m_params.value(QStringLiteral("roiHeight"), 0).toInt();
                if (roiW > 0 && roiH > 0) {
                    const cv::Rect box = cv::Rect(roiCol, roiRow, roiW, roiH)
                                         & cv::Rect(0, 0, mat.cols, mat.rows);
                    if (box.empty()) {
                        m_params[QStringLiteral("lastError")] =
                            QStringLiteral("圆拟合：ROI 与输入图像无交集");
                        m_params[QStringLiteral("moduleStatus")] = false;
                        return;
                    }
                    mat = mat(box).clone();
                    roiOffsetX = box.x;
                    roiOffsetY = box.y;
                }
            }

            cv::Mat bin;
            cv::threshold(mat, bin, 1, 255, cv::THRESH_BINARY);

            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(bin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);

            // 按点数从大到小取前 maxContours 条轮廓，每条轮廓一组候选点
            std::sort(contours.begin(), contours.end(),
                      [](const auto &a, const auto &b) { return a.size() > b.size(); });
            for (size_t i = 0; i < contours.size() && i < static_cast<size_t>(maxContours); ++i) {
                QVector<QPointF> group;
                group.reserve(static_cast<int>(contours[i].size()));
                for (const cv::Point &p : contours[i])
                    group.append(QPointF(p.x, p.y));
                if (!group.isEmpty())
                    groups.append(group);
            }
        }

        // 全部候选点（合并顺序与掩码/逐点距离一一对应）
        const QVector<QPointF> allPoints = FitPointSource::mergeGroups(groups);

        QString failureReason;
        const CircleChoice choice = chooseCircle(groups, allPoints, initType, minPoints,
                                                 rejectPoints, rejectDistance,
                                                 &failureReason);
        if (!choice.valid) {
            m_params[QStringLiteral("lastError")] = failureReason;
            m_params[QStringLiteral("moduleStatus")] = false;
            setOutputData(1, QSharedPointer<DataObject>());
            m_outputImage = m_inputImage;   // 订阅模式下为空图，不影响 Measure 主输出
            return;
        }

        // 加回 ROI 偏移：对外始终是整图坐标
        const double bestCol = choice.col + roiOffsetX;
        const double bestRow = choice.row + roiOffsetY;
        m_params[QStringLiteral("fitRow")] = bestRow;
        m_params[QStringLiteral("fitCol")] = bestCol;
        m_params[QStringLiteral("fitRadius")] = choice.radius;
        m_params[QStringLiteral("fitPointCount")] = choice.usedPoints;
        m_params[QStringLiteral("fitRejectedCount")] = choice.rejectedPoints;
        m_params[QStringLiteral("fitResidual")] = choice.residual;
        m_params[QStringLiteral("fitStatus")] = 1;
        m_params[QStringLiteral("fitInputCount")] = allPoints.size();
        m_params[QStringLiteral("fitMatchedCount")] = choice.usedPoints;

        // 逐点字段（官方「输出拟合点 / 拟合距离 / 匹配点」）：径向距离对**全部输入点**计算，
        // 掩码标记哪些点参与最终拟合；拟合点只输出参与拟合的那些点（整图坐标）。
        QVector<double> pointDistances;
        pointDistances.reserve(allPoints.size());
        QVector<QPointF> matchedPoints;
        matchedPoints.reserve(choice.usedPoints);
        for (int i = 0; i < allPoints.size(); ++i) {
            const QPointF &point = allPoints.at(i);
            const double distance =
                std::fabs(std::hypot(point.x() - choice.col, point.y() - choice.row)
                          - choice.radius);
            pointDistances.append(distance);
            if (choice.usedMask.value(i, false)) {
                matchedPoints.append(QPointF(point.x() + roiOffsetX, point.y() + roiOffsetY));
            }
        }
        m_params[QStringLiteral("fitInputPoints")] =
            FitPointSource::joinPoints(matchedPoints, 2);
        m_params[QStringLiteral("fitPointDistances")] =
            FitPointSource::joinNumbers(pointDistances, 2);
        m_params[QStringLiteral("fitPointMask")] = FitPointSource::joinMask(choice.usedMask);
        m_params[QStringLiteral("fitPointHint")] =
            FitPointSource::pointRowsHint(allPoints.size());

        // 圆起始角度/角度范围：按参与拟合点的圆周覆盖计算（整图坐标）
        double startAngle = 0.0;
        double angleRange = 0.0;
        circleAngularCoverage(matchedPoints, bestCol, bestRow, &startAngle, &angleRange);
        m_params[QStringLiteral("fitStartAngle")] = startAngle;
        m_params[QStringLiteral("fitAngleRange")] = angleRange;

        m_params["moduleStatus"] = true;
        m_outputImage = m_inputImage;

        MeasureResult res;
        res.type = QStringLiteral("circle");
        res.valueName = QStringLiteral("圆心/半径");
        res.valid = true;
        res.value = choice.radius;
        res.point1 = QPointF(bestCol, bestRow);
        res.extraValues = QVector<double>({bestRow, bestCol, choice.radius,
                                           double(choice.usedPoints)});
        auto resObj = QSharedPointer<DataObject>::create();
        resObj->setMeasureResult(res);
        setOutputData(1, resObj);
    } catch (const std::exception &e) {
        m_params[QStringLiteral("lastError")] =
            QString::fromUtf8(e.what());   // 原因到面板（2026-10-26）
        VFP_DEBUG << "OpencvFitCircleNode error:" << e.what();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params[QStringLiteral("lastError")] = unknownExceptionReason();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvFitCircleNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvFitCircleNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

QVector<NodeResultField> OpencvFitCircleNode::resultDisplayFields() const
{
    // VM「模块结果」口径：模块级 耗时/模块状态 + 检测区域（矩形 ROI）+ 圆环结果 +
    // 拟合状态/拟合点数/匹配点数/拟合误差；逐点字段（拟合点/拟合距离/匹配点）按输入规模显示。
    QVector<NodeResultField> fields = HalconNode::resultDisplayFields();
    fields << resultValue(QStringLiteral("fitCol"), QStringLiteral("圆心X"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("fitRow"), QStringLiteral("圆心Y"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("fitRadius"), QStringLiteral("圆半径"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("fitStartAngle"), QStringLiteral("圆起始角度"),
                          QStringLiteral("°"), 2)
           << resultValue(QStringLiteral("fitAngleRange"), QStringLiteral("圆角度范围"),
                          QStringLiteral("°"), 2)
           << resultValue(QStringLiteral("fitStatus"), QStringLiteral("拟合状态"),
                          QString(), 0)
           << resultValue(QStringLiteral("fitInputCount"), QStringLiteral("拟合点数"),
                          QString(), 0)
           << resultValue(QStringLiteral("fitMatchedCount"), QStringLiteral("匹配点数"),
                          QString(), 0)
           << resultValue(QStringLiteral("fitResidual"), QStringLiteral("圆拟合误差(本仓RMS)"),
                          QStringLiteral("px"), 3);

    const int inputCount = m_params.value(QStringLiteral("fitInputCount"), 0).toInt();
    if (inputCount > 0 && inputCount <= FitPointSource::maxTablePointRows()) {
        fields << resultPoint(QStringLiteral("fitInputPoints"), QStringLiteral("拟合点%1"),
                              QStringLiteral("坐标"), 2)
               << resultList(QStringLiteral("fitPointDistances"), QStringLiteral("拟合距离%1"),
                             QStringLiteral("距离"), QStringLiteral("px"), 2)
               << resultList(QStringLiteral("fitPointMask"), QStringLiteral("匹配点%1"),
                             QStringLiteral("状态"), QString(), 0);
    } else if (inputCount > FitPointSource::maxTablePointRows()) {
        fields << resultValue(QStringLiteral("fitPointHint"), QStringLiteral("逐点结果"));
    }
    return fields;
}

RoiShape OpencvFitCircleNode::geometryRoi() const
{
    RoiShape s;
    const int w = m_params.value(QStringLiteral("roiWidth"), 0).toInt();
    const int h = m_params.value(QStringLiteral("roiHeight"), 0).toInt();
    if (w <= 0 || h <= 0) {
        return s;   // 未设置 ROI：不显示框（type 保持默认 None）
    }
    s.type = RoiType::Rect;
    s.p1 = QPointF(m_params.value(QStringLiteral("roiCol"), 0).toInt(),
                   m_params.value(QStringLiteral("roiRow"), 0).toInt());
    s.p2 = QPointF(s.p1.x() + w, s.p1.y() + h);
    return s;
}

void OpencvFitCircleNode::applyGeometryRoi(const RoiShape &shape)
{
    // 「清除几何」传进来的是默认构造的 RoiShape（type=None）→ 回到全图
    if (shape.type == RoiType::None) {
        setParam(QStringLiteral("roiRow"), 0);
        setParam(QStringLiteral("roiCol"), 0);
        setParam(QStringLiteral("roiWidth"), 0);
        setParam(QStringLiteral("roiHeight"), 0);
        return;
    }
    if (shape.type != RoiType::Rect) {
        return;
    }
    const QRectF r = QRectF(shape.p1, shape.p2).normalized();
    setParam(QStringLiteral("roiCol"), qRound(r.left()));
    setParam(QStringLiteral("roiRow"), qRound(r.top()));
    setParam(QStringLiteral("roiWidth"), qRound(r.width()));
    setParam(QStringLiteral("roiHeight"), qRound(r.height()));
}
