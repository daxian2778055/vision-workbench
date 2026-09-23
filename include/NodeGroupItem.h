#pragma once

#include <QGraphicsItem>
#include <QJsonObject>
#include <QList>
#include <QRectF>
#include <QString>

class FlowScene;

/// 算子分组框（画布上的容器背景，FR1.9）：把若干算子标成一组，便于阅读与整体搬移。
///
/// 设计契约（想改之前先读——几个"看起来更简单"的做法都被否掉了）：
///  · **纯视觉、不参与执行**：只动图元与位置，不改节点/连线、不发 nodeAdded / connection* 信号，
///    执行器的图结构缓存完全看不到它（用例已钉"分组后流程照跑且结果不变"）。
///  · **成员用模块号（moduleId）标识**：它是本仓既有的节点稳定身份（{模块号.参数名} 引用、结果表键、
///    配方键都按它索引），且方案加载时会恢复（见 ProjectManager::sceneFromJson 的模块号还原）。
///    **不能用"节点下标"**：方案里节点下标来自 nodes()（按指针地址排序），换会话/加载一遍就变，
///    分组会"移花接木"到别的算子上；也不能用显示名（可重名、可改名）。
///  · **只有标题栏可拖动**：框体其余区域必须放行鼠标（event->ignore()），否则组内空白处的框选
///    （rubber band）会被吃掉——"整框可拖"手感更顺，代价是组内再也框选不了算子。
///  · **撤销记录时机**：第一次真正移动时、在基类移动**之前**记录。按下就记会给每次单击都塞一条
///    空快照（用户按 Ctrl+Z 出现"什么都没发生"的一步）；沿用"释放时才记"又会把**移动后**的状态存进
///    撤销栈，导致第一次撤销无效（节点移动当前就是这个行为，用例已钉住该现状）。
class NodeGroupItem : public QGraphicsItem
{
public:
    enum { Type = UserType + 22 };
    int type() const override { return Type; }

    explicit NodeGroupItem(const QString &title = QString(), QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    QString title() const { return m_title; }
    void setTitle(const QString &title);

    /// 成员模块号（只保留场景中确实存在的；顺序无意义）
    QList<int> memberIds() const { return m_members; }
    int memberCount() const { return m_members.size(); }
    bool hasMember(int moduleId) const;
    /// 覆盖设置成员：去重 + 丢掉场景里不存在的模块号
    void setMembers(const QList<int> &moduleIds);
    /// 移除一个成员；返回是否真的移除了
    bool removeMember(int moduleId);
    bool isEmpty() const { return m_members.isEmpty(); }

    /// 按成员当前位置重算框体（左上角随成员外接矩形走，含标题栏）；无有效成员返回 false
    bool fitToMembers();

    /// 整体移动：框体与全部成员各移动 delta（撤销由调用方在移动前记录）
    void moveGroupBy(const QPointF &delta);
    /// 只移动成员（框体由基类拖动时用）
    void moveMembersBy(const QPointF &delta);

    /// 标题栏高度：只有这个带状区域接受鼠标拖动
    static qreal titleBarHeight();
    /// itemPos 是否落在标题栏内
    bool hitTitleBar(const QPointF &itemPos) const;

    QJsonObject toJson() const;
    static NodeGroupItem *fromJson(const QJsonObject &json);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override;

private:
    FlowScene *flowScene() const;
    void renameInteractively();

    QString m_title;
    QRectF m_rect {0, 0, 240, 140};   ///< 本图元坐标（左上 0,0）
    QList<int> m_members;             ///< 成员模块号
    bool m_undoRecorded = false;      ///< 本次拖动是否已记录撤销
};
