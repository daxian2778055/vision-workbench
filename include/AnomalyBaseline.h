#pragma once

// 与 OpencvUtil.h 同一惯例：本头可能被 HALCON/windows 头之前包含，
// Windows 的 min/max 宏会直接破坏 OpenCV 头里的 std::min/std::max
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <QString>
#include <opencv2/core.hpp>
#include <vector>

/// OK 样本统计基线（G-P1-2 后端 A）：逐像素 Welford 在线累积 (μ, M2)，判据 `|x-μ| / max(σ, floor)`。
///
/// 为什么与黄金差影分开：差影的模型是**一张图 + 一个灰度阈值**，OK 样本自身有波动
/// （光照漂移、随机纹理、轻微形变）时必然假阳；本类的模型是**逐像素分布**，阈值语义是
/// "偏离几个 σ"，同样的波动被 σ 吸收，才能把阈值降到检得出小缺陷的位置。
///
/// 内存与体积：只存 μ/M2 两个 CV_32F 平面（2448×2048 ≈ 40 MB），**不留在方案里** ——
/// 基线随 `AnomalyDetectNode::baselinePath` 落独立二进制文件，理由见 `save()`。
class AnomalyBaseline
{
public:
    /// 文件魔数与版本（`VFPB` = VisionFlowPlatform Baseline）
    static constexpr qint32 kMagic = 0x42504656;
    static constexpr qint32 kVersion = 1;

    bool isEmpty() const { return m_mean.empty(); }
    int sampleCount() const { return m_n; }
    cv::Size size() const { return m_mean.size(); }
    /// 参考图（CV_32F 单通道）：首个教学样本。**对齐必须用它而不是 μ**——
    /// 多张 OK 图平均会把工件边缘糊成渐变带，按它做相位相关会把正常件的边缘判成错位。
    cv::Mat reference() const { return m_ref; }
    /// μ 平面（CV_32F 只读副本），给单测核对累积结果与真值
    cv::Mat mean() const { return m_mean; }

    void clear();

    /// 累积一个样本。尺寸与已累积基线不符时返回 false 且**不改动状态**（绝不静默 resize：
    /// 尺寸变了就是另一套模型，硬套只会产出看着像噪声的热图）。
    bool addSample(const cv::Mat &gray8);

    /// σ 平面（= sqrt(M2/n)，n<2 时全零）。只读副本，给测试与可视化用。
    cv::Mat sigma() const;

    /// score = |x-μ| / max(σ, sigmaFloor)，CV_32F；尺寸不符返回空矩阵。
    /// `sigmaFloor`（灰度单位）不可省：平坦区 σ→0 时分数无界，整图都会越过 k 阈值。
    cv::Mat scoreMap(const cv::Mat &gray8, double sigmaFloor) const;

    bool save(const QString &path, QString *error = nullptr) const;
    bool load(const QString &path, QString *error = nullptr);

private:
    cv::Mat m_mean;   ///< CV_32F
    cv::Mat m_m2;     ///< CV_32F，平方差累加量（Welford 的 M2）
    cv::Mat m_ref;    ///< CV_32F，首个样本（对齐参考）
    int m_n = 0;
};

/// OK 样本 PCA 模型（G-P1-2 后端 B）：**整体**建模 OK 流形，判据是"重构不回来多少"。
///
/// 与后端 A 的分工（互补，不是替代）：A 逐像素独立，看得见"这点亮了"，看不见"局部都合理、
/// 整体形状不对"（元件少贴/转位/图文错位这类**结构异常**）。PCA 只保留 OK 样本张成的前 q 个
/// 主成分，异常图在这组基上重构不出来 → 残差热图。
///
/// 两个必须付的代价（都是设计决定，不是省事）：
///  · **批量拟合**：PCA 不是在线统计，教学样本要先攒着再一次拟合（样本数上限见调用方），
///    所以 `AnomalyPcaModel::train` 收的是整批样本，而不是 `addSample`；
///  · **分析尺寸缩放**：D = 像素数，2448×2048 时协方差/基向量按 D 展开就是几百 MB 起，
///    故先把 ROI 等比缩到 `analysisMaxSide`（默认 256）再建模。热图随后放大了给外观，
///    精度损失记在"结构异常检测"这个用途上——要检 3 像素的小点该用后端 A。
class AnomalyPcaModel
{
public:
    static constexpr qint32 kMagic = 0x51504656;   ///< `VFPQ`
    static constexpr qint32 kVersion = 1;

    bool isEmpty() const { return m_basis.empty(); }
    int sampleCount() const { return m_n; }
    cv::Size analysisSize() const { return m_size; }
    /// 实际保留的主成分数（数值退化的方向会被丢弃，故可能小于请求的 components）
    int components() const { return m_basis.rows; }

    /// 用整批样本拟合。`samples` 必须是同一尺寸的灰度图（调用方负责先对齐、先缩放）。
    /// `components` 夹到 [1, 样本数-1]（超过就没残差了——q=N-1 时任何样本都能完美重构）。
    /// 少于 2 个样本返回 false：一个点没有流形可言。
    bool train(const std::vector<cv::Mat> &samples, int components, QString *error = nullptr);

    /// 残差热图打分：`|x - 重构(x)| / max(σ_残差, floorValue)`，CV_32F；尺寸不符返回空。
    cv::Mat scoreMap(const cv::Mat &gray8, double floorValue) const;

    bool save(const QString &path, QString *error = nullptr) const;
    bool load(const QString &path, QString *error = nullptr);

private:
    cv::Size m_size;
    int m_n = 0;
    cv::Mat m_mean;     ///< 1×D CV_32F
    cv::Mat m_basis;    ///< q×D CV_32F，行 = 单位主成分
    cv::Mat m_rsigma;   ///< 1×D CV_32F，逐像素训练残差标准差
};
