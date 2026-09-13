#pragma once

#include "HalconNode.h"
#include <QWidget>

/// 卡尺测量算子（直线卡尺，输出边缘点）
class CaliperMeasureNode : public HalconNode
{
    Q_OBJECT
public:
    explicit CaliperMeasureNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;

    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

signals:
    /// 请求在画布上绘制 ROI（由主窗口协调进入 ROI 编辑模式）
    void roiPickRequested();
};
