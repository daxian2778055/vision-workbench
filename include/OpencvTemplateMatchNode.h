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
};
