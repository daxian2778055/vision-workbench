#pragma once

#include "HalconNode.h"

/// OpenCV 卡尺测量节点：沿直线方向逐采样窗口做法线方向一维边缘搜索（亚像素），
/// 支持单边/边对模式、剖面投影与结果排序，
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
    /// VM「模块结果」口径：只声明结果字段（顺序/名称/单位/精度与官方字段表一致）
    QVector<NodeResultField> resultDisplayFields() const override;

    RoiType geometryRoiType() const override { return RoiType::Line; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;

private:
    /// VM 参数条件显隐：官方多处注明「边缘模式设置为边缘对时，才有该参数」。
    void applyParamVisibility(QWidget *panel);
};
