#pragma once

#include "HalconNode.h"
#include "ObjectTracking.h"

#include <QHash>
#include <QStringList>

/// 流水线目标计数（2026-10-28）：YOLO 检测 → IOU 跟踪给稳定 ID → 过线判定 → 累计计数。
/// 与 DnnDetectNode 的区别：检测节点只回答"这一帧有几个"，本节点回答"**过去多少了**"，
/// 靠跨帧身份避免同一物体被重复计数（单帧检测会重复计，这是产线计数最常见的坑）。
class ObjectCountingNode : public HalconNode
{
    Q_OBJECT
public:
    explicit ObjectCountingNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;
    /// VM「模块结果」展示口径：累计计数/本帧新增/在跟目标数
    QVector<NodeResultField> resultDisplayFields() const override;

    /// 计数线 = 两个端点（lineA_x/y、lineB_x/y）。补上这三个虚函数后，
    /// 模块编辑器的「ROI区域」页签会自动出现绘制/清除按钮，主画布可直接拖端点，
    /// 拖完经 writeRoiToNode → applyGeometryRoi 写回参数并触发预览。
    RoiType geometryRoiType() const override { return RoiType::Line; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;

    /// 清零累计计数（换批次/换料时用；也可把 resetCounter 参数置 true 触发一次）
    void resetCounting();

    /// 每次开始执行时：若 resetOnStart 为真则清零累计（2026-10-28）
    void onRoundStart() override;

private:
    ObjectTracker m_tracker;
    LineCounter m_lineCounter;
    int m_totalCount = 0;
    QHash<QString, int> m_perClassCount;
    QString m_lastLineKey;   /// 线路参数指纹：变了就重建计数线并清侧记录
    bool m_initedLine = false;
};
