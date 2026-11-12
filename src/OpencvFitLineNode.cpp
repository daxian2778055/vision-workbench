#include "OpencvFitLineNode.h"
#include "FitPointSource.h"
#include "NodeResultFields.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include "FitRobust.h"
#include <opencv2/imgproc.hpp>
#include <QWidget>
#include <algorithm>
#include <cmath>

namespace {

/// 候选点到拟合直线的垂距 RMS：用于「局部最优」比较候选，也作为结果里的拟合残差。
/// 开启剔除时只统计未被剔除的点，口径与"参与拟合的点"一致。
double lineResidualRms(const QVector<QPointF> &points, const FitLineResult &fit,
                       bool discardOutliers, double rejectDistance)
{
    if (!fit.valid || points.isEmpty())
        return 0.0;

    double sumSquares = 0.0;
    int count = 0;
    for (const QPointF &point : points) {
        // NaN/Inf 点会把整组残差污染成 NaN，使「局部最优」比较恒假
        // （2026-11-12 审核）。FitRobust::fitLine 本体已过滤，这里补上统计侧。
        if (!std::isfinite(point.x()) || !std::isfinite(point.y()))
            continue;
        const double dx = point.x() - fit.x0;
        const double dy = point.y() - fit.y0;
        const double distance = std::fabs(-fit.vy * dx + fit.vx * dy);   // 法向投影
        if (discardOutliers && distance > rejectDistance)
            continue;
        sumSquares += distance * distance;
        ++count;
    }
    if (count == 0)
        return 0.0;
    return std::sqrt(sumSquares / count);
}

/// 一组候选的拟合结果与残差（「局部最优」按残差取最小）
struct LineChoice
{
    bool valid = false;
    FitLineResult fit;
    double residual = 0.0;
    /// 与**全部候选点**（各组按顺序拼接）一一对应的匹配掩码
    QVector<bool> usedMask;
    QString failureReason;
};

/// 按初始化类型选点：
///   全局法  = 全部候选组合并成一个点集拟合一次（与旧行为一致）；
///   局部最优 = 每个候选组单独拟合，取残差最小的一组（点集被拆成多条轮廓时更稳）。
LineChoice chooseLine(const QVector<QVector<QPointF>> &groups,
                      const QVector<QPointF> &allPoints, const FitLineParams &params,
                      int initType, int minPoints)
{
    const bool discardOutliers = params.rejectPoints > 0 && params.rejectDistance > 0.0;
    LineChoice choice;

    if (initType == 0) {
        if (allPoints.size() < minPoints) {
            choice.failureReason = QStringLiteral("有效轮廓点不足（%1 < %2），无法拟合直线")
                                       .arg(allPoints.size())
                                       .arg(minPoints);
            return choice;
        }
        choice.fit = FitRobust::fitLine(allPoints, params);
        choice.valid = choice.fit.valid;
        if (!choice.valid) {
            choice.failureReason = choice.fit.failureReason;
        } else {
            choice.usedMask = choice.fit.usedMask;   // 已在全部候选点上
            choice.residual = lineResidualRms(allPoints, choice.fit, discardOutliers,
                                              params.rejectDistance);
        }
        return choice;
    }

    int offset = 0;
    for (const QVector<QPointF> &group : groups) {
        const int groupOffset = offset;
        offset += group.size();
        if (group.size() < minPoints)
            continue;
        const FitLineResult fit = FitRobust::fitLine(group, params);
        if (!fit.valid) {
            choice.failureReason = fit.failureReason;
            continue;
        }
        const double residual = lineResidualRms(group, fit, discardOutliers,
                                                params.rejectDistance);
        if (!choice.valid || residual < choice.residual) {
            choice.valid = true;
            choice.fit = fit;
            choice.residual = residual;
            // 只有被选中那一组的点可能参与拟合，其余组一律 0
            choice.usedMask = QVector<bool>(allPoints.size(), false);
            for (int i = 0; i < group.size(); ++i)
                choice.usedMask[groupOffset + i] = fit.usedMask.value(i, false);
        }
    }
    if (!choice.valid && choice.failureReason.isEmpty())
        choice.failureReason = QStringLiteral("没有满足最小点数的轮廓（局部最优）");
    return choice;
}

} // namespace

