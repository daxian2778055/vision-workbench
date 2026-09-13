#pragma once

#include "HalconNode.h"

/// OpenCV 卡尺测量节点：沿直线方向逐采样窗口做法线方向一维边缘搜索（亚像素），
/// 替代替换版环境下损坏的 HALCON metrology 测量内核
class OpencvCaliperNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvCaliperNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    RoiType geometryRoiType() const override { return RoiType::Line; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;
};
