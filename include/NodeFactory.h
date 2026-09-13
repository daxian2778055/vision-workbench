#pragma once

#include "NodeBase.h"
#include <QString>

/// 集中创建算子节点，供 FlowScene / 工具箱 / 拖放共用，避免多处 if-else 分叉
class NodeFactory
{
public:
    /// 根据分类与工具名创建节点；`nodeName` 可为具体工具名或分类占位（如「图像采集」），由内部解析
    static NodeBase *createNode(QObject *parent, NodeBase::NodeType category, const QString &nodeName);

    /// 解析工具库拖放的 mime 文本为「分类 + 展示名」
    static bool parseDropMimeText(const QString &mimeText, NodeBase::NodeType &outCategory, QString &outNodeName);

    /// 工具栏 QTreeWidgetItem::data(Qt::UserRole) 的稳定 ID（如 MvsImageSourceNode）→ 创建用分类与 nodeName（与双击 addNode 一致）
    static bool resolvePaletteToolId(const QString &paletteId,
                                    NodeBase::NodeType &outCategory, QString &outNodeName);

    /// 拖拽 MIME：`setData(toolPaletteMimeFormat(), paletteId_utf8)` 与文案拖放共存
    static QString toolPaletteMimeFormat();

private:
    static NodeBase *tryCreateNamedTool(QObject *parent, const QString &name);
    static NodeBase *createDefaultForCategory(QObject *parent, NodeBase::NodeType category);
};