OpencvFitLineNode::OpencvFitLineNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("直线拟合"));
    m_type = SHAPE_ANALYSIS;
}

void OpencvFitLineNode::init()
{
    HalconNode::init();
    // VM「图形生成 → 直线」的输入点集：走逻辑槽（订阅），不在画布上多长一个输入点。
    // 「输入方式」= 按坐标时用点集X / 点集Y 两个数值序列。
    addInputSlot(FitPointSource::slotName(), PortDataType::Any);
    addInputSlot(FitPointSource::pointXSlotName(), PortDataType::Any);
    addInputSlot(FitPointSource::pointYSlotName(), PortDataType::Any);
    addOutputPort(QStringLiteral("拟合结果"), PortDataType::Measure);
    registerParams({
        makeIntParam(QStringLiteral("minPoints"), 20, 3, 100000,
                     QStringLiteral("参与拟合的最小点数")),
        makeIntParam(QStringLiteral("maxContours"), 10, 1, 1000,
                     QStringLiteral("参与拟合的最大轮廓数")),
        // 输入方式（VM「图形生成 → 直线」）：点集订阅按点还是按坐标
        makeEnumParam(QStringLiteral("inputMode"), 0,
                      {QStringLiteral("按点"), QStringLiteral("按坐标")},
                      QStringLiteral("输入方式")),
        // 初始化类型（VM「图形生成 → 直线」）：点集怎么组织成一次拟合
        makeEnumParam(QStringLiteral("initType"), 0,
                      {QStringLiteral("全局法"), QStringLiteral("局部最优")},
                      QStringLiteral("初始化类型")),
        // 搜索区域（ROI）：模块编辑器里在图上拖矩形即可写回；宽或高为 0 表示全图（原行为）
        makeIntParam(QStringLiteral("roiRow"), 0, 0, 100000, QStringLiteral("ROI 起始行")),
        makeIntParam(QStringLiteral("roiCol"), 0, 0, 100000, QStringLiteral("ROI 起始列")),
        makeIntParam(QStringLiteral("roiWidth"), 0, 0, 100000,
                     QStringLiteral("ROI 宽度（0=全图）")),
        makeIntParam(QStringLiteral("roiHeight"), 0, 0, 100000,
                     QStringLiteral("ROI 高度（0=全图）")),
        // VM「图形生成 → 直线」的稳健拟合参数（默认值保持原有最小二乘行为）
        makeEnumParam(QStringLiteral("weightFunc"), 0,
                      {QStringLiteral("最小二乘"), QStringLiteral("Huber"),
                       QStringLiteral("Tukey")},
                      QStringLiteral("权重函数")),
        makeIntParam(QStringLiteral("maxIterations"), 100, 1, 10000,
                     QStringLiteral("最大迭代次数")),
        makeIntParam(QStringLiteral("rejectPoints"), 0, 0, 100000,
                     QStringLiteral("剔除点数")),
        makeDoubleParam(QStringLiteral("rejectDistance"), 0.0, 0.0, 100000.0,
                        QStringLiteral("剔除距离"), QStringLiteral("px")),
        makeBoolParam(QStringLiteral("angleConstrained"), false,
                      QStringLiteral("启用直线角度约束")),
        makeDoubleParam(QStringLiteral("expectedAngle"), 0.0, -180.0, 180.0,
                        QStringLiteral("期望角度"), QStringLiteral("°")),
        makeDoubleParam(QStringLiteral("angleTolerance"), 10.0, 0.0, 90.0,
                        QStringLiteral("角度容忍范围"), QStringLiteral("°")),
    });
    // 参数说明（帮助页 tooltip 列）
    setParamTooltip(QStringLiteral("inputMode"),
                    QStringLiteral("按点：订阅一个点集（XLD/Region/卡尺点等）；按坐标：分别订阅"
                                   "点集X与点集Y的数值数组，按序配成点（数量必须一致）"));
    setParamTooltip(QStringLiteral("initType"),
                    QStringLiteral("全局法把全部候选轮廓点合并成一次拟合；局部最优逐轮廓拟合，"
                                   "取垂距残差最小的一条（点数不足的轮廓会被跳过）"));
    setParamTooltip(QStringLiteral("weightFunc"),
                    QStringLiteral("最小二乘对离群点敏感；Huber/Tukey 按 MAD 降权，Tukey 可完全剔除"));
    setParamTooltip(QStringLiteral("maxIterations"),
                    QStringLiteral("仅 Huber/Tukey 生效：IRLS 迭代上限，权重收敛即提前结束"));
    setParamTooltip(QStringLiteral("rejectPoints"),
                    QStringLiteral("初拟合后最多剔除的离群点数；与剔除距离同时 >0 才生效"));
    setParamTooltip(QStringLiteral("rejectDistance"),
                    QStringLiteral("点到初拟合直线距离超过该值视为离群点（px）"));
    setParamTooltip(QStringLiteral("angleConstrained"),
                    QStringLiteral("启用后拟合角超出期望±容忍范围即判失败，并给出偏差"));
    m_params[QStringLiteral("fitAngle")] = 0.0;
    m_params[QStringLiteral("fitRow")] = 0.0;
    m_params[QStringLiteral("fitCol")] = 0.0;
    m_params[QStringLiteral("fitPointCount")] = 0;
    m_params[QStringLiteral("fitRejectedCount")] = 0;
    m_params[QStringLiteral("fitResidual")] = 0.0;
    // VM「模块结果」逐点/状态字段（官方口径）：拟合状态 1/0、拟合点数（输入）、匹配点数、逐点结果
    m_params[QStringLiteral("fitStatus")] = 0;
    m_params[QStringLiteral("fitInputCount")] = 0;
    m_params[QStringLiteral("fitMatchedCount")] = 0;
    m_params[QStringLiteral("fitInputPoints")] = QString();
    m_params[QStringLiteral("fitPointDistances")] = QString();
    m_params[QStringLiteral("fitPointMask")] = QString();
    m_params[QStringLiteral("fitPointHint")] = QString();
}

