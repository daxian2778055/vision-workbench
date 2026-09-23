#include "FlowSnippet.h"
#include "FlowScene.h"
#include "NodeBase.h"
#include "NodeRegistry.h"
#include "NodeFactory.h"
#include "NodeGroupItem.h"
#include "Port.h"
#include "Connection.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QDateTime>
#include <QMap>
#include <QObject>
#include <QSet>

namespace {
const QString kKind = QStringLiteral("vfp.flowSnippet");
const QString kKindKey = QStringLiteral("kind");
const QString kVersionKey = QStringLiteral("version");
const QString kNodesKey = QStringLiteral("nodes");
const QString kConnectionsKey = QStringLiteral("connections");
const QString kGroupsKey = QStringLiteral("groups");
const QString kTypeIdKey = QStringLiteral("typeId");
const QString kNodeTypeKey = QStringLiteral("nodeType");
const QString kNodeNameKey = QStringLiteral("nodeName");
const QString kRelXKey = QStringLiteral("relativeX");
const QString kRelYKey = QStringLiteral("relativeY");
const QString kDroppedKey = QStringLiteral("droppedBoundaryConnections");

/// 按 typeId 优先、类型枚举兜底建算子（与 duplicateNode / createNodeFromTemplate 同一条路径，
/// 因此新增算子无需为片段功能做任何适配）。
NodeBase *createNodeFromSnippet(FlowScene *scene, const QJsonObject &nodeJson)
{
    // 走场景的统一创建入口：它内部会 init()（端口在 init 里建，直接 createById 会得到"没有端口
    // 的算子"——连不上线但流程照跑，最难查的一类）。顺序与 FlowScene::createNode 一致：
    // 先 init 建端口，再 fromJson 覆盖参数。
    NodeBase *node = scene->createNodeByTypeIdOrName(nodeJson.value(kTypeIdKey).toString(),
                                                     nodeJson.value(kNodeTypeKey).toInt(-1),
                                                     nodeJson.value(kNodeNameKey).toString());
    if (!node)
        return nullptr;
    // 片段里的 nodeJson 同时含"算子参数"与"片段元信息"（typeId/nodeType/relativeX…）：
    // 与 ProjectManager::sceneFromJson 喂给 fromJson 的内容同构，算子实现早已容忍多余键。
    node->fromJson(nodeJson);
    return node;
}
}   // namespace

