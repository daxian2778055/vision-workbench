#pragma once

#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QString>

class FlowScene;
class NodeBase;
class QJsonArray;

/// 子图片段（**多算子 + 内部连线 [+ 分组]**）的抓取、校验与插入。
///
/// 这是 FR15.10「子流程调用与复用」里的**设计期复用**半步（复制/粘贴、片段文件跨方案搬运）；
/// **运行期**"把一段流程当算子调用"属于执行引擎能力，本轮未做——接口草案见
/// `docs/子流程复用设计草案.md`。两者不要混为一谈：片段是"拷一份出来"，子流程是"调同一个"。
///
/// 为什么需要它：既有的复用能力都是**单算子**级的（`FlowScene::duplicateNode` 单算子、
/// `NodeTemplateStore` 单算子模板），而现场真正反复出现的是"一小段固定的流程"（预处理段、
/// 通信上报段…）。手工重连这些线段既慢又容易连错。
///
/// 关键设计（都有代价，改之前先读）：
///  1. **内部连线才带走**：只保留两端都在选中集内的连线。跨边界连线插入后无从对应（对端在
///     源场景里），丢弃并**如实计数**——用户必须知道"少了几根线"，而不是以为粘全了。
///  2. **相对坐标**：以选中集左上角为基准存 `relativeX/relativeY`，插入时整体平移到目标点，
///     粘到别处不会跑飞。
///  3. **插入时重新发号**：`moduleId` 是**方案内**身份（执行器缓存、{模块号.参数名} 引用都按它索引），
///     跨方案/同场景粘贴都可能与现有号撞车，故片段里**不带** moduleId，插入时由新算子的构造分配；
///     名字撞车则加后缀（变量引用按名字定位，撞名会让引用指向不确定）。
///  4. **原子性**：任一个算子建不出来就整段放弃（半截子图比不粘贴更糟——用户以为粘全了）。
///  5. 片段是**合法 JSON 文本**，可直接贴进邮件/工单；导入外部文件前必须过 `isValid()`。
class FlowSnippet
{
public:
    /// 片段格式版本（1 = 初版）。读入时 version > 本值即拒绝，避免"看不懂的字段被静默忽略"。
    static constexpr int kVersion = 1;

    /// 抓取片段。nodes 里不属于该场景的会被忽略；空集返回空对象。
    /// droppedBoundaryConnections：被丢弃的"一端在选中集内"的连线数（如实告知用户）。
    static QJsonObject capture(FlowScene *scene, const QList<NodeBase *> &nodes,
                              int *droppedBoundaryConnections = nullptr);

    /// 校验片段（导入外部文件/剪贴板文本时必须先过这一关）；error 说明失败原因
    static bool isValid(const QJsonObject &snippet, QString *error = nullptr);

    /// 插入片段：返回新建的算子（失败返回空且场景不被改动）。
    /// at 为插入点（片段左上角落在这里）。**撤销由调用方在插入前记录一次**（内部批量抑制分步记录）。
    static QList<NodeBase *> insert(FlowScene *scene, const QJsonObject &snippet, const QPointF &at,
                                    QString *error = nullptr);

    /// 建议的插入左上角：让片段整体落在 center（一般传视图中心），而不是让左上角落在那里
    /// ——否则大片段会有一多半跑到视口外，用户以为"没粘上"。
    static QPointF suggestedInsertTopLeft(const QJsonObject &snippet, const QPointF &center);

    /// 剪贴板文本 ↔ 片段
    static QString toText(const QJsonObject &snippet);
    static QJsonObject fromText(const QString &text, QString *error = nullptr);

    /// 片段文件扩展名（含点）
    static QString fileExtension();

private:
    FlowSnippet() = delete;
};
