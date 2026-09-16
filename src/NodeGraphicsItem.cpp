#include "NodeGraphicsItem.h"
#include "NodeBase.h"
#include "Port.h"
#include "PortGraphicsItem.h"
#include "FlowScene.h"
#include "Connection.h"
#include <QPainter>
#include "ConnectionGraphicsItem.h"
#include <QMenu>
#include <QAction>
#include <QDialog>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QPushButton>
#include <QFont>
#include <QInputDialog>
#include <QLineEdit>
#include <QStyleOptionGraphicsItem>
#include <QStyle>
#include <QJsonObject>
#include <QSet>
#include <functional>

using MyConnection = MyProject::Connection;

namespace {
QJsonObject s_copiedParams;

QList<NodeBase *> collectUpstreamNodes(NodeBase *node)
{
    QList<NodeBase *> result;
    QSet<NodeBase *> visited;
    std::function<void(NodeBase *)> walk = [&](NodeBase *n) {
        if (!n || visited.contains(n))
            return;
        visited.insert(n);
        for (Port *in : n->inputPorts()) {
            if (!in)
                continue;
            for (MyConnection *c : in->connections()) {
                NodeBase *src = c ? c->getSourceNode() : nullptr;
                if (src && src != n) {
                    result.append(src);
                    walk(src);
                }
            }
        }
    };
    walk(node);
    return result;
}
}

NodeGraphicsItem::NodeGraphicsItem(NodeBase *node, QGraphicsItem *parent)
    : QGraphicsItem(parent), m_node(node), m_selected(false)
{
    setFlag(QGraphicsItem::ItemIsMovable);
    setFlag(QGraphicsItem::ItemIsSelectable);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges);
    
    m_boundingRect = QRectF(0, 0, node->size().width(), node->size().height());
    updatePorts();
}

NodeGraphicsItem::~NodeGraphicsItem()
{
    qDeleteAll(m_portItems.values());
}

QRectF NodeGraphicsItem::boundingRect() const
{
    return m_boundingRect;
}

void NodeGraphicsItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(widget);
    painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing, true);

    QColor fill(0x37, 0x39, 0x40);
    QColor edge(0x55, 0x58, 0x62);
    if (m_node->hasExecuted()) {
        if (m_node->executionSuccess()) {
            fill = QColor(0x1e, 0x38, 0x2e);
            edge = QColor(0x73, 0xd1, 0x3d);
        } else {
            fill = QColor(0x3c, 0x28, 0x29);
            edge = QColor(0xff, 0x78, 0x7f);
        }
    }

    // 禁用算子：灰显外观
    if (!m_node->isEnabled()) {
        fill = QColor(0x2a, 0x2a, 0x2e);
        edge = QColor(0x4a, 0x4a, 0x50);
    }

    painter->setBrush(fill);
    painter->setPen(QPen(edge, 1));
    painter->drawRoundedRect(m_boundingRect, 6, 6);

    QFont font = painter->font();
    font.setFamilies({QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Microsoft YaHei"), QStringLiteral("Segoe UI")});
    font.setPointSizeF(10.5);
    painter->setFont(font);
    painter->setPen(QColor(0xee, 0xee, 0xf4));
    painter->drawText(m_boundingRect, Qt::AlignCenter, m_node->fullName());

    const bool hilite = m_selected || (option && (option->state & QStyle::State_Selected));
    if (hilite) {
        painter->setPen(QPen(QColor(0x40, 0xa9, 0xff), 2.6));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(m_boundingRect.adjusted(-1, -1, 1, 1), 6, 6);
    }
}

NodeBase *NodeGraphicsItem::node() const
{
    return m_node;
}

PortGraphicsItem *NodeGraphicsItem::getPortGraphicsItem(Port *port) const
{
    return m_portItems.value(port, nullptr);
}

void NodeGraphicsItem::updatePosition()
{
    setPos(m_node->position());
}

void NodeGraphicsItem::syncNodeGeometry()
{
    if (!m_node)
        return;
    m_node->setPosition(pos());
    FlowScene *scene = dynamic_cast<FlowScene *>(this->scene());
    if (!scene)
        return;
    for (Port *port : m_node->inputPorts()) {
        PortGraphicsItem *portItem = m_portItems.value(port, nullptr);
        if (portItem)
            port->setPosition(portItem->mapToScene(QPointF(0, 0)));
        for (MyConnection *conn : port->connections()) {
            if (ConnectionGraphicsItem *connItem = scene->getGraphicsItemForConnection(conn))
                connItem->updatePath();
        }
    }
    for (Port *port : m_node->outputPorts()) {
        PortGraphicsItem *portItem = m_portItems.value(port, nullptr);
        if (portItem)
            port->setPosition(portItem->mapToScene(QPointF(0, 0)));
        for (MyConnection *conn : port->connections()) {
            if (ConnectionGraphicsItem *connItem = scene->getGraphicsItemForConnection(conn))
                connItem->updatePath();
        }
    }
}

void NodeGraphicsItem::updatePorts()
{
    // Remove existing port items
    qDeleteAll(m_portItems.values());
    m_portItems.clear();
    
    // Create input ports (left side)
    int inputCount = m_node->inputPorts().size();
    for (int i = 0; i < inputCount; ++i) {
        Port *port = m_node->inputPorts()[i];
        PortGraphicsItem *portItem = new PortGraphicsItem(port, this);
        portItem->setPos(0, (i + 1) * (m_boundingRect.height() / (inputCount + 1)));
        m_portItems[port] = portItem;
        // Set port position in scene coordinates
        port->setPosition(portItem->mapToScene(QPointF(0, 0)));
    }
    
    // Create output ports (right side)
    int outputCount = m_node->outputPorts().size();
    for (int i = 0; i < outputCount; ++i) {
        Port *port = m_node->outputPorts()[i];
        PortGraphicsItem *portItem = new PortGraphicsItem(port, this);
        portItem->setPos(m_boundingRect.width(), (i + 1) * (m_boundingRect.height() / (outputCount + 1)));
        m_portItems[port] = portItem;
        // Set port position in scene coordinates
        port->setPosition(portItem->mapToScene(QPointF(0, 0)));
    }
}

void NodeGraphicsItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    m_pressPos = pos();
    QGraphicsItem::mousePressEvent(event);
}

void NodeGraphicsItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsItem::mouseMoveEvent(event);
    FlowScene *scene = dynamic_cast<FlowScene *>(this->scene());
    if (!scene)
        return;
    for (QGraphicsItem *it : scene->selectedItems()) {
        if (auto *n = dynamic_cast<NodeGraphicsItem *>(it))
            n->syncNodeGeometry();
    }
    scene->update();
}

void NodeGraphicsItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsItem::mouseReleaseEvent(event);
    // 移动结束：位置发生变化时记录撤销快照（移动过程中不记录）
    if ((pos() - m_pressPos).manhattanLength() > 1.0) {
        if (FlowScene *scene = dynamic_cast<FlowScene *>(this->scene())) {
            scene->recordUndo();
        }
    }
}

void NodeGraphicsItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsItem::mouseDoubleClickEvent(event);
    if (auto *scene = dynamic_cast<FlowScene *>(this->scene()))
        scene->requestNodeEdit(m_node);
}

void NodeGraphicsItem::contextMenuEvent(QGraphicsSceneContextMenuEvent *event)
{
    FlowScene *scene = dynamic_cast<FlowScene *>(this->scene());
    if (!scene) return;

    const bool locked = scene->isEditLocked();
    const QList<NodeBase *> multi = scene->selectedNodes();
    const bool batch = multi.size() > 1;
    QMenu menu;

    QAction *editAction = menu.addAction(QStringLiteral("编辑模块…"));
    menu.addSeparator();
    QAction *batchEnableAction = nullptr;
    QAction *batchDisableAction = nullptr;
    QAction *batchDeleteAction = nullptr;
    if (batch) {
        batchEnableAction = menu.addAction(QStringLiteral("启用所选 (%1)").arg(multi.size()));
        batchDisableAction = menu.addAction(QStringLiteral("禁用所选 (%1)").arg(multi.size()));
        batchDeleteAction = menu.addAction(QStringLiteral("删除所选 (%1)").arg(multi.size()));
        batchEnableAction->setEnabled(!locked);
        batchDisableAction->setEnabled(!locked);
        batchDeleteAction->setEnabled(!locked);
        menu.addSeparator();
    }

    // 执行相关
    QAction *executeToHereAction = menu.addAction(QStringLiteral("执行到此处"));
    executeToHereAction->setEnabled(!locked);
    QAction *executeFromHereAction = menu.addAction(QStringLiteral("从此处执行"));
    executeFromHereAction->setEnabled(!locked);
    QAction *executeSingleNodeAction = menu.addAction(QStringLiteral("仅执行此算子"));
    executeSingleNodeAction->setEnabled(!locked);
    // 与「仅执行此算子」的区别：会先作废本算子及下游缓存，因此下游拿到的是新值而非上一轮残留
    QAction *recomputeDownstreamAction = menu.addAction(QStringLiteral("重算此算子及下游"));
    recomputeDownstreamAction->setEnabled(!locked);
    menu.addSeparator();

    // 编辑相关
    QAction *renameAction = menu.addAction(locked ? "锁定中，无法重命名" : QStringLiteral("重命名..."));
    renameAction->setEnabled(!locked);
    QAction *copyAction = menu.addAction(locked ? "锁定中，无法复制" : QStringLiteral("复制"));
    copyAction->setEnabled(!locked);
    QAction *copyParamsAction = menu.addAction(QStringLiteral("复制参数"));
    copyParamsAction->setEnabled(!locked);
    QAction *pasteParamsAction = menu.addAction(QStringLiteral("粘贴参数"));
    pasteParamsAction->setEnabled(!locked && !s_copiedParams.isEmpty());
    menu.addSeparator();

    // 启用/禁用
    QAction *toggleAction = menu.addAction(m_node->isEnabled() ? QStringLiteral("禁用") : QStringLiteral("启用"));
    toggleAction->setEnabled(!locked);
    QAction *disableUpstreamAction = menu.addAction(QStringLiteral("禁用上游链路"));
    disableUpstreamAction->setEnabled(!locked);
    menu.addSeparator();

    // 查看相关
    QAction *viewOutputAction = menu.addAction(QStringLiteral("查看输出数据"));
    QAction *viewHelpAction = menu.addAction(QStringLiteral("查看帮助 (F1)"));
    menu.addSeparator();

    // 删除
    QAction *deleteAction = menu.addAction(locked ? "锁定中，无法删除" : QStringLiteral("删除"));
    deleteAction->setEnabled(!locked);

    QAction *selectedAction = menu.exec(event->screenPos());
    if (!selectedAction) return;

    if (batch && selectedAction == batchEnableAction && !locked) {
        scene->setSelectedNodesEnabled(true);
        return;
    }
    if (batch && selectedAction == batchDisableAction && !locked) {
        scene->setSelectedNodesEnabled(false);
        return;
    }
    if (batch && selectedAction == batchDeleteAction && !locked) {
        scene->deleteSelectedItems();
        return;
    }
    if (selectedAction == editAction) {
        scene->requestNodeEdit(m_node);
    } else if (selectedAction == renameAction && !locked) {
        bool ok = false;
        const QString newName = QInputDialog::getText(
            nullptr, QStringLiteral("重命名算子"),
            QStringLiteral("新名称:"), QLineEdit::Normal, m_node->name(), &ok);
        if (ok && !newName.trimmed().isEmpty()) {
            m_node->setName(newName.trimmed());
            update();
        }
    } else if (selectedAction == copyAction && !locked) {
        NodeBase *clone = scene->duplicateNode(m_node);
        if (clone) {
            NodeGraphicsItem *cloneItem = scene->getGraphicsItemForNode(clone);
            if (cloneItem) cloneItem->setSelected(true);
        }
    } else if (selectedAction == copyParamsAction && !locked) {
        const QJsonObject json = m_node->toJson();
        s_copiedParams = json.value(QStringLiteral("params")).toObject();
    } else if (selectedAction == pasteParamsAction && !locked && !s_copiedParams.isEmpty()) {
        QJsonObject json = m_node->toJson();
        json.insert(QStringLiteral("params"), s_copiedParams);
        m_node->fromJson(json);
        update();
    } else if (selectedAction == toggleAction && !locked) {
        m_node->setEnabled(!m_node->isEnabled());
        update();
    } else if (selectedAction == disableUpstreamAction && !locked) {
        for (NodeBase *node : collectUpstreamNodes(m_node)) {
            node->setEnabled(false);
        }
        scene->update();
    } else if (selectedAction == executeToHereAction && !locked) {
        scene->requestExecuteToHere(m_node);
    } else if (selectedAction == executeFromHereAction && !locked) {
        scene->requestExecuteFromHere(m_node);
    } else if (selectedAction == executeSingleNodeAction && !locked) {
        m_node->execute();
    } else if (selectedAction == recomputeDownstreamAction && !locked) {
        scene->requestRecomputeFrom(m_node);
    } else if (selectedAction == viewOutputAction) {
        scene->requestNodeOutputData(m_node);
    } else if (selectedAction == viewHelpAction) {
        scene->requestNodeHelp(m_node);
    } else if (selectedAction == deleteAction && !locked) {
        scene->removeNode(m_node);
    }
}
