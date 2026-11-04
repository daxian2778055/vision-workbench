#include "OpencvCaliperNode.h"
#include "NodeResultFields.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>
#include <QWidget>

namespace {

/// 给参数补分组标题（自动面板按组渲染）
ParamSpec grouped(ParamSpec spec, const QString &group)
{
    spec.group = group;
    return spec;
}

/// 单个亚像素边缘（图像坐标 + 对比度 + 上升/下降方向）
struct EdgeHit {
    double col = 0.0;
    double row = 0.0;
    double contrast = 0.0;
    bool rising = false;   /// true=沿法线方向由暗到亮（梯度为正）
    int profile = 0;       /// 所属剖面序号（用于"按位置"排序）
};

/// 边对（两条极性相反、间距不超过 pairWidth 的边缘）
struct PairHit {
    double col1 = 0.0;
    double row1 = 0.0;
    double col2 = 0.0;
    double row2 = 0.0;
    double width = 0.0;
    double contrast = 0.0;
    int profile = 0;
};

}   // namespace

OpencvCaliperNode::OpencvCaliperNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("卡尺测量"));
    m_type = SHAPE_ANALYSIS;
}

void OpencvCaliperNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("边缘点"), PortDataType::Measure);
    addOutputPort(QStringLiteral("边缘数"), PortDataType::Number);
    addOutputPort(QStringLiteral("边缘对"), PortDataType::Measure);
    registerParams({
        // ---- ROI（与画布上的卡尺线一致）----
        grouped(makeDoubleParam(QStringLiteral("row1"), 100.0, 0.0, 100000.0,
                                QStringLiteral("起点行")),
                QStringLiteral("卡尺")),
        grouped(makeDoubleParam(QStringLiteral("col1"), 100.0, 0.0, 100000.0,
                                QStringLiteral("起点列")),
                QStringLiteral("卡尺")),
        grouped(makeDoubleParam(QStringLiteral("row2"), 200.0, 0.0, 100000.0,
                                QStringLiteral("终点行")),
                QStringLiteral("卡尺")),
        grouped(makeDoubleParam(QStringLiteral("col2"), 100.0, 0.0, 100000.0,
                                QStringLiteral("终点列")),
                QStringLiteral("卡尺")),
        grouped(makeIntParam(QStringLiteral("halfWidth"), 10, 1, 200,
                             QStringLiteral("法线方向测量半宽"), QStringLiteral("px")),
                QStringLiteral("卡尺")),
        grouped(makeDoubleParam(QStringLiteral("step"), 5.0, 1.0, 100.0,
                                QStringLiteral("沿直线采样步长"), QStringLiteral("px")),
                QStringLiteral("卡尺")),
        // ---- 边缘提取 ----
        grouped(makeEnumParam(QStringLiteral("edgeMode"), 0,
                              {QStringLiteral("单边"), QStringLiteral("边对")},
                              QStringLiteral("边缘模式")),
                QStringLiteral("边缘")),
        grouped(makeIntParam(QStringLiteral("pairWidth"), 20, 1, 10000,
                             QStringLiteral("边对最大间距（仅边对模式）"), QStringLiteral("px")),
                QStringLiteral("边缘")),
        grouped(makeDoubleParam(QStringLiteral("threshold"), 10.0, 0.0, 255.0,
                                QStringLiteral("边缘梯度阈值")),
                QStringLiteral("边缘")),
        grouped(makeIntParam(QStringLiteral("polarity"), 0, 0, 2,
                             QStringLiteral("极性：0=任意，1=亮到暗，2=暗到亮")),
                QStringLiteral("边缘")),
        grouped(makeEnumParam(QStringLiteral("projectionType"), 0,
                              {QStringLiteral("均值"), QStringLiteral("最大值"),
                               QStringLiteral("最小值")},
                              QStringLiteral("剖面投影")),
                QStringLiteral("边缘")),
        grouped(makeIntParam(QStringLiteral("profileWidth"), 1, 1, 101,
                             QStringLiteral("投影宽度（沿卡尺方向，1=逐像素）"),
                             QStringLiteral("px")),
                QStringLiteral("边缘")),
        grouped(makeBoolParam(QStringLiteral("subpixel"), true,
                              QStringLiteral("亚像素插值")),
                QStringLiteral("边缘")),
        // ---- 结果筛选 ----
        grouped(makeIntParam(QStringLiteral("maxCount"), 0, 0, 10000,
                             QStringLiteral("最大结果数（0=不限）")),
                QStringLiteral("结果")),
        grouped(makeEnumParam(QStringLiteral("sortType"), 0,
                              {QStringLiteral("按位置"), QStringLiteral("按对比度")},
                              QStringLiteral("结果排序")),
                QStringLiteral("结果")),
        grouped(makeBoolParam(QStringLiteral("showCaliperPair"), true,
                              QStringLiteral("显示边对连线")),
                QStringLiteral("结果")),
    });

    setParamDirect(QStringLiteral("edgeCount"), 0);
    setParamDirect(QStringLiteral("rawEdgeCount"), 0);
    setParamDirect(QStringLiteral("pairCount"), 0);
    setParamDirect(QStringLiteral("avgWidth"), 0.0);
    setParamDirect(QStringLiteral("minWidth"), 0.0);
    setParamDirect(QStringLiteral("maxWidth"), 0.0);
    setParamDirect(QStringLiteral("avgContrast"), 0.0);
    setParamDirect(QStringLiteral("maxContrast"), 0.0);
}

