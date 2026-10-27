#pragma once

#include "HalconNode.h"

/// 特征检索节点（2026-10-27）：图库建索引 + 以图查图。
/// 两种工作方式（参数 mode）：
///   · 入库（Add）：把输入图的特征存进 feature_samples；
///   · 检索（Search）：在库里找最像的 topK 个样本。
/// 输出：0 号=输入图透传；1 号=结果 JSON（TopK 明细）；2 号=测量结果（最高分）。
class FeatureSearchNode : public HalconNode
{
    Q_OBJECT
public:
    explicit FeatureSearchNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
};
