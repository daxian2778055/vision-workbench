#pragma once

#include "HalconNode.h"
#include <opencv2/core.hpp>

/// OpenCV 模板匹配节点：cv::matchTemplate（灰度/NCC），支持从图像训练保存模板
class OpencvTemplateMatchNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvTemplateMatchNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    RoiType geometryRoiType() const override { return RoiType::RotatedRect; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;

    /// 按几何从灰度图抠出直立模板块（轴对齐或旋转框）
    static cv::Mat extractTemplatePatch(const cv::Mat &gray, const RoiShape &roi);

    /// 编辑器可视化：当前模板的强梯度点（图像坐标）。GOC 直接用这些点打分；
    /// 灰度法只作为模板质量参考。
    QVector<QPointF> editorFeaturePoints() const override;

    /// 有效边缘阈值：手动模式取 edgeThreshold；自动模式取模板梯度 70 分位（clamp 6..80）。
    /// 打分与可视化必须共用它，否则"看到的点"和"打分的点"又分叉。
    double effectiveEdgeThreshold(const cv::Mat &tmpl, int aperture) const;
};
