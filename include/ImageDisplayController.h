#pragma once

#include <QObject>
#include <QMap>
#include <QString>
#include <QVector>
#include <functional>

#include <halconcpp/HalconCpp.h>
#include "HalconWindow.h"   // OverlayShape / RoiShape / RoiType

class QLabel;
class NodeBase;
class FlowScene;

/// 图像显示路由与画布交互（从 MainWindow 抽出的真实实现，对齐现状能力）。
///
/// 职责：
///   - 显示决策：下拉框选择 > 画布选中节点 > 兜底（resolveDisplayNode）；
///   - 每场景的"显式显示源"选择表（下拉框写入；不同流程各自独立记忆）；
///   - 显示去重：setImage + 叠加图元 + 来源标签（原先 4 处重复代码统一）；
///   - 测量结果 → 叠加图元（collectOverlayFromNode）；
///   - 画布 ROI 取点状态机：开始取点 / 收笔写回测量节点参数。
///
/// 场景与画布选中通过 provider 注入（MainWindow 只提供两个取值函数），
/// 控制器不反向依赖 MainWindow；无画布时可单独实例化用于决策逻辑测试。
class ImageDisplayController : public QObject
{
    Q_OBJECT
public:
    explicit ImageDisplayController(HalconWindow *view = nullptr, QLabel *sourceLabel = nullptr,
                                    QObject *parent = nullptr);

    void setSceneProvider(std::function<FlowScene *()> provider) { m_sceneProvider = std::move(provider); }
    void setCanvasSelectionProvider(std::function<NodeBase *()> provider)
    { m_canvasSelectionProvider = std::move(provider); }

    /// 显示决策：下拉框选择 > 画布选中节点 > fallbackNode
    NodeBase *resolveDisplayNode(NodeBase *fallbackNode = nullptr) const;

    /// 显示指定节点的输出端口 0（含叠加图元与来源标签）；无可用图像返回 false
    bool displayNodeOutput(NodeBase *node);

    /// 低层显示：图像 +（可选）叠加图元 + 来源标签；overlaySource 为空则不动叠加
    void showImage(const HalconCpp::HImage &image, const QString &sourceName,
                   NodeBase *overlaySource = nullptr);

    /// 测量结果 → 叠加图元（供 HalconWindow 绘制）
    QVector<OverlayShape> collectOverlayFromNode(NodeBase *node) const;

    /// 每场景的显式显示源（下拉框），nullptr 表示未显式选择
    void setSelectedOutputNode(FlowScene *scene, NodeBase *node);
    NodeBase *selectedOutputNode(FlowScene *scene) const;
    void clearSelection();
    /// 删除场景时清掉该场景的显式显示源键（否则场景对象已释放但键仍在 → 悬垂指针）
    void removeScene(FlowScene *scene);

    /// B3：主画布直接编辑算子几何 ROI（模板框/搜索框/卡尺线…）。
    /// 传 nullptr 关闭。返回 false = 该节点没有可编辑几何或无画布。
    /// 运行中调用是安全的：只写参数（线程安全），执行线程下一轮读到新值。
    /// preferSearchRegion=true（默认，VM 口径）：有检测区域的工具，画布上的可编辑框就是
    /// 搜索区域，模板框退为只读绿框；置 false 则改回编辑模板框句柄。
    bool showNodeGeometry(NodeBase *node, bool preferSearchRegion = true);
    /// B3：主画布直接编辑该节点的「搜索区域」（模板匹配等有 searchRegion* 参数的工具）。
    /// 与几何编辑互斥；同样只写参数，运行中改下一轮生效。
    bool showNodeSearchRegion(NodeBase *node);
    /// 该节点是否声明了搜索区域参数（searchRegionWidth/Height）
    static bool hasSearchRegion(NodeBase *node);
    /// 当前主画布是否在编辑"搜索区域"（参数面板用它回显开关状态）
    bool editingSearchRegion() const { return m_searchRegionNode != nullptr; }
    /// 开始在画布上取点；返回 false = 无画布或无目标节点（调用方不应做提示）
    bool startRoiPick(NodeBase *node);
    /// 画布 ROI 收笔：清状态 + 关 ROI 编辑 + 写回目标节点参数；返回目标节点（无目标/取消/未知类型为 nullptr）
    NodeBase *writeRoiToNode(const RoiShape &shape);

private:
    HalconWindow *m_imageView;
    QLabel *m_sourceLabel;
    QMap<FlowScene *, NodeBase *> m_selectedOutputNodes;
    NodeBase *m_roiPickNode = nullptr;
    /// B3：主画布当前正在编辑几何的节点（与"取点"互斥）
    NodeBase *m_geometryRoiNode = nullptr;
    /// B3：主画布当前正在编辑搜索区域的节点
    NodeBase *m_searchRegionNode = nullptr;
    std::function<FlowScene *()> m_sceneProvider;
    std::function<NodeBase *()> m_canvasSelectionProvider;
};
