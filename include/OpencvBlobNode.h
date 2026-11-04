#pragma once

#include "HalconNode.h"

/// OpenCV Blob 分析节点：灰度阈值/极性 → 连通域 → 面积/宽高/圆度/孔洞筛选 → 排序与数量限制，
/// 结果含数量、面积统计与每个 Blob 的检测框（画布叠加用）
class OpencvBlobNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvBlobNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    bool supportsMaskEdit() const override { return true; }
};
