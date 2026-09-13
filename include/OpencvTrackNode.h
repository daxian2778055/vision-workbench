#pragma once

#include "HalconNode.h"
#include "OpenCvTracker.h"

/// OpenCV 目标跟踪节点：帧间模板匹配跟踪（无需 contrib tracking 模块）
/// 首次运行时以 initRect 区域为模板，后续帧在上一位置邻域搜索并输出目标中心
class OpencvTrackNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvTrackNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

private:
    SimpleTracker::Tracker m_tracker;  /// 帧间跟踪器（模板匹配）
    cv::Rect m_lastInitRect;           /// 上次初始化区域（变化时重新初始化）
};
