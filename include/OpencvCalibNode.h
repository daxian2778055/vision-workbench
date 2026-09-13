#pragma once

#include "HalconNode.h"
#include <opencv2/core.hpp>
#include <QVector>

/// OpenCV 相机标定节点：棋盘格检测 + 多帧采集 + calibrateCamera，
/// 替代替换版环境下不可用的 HALCON 标定链路（CalibrationNode）
class OpencvCalibNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvCalibNode(QObject *parent = nullptr);
    ~OpencvCalibNode() override;

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

private:
    /// 执行标定（cornerSets 已够帧数时调用）；返回是否成功
    bool performCalibration(QString *detail);

    QVector<std::vector<cv::Point2f>> m_cornerSets;    // 每帧棋盘格角点
    QVector<std::vector<cv::Point3f>> m_objectPoints;  // 对应 3D 点（mm）
    cv::Size m_imageSize {0, 0};                       // 最近一帧检测图像的尺寸
    int m_consecutiveFailures = 0;                     // 连续检测失败计数（环境提示用）
};