bool OpencvFitLineNode::requiresInputImage() const
{
    // 订阅是权威输入：任一点集/坐标槽有绑定就不再要求图像，订阅无效时由 run() 明确判失败，
    // 不能悄悄回退到阈值图（VM 的输入点集与图像是两种互斥输入）。
    return !hasInputBinding(kPointSlot) && !hasInputBinding(kPointXSlot)
           && !hasInputBinding(kPointYSlot);
}

bool OpencvFitLineNode::process()
{
    // 缺图时基类会在 run() 之前返回，读数必须先作废。
    m_params[QStringLiteral("fitAngle")] = 0.0;
    m_params[QStringLiteral("fitRow")] = 0.0;
    m_params[QStringLiteral("fitCol")] = 0.0;
    m_params[QStringLiteral("fitPointCount")] = 0;
    m_params[QStringLiteral("fitRejectedCount")] = 0;
    m_params[QStringLiteral("fitResidual")] = 0.0;
    m_params[QStringLiteral("fitStatus")] = 0;
    m_params[QStringLiteral("fitInputCount")] = 0;
    m_params[QStringLiteral("fitMatchedCount")] = 0;
    m_params[QStringLiteral("fitInputPoints")] = QString();
    m_params[QStringLiteral("fitPointDistances")] = QString();
    m_params[QStringLiteral("fitPointMask")] = QString();
    m_params[QStringLiteral("fitPointHint")] = QString();
    return HalconNode::process();
}

