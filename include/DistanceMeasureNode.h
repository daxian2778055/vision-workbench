#pragma once

#include "HalconNode.h"

/// 两点距离测量算子
class DistanceMeasureNode : public HalconNode
{
    Q_OBJECT
public:
    explicit DistanceMeasureNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
    /// VM4.4「点点测量 → 模块结果」角度字段（2026-11-17 本机帮助取证）
    QVector<NodeResultField> resultDisplayFields() const override;
};
