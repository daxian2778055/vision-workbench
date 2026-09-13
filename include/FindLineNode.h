#pragma once

#include "HalconNode.h"
#include <QWidget>

/// 线段查找算子（亚像素直线拟合）
class FindLineNode : public HalconNode
{
    Q_OBJECT
public:
    explicit FindLineNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;

    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

signals:
    /// 请求在画布上绘制 ROI（由主窗口协调进入 ROI 编辑模式）
    void roiPickRequested();
};
