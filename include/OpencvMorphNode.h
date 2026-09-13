#pragma once

#include "HalconNode.h"

/// OpenCV 形态学节点：腐蚀/膨胀/开/闭，替代替换版环境下损坏的 HALCON 区域形态学
class OpencvMorphNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvMorphNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
};