void OpencvFitLineNode::run(bool /*autoSwitch*/)
{
    try {
        const int minPoints =
            std::max(2, m_params.value(QStringLiteral("minPoints"), 20).toInt());
        const int maxContours =
            std::max(1, m_params.value(QStringLiteral("maxContours"), 10).toInt());
        const int initType =
            qBound(0, m_params.value(QStringLiteral("initType"), 0).toInt(), 1);
        const int inputMode =
            qBound(0, m_params.value(QStringLiteral("inputMode"), 0).toInt(), 1);

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
            // 与图像模式同一口径：最多取点数最多的前 maxContours 组
            FitPointSource::limitGroups(&groups, maxContours);
        } else {
            HImage input(m_inputImage);
            if (!input.IsInitialized()) {
                // 正常流程不会到这里（requiresInputImage 已判失败），保留兜底原因
                m_params[QStringLiteral("lastError")] =
                    QStringLiteral("直线拟合：未订阅点集且缺少输入图像");
                m_params[QStringLiteral("moduleStatus")] = false;
                setOutputData(1, QSharedPointer<DataObject>());
                m_outputImage.Clear();
                return;
            }
            cv::Mat mat = OpencvUtil::himageToMat(input);
            if (mat.empty()) {
                m_params[QStringLiteral("lastError")] =
                    QStringLiteral("直线拟合：输入图像无法转换为 OpenCV 矩阵");
                m_params["moduleStatus"] = false;
                return;
            }
            if (mat.channels() == 3) {
                cv::cvtColor(mat, mat, cv::COLOR_BGR2GRAY);
            }

            // ROI：只在用户拖出的矩形内找轮廓（宽或高为 0 = 全图，与原行为一致）。
            // 直线拟合里只有"拟合点"需要平移回整图坐标（见下面 x0/y0）；角度由方向向量决定，
            // 平移不改变方向，所以 fitAngle 不需要任何换算。
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
                            QStringLiteral("直线拟合：ROI 与输入图像无交集");
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
                // cv::Point 仍是 ROI 局部坐标；偏移在结果上一次性加回
                for (const cv::Point &p : contours[i])
                    group.append(QPointF(p.x, p.y));
                if (!group.isEmpty())
                    groups.append(group);
            }
        }

        // 稳健拟合（VM「图形生成 → 直线」参数面）：权重函数 / 迭代次数 / 剔除点 / 角度约束。
        // 默认（最小二乘、不剔除、不约束）与旧的 cv::fitLine(DIST_L2) 同口径。
        FitLineParams fitParams;
        const int weightIndex = m_params.value(QStringLiteral("weightFunc"), 0).toInt();
        fitParams.weight = (weightIndex == 1) ? FitWeight::Huber
                           : (weightIndex == 2) ? FitWeight::Tukey
                                                : FitWeight::LeastSquares;
        fitParams.maxIterations =
            std::max(1, m_params.value(QStringLiteral("maxIterations"), 100).toInt());
        fitParams.rejectPoints =
            std::max(0, m_params.value(QStringLiteral("rejectPoints"), 0).toInt());
        fitParams.rejectDistance =
            std::max(0.0, m_params.value(QStringLiteral("rejectDistance"), 0.0).toDouble());
        fitParams.angleConstrained =
            m_params.value(QStringLiteral("angleConstrained"), false).toBool();
        fitParams.expectedAngleDeg =
            m_params.value(QStringLiteral("expectedAngle"), 0.0).toDouble();
        fitParams.angleToleranceDeg =
            std::fabs(m_params.value(QStringLiteral("angleTolerance"), 10.0).toDouble());

        // 全部候选点（合并顺序与掩码/逐点距离一一对应）
        const QVector<QPointF> allPoints = FitPointSource::mergeGroups(groups);

        const LineChoice choice = chooseLine(groups, allPoints, fitParams, initType, minPoints);
        if (!choice.valid) {
            m_params[QStringLiteral("lastError")] =
                choice.failureReason.isEmpty() ? QStringLiteral("直线拟合失败")
                                               : choice.failureReason;
            m_params[QStringLiteral("moduleStatus")] = false;
            setOutputData(1, QSharedPointer<DataObject>());
            m_outputImage = m_inputImage;   // 订阅模式下为空图，不影响 Measure 主输出
            return;
        }

        const FitLineResult &fit = choice.fit;
        m_params[QStringLiteral("fitPointCount")] = fit.usedPoints;
        m_params[QStringLiteral("fitRejectedCount")] = fit.rejectedPoints;
        m_params[QStringLiteral("fitResidual")] = choice.residual;
        m_params[QStringLiteral("fitStatus")] = 1;
        m_params[QStringLiteral("fitInputCount")] = allPoints.size();
        m_params[QStringLiteral("fitMatchedCount")] = fit.usedPoints;

        // 逐点字段（官方「输出拟合点 / 拟合距离 / 匹配点」）：距离对**全部输入点**计算，
        // 掩码标记哪些点参与最终拟合；拟合点只输出参与拟合的那些点（整图坐标）。
        QVector<double> pointDistances;
        pointDistances.reserve(allPoints.size());
        QVector<QPointF> matchedPoints;
        matchedPoints.reserve(fit.usedPoints);
        for (int i = 0; i < allPoints.size(); ++i) {
            const QPointF &point = allPoints.at(i);
            const double distance = std::fabs(-fit.vy * (point.x() - fit.x0)
                                              + fit.vx * (point.y() - fit.y0));
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

        // 加回 ROI 偏移：对外始终是整图坐标。只要这一处平移，下面的 fitRow/fitCol、
        // 两个端点、extraValues 就都自动是全图坐标；角度来自方向向量，不受平移影响。
        const double x0 = fit.x0 + roiOffsetX;
        const double y0 = fit.y0 + roiOffsetY;
        const double vx = fit.vx;
        const double vy = fit.vy;
        const double angleDeg = fit.angleDeg;

        m_params[QStringLiteral("fitAngle")] = angleDeg;
        m_params[QStringLiteral("fitRow")] = y0;
        m_params[QStringLiteral("fitCol")] = x0;
        m_params["moduleStatus"] = true;

        // 输出 Measure：直线上的两个端点（过 (x0,y0) 沿方向 (vx,vy) 外推）
        const double half = 200.0;
        MeasureResult res;
        res.type = QStringLiteral("line");
        res.valueName = QStringLiteral("直线角度");
        res.valid = true;
        res.value = angleDeg;
        res.point1 = QPointF(x0 - vx * half, y0 - vy * half);
        res.point2 = QPointF(x0 + vx * half, y0 + vy * half);
        res.extraValues = QVector<double>({vx, vy, x0, y0, double(fit.usedPoints)});
        auto resObj = QSharedPointer<DataObject>::create();
        resObj->setMeasureResult(res);
        setOutputData(1, resObj);

        m_outputImage = m_inputImage;
    } catch (const std::exception &e) {
        m_params[QStringLiteral("lastError")] =
            QString::fromUtf8(e.what());   // 原因到面板（2026-10-26）
        VFP_DEBUG << "OpencvFitLineNode error:" << e.what();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params[QStringLiteral("lastError")] = unknownExceptionReason();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvFitLineNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvFitLineNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

QVector<NodeResultField> OpencvFitLineNode::resultDisplayFields() const
{
    // VM「模块结果」口径：模块级 耗时/模块状态 + 检测区域（矩形 ROI）+ 直线结果 +
    // 拟合状态/拟合点数/匹配点数/拟合误差；逐点字段（拟合点/拟合距离/匹配点）按输入规模显示。
    QVector<NodeResultField> fields = HalconNode::resultDisplayFields();
    fields << resultValue(QStringLiteral("fitAngle"), QStringLiteral("拟合角度"),
                          QStringLiteral("°"), 3)
           << resultValue(QStringLiteral("fitCol"), QStringLiteral("拟合点X"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("fitRow"), QStringLiteral("拟合点Y"),
                          QStringLiteral("px"), 2)
           << resultValue(QStringLiteral("fitStatus"), QStringLiteral("拟合状态"),
                          QString(), 0)
           << resultValue(QStringLiteral("fitInputCount"), QStringLiteral("拟合点数"),
                          QString(), 0)
           << resultValue(QStringLiteral("fitMatchedCount"), QStringLiteral("匹配点数"),
                          QString(), 0)
           << resultValue(QStringLiteral("fitResidual"), QStringLiteral("直线拟合误差(本仓RMS)"),
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

RoiShape OpencvFitLineNode::geometryRoi() const
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

void OpencvFitLineNode::applyGeometryRoi(const RoiShape &shape)
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
