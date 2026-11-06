#pragma once

#include "HalconNode.h"

/// OpenCV 灰度统计节点：替代替换版环境下损坏的 HALCON MinMaxGray 统计
class OpencvPixelStatsNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvPixelStatsNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
    /// VM「模块结果」展示口径：最小/最大/平均灰度
    QVector<NodeResultField> resultDisplayFields() const override;
};
