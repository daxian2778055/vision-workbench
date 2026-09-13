#pragma once

#include <QGraphicsObject>
#include <QPointF>
#include <QColor>

class Port;
class PortGraphicsItem;
class FlowScene;

/// 连线拖拽辅助类：绘制临时连线 + 高亮可连接端口
class ConnectionDragHelper : public QGraphicsObject
{
    Q_OBJECT

public:
    explicit ConnectionDragHelper(FlowScene *scene, QGraphicsItem *parent = nullptr);
    ~ConnectionDragHelper() override = default;

    /// 开始拖拽连线
    void startDrag(PortGraphicsItem *sourcePort, const QPointF &startPos);

    /// 更新拖拽位置
    void updateDrag(const QPointF &currentPos);

    /// 结束拖拽连线
    void endDrag();

    /// 取消拖拽
    void cancelDrag();

    /// 是否正在拖拽
    bool isDragging() const { return m_dragging; }

    /// 获取拖拽的源端口
    PortGraphicsItem *sourcePortItem() const { return m_sourcePortItem; }

    /// 高亮可连接的目标端口
    void highlightTargetPort(PortGraphicsItem *targetPort);

    /// 清除高亮
    void clearHighlight();

    // QGraphicsItem 接口
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

signals:
    /// 连线创建完成信号
    void connectionCreated(Port *sourcePort, Port *targetPort);

private:
    /// 检查端口是否可以连接
    bool canConnect(Port *source, Port *target) const;

    /// 获取端口类型匹配的颜色
    QColor getPortMatchColor(Port *source, Port *target) const;

    FlowScene *m_flowScene = nullptr;
    PortGraphicsItem *m_sourcePortItem = nullptr;
    PortGraphicsItem *m_highlightedPort = nullptr;
    QPointF m_startPos;
    QPointF m_currentPos;
    bool m_dragging = false;

    // 颜色常量
    const QColor VALID_COLOR = QColor(0x52, 0xc4, 0x1a);      // 绿色：可连接
    const QColor INVALID_COLOR = QColor(0xff, 0x4d, 0x4f);    // 红色：不可连接
    const QColor DRAG_LINE_COLOR = QColor(0x18, 0x90, 0xff);  // 蓝色：拖拽线
    const QColor HIGHLIGHT_COLOR = QColor(0x18, 0x90, 0xff);  // 蓝色：高亮
};
