#pragma once

#include <QGraphicsScene>
#include <QMap>
#include <QList>
#include <QVector>
#include <QJsonObject>
#include <QVariant>
#include <QStringList>
#include <QRecursiveMutex>
#include <QPointer>
#include <utility>
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
class NodeGroupItem;

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

    /// M-2：析构时"仍有存活快照"的断言开关（默认开启，生产路径永远保持开启）。
    /// 仅 testSnapshotGuardSurvivesSceneDestruction 这类**刻意**制造"场景先亡"的用例在用例内临时关闭；
    /// 否则 Debug 构建下该用例必然 abort（仓库目前只跑 Release，等于埋了个 Debug 必炸的雷）。
    static void setDanglingSnapshotAssertEnabled(bool on) { s_danglingSnapshotAssertEnabled = on; }

    NodeBase *createNode(NodeBase::NodeType type, const QPointF &pos, const QString &nodeName = "");

    /// 按注册表 typeId（优先）或类型枚举（兜底）新建一个**已初始化**的算子（不接入场景）。
    /// **必须走这里**：端口是在各算子的 init() 里建的，而 `NodeRegistry::createById` /
    /// `NodeFactory::createNode` 都不会替你调它——漏掉的表现是"插入/复制出来的算子没有端口、
    /// 连不上线"，可流程照样能跑（最隐蔽的一类）。调用方随后 fromJson() 覆盖参数、再接入场景。
    NodeBase *createNodeByTypeIdOrName(const QString &typeId, int typeValue, const QString &name);
    /// 复制算子（含参数），生成偏移的副本并加入场景；失败返回 nullptr
    NodeBase *duplicateNode(NodeBase *node);
    /// 把已在场景外构造好的算子接入场景（建图形项、登记、发信号）。
    /// 从 duplicateNode 尾部抽出，让"外部构造"的路径（如节点模板）复用同一套接入逻辑。
    void adoptNode(NodeBase *node, const QPointF &pos);
    /// 按节点模板插入算子（模板名来自 NodeTemplateStore）；失败返回 nullptr。
    /// 重名时自动加后缀：变量引用按算子名定位，撞名会让引用指向哪个算子变得不确定。
    NodeBase *createNodeFromTemplate(const QString &templateName, const QPointF &pos);
    void removeNode(NodeBase *node);

    /// @param relaxedSemantics true 时不校验语义类型（仅载入工程等）
    MyProject::Connection *createConnection(Port *source, Port *target,
                                            bool relaxedSemantics = false);
    void removeConnection(MyProject::Connection *connection);

    QList<NodeBase *> nodes() const;
    QList<MyProject::Connection *> connections() const;

    // ---- 执行期图快照（S1）----
    /// 拓扑快照值类型：只含节点/连边裸指针，不含图元。
    struct GraphSnapshot {
        QList<NodeBase *> nodes;
        QList<MyProject::Connection *> connections;
    };
    /// RAII 句柄：持有期间，从图中删除节点/连边只做"墓碑"（成员集立即摘除 + 延迟析构），
    /// 保证执行线程手里那批裸指针在整轮结束前不会悬垂；析构（含提前 return / 异常 / break）即释放。
    /// 生命周期规则：同一时刻只允许执行线程持有一个（执行器为单线程，UI 侧同步执行亦为单发）。
    class GraphSnapshotGuard
    {
    public:
        GraphSnapshotGuard() = default;
        /// 形参用 QObject* 而非 FlowScene*：嵌套类定义在 FlowScene 内部，此处 FlowScene 尚不完整，
        /// 传给 QPointer<QObject> 的基类转换需要完整性；实际调用点 captureGraphSnapshot() 在 .cpp 传 this。
        GraphSnapshotGuard(QObject *scene, GraphSnapshot snapshot)
            : m_scene(scene), m_snapshot(std::move(snapshot)) {}
        ~GraphSnapshotGuard() { releaseHeld(); }
        GraphSnapshotGuard(GraphSnapshotGuard &&o) noexcept
            : m_scene(o.m_scene), m_snapshot(std::move(o.m_snapshot)) { o.m_scene = nullptr; }
        GraphSnapshotGuard &operator=(GraphSnapshotGuard &&o) noexcept
        {
            if (this != &o) {
                releaseHeld();
                m_scene = o.m_scene;
                m_snapshot = std::move(o.m_snapshot);
                o.m_scene = nullptr;
            }
            return *this;
        }
        GraphSnapshotGuard(const GraphSnapshotGuard &) = delete;
        GraphSnapshotGuard &operator=(const GraphSnapshotGuard &) = delete;

        bool isHeld() const { return !m_scene.isNull(); }
        const GraphSnapshot &snapshot() const { return m_snapshot; }

    private:
        /// 释放句柄。N-2：改用弱引用——场景若已先析构（QPointer 自动置空）则直接跳过，
        /// 绝不对已亡场景回调 releaseGraphSnapshot()（旧实现是裸指针 → 场景先亡即 UAF）。
        void releaseHeld();
        QPointer<QObject> m_scene;   /// 弱引用：只判"场景还在不在"，不延长其生命
        GraphSnapshot m_snapshot;
    };
    /// 捕获一次拓扑快照（持锁拷贝成员集）并登记一个存活句柄；未释放前删除一律走墓碑
    GraphSnapshotGuard captureGraphSnapshot();

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
    /// 批量编辑：期间内的 createConnection / removeNode 等不再各自记录撤销
    /// （由调用方在批量开始前记录一次）⇒ "一次操作 = 一步撤销"（片段粘贴这类多步动作必用）。
    /// 必须成对调用；FlowSnippet::insert 内部已按对使用。
    void beginUndoBatch();
    void endUndoBatch();
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

    // ---- 算子分组（Group，FR1.9）----
    /// 用当前选中的算子建立分组（少于 2 个算子返回 nullptr）。
    /// 分组是**纯视觉容器**：不改节点/连线、不发 nodeAdded / connection* 信号，因此执行器
    /// 完全看不到它（执行不受影响）。数据随方案保存（extras 的 nodeGroups），也随撤销快照走。
    NodeGroupItem *createGroupFromSelection(const QString &title = QString());
    /// 解散分组：只删框，组内算子原样保留（记撤销）
    void removeGroup(NodeGroupItem *group);
    /// 解散当前**选中**的分组，返回解散个数（Delete 键与菜单入口共用）
    int dissolveSelectedGroups();
    QList<NodeGroupItem *> groups() const { return m_groups; }
    /// 算子所属分组（不在任何分组则返回 nullptr）
    NodeGroupItem *groupOfNode(NodeBase *node) const;
    /// 按模块号查算子（分组成员定位用）
    NodeBase *nodeByModuleId(int moduleId) const;

    /// 生成场景内不重名的算子名（base → base_2 → base_3…；exclude 为刚插入的算子自身，可空）。
    /// 变量引用按算子名定位，撞名会让"引用指向哪个算子"变得不确定——凡插入新算子（模板/片段）
    /// 都要过这一关，故做成单一实现，避免多处规则漂移。
    QString makeUniqueNodeName(const QString &base, NodeBase *exclude = nullptr) const;
    /// 接管一个"从方案/片段载入"的分组框：挂进场景 + 归属容器 + 按给定尺寸设框
    /// （尺寸随文件存过，不该再按成员位置去猜），并跟随编辑锁定状态与折叠状态。
    void registerLoadedGroup(NodeGroupItem *group, qreal width, qreal height);

    /// 按**全部**折叠分组的当前状态，统一重算"算子/连线图元是否可见"。
    /// 做成单一来源的原因：折叠会隐藏成员与其相关连线，而连线两端可能分属不同分组、
    /// 算子也可能同时属于多个分组——逐个分组去 setVisible 必然算错（展开一个组却让
    /// 另一个折叠组的成员露出来）。隐藏的成员会被取消选中，避免"删除了看不见的算子"。
    void refreshGroupVisibility();

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

    /// 流程名称：方案持久化 + 触发按名路由。持久化后改名/重排/删除再保存载入能保持身份，
    /// 避免触发绑定按"页签序号重建名"被平移到别的流程（S2）。
    QString flowName() const { return m_flowName; }
    void setFlowName(const QString &name) { m_flowName = name; }

    /// 运行模式（每流程一份，随方案持久化）：0=连续 / 1=软触发 / 2=硬触发。
    /// 存 int 以免 FlowScene 依赖 FlowExecutor 的枚举；取值与 FlowMode 的顺序一致。
    /// 现场要求"不是所有流程都要连续"——所以模式必须跟着流程走并落盘，而不是全局一份。
    int flowMode() const { return m_flowMode; }
    void setFlowMode(int mode) { m_flowMode = mode; }

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
    /// 画布聚焦时按下 Ctrl+C / Ctrl+V：请求"复制选中子图 / 粘贴片段"。
    /// 不在场景里直接做：粘贴位置需要视图（窗口侧知道当前视图中心）；这样做也让参数框、日志等
    /// 文本框里的 Ctrl+C 保持原生复制——那些控件不经过本场景。
    void copySelectionRequested();
    void pasteRequested();
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

    // ---- S1 执行期隔离：成员集读写锁 + 墓碑延迟析构 ----
    /// 保护 m_nodeItems / m_connectionItems / 墓碑队列（递归锁：removeNode→removeConnection 会嵌套）
    mutable QRecursiveMutex m_graphMutex;
    int m_liveSnapshotCount = 0;                    ///> 存活快照句柄数（>0 时删除只摘除不析构）
    static bool s_danglingSnapshotAssertEnabled;    ///> M-2：析构断言开关（默认 true）
    QList<NodeBase *> m_retiredNodes;               ///> 墓碑：待析构节点（存活句柄归零后 flush）
    QList<MyProject::Connection *> m_retiredConnections;

    /// 句柄析构回调：计数归零时把墓碑 flush 回场景线程执行（绝不从工作线程析构节点）
    void releaseGraphSnapshot();
    /// 真正析构墓碑（仅 m_liveSnapshotCount==0；必须在场景线程执行）
    void flushRetired();
    /// 删除分组图元（**不记撤销**：由调用方决定何时记录，避免把"改动后"的状态塞进撤销栈）
    void deleteGroupItem(NodeGroupItem *group);
    QList<CommentGraphicsItem *> m_comments;
    QList<NodeGroupItem *> m_groups;   /// 算子分组框（FR1.9；纯视觉容器，随方案与撤销快照持久化）
    QMap<QString, FlowVariable> m_flowVariables;
    QMap<QString, FlowFixture> m_fixtures;
    QString m_flowName;          /// 流程名称（方案持久化 + 触发按名路由，S2）
    int m_flowMode = 1;          /// 运行模式（0=连续 1=软触发 2=硬触发；默认软触发，随方案持久化）
    bool m_editLocked = false;   /// 编辑锁定（连续模式运行时禁止布图操作）

    // ---- 撤销/重做状态 ----
    QVector<QJsonObject> m_undoStack;
    QVector<QJsonObject> m_redoStack;
    static constexpr int kUndoLimit = 50;  /// 撤销深度上限
    bool m_restoring = false;   /// 正在从快照恢复（抑制 recordUndo 递归）
    int m_undoBatch = 0;        /// 批量操作计数（批量内只记录一次撤销）
    /// 用快照重建场景（清空 + fromJson）
    void restoreSnapshot(const QJsonObject &snapshot);};
