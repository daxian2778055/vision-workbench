#pragma once

#include <QGraphicsScene>
#include <QMap>
#include <QList>
#include <QVector>
#include <QJsonObject>
#include <QVariant>
#include <QStringList>
#include "NodeBase.h"

class QPainter;
class QKeyEvent;

class Port;
namespace MyProject {
    class Connection;
}

class NodeGraphicsItem;
class ConnectionGraphicsItem;
class ConnectionDragHelper;
class CommentGraphicsItem;

/// 流程级变量（随 .vfp 流程走，不是进程单例）
struct FlowVariable {
    QString name;
    int type = 2; /// 0=Int 1=Bool 2=Float 3=String
    QVariant value;
    QString description;
};

/// 命名夹具：6 元齐次 + 可选匹配位姿
struct FlowFixture {
    QString name;
    QVector<double> hom; /// m11 m12 m13 m21 m22 m23
    double poseRow = 0.0;
    double poseCol = 0.0;
    double poseAngle = 0.0;
    double poseScale = 1.0;
    bool hasHom = false;
    bool hasPose = false;
};

class FlowScene : public QGraphicsScene
{
    Q_OBJECT

public:
    FlowScene(QObject *parent = nullptr);
    ~FlowScene();

    NodeBase *createNode(NodeBase::NodeType type, const QPointF &pos, const QString &nodeName = "");
    /// 复制算子（含参数），生成偏移的副本并加入场景；失败返回 nullptr
    NodeBase *duplicateNode(NodeBase *node);
    void removeNode(NodeBase *node);

    /// @param relaxedSemantics true 时不校验语义类型（仅载入工程等）
    MyProject::Connection *createConnection(Port *source, Port *target,
                                            bool relaxedSemantics = false);
    void removeConnection(MyProject::Connection *connection);

    QList<NodeBase *> nodes() const;
    QList<MyProject::Connection *> connections() const;

    NodeGraphicsItem *getGraphicsItemForNode(NodeBase *node) const;
    ConnectionGraphicsItem *getGraphicsItemForConnection(MyProject::Connection *connection) const;
    ConnectionDragHelper *dragHelper() const { return m_dragHelper; }

    /// 右键菜单转发（NodeGraphicsItem 非 QObject）
    void requestNodeHelp(NodeBase *node);
    void requestNodeOutputData(NodeBase *node);
    void requestExecuteToHere(NodeBase *node);
    void requestExecuteFromHere(NodeBase *node);
    /// 请求「重算该算子及其下游」（由 MainWindow 作废该段缓存后重跑）
    void requestRecomputeFrom(NodeBase *node);
    void requestNodeEdit(NodeBase *node);

    /// 清空场景（唯一受支持的清空入口）。
    /// 注意：禁止调用 QGraphicsScene::clear()——它会直接销毁图元，但本类用于反查
    /// 图元的 m_nodeItems / m_connectionItems 仍持有这些已析构指针，之后析构场景或
    /// 删除节点时会二次删除而崩溃。清空请一律使用本方法。
    void clearScene();

    /// 锁定/解锁编辑（连续模式运行时禁止画布改动）
    void setEditLocked(bool locked);
    bool isEditLocked() const;

    // ---- 撤销/重做（快照式，覆盖节点增删/连线增删/节点移动） ----
    /// 记录当前场景状态到撤销栈（结构变化前调用）
    void recordUndo();
    /// 撤销一步；成功返回 true
    bool undo();
    /// 重做一步；成功返回 true
    bool redo();
    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }

    /// 删除当前选中的算子与注释
    void deleteSelectedItems();
    /// 批量启用/禁用选中算子
    void setSelectedNodesEnabled(bool on);
    QList<NodeBase *> selectedNodes() const;

    CommentGraphicsItem *addComment(const QPointF &pos, const QString &text = QString());
    void removeComment(CommentGraphicsItem *item);
    QList<CommentGraphicsItem *> comments() const { return m_comments; }

    void setFlowVariable(const QString &name, int type, const QVariant &value,
                         const QString &description = QString());
    bool removeFlowVariable(const QString &name);
    QVariant flowVariable(const QString &name) const;
    QMap<QString, FlowVariable> flowVariables() const { return m_flowVariables; }

    void setFixture(const FlowFixture &fixture);
    void setFixtureHomography(const QString &name, const QVector<double> &hom);
    void setFixturePose(const QString &name, double row, double col,
                        double angleDeg, double scale);
    FlowFixture fixture(const QString &name) const;
    QStringList fixtureNames() const;
    QMap<QString, FlowFixture> fixtures() const { return m_fixtures; }

    QJsonObject extrasToJson() const;
    void extrasFromJson(const QJsonObject &json);

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void dragEnterEvent(QGraphicsSceneDragDropEvent *event) override;
    void dragMoveEvent(QGraphicsSceneDragDropEvent *event) override;
    void dropEvent(QGraphicsSceneDragDropEvent *event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override;

signals:
    void nodeAdded(NodeBase *node);
    void nodeRemoved(NodeBase *node);
    void connectionAdded(MyProject::Connection *connection);
    void connectionRemoved(MyProject::Connection *connection);
    void nodeSelected(NodeBase *node);
    void connectionRejected(const QString &reason);
    void undoAvailable(bool available);
    void redoAvailable(bool available);
    void nodeGraphicsItemCreated(NodeGraphicsItem *item);
    void nodeHelpRequested(NodeBase *node);
    void nodeOutputDataRequested(NodeBase *node);
    void executeToHereRequested(NodeBase *node);
    void executeFromHereRequested(NodeBase *node);
    /// 请求「重算该算子及其下游」（作废该段缓存后只重跑这一段）
    void recomputeFromRequested(NodeBase *node);
    void nodeEditRequested(NodeBase *node);

private:
    ConnectionDragHelper *m_dragHelper = nullptr;
    QMap<NodeBase *, NodeGraphicsItem *> m_nodeItems;
    QMap<MyProject::Connection *, ConnectionGraphicsItem *> m_connectionItems;
    QList<CommentGraphicsItem *> m_comments;
    QMap<QString, FlowVariable> m_flowVariables;
    QMap<QString, FlowFixture> m_fixtures;
    bool m_editLocked = false;   /// 编辑锁定（连续模式运行时禁止布图操作）

    // ---- 撤销/重做状态 ----
    QVector<QJsonObject> m_undoStack;
    QVector<QJsonObject> m_redoStack;
    static constexpr int kUndoLimit = 50;  /// 撤销深度上限
    bool m_restoring = false;   /// 正在从快照恢复（抑制 recordUndo 递归）
    int m_undoBatch = 0;        /// 批量操作计数（批量内只记录一次撤销）
    /// 用快照重建场景（清空 + fromJson）
    void restoreSnapshot(const QJsonObject &snapshot);};
