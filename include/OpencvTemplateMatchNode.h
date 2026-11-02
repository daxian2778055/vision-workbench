#pragma once

#include "HalconNode.h"
#include <QJsonObject>
#include <opencv2/core.hpp>

/// 一个模板对象（VM「特征模板」页里的一行）。
/// 为什么需要它：原来模板只是节点上的一个 templatePath 字符串——没有"创建/编辑/删除"、
/// 也没有每模板独立的边缘参数。对齐 VM 的第一步是把模板从"一个路径"变成"一条记录"。
struct TemplateEntry
{
    QString name;                       ///< 显示名（默认取文件名）
    QString imagePath;                  ///< 模板图路径（相对项目目录或绝对）
    RoiShape shape;                     ///< 主形状（=shapes.first()，老字段兼容）
    /// 全部形状（VM 一个模板可由多块形状组成）。空 = 回退 shape。
    QVector<RoiShape> shapes;
    double edgeThreshold = 20.0;        ///< 对比度阈值（手动模式）
    bool autoThreshold = true;          ///< 阈值模式：true=自动（模板梯度分位数）
    int aperture = 3;                   ///< 特征尺度（Sobel 核 3/5/7）
    int coarseLongSide = 480;           ///< 速度尺度（粗搜长边）

    QJsonObject toJson() const;
    static TemplateEntry fromJson(const QJsonObject &obj);
    /// 参与匹配的形状集合（shapes 为空时返回 {shape}）
    QVector<RoiShape> allShapes() const
    {
        if (!shapes.isEmpty())
            return shapes;
        return shape.type == RoiType::None ? QVector<RoiShape>() : QVector<RoiShape>{shape};
    }
};

/// OpenCV 模板匹配节点：cv::matchTemplate（灰度/NCC），支持从图像训练保存模板
class OpencvTemplateMatchNode : public HalconNode
{
    Q_OBJECT
public:
    explicit OpencvTemplateMatchNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    /// 单个模板的完整匹配流程（原 run 的实现）。多模板模式由 run() 逐模板调用后取最优。
    void runSingle(bool autoSwitch);
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    RoiType geometryRoiType() const override { return RoiType::RotatedRect; }
    RoiShape geometryRoi() const override;
    void applyGeometryRoi(const RoiShape &shape) override;

    /// 按几何从灰度图抠出直立模板块（轴对齐或旋转框）
    static cv::Mat extractTemplatePatch(const cv::Mat &gray, const RoiShape &roi);

    /// 编辑器可视化：当前模板的强梯度点（图像坐标）。GOC 直接用这些点打分；
    /// 灰度法只作为模板质量参考。
    QVector<QPointF> editorFeaturePoints() const override;

    /// 有效边缘阈值：手动模式取 edgeThreshold；自动模式取模板梯度 70 分位（clamp 6..80）。
    /// 打分与可视化必须共用它，否则"看到的点"和"打分的点"又分叉。
    double effectiveEdgeThreshold(const cv::Mat &tmpl, int aperture) const;

    // ---- 模板列表（VM 对齐）----
    /// 解析 m_params["templates"]；空列表时用 templatePath 合成一条（老方案零迁移）。
    QVector<TemplateEntry> templates() const;
    void setTemplates(const QVector<TemplateEntry> &entries);
    /// 当前生效的模板序号（越界自动夹取）
    int activeTemplateIndex() const;
    void setActiveTemplateIndex(int index);
    /// 当前生效的模板（列表为空时按节点参数合成一条，保证调用方永远拿到"一个模板"）
    TemplateEntry activeTemplate() const;
    /// 老 templatePath → 模板列表的一次性迁移（幂等）
    void migrateLegacyTemplate();

    /// 有效边缘阈值（显式传入本模板参数；run/可视化/对话框共用）
    static double effectiveEdgeThresholdFor(const cv::Mat &tmpl, int aperture,
                                            double manualThreshold, bool autoMode);
    /// 模板块 → 图像坐标的强梯度点（对话框预览与编辑器共用同一映射）。
    /// mask 非空且尺寸与模板块一致时，掩膜外（值为 0）的点被跳过。
    static QVector<QPointF> featurePointsForPatch(const cv::Mat &tmpl, const RoiShape &roi,
                                                  double threshold, int aperture,
                                                  const cv::Mat &mask = cv::Mat());

    /// 多形状模板：所有形状的并集包围盒（裁剪口径的唯一出处）
    static QRectF shapesBounds(const QVector<RoiShape> &shapes);
    /// 多形状模板：并集形状掩膜（8U，255=保留），尺寸 = shapesBounds 的四舍五入
    static cv::Mat shapesMask(const QVector<RoiShape> &shapes, const QRectF &box);
};
