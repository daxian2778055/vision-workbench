#pragma once

#include "HalconNode.h"

/// OpenCV Blob 分析节点：输入二值图（>0 为前景），输出连通域统计
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