QJsonObject FlowSnippet::capture(FlowScene *scene, const QList<NodeBase *> &nodes,
                                 int *droppedBoundaryConnections)
{
    if (droppedBoundaryConnections)
        *droppedBoundaryConnections = 0;
    QJsonObject out;
    if (!scene)
        return out;

    const QList<NodeBase *> sceneNodes = scene->nodes();
    QList<NodeBase *> picked;
    for (NodeBase *n : nodes) {
        if (n && sceneNodes.contains(n) && !picked.contains(n))
            picked.append(n);
    }
    if (picked.isEmpty())
        return out;

    // 相对坐标基准 = 选中集左上角（粘贴到别处不会跑飞）
    QPointF base = picked.first()->position();
    for (NodeBase *n : picked) {
        base.setX(qMin(base.x(), n->position().x()));
        base.setY(qMin(base.y(), n->position().y()));
    }

    QMap<NodeBase *, int> indexOf;
    QJsonArray nodesJson;
    for (NodeBase *n : picked) {
        QJsonObject nodeJson = n->toJson();
        // 号与位置都不随片段走：号是"方案内身份"（跨方案必撞），位置改用相对坐标
        nodeJson.remove(QStringLiteral("moduleId"));
        nodeJson.remove(QStringLiteral("id"));
        nodeJson.remove(QStringLiteral("position"));
        nodeJson.remove(QStringLiteral("executionSuccess"));
        nodeJson[kNodeTypeKey] = int(n->type());
        nodeJson[kNodeNameKey] = n->name();
        const QString typeId = n->property("vfpNodeTypeId").toString();
        if (!typeId.isEmpty())
            nodeJson[kTypeIdKey] = typeId;
        nodeJson[kRelXKey] = n->position().x() - base.x();
        nodeJson[kRelYKey] = n->position().y() - base.y();
        indexOf.insert(n, nodesJson.size());
        nodesJson.append(nodeJson);
    }
    out[kNodesKey] = nodesJson;

    // 连线：只保留两端都在选中集内的
    QJsonArray conns;
    int dropped = 0;
    for (MyProject::Connection *conn : scene->connections()) {
        if (!conn)
            continue;
        Port *sourcePort = conn->sourcePort();
        Port *targetPort = conn->targetPort();
        if (!sourcePort || !targetPort)
            continue;
        NodeBase *sourceNode = sourcePort->node();
        NodeBase *targetNode = targetPort->node();
        const bool hasSource = indexOf.contains(sourceNode);
        const bool hasTarget = indexOf.contains(targetNode);
        if (!hasSource || !hasTarget) {
            if (hasSource || hasTarget)
                ++dropped;   // 只有"一端在内"才算被丢弃；两端都在外面与本片段无关
            continue;
        }
        QJsonObject connJson;
        connJson[QStringLiteral("from")] = indexOf.value(sourceNode);
        connJson[QStringLiteral("fromPort")] = sourceNode->outputPorts().indexOf(sourcePort);
        connJson[QStringLiteral("to")] = indexOf.value(targetNode);
        connJson[QStringLiteral("toPort")] = targetNode->inputPorts().indexOf(targetPort);
        conns.append(connJson);
    }
    out[kConnectionsKey] = conns;
    out[kDroppedKey] = dropped;
    if (droppedBoundaryConnections)
        *droppedBoundaryConnections = dropped;

    // 分组：只带走"成员全在选中集内"的组。部分选中会把一个组拆成两半，语义不清，
    // 与其猜用户想怎样，不如不带组（算子照旧复制，只是不建组）。
    QJsonArray groups;
    const QPointF groupOrigin = base;
    for (NodeGroupItem *group : scene->groups()) {
        if (!group || group->memberIds().isEmpty())
            continue;
        QJsonArray members;
        bool allInside = true;
        for (int moduleId : group->memberIds()) {
            NodeBase *n = scene->nodeByModuleId(moduleId);
            if (!n || !indexOf.contains(n)) {
                allInside = false;
                break;
            }
            members.append(indexOf.value(n));
        }
        if (!allInside)
            continue;
        const QRectF rect = group->mapRectToScene(group->boundingRect());
        QJsonObject groupJson;
        groupJson[QStringLiteral("title")] = group->title();
        groupJson[QStringLiteral("members")] = members;
        groupJson[kRelXKey] = rect.x() - groupOrigin.x();
        groupJson[kRelYKey] = rect.y() - groupOrigin.y();
        groupJson[QStringLiteral("w")] = rect.width();
        groupJson[QStringLiteral("h")] = rect.height();
        groups.append(groupJson);
    }
    out[kGroupsKey] = groups;

    out[kKindKey] = kKind;
    out[kVersionKey] = kVersion;
    out[QStringLiteral("capturedAt")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    return out;
}

bool FlowSnippet::isValid(const QJsonObject &snippet, QString *error)
{
    const auto fail = [error](const QString &reason) {
        if (error)
            *error = reason;
        return false;
    };

    if (snippet.isEmpty())
        return fail(QObject::tr("片段为空"));
    if (snippet.value(kKindKey).toString() != kKind)
        return fail(QObject::tr("不是方案片段（kind 不匹配，可能贴错了内容）"));

    const int version = snippet.value(kVersionKey).toInt(0);
    if (version <= 0 || version > kVersion) {
        return fail(QObject::tr("片段版本不支持：%1（本程序支持到 %2）")
                        .arg(version)
                        .arg(kVersion));
    }

    const QJsonArray nodes = snippet.value(kNodesKey).toArray();
    if (nodes.isEmpty())
        return fail(QObject::tr("片段里没有任何算子"));

    for (int i = 0; i < nodes.size(); ++i) {
        const QJsonObject nodeJson = nodes.at(i).toObject();
        if (nodeJson.isEmpty())
            return fail(QObject::tr("片段第 %1 个算子不是有效对象").arg(i + 1));
        if (nodeJson.value(kTypeIdKey).toString().isEmpty()
            && nodeJson.value(kNodeTypeKey).toInt(-1) < 0) {
            return fail(QObject::tr("片段第 %1 个算子缺少类型信息（typeId/nodeType 均无）")
                            .arg(i + 1));
        }
    }
    return true;
}

QList<NodeBase *> FlowSnippet::insert(FlowScene *scene, const QJsonObject &snippet, const QPointF &at,
                                      QString *error)
{
    QList<NodeBase *> created;
    if (!scene)
        return created;

    QString reason;
    if (!isValid(snippet, &reason)) {
        if (error)
            *error = reason;
        return created;
    }
    if (scene->isEditLocked()) {
        if (error)
            *error = QObject::tr("流程处于编辑锁定状态（运行中），无法插入片段");
        return created;
    }

    const QJsonArray nodesJson = snippet.value(kNodesKey).toArray();

    // 批量编辑：期间的 createConnection/removeNode 不再各自记录撤销，
    // 由调用方在插入前记录一次 ⇒ "一次粘贴 = 一步撤销"。
    scene->beginUndoBatch();

    int failedIndex = -1;
    for (int i = 0; i < nodesJson.size(); ++i) {
        const QJsonObject nodeJson = nodesJson.at(i).toObject();
        NodeBase *node = createNodeFromSnippet(scene, nodeJson);
        if (!node) {
            failedIndex = i;
            break;
        }
        const QPointF pos = at
                            + QPointF(nodeJson.value(kRelXKey).toDouble(),
                                      nodeJson.value(kRelYKey).toDouble());
        scene->adoptNode(node, pos);
        created.append(node);
    }

    if (failedIndex >= 0) {
        // 原子性：任一算子建不出来就整段放弃——半截子图比"没粘贴"更糟（用户以为粘全了）
        for (NodeBase *n : created)
            scene->removeNode(n);
        created.clear();
        scene->endUndoBatch();
        if (error) {
            *error = QObject::tr("片段第 %1 个算子无法创建（类型未注册或参数不兼容），已取消插入")
                         .arg(failedIndex + 1);
        }
        return created;
    }

    // 名字去重：变量引用按算子名定位，撞名会让"引用指向哪个算子"变得不确定
    for (NodeBase *n : created) {
        const QString unique = scene->makeUniqueNodeName(n->name(), n);
        if (!unique.isEmpty() && unique != n->name())
            n->setName(unique);
    }

    // 连线（端口按序号对接：算子类型相同则端口布局相同；越界/成环则丢弃并计数）
    int droppedConnections = 0;
    for (const QJsonValue &cv : snippet.value(kConnectionsKey).toArray()) {
        const QJsonObject connJson = cv.toObject();
        const int fromIndex = connJson.value(QStringLiteral("from")).toInt(-1);
        const int toIndex = connJson.value(QStringLiteral("to")).toInt(-1);
        if (fromIndex < 0 || toIndex < 0 || fromIndex >= created.size() || toIndex >= created.size()) {
            ++droppedConnections;
            continue;
        }
        NodeBase *sourceNode = created.at(fromIndex);
        NodeBase *targetNode = created.at(toIndex);
        const int fromPort = connJson.value(QStringLiteral("fromPort")).toInt(-1);
        const int toPort = connJson.value(QStringLiteral("toPort")).toInt(-1);
        if (fromPort < 0 || toPort < 0 || fromPort >= sourceNode->outputPorts().size()
            || toPort >= targetNode->inputPorts().size()) {
            ++droppedConnections;
            continue;
        }
        if (!scene->createConnection(sourceNode->outputPorts().at(fromPort),
                                     targetNode->inputPorts().at(toPort), true)) {
            ++droppedConnections;   // 例如与场景里现有连线构成环
        }
    }

    // 分组（成员序号 → 新算子的模块号）
    for (const QJsonValue &gv : snippet.value(kGroupsKey).toArray()) {
        const QJsonObject groupJson = gv.toObject();
        QList<int> memberIds;
        for (const QJsonValue &mv : groupJson.value(QStringLiteral("members")).toArray()) {
            const int index = mv.toInt(-1);
            if (index >= 0 && index < created.size())
                memberIds.append(created.at(index)->moduleId());
        }
        if (memberIds.size() < 2)
            continue;   // 不足 2 个成员的组没有意义（与"创建分组"门槛一致）
        auto *group = new NodeGroupItem(groupJson.value(QStringLiteral("title")).toString());
        group->setPos(at + QPointF(groupJson.value(kRelXKey).toDouble(),
                                   groupJson.value(kRelYKey).toDouble()));
        // 先接管进场景，再设成员：setMembers 只在"已挂进场景"时才做存在性过滤，
        // 这样片段里指向已失效算子的成员会被丢掉，而不是留下一个幽灵成员。
        scene->registerLoadedGroup(group, groupJson.value(QStringLiteral("w")).toDouble(),
                                   groupJson.value(QStringLiteral("h")).toDouble());
        group->setMembers(memberIds);
    }

    scene->endUndoBatch();
    return created;
}

QPointF FlowSnippet::suggestedInsertTopLeft(const QJsonObject &snippet, const QPointF &center)
{
    double maxX = 0.0;
    double maxY = 0.0;
    for (const QJsonValue &v : snippet.value(kNodesKey).toArray()) {
        const QJsonObject nodeJson = v.toObject();
        const QJsonObject sizeJson = nodeJson.value(QStringLiteral("size")).toObject();
        const double w = sizeJson.value(QStringLiteral("width")).toDouble(120.0);
        const double h = sizeJson.value(QStringLiteral("height")).toDouble(80.0);
        maxX = qMax(maxX, nodeJson.value(kRelXKey).toDouble() + w);
        maxY = qMax(maxY, nodeJson.value(kRelYKey).toDouble() + h);
    }
    return center - QPointF(maxX / 2.0, maxY / 2.0);
}

QString FlowSnippet::toText(const QJsonObject &snippet)
{
    return QString::fromUtf8(QJsonDocument(snippet).toJson(QJsonDocument::Indented));
}

QJsonObject FlowSnippet::fromText(const QString &text, QString *error)
{
    const auto fail = [error](const QString &reason) {
        if (error)
            *error = reason;
        return QJsonObject();
    };
    if (text.trimmed().isEmpty())
        return fail(QObject::tr("剪贴板为空"));
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return fail(QObject::tr("剪贴板内容不是合法的方案片段（JSON 解析失败：%1）")
                        .arg(parseError.errorString()));
    }
    return doc.object();
}

QString FlowSnippet::fileExtension()
{
    return QStringLiteral(".vfseg");
}
