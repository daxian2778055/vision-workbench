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
    /// 屏蔽形状（保留区里再挖掉的部分：logo/水印/背景）。掩膜 = 保留 − 屏蔽。
    QVector<RoiShape> excludes;
    /// 匹配中心（锚点）：相对模板中心的偏移（模板像素、尺度 1）。默认 0 = 匹配点与匹配框中心重合。
    /// VM 的「匹配中心 X/Y + 锁定」就是这一个点：结果里的「匹配点」= 它在场景中的位置。
    double anchorDx = 0.0;
    double anchorDy = 0.0;
    bool anchorLocked = false;          ///< 仅约束编辑器里的 X/Y 输入框（锁定后不可改）
    /// 擦除轮廓点区域（VM「擦除轮廓点」）：圆形区域内的特征点不参与匹配。
    QVector<RoiShape> erases;           ///< type=Circle，p1=圆心，p2=圆周一点（半径）
    // ── VM「模板配置参数」官方口径（见 docs/VM4.4高精度匹配对齐规范.md）──
    // 注：VM 用图像金字塔顶层/底层；我们没有金字塔，按等效效果实现（在规范里注明）。
    // 默认值：阈值 5 灰度差、不稀疏、不过滤；建模板时由 suggestParams 给 VM 口径建议值。
    int contrastThreshold = 5;          ///< 对比度阈值 1~255：特征点与背景的灰度差
    bool autoThreshold = false;         ///< 阈值模式：true=自动（按模板对比度自动生成）
    int featureScale = 1;               ///< 特征尺度 1~20（≤速度尺度）：特征点提取粒度，1 最精细
    int speedScale = 3;                 ///< 速度尺度 1~20：搜索降采样倍率，越大越快越易漏检
    int minChainLength = 1;             ///< 最小链长：特征链短于此长度不保留
    bool chainModeAuto = false;         ///< 链长模式：true=自动（按特征尺度给）

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
    /// VM 4.4「（高精度）匹配 → 模块结果」展示口径
    QVector<NodeResultField> resultDisplayFields() const override;

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
    /// 解析 m_params["templates"]；空列表时用 templatePath 合成一条（单模板训练模式）。
    QVector<TemplateEntry> templates() const;
    void setTemplates(const QVector<TemplateEntry> &entries);
    /// 当前生效的模板序号（越界自动夹取）
    int activeTemplateIndex() const;
    void setActiveTemplateIndex(int index);
    /// 当前生效的模板（列表为空时按节点参数合成一条，保证调用方永远拿到"一个模板"）
    TemplateEntry activeTemplate() const;

    /// 有效边缘阈值（显式传入本模板参数；run/可视化/对话框共用）
    static double effectiveEdgeThresholdFor(const cv::Mat &tmpl, int aperture,
                                            double manualThreshold, bool autoMode);
    /// 模板内容指纹（FNV-1a，全像素）。导出是为了让测试锁住「内容变了缓存键就变」
    /// 这条不变量（复核 P1-4）；生产代码只在变形缓存键里用。
    static quint64 templateFingerprint(const cv::Mat &tmpl);
    /// 锚点（模板局部系偏移）→ 图像坐标（2026-11-08 复核 P1-2）。
    /// 必须与 warpTemplate 所用 cv::getRotationMatrix2D 的线性部分同一口径
    /// [cos sin; -sin cos]：模板就是按该矩阵旋转后再去匹配的；锚点若用共轭旋转
    /// [cos -sin; sin cos]，角度非 0 时匹配点会相对中心整体镜像（差 2θ）。
    /// 缩放在旋转之前应用（与 warpTemplate 里 M.at(0,0)*=scaleX 的顺序一致）。
    static QPointF mapAnchorToImage(double centerX, double centerY, double angleDeg,
                                    double anchorDx, double anchorDy, double scale);
    /// 模板块 → 图像坐标的强梯度点（对话框预览与编辑器共用同一映射）。
    /// mask 非空且尺寸与模板块一致时，掩膜外（值为 0）的点被跳过。
    static QVector<QPointF> featurePointsForPatch(const cv::Mat &tmpl, const RoiShape &roi,
                                                  double threshold, int aperture,
                                                  const cv::Mat &mask = cv::Mat(),
                                                  int featureScale = 1, int minChain = 1,
                                                  const QVector<RoiShape> &erases = QVector<RoiShape>());

    /// 建/改模板后的参数建议（VM「创建模板成功 → 参数自动变成 8.0/2/37」同款口径）。
    /// 目的：操作员画完模板就能直接跑，不必自己猜 4 个边缘参数。
    struct ParamSuggestion
    {
        int contrastThreshold = 20;   ///< 对比度阈值（灰度差 1~255）
        bool autoThreshold = true;    ///< 阈值模式：自动
        int featureScale = 3;         ///< 特征尺度 1~20
        int speedScale = 3;           ///< 速度尺度 1~20
        int minChainLength = 3;       ///< 最小链长
    };
    static ParamSuggestion suggestParams(const cv::Mat &tmpl);
    /// 按当前模板算建议值并写回节点参数（训练/建模板后调用）；模板不可用时返回 false
    bool applySuggestedParams();

    /// VM 4.4「结果显示 → 结果判断」：按已启用的固定条件（数量/角度/尺度/分数/
    /// 匹配点/匹配框中心）判定本轮 OK/NG。reason 返回第一条不满足的条件说明。
    /// 未启用任何条件时返回 true（保持旧行为）。
    bool resultJudgmentOk(QString *reason) const;

    /// 多形状模板：所有形状的并集包围盒（裁剪口径的唯一出处）
    static QRectF shapesBounds(const QVector<RoiShape> &shapes);
    /// 多形状模板：并集形状掩膜（8U，255=保留），尺寸 = shapesBounds 的四舍五入。
    /// excludes 非空时从保留区里减掉（logo/水印等屏蔽）；shapes 为空但 excludes 非空时
    /// 默认整块保留再减。
    static cv::Mat shapesMask(const QVector<RoiShape> &shapes, const QRectF &box,
                              const QVector<RoiShape> &excludes = QVector<RoiShape>());
};