void OpencvCaliperNode::run(bool /*autoSwitch*/)
{
    try {
        HImage input(m_inputImage);
        if (!input.IsInitialized()) {
            m_outputImage.Clear();
            return;
        }
        cv::Mat gray = OpencvUtil::himageToMat(input);
        if (gray.empty()) {
            m_params["moduleStatus"] = false;
            return;
        }
        // 非 8 位输入的显式拒绝（2026-10-25 复核 P2）：桥接修好后 16 位图会如实返回 CV_16U，
        // 而这里的阈值 0-255 / at<uchar> / 外部 8 位入口都按 8 位写死——静默错值比判红更糟。
        {
            QString depthWhy;
            if (!OpencvUtil::requireGray8(gray, &depthWhy))
                throw std::runtime_error(depthWhy.toStdString());
        }
        if (gray.channels() == 3) {
            cv::cvtColor(gray, gray, cv::COLOR_BGR2GRAY);
        }

        const double r1 = m_params.value(QStringLiteral("row1"), 100.0).toDouble();
        const double c1 = m_params.value(QStringLiteral("col1"), 100.0).toDouble();
        const double r2 = m_params.value(QStringLiteral("row2"), 200.0).toDouble();
        const double c2 = m_params.value(QStringLiteral("col2"), 100.0).toDouble();
        const int half = m_params.value(QStringLiteral("halfWidth"), 10).toInt();
        const double step = m_params.value(QStringLiteral("step"), 5.0).toDouble();
        const double thr = m_params.value(QStringLiteral("threshold"), 10.0).toDouble();
        const int polarity = m_params.value(QStringLiteral("polarity"), 0).toInt();
        const int edgeMode = m_params.value(QStringLiteral("edgeMode"), 0).toInt();
        const int pairWidth = m_params.value(QStringLiteral("pairWidth"), 20).toInt();
        const int projectionType = m_params.value(QStringLiteral("projectionType"), 0).toInt();
        const int profileWidth = m_params.value(QStringLiteral("profileWidth"), 1).toInt();
        const bool useSubpixel = m_params.value(QStringLiteral("subpixel"), true).toBool();
        const int maxCount = m_params.value(QStringLiteral("maxCount"), 0).toInt();
        const int sortType = m_params.value(QStringLiteral("sortType"), 0).toInt();

        const double dx = c2 - c1;
        const double dy = r2 - r1;
        const double L = std::hypot(dx, dy);
        if (L < 1.0) {
            // 显式失败分支也要有可读原因（2026-10-26 复核）：原来只判红，面板上看不到为什么
            m_params[QStringLiteral("lastError")] =
                QStringLiteral("卡尺两点重合（长度为 0），无法定义搜索方向");
            m_params["moduleStatus"] = false;
            m_outputImage = m_inputImage;
            return;
        }
        const double ux = dx / L;   // 直线方向单位向量（卡尺宽度方向）
        const double uy = dy / L;
        const double nx = -uy;      // 法线方向单位向量（边缘搜索方向）
        const double ny = ux;

        const int K = std::max(1, static_cast<int>(L / step));
        const int M = half * 2 + 1; // 剖面长度
        const int projHalf = std::max(0, (profileWidth - 1) / 2);   // 总采样数 = 2*projHalf+1 ≤ profileWidth

        // 双线性采样；越界返回 0（与旧实现一致，避免边界外亮度参与投影）
        auto sampleGray = [&gray](double px, double py) -> double {
            if (px < 0.0 || py < 0.0 || px >= gray.cols - 1 || py >= gray.rows - 1)
                return 0.0;
            const int x0 = static_cast<int>(px);
            const int y0 = static_cast<int>(py);
            const double fx = px - x0;
            const double fy = py - y0;
            return gray.at<uchar>(y0, x0) * (1 - fx) * (1 - fy)
                   + gray.at<uchar>(y0, x0 + 1) * fx * (1 - fy)
                   + gray.at<uchar>(y0 + 1, x0) * (1 - fx) * fy
                   + gray.at<uchar>(y0 + 1, x0 + 1) * fx * fy;
        };

        std::vector<EdgeHit> hits;    // 单边模式：保留的边缘
        std::vector<PairHit> pairs;   // 边对模式：保留的边对
        int rawEdgeCount = 0;         // 极性过滤后的候选边缘总数（含未输出的）

        for (int k = 0; k <= K; ++k) {
            const double t = (K == 0) ? 0.0 : static_cast<double>(k) / K;
            const double cx = c1 + dx * t;
            const double cy = r1 + dy * t;

            // 法线方向一维剖面：沿卡尺方向按 projectionType 聚合 profileWidth 个采样点
            std::vector<float> profile(M);
            for (int m = 0; m < M; ++m) {
                const double off = (m - half);
                const double px = cx + nx * off;
                const double py = cy + ny * off;
                if (projHalf == 0) {
                    profile[m] = static_cast<float>(sampleGray(px, py));
                    continue;
                }
                double sum = 0.0;
                double maxV = -1e9;
                double minV = 1e9;
                int samples = 0;
                for (int j = -projHalf; j <= projHalf; ++j) {
                    const double v = sampleGray(px + ux * j, py + uy * j);
                    sum += v;
                    maxV = std::max(maxV, v);
                    minV = std::min(minV, v);
                    ++samples;
                }
                if (projectionType == 1) {
                    profile[m] = static_cast<float>(maxV);
                } else if (projectionType == 2) {
                    profile[m] = static_cast<float>(minV);
                } else {
                    profile[m] = static_cast<float>(sum / samples);
                }
            }

            // 一阶梯度
            std::vector<float> grad(M - 1);
            for (int m = 0; m < M - 1; ++m) {
                grad[m] = profile[m + 1] - profile[m];
            }

            // 收集本剖面所有满足阈值的跨零极值点（一个窗口可输出多个边缘，对应 measure_select='all'）
            std::vector<EdgeHit> profileEdges;
            for (int m = 1; m < M - 2; ++m) {
                const float gPrev = grad[m - 1];
                const float gNext = grad[m + 1];
                // 局部极值：梯度符号跨零
                if (!((gPrev >= 0.0f && gNext <= 0.0f) || (gPrev <= 0.0f && gNext >= 0.0f))) {
                    continue;
                }
                if (std::fabs(grad[m]) < thr) continue;

                const bool rising = grad[m] > 0.0f;   // 沿法线方向变亮

                // 极性过滤：单边模式逐条过滤；边对模式留给配对时判首边（否则先删掉一条就配不成对）
                if (edgeMode == 0) {
                    if (polarity == 1 && rising) continue;    // 亮→暗
                    if (polarity == 2 && !rising) continue;   // 暗→亮
                }

                // 亚像素：抛物线插值（用 m-1, m, m+1 的梯度幅值）
                double sub = static_cast<double>(m);
                if (useSubpixel) {
                    const double g0 = std::fabs(grad[m - 1]);
                    const double g1 = std::fabs(grad[m]);
                    const double g2 = std::fabs(grad[m + 1]);
                    const double denom = g0 - 2 * g1 + g2;
                    if (std::fabs(denom) > 1e-9) {
                        sub += 0.5 * (g0 - g2) / denom;
                    }
                }

                const double off = sub - half;
                EdgeHit hit;
                hit.col = cx + nx * off;
                hit.row = cy + ny * off;
                hit.contrast = std::fabs(static_cast<double>(grad[m]));
                hit.rising = rising;
                hit.profile = k;
                profileEdges.push_back(hit);
            }
            rawEdgeCount += static_cast<int>(profileEdges.size());

            if (edgeMode == 1) {
                // 边对：同一剖面内相邻、极性相反、间距不超过 pairWidth 的两条边缘
                for (size_t i = 0; i + 1 < profileEdges.size(); ++i) {
                    const EdgeHit &e1 = profileEdges[i];
                    const EdgeHit &e2 = profileEdges[i + 1];
                    if (e1.rising == e2.rising) continue;
                    const double gap = std::hypot(e2.col - e1.col, e2.row - e1.row);
                    if (gap <= 0.0 || gap > static_cast<double>(pairWidth)) continue;
                    if (polarity == 1 && e1.rising) continue;    // 首边必须是 亮→暗
                    if (polarity == 2 && !e1.rising) continue;   // 首边必须是 暗→亮
                    PairHit p;
                    p.col1 = e1.col;
                    p.row1 = e1.row;
                    p.col2 = e2.col;
                    p.row2 = e2.row;
                    p.width = gap;
                    p.contrast = std::min(e1.contrast, e2.contrast);
                    p.profile = k;
                    pairs.push_back(p);
                    ++i;   // 非重叠配对：一条边最多属于一个边对
                }
            } else {
                hits.insert(hits.end(), profileEdges.begin(), profileEdges.end());
            }
        }

        // 排序：默认按位置（采集顺序即沿卡尺方向的空间顺序）；按对比度时从强到弱
        if (sortType == 1) {
            if (edgeMode == 1) {
                std::stable_sort(pairs.begin(), pairs.end(),
                                 [](const PairHit &a, const PairHit &b) {
                                     return a.contrast > b.contrast;
                                 });
            } else {
                std::stable_sort(hits.begin(), hits.end(),
                                 [](const EdgeHit &a, const EdgeHit &b) {
                                     return a.contrast > b.contrast;
                                 });
            }
        }
        if (maxCount > 0) {
            if (edgeMode == 1 && static_cast<int>(pairs.size()) > maxCount)
                pairs.resize(static_cast<size_t>(maxCount));
            if (edgeMode == 0 && static_cast<int>(hits.size()) > maxCount)
                hits.resize(static_cast<size_t>(maxCount));
        }

        // ---- 结果字段 ----
        const int resultCount = edgeMode == 1 ? static_cast<int>(pairs.size())
                                              : static_cast<int>(hits.size());
        double totalContrast = 0.0;
        double maxContrast = 0.0;
        for (const EdgeHit &e : hits) {
            totalContrast += e.contrast;
            maxContrast = std::max(maxContrast, e.contrast);
        }
        for (const PairHit &p : pairs) {
            totalContrast += p.contrast;
            maxContrast = std::max(maxContrast, p.contrast);
        }
        const double avgContrast = resultCount > 0 ? totalContrast / resultCount : 0.0;

        double minWidth = 0.0;
        double maxWidth = 0.0;
        double totalWidth = 0.0;
        for (const PairHit &p : pairs) {
            totalWidth += p.width;
            if (minWidth <= 0.0 || p.width < minWidth) minWidth = p.width;
            if (maxWidth <= 0.0 || p.width > maxWidth) maxWidth = p.width;
        }
        const double avgWidth = !pairs.empty()
                                    ? totalWidth / static_cast<double>(pairs.size())
                                    : 0.0;

        m_params[QStringLiteral("edgeCount")] = resultCount;
        m_params[QStringLiteral("rawEdgeCount")] = rawEdgeCount;
        m_params[QStringLiteral("pairCount")] = static_cast<int>(pairs.size());
        m_params[QStringLiteral("avgWidth")] = avgWidth;
        m_params[QStringLiteral("minWidth")] = minWidth;
        m_params[QStringLiteral("maxWidth")] = maxWidth;
        m_params[QStringLiteral("avgContrast")] = avgContrast;
        m_params[QStringLiteral("maxContrast")] = maxContrast;
        m_params["moduleStatus"] = true;
        m_outputImage = m_inputImage;

        // 输出边缘点/边对中点（Measure：value=结果数，extraValues=[col0,row0,col1,row1,...]）
        QVector<double> points;
        if (edgeMode == 1) {
            for (const PairHit &p : pairs) {
                points.append((p.col1 + p.col2) * 0.5);
                points.append((p.row1 + p.row2) * 0.5);
            }
        } else {
            for (const EdgeHit &e : hits) {
                points.append(e.col);
                points.append(e.row);
            }
        }
        MeasureResult res;
        res.type = QStringLiteral("caliper");
        res.valueName = edgeMode == 1 ? QStringLiteral("边对") : QStringLiteral("边缘点");
        res.valid = resultCount > 0;
        res.value = static_cast<double>(resultCount);
        res.extraValues = points;
        auto resObj = QSharedPointer<DataObject>::create();
        resObj->setMeasureResult(res);
        setOutputData(1, resObj);

        auto numObj = QSharedPointer<DataObject>::create();
        numObj->setValue(double(resultCount));
        setOutputData(2, numObj);

        // 边对明细（type="caliperPair"：extraValues=[col1,row1,col2,row2,width,...]）
        if (edgeMode == 1 && !pairs.empty()) {
            MeasureResult pairRes;
            pairRes.type = QStringLiteral("caliperPair");
            pairRes.valueName = QStringLiteral("边对宽度");
            pairRes.valid = true;
            pairRes.value = avgWidth;
            pairRes.point1 = QPointF((pairs.front().col1 + pairs.front().col2) * 0.5,
                                     (pairs.front().row1 + pairs.front().row2) * 0.5);
            QVector<double> pairExtra;
            for (const PairHit &p : pairs) {
                pairExtra.append(p.col1);
                pairExtra.append(p.row1);
                pairExtra.append(p.col2);
                pairExtra.append(p.row2);
                pairExtra.append(p.width);
            }
            pairRes.extraValues = pairExtra;
            auto pairObj = QSharedPointer<DataObject>::create();
            pairObj->setMeasureResult(pairRes);
            setOutputData(3, pairObj);
        } else {
            setOutputData(3, QSharedPointer<DataObject>());
        }
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvCaliperNode error:" << e.what();
        m_params[QStringLiteral("lastError")] = QString::fromUtf8(e.what());   // 原因到面板（2026-10-26）
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params[QStringLiteral("lastError")] = unknownExceptionReason();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvCaliperNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvCaliperNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

RoiShape OpencvCaliperNode::geometryRoi() const
{
    RoiShape s;
    s.type = RoiType::Line;
    s.p1 = QPointF(m_params.value(QStringLiteral("col1"), 100.0).toDouble(),
                   m_params.value(QStringLiteral("row1"), 100.0).toDouble());
    s.p2 = QPointF(m_params.value(QStringLiteral("col2"), 100.0).toDouble(),
                   m_params.value(QStringLiteral("row2"), 200.0).toDouble());
    return s;
}

void OpencvCaliperNode::applyGeometryRoi(const RoiShape &shape)
{
    if (shape.type != RoiType::Line)
        return;
    setParam(QStringLiteral("col1"), shape.p1.x());
    setParam(QStringLiteral("row1"), shape.p1.y());
    setParam(QStringLiteral("col2"), shape.p2.x());
    setParam(QStringLiteral("row2"), shape.p2.y());
}
