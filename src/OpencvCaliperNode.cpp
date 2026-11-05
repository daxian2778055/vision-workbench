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
#include <QElapsedTimer>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSet>
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
    double score = 0.0;    ///< VM 极值点评分（0~1）；无计分项时 = contrast/255
    double offset = 0.0;   ///< 相对投影区域中心沿法线的位移（位置评分输入）
    double gray = 0.0;     ///< 边缘处灰度（灰度评分输入）
    int polarity = 3;      ///< 官方结果：1=从黑到白，2=从白到黑，3=任意极性
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
    double score = 0.0;    ///< VM 极值点评分（0~1）
    double offset = 0.0;   ///< 边对中心相对投影区域中心沿法线的位移
    double gray = 0.0;     ///< 两条边缘的灰度均值
    int polarity = 3;      ///< 首边极性（官方 1=黑到白 / 2=白到黑 / 3=任意）
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
        // VM「投影方向」：官方「纵向投影…投向矩形宽 / 横向投影…投向矩形高」。
        // 这里「投向 X」= 一维信号沿 X 方向排列。0=横向（信号沿法线，老行为）、
        // 1=纵向（信号沿卡尺线）；两者共用同一个矩形 ROI，只是交换采样轴。
        grouped(makeEnumParam(QStringLiteral("projectionDirection"), 0,
                              {QStringLiteral("横向投影（投向矩形高）"),
                               QStringLiteral("纵向投影（投向矩形宽）")},
                              QStringLiteral("投影方向")),
                QStringLiteral("卡尺")),
        // VM「模糊边缘」：官方「开启后，可增强候选边缘点集的提取能力，获取更多候选点集数……
        // 但耗时增加明显」。当前实现为把极值门槛降到一半以纳入更弱的候选，未复刻其额外计算开销。
        grouped(makeBoolParam(QStringLiteral("blurEdge"), false,
                              QStringLiteral("模糊边缘（纳入更弱候选）")),
                QStringLiteral("边缘")),
        // ---- 边缘提取 ----
        grouped(makeEnumParam(QStringLiteral("edgeMode"), 0,
                              {QStringLiteral("单边"), QStringLiteral("边对")},
                              QStringLiteral("边缘模式")),
                QStringLiteral("边缘")),
        grouped(makeIntParam(QStringLiteral("pairWidth"), 20, 1, 10000,
                             QStringLiteral("边对最大间距（仅边对模式）"), QStringLiteral("px")),
                QStringLiteral("边缘")),
        // ---- VM「边缘对宽度」+ 极值点评分（官方 GUID-59D1E6A2「参数配置」）----
        // 官方：边缘对宽度「单独调节该参数无法直接筛选出期望的边缘对」，只是计分因子的
        // 缩放因子。故新增 expectedPairWidth 专用于计分；pairWidth 保留为硬上限以兼容老方案。
        grouped(makeDoubleParam(QStringLiteral("expectedPairWidth"), 0.0, 0.0, 10000.0,
                                QStringLiteral("边缘对宽度（计分缩放因子，0=不启用间距评分）"),
                                QStringLiteral("px")),
                QStringLiteral("评分")),
        grouped(makeBoolParam(QStringLiteral("scoreSpacing"), false,
                              QStringLiteral("间距计分（|距离/宽度−1|，理想 0）")),
                QStringLiteral("评分")),
        grouped(makeBoolParam(QStringLiteral("scoreSpacingDiff"), false,
                              QStringLiteral("间距差计分（|(距离−宽度)/宽度|，理想 0）")),
                QStringLiteral("评分")),
        grouped(makeBoolParam(QStringLiteral("scoreRelativeSpacingDiff"), false,
                              QStringLiteral("相对间距差计分（双边，(距离−宽度)/宽度）")),
                QStringLiteral("评分")),
        grouped(makeBoolParam(QStringLiteral("scoreContrast"), false,
                              QStringLiteral("对比度计分（对比度/255，边对取均值）")),
                QStringLiteral("评分")),
        // 官方位置评分四类：输入都是「候选中心相对投影区域中心的位置差」，
        // 我们的投影区域中心 = 卡尺线中点，位置差 = 候选沿法线的位移 offset。
        grouped(makeBoolParam(QStringLiteral("scorePosition"), false,
                              QStringLiteral("位置计分（|offset|/法线半宽）")),
                QStringLiteral("评分")),
        grouped(makeBoolParam(QStringLiteral("scoreRelativePosition"), false,
                              QStringLiteral("相对位置计分（offset/法线半宽，可正可负）")),
                QStringLiteral("评分")),
        grouped(makeBoolParam(QStringLiteral("scoreNormPosition"), false,
                              QStringLiteral("归一位置计分（|offset|/边缘对宽度）")),
                QStringLiteral("评分")),
        grouped(makeBoolParam(QStringLiteral("scoreNormRelativePosition"), false,
                              QStringLiteral("归一相对位置计分（offset/边缘对宽度）")),
                QStringLiteral("评分")),
        grouped(makeBoolParam(QStringLiteral("scoreGray"), false,
                              QStringLiteral("灰度计分（边缘灰度/255，边对取均值）")),
                QStringLiteral("评分")),
        grouped(makeEnumParam(QStringLiteral("curveType"), 1,
                              {QStringLiteral("递增"), QStringLiteral("递减")},
                              QStringLiteral("曲线类型")),
                QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("curveX0"), 0.0, -1000.0, 1000.0,
                                QStringLiteral("曲线起点 x0")),
                QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("curveXc"), 0.5, -1000.0, 1000.0,
                                QStringLiteral("曲线 X中点 xc")),
                QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("curveX1"), 1.0, -1000.0, 1000.0,
                                QStringLiteral("曲线终点 x1")),
                QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("curveY0"), 0.0, 0.0, 1.0,
                                QStringLiteral("曲线低分 y0")),
                QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("curveY1"), 1.0, 0.0, 1.0,
                                QStringLiteral("曲线高分 y1")),
                QStringLiteral("评分")),
        grouped(makeDoubleParam(QStringLiteral("threshold"), 10.0, 0.0, 255.0,
                                QStringLiteral("边缘梯度阈值")),
                QStringLiteral("边缘")),
        // VM「滤波尺寸」：一维差分滤波核，核越大抑制噪声越强但极值位置精度越差。
        // 1 = 纯一阶差分（老行为），奇数 3/5/7/9 = 先盒式平滑再差分。
        grouped(makeIntParam(QStringLiteral("filterSize"), 1, 1, 9,
                             QStringLiteral("滤波尺寸（1=一阶差分，奇数越大越抗噪）")),
                QStringLiteral("边缘")),
        grouped(makeIntParam(QStringLiteral("polarity"), 0, 0, 2,
                             QStringLiteral("边缘0极性：0=任意，1=白到黑，2=黑到白")),
                QStringLiteral("边缘")),
        // VM「边缘1极性」：仅边缘对模式出现，约束配对中的第二条边
        grouped(makeIntParam(QStringLiteral("edge1Polarity"), 0, 0, 2,
                             QStringLiteral("边缘1极性（边对模式）：0=任意，1=白到黑，2=黑到白")),
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
        // VM「排序方式」四选一。0/1 保持老语义（方向正向 / 分数降序），
        // 2=分数升序、3=方向逆向为新增。
        grouped(makeEnumParam(QStringLiteral("sortType"), 0,
                              {QStringLiteral("方向正向"), QStringLiteral("分数降序"),
                               QStringLiteral("分数升序"), QStringLiteral("方向逆向")},
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
    setParamDirect(QStringLiteral("edgeScores"), QString());
    setParamDirect(QStringLiteral("avgScore"), 0.0);
    setParamDirect(QStringLiteral("elapsedMs"), 0.0);
    setParamDirect(QStringLiteral("edgePolarities"), QString());
    setParamDirect(QStringLiteral("edgePositions"), QString());
    setParamDirect(QStringLiteral("edgeGrays"), QString());
}

void OpencvCaliperNode::run(bool /*autoSwitch*/)
{
    try {
        QElapsedTimer runTimer;          // 官方模块结果「耗时（ms）」
        runTimer.start();
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
        // VM「模糊边缘」：官方说明是「增强候选边缘点集的提取能力，获取更多候选点集数」，
        // 未给具体算法。这里把极值门槛减半以纳入更弱的候选（默认关 = 老行为不变）。
        const bool blurEdge = m_params.value(QStringLiteral("blurEdge"), false).toBool();
        const double effThr = blurEdge ? (thr * 0.5) : thr;
        const int polarity = m_params.value(QStringLiteral("polarity"), 0).toInt();
        const int edge1Polarity = m_params.value(QStringLiteral("edge1Polarity"), 0).toInt();
        int filterSize = qBound(1, m_params.value(QStringLiteral("filterSize"), 1).toInt(), 9);
        if (filterSize % 2 == 0)
            --filterSize;                 // VM 滤波核取奇数
        const int edgeMode = m_params.value(QStringLiteral("edgeMode"), 0).toInt();
        const int pairWidth = m_params.value(QStringLiteral("pairWidth"), 20).toInt();
        const int projectionType = m_params.value(QStringLiteral("projectionType"), 0).toInt();
        const int profileWidth = m_params.value(QStringLiteral("profileWidth"), 1).toInt();
        const bool useSubpixel = m_params.value(QStringLiteral("subpixel"), true).toBool();
        const int maxCount = m_params.value(QStringLiteral("maxCount"), 0).toInt();
        const int sortType = m_params.value(QStringLiteral("sortType"), 0).toInt();
        // VM 极值点评分（默认全关 = 老行为：分数退化为 contrast/255）
        const double expectedW =
            m_params.value(QStringLiteral("expectedPairWidth"), 0.0).toDouble();
        const bool scoreSpacing = m_params.value(QStringLiteral("scoreSpacing"), false).toBool();
        const bool scoreSpacingDiff =
            m_params.value(QStringLiteral("scoreSpacingDiff"), false).toBool();
        const bool scoreRelativeSpacingDiff =
            m_params.value(QStringLiteral("scoreRelativeSpacingDiff"), false).toBool();
        const bool scoreContrast =
            m_params.value(QStringLiteral("scoreContrast"), false).toBool();
        const bool scorePosition =
            m_params.value(QStringLiteral("scorePosition"), false).toBool();
        const bool scoreRelativePosition =
            m_params.value(QStringLiteral("scoreRelativePosition"), false).toBool();
        const bool scoreNormPosition =
            m_params.value(QStringLiteral("scoreNormPosition"), false).toBool();
        const bool scoreNormRelativePosition =
            m_params.value(QStringLiteral("scoreNormRelativePosition"), false).toBool();
        const bool scoreGray =
            m_params.value(QStringLiteral("scoreGray"), false).toBool();
        const bool anyScore = scoreSpacing || scoreSpacingDiff
                              || scoreRelativeSpacingDiff || scoreContrast
                              || scorePosition || scoreRelativePosition
                              || scoreNormPosition || scoreNormRelativePosition
                              || scoreGray;
        const int curveType = m_params.value(QStringLiteral("curveType"), 1).toInt();
        const double cx0 = m_params.value(QStringLiteral("curveX0"), 0.0).toDouble();
        const double cxc = m_params.value(QStringLiteral("curveXc"), 0.5).toDouble();
        const double cx1 = m_params.value(QStringLiteral("curveX1"), 1.0).toDouble();
        const double cy0 = m_params.value(QStringLiteral("curveY0"), 0.0).toDouble();
        const double cy1 = m_params.value(QStringLiteral("curveY1"), 1.0).toDouble();

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
        const int M = half * 2 + 1; // 法线方向采样点数
        const int projHalf = std::max(0, (profileWidth - 1) / 2);   // 总采样数 = 2*projHalf+1 ≤ profileWidth

        const int projectionDirection =
            m_params.value(QStringLiteral("projectionDirection"), 0).toInt();
        const bool longitudinal = (projectionDirection == 1);
        // 横向投影（老行为）：一维信号沿法线 n，K+1 个剖面沿卡尺线 u 排列；
        // 纵向投影：          一维信号沿卡尺线 u，M 个剖面沿法线 n 排列。
        // 两种方向覆盖同一个矩形 ROI（卡尺线 × ±half），只是交换「信号轴」与「剖面轴」。
        const int nSig = longitudinal ? (K + 1) : M;
        const int nProf = longitudinal ? M : (K + 1);
        const double sigHalf = longitudinal ? (K * 0.5) : half;
        const double sigSpacing = longitudinal ? (L / std::max(1, K)) : 1.0;
        const double sx = longitudinal ? ux : nx;   // 一维信号方向
        const double sy = longitudinal ? uy : ny;
        const double qx = longitudinal ? nx : ux;   // 投影聚合方向（= 剖面排列方向）
        const double qy = longitudinal ? ny : uy;
        const double midCol = (c1 + c2) * 0.5;      // 投影区域中心
        const double midRow = (r1 + r2) * 0.5;

        // 双线性采样。VM：投影区域超出图像边界时「填充其他灰度值」；
        // 旧实现越界返回 0，在亮背景上会凭空造出一条 亮→黑 的伪边缘，改用边界复制。
        auto sampleGray = [&gray](double px, double py) -> double {
            const double cx = std::min(std::max(px, 0.0), double(gray.cols - 1));
            const double cy = std::min(std::max(py, 0.0), double(gray.rows - 1));
            const int x0 = static_cast<int>(cx);
            const int y0 = static_cast<int>(cy);
            const int x1 = std::min(x0 + 1, gray.cols - 1);
            const int y1 = std::min(y0 + 1, gray.rows - 1);
            const double fx = cx - x0;
            const double fy = cy - y0;
            return gray.at<uchar>(y0, x0) * (1 - fx) * (1 - fy)
                   + gray.at<uchar>(y0, x1) * fx * (1 - fy)
                   + gray.at<uchar>(y1, x0) * (1 - fx) * fy
                   + gray.at<uchar>(y1, x1) * fx * fy;
        };

        std::vector<EdgeHit> hits;    // 单边模式：保留的边缘
        std::vector<PairHit> pairs;   // 边对模式：保留的边对
        int rawEdgeCount = 0;         // 极性过滤后的候选边缘总数（含未输出的）

        for (int pIdx = 0; pIdx < nProf; ++pIdx) {
            double baseX = 0.0, baseY = 0.0;
            double profOffset = 0.0;   // 本剖面相对投影区域中心沿法线的位移
            if (longitudinal) {
                profOffset = pIdx - half;
                baseX = midCol + nx * profOffset;
                baseY = midRow + ny * profOffset;
            } else {
                const double t = (K == 0) ? 0.0 : static_cast<double>(pIdx) / K;
                baseX = c1 + dx * t;      // 与老实现逐位一致
                baseY = r1 + dy * t;
            }

            // 一维投影剖面：沿信号轴 sig 取 nSig 个点，沿聚合轴 q 按 projectionType
            // 聚合 profileWidth 个采样点（横向投影时 q=卡尺线方向，与老实现一致）。
            std::vector<float> profile(nSig);
            for (int m = 0; m < nSig; ++m) {
                const double s = (m - sigHalf) * sigSpacing;
                const double px = baseX + sx * s;
                const double py = baseY + sy * s;
                if (projHalf == 0) {
                    profile[m] = static_cast<float>(sampleGray(px, py));
                    continue;
                }
                double sum = 0.0;
                double maxV = -1e9;
                double minV = 1e9;
                int samples = 0;
                for (int j = -projHalf; j <= projHalf; ++j) {
                    const double v = sampleGray(px + qx * j, py + qy * j);
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

            // VM 步骤3「一维差分滤波」：以 m+0.5 为中心，右半核均值 − 左半核均值。
            // 注意必须是**两侧各自求均值再相减**；若写成「先盒式平滑再差分」，
            // 代数上会约掉中间项、只剩两个端点，噪声完全没被平均掉（实测 k=5 反而
            // 比 k=1 报出更多伪边缘）。filterSize=1 时每侧 1 点，逐位等于老的一阶差分。
            const int side = (filterSize - 1) / 2 + 1;
            auto clampIdx = [&](int i) { return std::min(nSig - 1, std::max(0, i)); };
            std::vector<float> grad(std::max(0, nSig - 1));
            for (int m = 0; m < nSig - 1; ++m) {
                double right = 0.0, left = 0.0;
                for (int j = 0; j < side; ++j) {
                    right += profile[clampIdx(m + 1 + j)];
                    left += profile[clampIdx(m - j)];
                }
                grad[m] = static_cast<float>((right - left) / side);
            }

            // 收集本剖面所有满足阈值的跨零极值点（一个窗口可输出多个边缘，对应 measure_select='all'）
            std::vector<EdgeHit> profileEdges;
            for (int m = 1; m < nSig - 2; ++m) {
                const float gp = grad[m - 1];
                const float gc = grad[m];
                const float gn = grad[m + 1];
                const double a0 = std::fabs(static_cast<double>(gp));
                const double a1 = std::fabs(static_cast<double>(gc));
                const double a2 = std::fabs(static_cast<double>(gn));
                // 局部极值（非极大值抑制）：幅值不小于左侧、严格大于右侧 —— 平顶只在末端记一次。
                // 旧逻辑要求「梯度符号跨零」，只对一阶差分下阶跃两侧恰为 0 成立；
                // 滤波核放大后梯度峰两侧同号，真实边缘会被全部漏检（实测 k=5 → 0 条）。
                if (!(a1 >= a0 && a1 > a2))
                    continue;
                if (a1 < effThr)
                    continue;

                const bool rising = gc > 0.0f;   // 沿法线方向变亮

                // 极性过滤：单边模式逐条过滤；边对模式留给配对时判首边（否则先删掉一条就配不成对）
                if (edgeMode == 0) {
                    if (polarity == 1 && rising) continue;    // 亮→暗
                    if (polarity == 2 && !rising) continue;   // 暗→亮
                }

                // 亚像素：抛物线插值（用 m-1, m, m+1 的梯度幅值）。
                // grad[m] 是 profile[m+1]−profile[m]（或左右均值差），其物理位置在
                // m 与 m+1 之间，故基准取 m+0.5；旧实现取 m 会让边缘位置系统性
                // 偏半个采样间隔（实测 step=5 的纵向投影把 y=100 的边缘报成 95）。
                double sub = static_cast<double>(m) + 0.5;
                if (useSubpixel) {
                    const double denom = a0 - 2 * a1 + a2;
                    if (std::fabs(denom) > 1e-9)
                        sub += 0.5 * (a0 - a2) / denom;
                }

                const double offDist = (sub - sigHalf) * sigSpacing;
                EdgeHit hit;
                hit.col = baseX + sx * offDist;
                hit.row = baseY + sy * offDist;
                hit.contrast = a1;
                // 位置评分输入恒为「相对投影区域中心沿法线的位移」：
                // 横向投影时信号就在法线上（offDist）；纵向投影时来自剖面位置（profOffset）。
                hit.offset = longitudinal ? profOffset : offDist;
                hit.polarity = rising ? 1 : 2;   // 官方结果：1=从黑到白，2=从白到黑
                // 边缘处灰度：在亚像素位置对投影剖面做线性插值（灰度评分输入）
                {
                    const double base = std::floor(sub);
                    const int i0 = std::min(nSig - 1, std::max(0, static_cast<int>(base)));
                    const int i1 = std::min(nSig - 1, i0 + 1);
                    const double f = sub - base;
                    hit.gray = profile[i0] * (1.0 - f) + profile[i1] * f;
                }
                hit.rising = rising;
                hit.profile = pIdx;
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
                    if (polarity == 1 && e1.rising) continue;    // 边缘0极性：白→黑
                    if (polarity == 2 && !e1.rising) continue;   // 边缘0极性：黑→白
                    // VM「边缘1极性」：只在边缘对模式出现，约束配对中的第二条边
                    if (edge1Polarity == 1 && e2.rising) continue;
                    if (edge1Polarity == 2 && !e2.rising) continue;
                    PairHit p;
                    p.col1 = e1.col;
                    p.row1 = e1.row;
                    p.col2 = e2.col;
                    p.row2 = e2.row;
                    p.width = gap;
                    // 官方「如果是边缘对查找模式，则使用两个边缘的平均对比度作为评分因子」；
                    // 旧实现用 min，与官方口径不符。
                    p.contrast = (e1.contrast + e2.contrast) * 0.5;
                    p.offset = (e1.offset + e2.offset) * 0.5;
                    p.gray = (e1.gray + e2.gray) * 0.5;
                    p.polarity = e1.rising ? 1 : 2;
                    p.profile = pIdx;
                    pairs.push_back(p);
                    ++i;   // 非重叠配对：一条边最多属于一个边对
                }
            } else {
                hits.insert(hits.end(), profileEdges.begin(), profileEdges.end());
            }
        }

        // ---- VM 步骤6「极值点评分」----
        // 官方只给计分函数示意图（x0/xc/x1 控制走势、y0~y1 控制值域），未给解析式；
        // 这里按分段线性 S 曲线实现，参数名与值域与官方一致。计分项的 x 统一定义为
        // 「偏差量」(>=0，越小越好)，这样各类计分项可以共用一组曲线参数。
        auto incCurve = [&](double x) -> double {
            if (!(cx1 > cx0))
                return cy0;
            const double t = qBound(0.0, (x - cx0) / (cx1 - cx0), 1.0);
            const double tc = qBound(0.0, (cxc - cx0) / (cx1 - cx0), 1.0);
            double s = 0.0;
            if (tc <= 0.0)
                s = (t > 0.0) ? 1.0 : 0.0;
            else if (tc >= 1.0)
                s = (t >= 1.0) ? 1.0 : 0.0;
            else
                s = (t <= tc) ? 0.5 * t / tc : 0.5 + 0.5 * (t - tc) / (1.0 - tc);
            return cy0 + (cy1 - cy0) * s;
        };
        // curveType=1（递减，默认）：偏差越小分越高；0（递增）：反转。
        // 不强制取绝对值：官方「相对位置 / 归一相对位置」的输入可正可负，
        // 负侧由曲线 x0（可设为负值）决定，不能先 abs 掉。
        auto scoreOfDeviation = [&](double dev) -> double {
            const double inc = incCurve(dev);
            return (curveType == 0) ? inc : (cy0 + cy1 - inc);
        };
        // 双边计分：左右两个单边函数镜像合并，理想值 0（官方「相对间距差」专用）
        auto bilateral = [&](double signedDev) -> double {
            const double inc = incCurve(std::fabs(signedDev));
            return (curveType == 0) ? (cy0 + cy1 - inc) : inc;
        };
        auto computeScore = [&](double contrast, double width,
                                double offset, double gray) -> double {
            if (!anyScore)
                return qBound(0.0, contrast / 255.0, 1.0);   // 老行为：分数 = 对比度归一化
            double s = 1.0;
            const bool usable = (width > 0.0 && expectedW > 0.0);
            if (scoreSpacing && usable)                       // 官方：边对距离/边缘对宽度
                s *= scoreOfDeviation(std::fabs(width / expectedW - 1.0));
            if (scoreSpacingDiff && usable)                    // 官方：|(距离−宽度)/宽度|
                s *= scoreOfDeviation(std::fabs((width - expectedW) / expectedW));
            if (scoreRelativeSpacingDiff && usable)           // 官方：(距离−宽度)/宽度，双边
                s *= bilateral((width - expectedW) / expectedW);
            if (scoreContrast)                                // 官方：对比度/255，最大 1.0
                s *= scoreOfDeviation(1.0 - qBound(0.0, contrast / 255.0, 1.0));
            // 官方位置评分四类：投影区域中心 = 卡尺线中点，位置差 = 沿法线的 offset
            const double normHalf = std::max(1.0, static_cast<double>(half));
            if (scorePosition)                                // 绝对位置差（两侧皆正）
                s *= scoreOfDeviation(std::fabs(offset) / normHalf);
            if (scoreRelativePosition)                        // 位置差（一侧负一侧正）
                s *= scoreOfDeviation(offset / normHalf);
            if (scoreNormPosition && expectedW > 0.0)         // 绝对归一化距离，分母=边对宽度
                s *= scoreOfDeviation(std::fabs(offset) / expectedW);
            if (scoreNormRelativePosition && expectedW > 0.0) // 归一化位置差，分母=边对宽度
                s *= scoreOfDeviation(offset / expectedW);
            if (scoreGray)                                    // 官方：边缘灰度（边对取均值）
                s *= scoreOfDeviation(gray / 255.0);
            return qBound(0.0, s, 1.0);
        };
        for (EdgeHit &e : hits)
            e.score = computeScore(e.contrast, 0.0, e.offset, e.gray);
        for (PairHit &p : pairs)
            p.score = computeScore(p.contrast, p.width, p.offset, p.gray);

        // ① 官方「最大结果数」：超过则按分数由高到低保留 N 个。仅在启用计分时生效，
        //    未启用时按老行为走 sortType 顺序截断，避免破坏已存方案。
        if (maxCount > 0 && anyScore) {
            if (edgeMode == 1) {
                std::stable_sort(pairs.begin(), pairs.end(),
                                 [](const PairHit &a, const PairHit &b) {
                                     return a.score > b.score;
                                 });
                if (static_cast<int>(pairs.size()) > maxCount)
                    pairs.resize(static_cast<size_t>(maxCount));
            } else {
                std::stable_sort(hits.begin(), hits.end(),
                                 [](const EdgeHit &a, const EdgeHit &b) {
                                     return a.score > b.score;
                                 });
                if (static_cast<int>(hits.size()) > maxCount)
                    hits.resize(static_cast<size_t>(maxCount));
            }
        }

        // ② VM「排序方式」四选一：0=方向正向、1=分数降序、2=分数升序、3=方向逆向。
        //    0/1 老语义不变（无计分项时 score 与 contrast 单调一致）。
        if (sortType == 1 || sortType == 2) {
            const bool desc = (sortType == 1);
            if (edgeMode == 1) {
                std::stable_sort(pairs.begin(), pairs.end(),
                                 [desc](const PairHit &a, const PairHit &b) {
                                     return desc ? a.score > b.score : a.score < b.score;
                                 });
            } else {
                std::stable_sort(hits.begin(), hits.end(),
                                 [desc](const EdgeHit &a, const EdgeHit &b) {
                                     return desc ? a.score > b.score : a.score < b.score;
                                 });
            }
        } else if (sortType == 3) {
            std::reverse(pairs.begin(), pairs.end());
            std::reverse(hits.begin(), hits.end());
        }
        if (maxCount > 0 && !anyScore) {
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
        // 官方「模块结果」逐结果字段：分数 / 边缘*极性 / 边缘*位置 / 灰度。
        // 顺序与输出点一致（端口 1 的 extraValues）。
        QStringList scoreTexts, polarityTexts, positionTexts, grayTexts;
        double totalScore = 0.0;
        for (const EdgeHit &e : hits) {
            scoreTexts << QString::number(e.score, 'f', 4);
            polarityTexts << QString::number(e.polarity);
            positionTexts << QString::number(e.offset, 'f', 2);
            grayTexts << QString::number(e.gray, 'f', 1);
            totalScore += e.score;
        }
        for (const PairHit &p : pairs) {
            scoreTexts << QString::number(p.score, 'f', 4);
            polarityTexts << QString::number(p.polarity);
            positionTexts << QString::number(p.offset, 'f', 2);
            grayTexts << QString::number(p.gray, 'f', 1);
            totalScore += p.score;
        }
        m_params[QStringLiteral("elapsedMs")] = static_cast<double>(runTimer.elapsed());
        m_params[QStringLiteral("edgeScores")] = scoreTexts.join(QLatin1Char(','));
        m_params[QStringLiteral("edgePolarities")] = polarityTexts.join(QLatin1Char(','));
        m_params[QStringLiteral("edgePositions")] = positionTexts.join(QLatin1Char(','));
        m_params[QStringLiteral("edgeGrays")] = grayTexts.join(QLatin1Char(','));
        m_params[QStringLiteral("avgScore")] =
            resultCount > 0 ? totalScore / resultCount : 0.0;
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

/// VM 卡尺工具的条件显隐。官方「参数配置」原文中反复出现的条款：
///   「边缘模式设置为边缘对时，才有该参数」——适用于 边缘1极性 / 边缘对宽度 /
///    归一位置 / 归一相对位置 / 间距 / 间距差 / 相对间距差；
///   「边缘模式设置为单边缘时，该参数为对比度；设置为边缘对时，为边缘对对比度」。
/// 计分曲线参数则只在至少一个计分项启用时出现（官方把曲线配置挂在计分项下面）。
/// 实现沿用 OpencvBlobNode::applyParamVisibility 的先例：自动面板给控件设的
/// objectName 就是参数名，用 QFormLayout::setRowVisible 连标签一起隐藏。
void OpencvCaliperNode::applyParamVisibility(QWidget *panel)
{
    if (!panel)
        return;

    auto enumIndex = [panel, this](const QString &name) -> int {
        if (auto *c = panel->findChild<QComboBox *>(name))
            return c->currentIndex();
        return m_params.value(name, 0).toInt();
    };
    auto boolValue = [panel, this](const QString &name) -> bool {
        if (auto *c = panel->findChild<QCheckBox *>(name))
            return c->isChecked();
        return m_params.value(name, false).toBool();
    };
    const bool pairMode = (enumIndex(QStringLiteral("edgeMode")) == 1);

    static const QSet<QString> scoreKeys {
        QStringLiteral("scoreSpacing"), QStringLiteral("scoreSpacingDiff"),
        QStringLiteral("scoreRelativeSpacingDiff"), QStringLiteral("scoreContrast"),
        QStringLiteral("scorePosition"), QStringLiteral("scoreRelativePosition"),
        QStringLiteral("scoreNormPosition"), QStringLiteral("scoreNormRelativePosition"),
        QStringLiteral("scoreGray")
    };
    static const QSet<QString> pairOnly {
        QStringLiteral("pairWidth"), QStringLiteral("edge1Polarity"),
        QStringLiteral("expectedPairWidth"),
        QStringLiteral("scoreSpacing"), QStringLiteral("scoreSpacingDiff"),
        QStringLiteral("scoreRelativeSpacingDiff"),
        QStringLiteral("scoreNormPosition"), QStringLiteral("scoreNormRelativePosition")
    };
    static const QSet<QString> curveKeys {
        QStringLiteral("curveType"), QStringLiteral("curveX0"), QStringLiteral("curveXc"),
        QStringLiteral("curveX1"), QStringLiteral("curveY0"), QStringLiteral("curveY1")
    };

    bool anyScore = false;
    for (const QString &k : scoreKeys)
        anyScore = anyScore || boolValue(k);

    // 遍历所有分组的 form（参数分散在「卡尺/边缘/评分/结果」多个 group）
    for (QFormLayout *form : panel->findChildren<QFormLayout *>()) {
        for (int row = 0; row < form->rowCount(); ++row) {
            QLayoutItem *fieldItem = form->itemAt(row, QFormLayout::FieldRole);
            QWidget *field = fieldItem ? fieldItem->widget() : nullptr;
            const QString name = field ? field->objectName() : QString();
            if (name.isEmpty())
                continue;
            bool show = true;
            if (pairOnly.contains(name))
                show = pairMode;
            else if (curveKeys.contains(name))
                show = anyScore;
            form->setRowVisible(row, show);

            // 官方：单边缘时叫「对比度」，边缘对时叫「边缘对对比度」
            if (name == QStringLiteral("scoreContrast")) {
                QLayoutItem *labelItem = form->itemAt(row, QFormLayout::LabelRole);
                if (auto *lb = qobject_cast<QLabel *>(labelItem ? labelItem->widget() : nullptr)) {
                    lb->setText(pairMode ? QStringLiteral("边缘对对比度计分（均值/255）")
                                         : QStringLiteral("对比度计分（对比度/255）"));
                }
            }
        }
    }
}

QWidget *OpencvCaliperNode::createParamPanel()
{
    QWidget *panel = createAutoParamPanel();
    // 边缘模式切换、任一计分开关切换都要即时刷新显隐
    if (auto *modeCombo = panel->findChild<QComboBox *>(QStringLiteral("edgeMode"))) {
        connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, panel](int) { applyParamVisibility(panel); });
    }
    static const QStringList scoreWidgets {
        QStringLiteral("scoreSpacing"), QStringLiteral("scoreSpacingDiff"),
        QStringLiteral("scoreRelativeSpacingDiff"), QStringLiteral("scoreContrast"),
        QStringLiteral("scorePosition"), QStringLiteral("scoreRelativePosition"),
        QStringLiteral("scoreNormPosition"), QStringLiteral("scoreNormRelativePosition"),
        QStringLiteral("scoreGray")
    };
    for (const QString &k : scoreWidgets) {
        if (auto *cb = panel->findChild<QCheckBox *>(k)) {
            connect(cb, &QCheckBox::toggled, this,
                    [this, panel](bool) { applyParamVisibility(panel); });
        }
    }
    applyParamVisibility(panel);
    return panel;
}

void OpencvCaliperNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
    applyParamVisibility(panel);
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
