#pragma once

#include "HalconNode.h"

/// 相机标定算子：对标定板图像执行 FindCalibObject + CalibrateCameras，
/// 输出标定误差/内参（String 端口）与内参向量（Matrix 端口）
class CalibrationNode : public HalconNode
{
    Q_OBJECT
public:
    explicit CalibrationNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
};
