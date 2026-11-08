#pragma once

#include <QObject>
#include <QMap>
#include <QString>
#include <QVector>
#include <QVariant>
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
    /// 本轮结果变量提供者（FlowExecutor::nodeOutputsUpdated 的 vars 快照）。
    /// VM「结果显示 → 文本显示」里的 {elapsed}/{结果键} 优先从这里取；
    /// 未注入时回退读节点参数（结果类算子通常已把结果写回参数表）。
    void setResultVarsProvider(std::function<QVariantMap(NodeBase *)> provider)
    { m_resultVarsProvider = std::move(provider); }
    /// 双画面：副视图显示完整结果，主视图显示当前模块输入源（实装 4.4 语义）
    void setSecondaryView(HalconWindow *view) { m_secondaryView = view; }
    void setDualViewEnabled(bool on);
    bool dualViewEnabled() const { return m_dualViewEnabled; }

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
    /// 节点被删除时清掉所有指向它的裸指针（显式显示源/取点目标/几何编辑目标）。
    /// 缺这条时删除节点后下一次刷新/取点会解引用已析构对象。
    void forgetNode(NodeBase *node);

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
    /// 开始在画布上重画模板匹配的「检测区域」：polygon=true 逐点绘制，否则矩形框选。
    /// VM 模块窗是纯参数窗，绘制落在主画布上；返回 false = 无画布或该节点没有检测区域参数。
    bool startSearchRegionPick(NodeBase *node, bool polygon = false);
    /// 开始绘制 ROI 级屏蔽区（VM「屏蔽区创建=绘制」）：每次画一块，追加到 maskRegionPolygon。
    bool startMaskRegionPick(NodeBase *node);
    /// 开始重画算子几何 ROI（模板匹配的模板框）：rotatedRect=true 旋转矩形，否则轴对齐矩形。
    /// 返回 false = 无画布或该节点没有几何 ROI。
    bool startTemplateRoiPick(NodeBase *node, bool rotatedRect = true);
    /// 开始为检测区域字段取点：field 0=左列 1=上行 2=宽度 3=高度（VM 每个输入框右侧的取点按钮）。
    /// 取点完成后按字段写入 searchRegion* 并保留矩形句柄可继续微调。
    bool startCoordinatePick(NodeBase *node, int field);
    /// 通用几何 ROI 重画：按节点声明的 geometryRoiType 切到对应绘制模式（矩形/旋转矩形/圆/线）。
    /// VM 模块窗是纯页签窗，绘制入口在模块窗、落笔在主画布。
    bool startGeometryPick(NodeBase *node);
    /// 在主画布上编辑该节点的掩膜（左键保留/右键擦除）；返回 false = 无画布或节点不支持。
    bool startMaskEdit(NodeBase *node);
    /// 结束掩膜编辑（不写回；写回由 writeMaskBack 显式触发）。
    void stopMaskEdit();
    /// 掩膜编辑中把画布掩膜写回节点；返回 false = 当前没有掩膜编辑目标。
    bool writeMaskBack();
    bool maskEditActive() const { return m_maskEditNode != nullptr; }
    /// 画布 ROI 收笔：清状态 + 关 ROI 编辑 + 写回目标节点参数；返回目标节点（无目标/取消/未知类型为 nullptr）
    NodeBase *writeRoiToNode(const RoiShape &shape);

private:
    /// 双画面左窗：显示当前模块的输入图像（无输入则清空）；返回是否显示成功
    bool showInputOnPrimary(NodeBase *moduleNode);

    HalconWindow *m_imageView;
    HalconWindow *m_secondaryView = nullptr;
    bool m_dualViewEnabled = false;
    QLabel *m_sourceLabel;
    QMap<FlowScene *, NodeBase *> m_selectedOutputNodes;
    NodeBase *m_roiPickNode = nullptr;
    /// B3：主画布当前正在编辑几何的节点（与"取点"互斥）
    NodeBase *m_geometryRoiNode = nullptr;
    /// B3：主画布当前正在编辑搜索区域的节点
    NodeBase *m_searchRegionNode = nullptr;
    /// P1-8：主画布当前正在绘制 ROI 级屏蔽区的节点
    NodeBase *m_maskRegionNode = nullptr;
    /// 检测区域单字段取点的目标与字段（0=左列 1=上行 2=宽度 3=高度）
    NodeBase *m_coordinatePickNode = nullptr;
    int m_coordinatePickField = 0;
    /// 主画布掩膜编辑目标（左键保留/右键擦除，只写节点 editMask）
    NodeBase *m_maskEditNode = nullptr;
    std::function<FlowScene *()> m_sceneProvider;
    std::function<NodeBase *()> m_canvasSelectionProvider;
    std::function<QVariantMap(NodeBase *)> m_resultVarsProvider;
};
