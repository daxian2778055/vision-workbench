#pragma once

#include "HalconNode.h"

/// 二值化（OpenCV）
class OpencvThresholdNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvThresholdNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
    /// VM 4.4「图像二值化 → 模块结果」展示口径：耗时 / 模块状态 / 检测区域
    QVector<NodeResultField> resultDisplayFields() const override;

    bool supportsMaskEdit() const override { return true; }
    /// VM 图像二值化「ROI区域」：矩形 ROI（无 ROI 时全图处理）
    RoiType geometryRoiType() const override { return RoiType::Rect; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;

private:
    /// 按「二值化类型」显示/隐藏对应的运行参数行（VM 各类型只显示自己的参数）
    void applyParamVisibility(QWidget *panel);
};
