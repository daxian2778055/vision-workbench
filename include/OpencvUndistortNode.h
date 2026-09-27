#pragma once

#include "HalconNode.h"

/// 畸变校正算子：读取标定链路写入 CalibrationManager 的 9 元内参
/// （fx fy cx cy k1 k2 p1 p2 rms，默认键 cam_params）或手填内参，去畸变后吐出图像。
/// 这是推进计划 §3.2 结论 A 所指「③ 内参只有写侧、没有读侧」的消费端。
class OpencvUndistortNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvUndistortNode(QObject *parent = nullptr);
    void init() override;
    void run(bool autoSwitch = true) override;
};
