#pragma once

#include "HalconNode.h"
#include <opencv2/core.hpp>

/// Blob 形状度量（VM「Blob分析」口径）。暴露为公共类型便于无界面数值测试：
///   - 圆形度 = 连通域面积 / (π·d²)，d = 质心到轮廓点最大距离，圆为 1
///   - 矩形度 = 连通域面积 / 最小外接矩形面积，矩形为 1
///   - boxAngle = 最小外接矩形长轴与 x 轴夹角；axisAngle = 二阶中心矩主轴角度
/// 两者角度均为「顺时针为正、逆时针为负」，值域 (-90, 90]。
struct BlobShapeMetrics
{
    double perimeter = 0.0;
    double longAxis = 0.0;
    double shortAxis = 0.0;
    double boxWidth = 0.0;
    double boxHeight = 0.0;
    double rectangularity = 0.0;
    double centerBias = 0.0;
    double boxAngle = 0.0;
    double axisAngle = 0.0;
    double circularity = 0.0;
};

/// OpenCV Blob 分析节点：灰度阈值/极性 → 连通域 → 面积/宽高/圆度/孔洞筛选 → 排序与数量限制，
/// 结果含数量、面积统计与每个 Blob 的检测框（画布叠加用）
class OpencvBlobNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvBlobNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    bool supportsMaskEdit() const override { return true; }
    /// 单个连通域的形状度量（VM 口径）。labels 为 connectedComponentsWithStats 的标签图，
    /// stat 为该标签的 CC_STAT_* 行，cx/cy 为该连通域的**全图坐标**质心。
    /// 声明为静态公共方法，便于测试直接断言度量数值，不必经由筛选开关间接推断。
    static BlobShapeMetrics measureShape(const cv::Mat &labels, int label, const int *stat,
                                         double cx, double cy);
    /// VM Blob「ROI区域」：矩形 ROI（无 ROI 时全图）
    RoiType geometryRoiType() const override { return RoiType::Rect; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;

private:
    /// 按「阈值方式」显示/隐藏对应的阈值参数（VM 各方式只显示自己的参数）
    void applyParamVisibility(QWidget *panel);
};
