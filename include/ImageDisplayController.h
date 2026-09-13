#pragma once

#include <QObject>
#include <QLabel>

class HalconWindow;
class NodeBase;
class FlowScene;

/// 管理图像显示路由、HalconWindow 交互
class ImageDisplayController : public QObject
{
    Q_OBJECT
public:
    explicit ImageDisplayController(HalconWindow *view, QLabel *sourceLabel = nullptr,
                                     QObject *parent = nullptr);

    /// 显示指定节点的输出图像
    void displayNodeImage(NodeBase *node);

    /// 解析当前应显示的节点（下拉框选择 > 画布选中 > 兜底）
    NodeBase *resolveDisplayNode(NodeBase *fallbackNode = nullptr,
                                  FlowScene *currentScene = nullptr) const;

    /// 设置/获取用户显式选择的输出节点
    void setSelectedOutputNode(FlowScene *scene, NodeBase *node);
    NodeBase *selectedOutputNode(FlowScene *scene) const;

    /// 清除选择
    void clearSelection(FlowScene *scene);

    HalconWindow *view() const { return m_imageView; }

private:
    HalconWindow *m_imageView;
    QLabel *m_sourceLabel;
    QMap<FlowScene *, NodeBase *> m_selectedOutputNodes;
};
