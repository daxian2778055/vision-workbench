#pragma once

#include "HalconNode.h"

/// OpenCV 分类器训练节点：从目录批量训练 ANN_MLP 并保存模型
class OpencvTrainClassifierNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvTrainClassifierNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
    /// VM「模块结果」展示口径：训练状态/样本数/类别数/特征维度
    QVector<NodeResultField> resultDisplayFields() const override;
};
