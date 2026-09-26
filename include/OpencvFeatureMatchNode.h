#pragma once

#include "HalconNode.h"
#include <opencv2/core.hpp>

/// OpenCV 特征匹配节点：ORB/SIFT 检测 + BF/FLANN 比值检验 + 相似变换位姿
/// 定位结果写入 Fixture，供「位置修正」等下游节点消费（与模板匹配同一通道）
class OpencvFeatureMatchNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvFeatureMatchNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    RoiType geometryRoiType() const override { return RoiType::RotatedRect; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;
};
