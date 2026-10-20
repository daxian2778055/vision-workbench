#pragma once

#include <QColor>
#include <QIcon>

#include "NodeBase.h"   // NodeType 枚举（类别判定的命名来源）

/// 算子图标调色板面（词条切片 B 图标族首抽，从 MainWindow::generateNodeIcon 抽出）：
///
/// 契约钉（与原体逐行同语义）：
///  · categoryColor = 类别色语义主判定，七式恒定（采集蓝 0x1890ff、处理绿 0x52c41a、
///    形状黄 0xfaad14、测量红 0xff4d4f、逻辑紫 0x722ed1、输出橙 0xff7632、越界缺省灰
///    0x8c8c8c）；界外枚举值（cast 越界）落缺省灰，原体 default 同形；
///  · categoryIcon = 固定绘制钉：16px 透明底、抗锯齿、实心圆（类别色）＋白色描边环，
///    两笔同序同矩形（2,2,12,12）；本面仅枚举入参（不取节点实例），无场景依赖。
class NodeIconPalette final {
public:
    /// 类别色语义表（主判定位）。
    static QColor categoryColor(NodeBase::NodeType category);

    /// 圆形图标合成（16px；透明底＋类别色圆＋白描边环）。
    static QIcon categoryIcon(NodeBase::NodeType category);
};
