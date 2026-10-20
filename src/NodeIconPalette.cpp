#include "NodeIconPalette.h"

#include <QPainter>
#include <QPen>
#include <QPixmap>

// 算子图标调色板面（词条切片 B 图标族首抽，从 MainWindow::generateNodeIcon 抽出）：
//  · categoryColor = 类别色语义表（七式恒定；越界落缺省灰）——主判定位；
//  · categoryIcon = 固定绘制钉（16px 透明底＋抗锯齿＋实心圆＋白描边环，与原体同帧同序）。

QColor NodeIconPalette::categoryColor(NodeBase::NodeType category)
{
    switch (category) {
    case NodeBase::IMAGE_ACQUISITION:
        return QColor(0x18, 0x90, 0xff);  // 蓝色 - 图像采集
    case NodeBase::IMAGE_PROCESSING:
        return QColor(0x52, 0xc4, 0x1a);  // 绿色 - 图像处理
    case NodeBase::SHAPE_ANALYSIS:
        return QColor(0xfa, 0xad, 0x14);  // 黄色 - 形状分析
    case NodeBase::MEASUREMENT:
        return QColor(0xff, 0x4d, 0x4f);  // 红色 - 测量
    case NodeBase::LOGIC:
        return QColor(0x72, 0x2e, 0xd1);  // 紫色 - 逻辑控制
    case NodeBase::OUTPUT:
        return QColor(0xff, 0x76, 0x32);  // 橙色 - 输出
    default:
        break;
    }
    return QColor(0x8c, 0x8c, 0x8c);  // 灰色（缺省/越界）
}

QIcon NodeIconPalette::categoryIcon(NodeBase::NodeType category)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 实心圆（类别色）
    painter.setBrush(categoryColor(category));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(2, 2, 12, 12);

    // 白色描边环
    painter.setPen(QPen(Qt::white, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(2, 2, 12, 12);

    return QIcon(pixmap);
}
