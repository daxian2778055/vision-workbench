#pragma once

#include "HalconNode.h"
#include "AnomalyBaseline.h"

/// 异常检测（G-P1-2）：**只用 OK 样本**建模，检"没见过的一切"，输出**热图**。
///
/// 与 `OpencvDefectNode`（黄金差影）的分工不是"新旧替换"而是模型不同：
///  · 差影 = 一张黄金图 + 固定灰度阈值 ⇒ OK 样本自身有波动时必假阳；
///  · 本节点 = N 张 OK 图的逐像素分布 (μ,σ) + `k·σ` 判据 ⇒ 同样的波动被 σ 吸收。
/// 所以参数没有一处能互转，硬塞进同一节点只会让参数彼此失效。
///
/// 三段：教学（`trainMode`）→ 打分（score 图，见 `scoreMap`）→ 判定（阈值/形态学/面积）。
/// 教学与推理走同一张图：`trainMode=累加` 连续跑 N 轮即完成 N 样本建模，不需要额外 UI。
///
/// 输出约定（验收口径是热图）：
///  · `m_outputImage` 按 `overlayMode` 给纯热图 / 叠加 / 原图+框；热图**灰度 128 恰好等于判定阈值**
///    （score/k 的一半映射到 0..255），人眼和程序读同一把尺子；
///  · Measure 端口 `extraValues` 是 `x y w h` 四元组序列，与 `OpencvDefectNode` **完全同口径**，
///    下游与统计侧零改动；
///  · 与仓内所有 OpenCV 替代节点一致：**不构造 HRegion**（区域算子跨进程反转的既有实证）。
class AnomalyDetectNode : public HalconNode
{
    Q_OBJECT
public:
    explicit AnomalyDetectNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    RoiType geometryRoiType() const override { return RoiType::Rect; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;
    bool supportsMaskEdit() const override { return true; }

    /// 热图定标：`score == kSigma` → **128**（阈值处＝半亮），`2×kSigma` 及以上饱和到 255。
    /// 之所以暴露成静态方法：这条约定是"人眼看到的变红"与"程序判的 NG"必须同一把尺子，
    /// 只写在注释里守不住。
    static cv::Mat heatFromScore(const cv::Mat &score, double kSigma);

private:
    /// 在"当前后端的工作尺寸"上把 cur 对回 ref（ref 为空 = 不做变换），并写回 align* 回显参数。
    void alignWorking(const cv::Mat &ref, const cv::Mat &cur, cv::Mat &aligned);
    /// 后端 A：按 `trainMode` 累积/加载逐像素基线，并对（已对齐的）`inspect` 打分。
    /// 返回 false 表示模型不可用，`error` 给可见原因；`status` 是教学回显文本。
    bool teachAndScorePixel(const cv::Mat &inspect, const QString &path, int trainMode,
                            cv::Mat &score, QString &status, QString &error);
    /// 后端 B：把样本攒进环形缓冲（PCA 是批量拟合，不能像 Welford 那样增量），每轮重拟合后打分。
    /// 打分在缩放后的分析尺寸上做，再把 score 放回 ROI 尺寸（面积口径仍按原图像素）。
    bool teachAndScorePca(const cv::Mat &inspect, const QString &path, int trainMode,
                          int components, int analysisSide, cv::Mat &score, QString &status,
                          QString &error);

    AnomalyBaseline m_baseline;
    AnomalyPcaModel m_pca;
    std::vector<cv::Mat> m_pcaSamples;   ///< 后端 B 的 OK 样本环形缓冲（分析尺寸下）
    QString m_loadedFrom;   ///< 基线是从哪个文件加载来的；路径变了必须重载
};
