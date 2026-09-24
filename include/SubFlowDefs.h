#pragma once

#include <QJsonObject>
#include <QJsonArray>
#include <QList>
#include <QString>

/// 运行期子流程定义（FR15.10，方案 B：方案内命名子图）。
/// 成员用模块号标识（与分组 NodeGroupItem 同一套身份约定，跨会话稳定），
/// 成员就是画布上的真实算子 ⇒「一处修改、所有引用同步生效」。
/// 入口/出口在定义时自动推导并显式存储（v1 单进单出）：
///   入口 = 成员中唯一"无来自成员内部入边"的算子；
///   出口 = 成员中唯一"无去向成员内部出边"的算子。
/// 边界约定：成员与成员之外的算子之间**不允许连线**，数据一律经调用点（SubFlowNode）进出。
struct SubFlowDef {
    QString name;
    QList<int> members;
    int input = -1;
    int output = -1;

    bool isValid() const
    {
        return !name.isEmpty() && members.size() >= 2 && input >= 0 && output >= 0;
    }

    QJsonObject toJson() const
    {
        QJsonObject o;
        o[QStringLiteral("name")] = name;
        QJsonArray ms;
        for (int id : members)
            ms.append(id);
        o[QStringLiteral("members")] = ms;
        o[QStringLiteral("input")] = input;
        o[QStringLiteral("output")] = output;
        return o;
    }

    static SubFlowDef fromJson(const QJsonObject &o)
    {
        SubFlowDef d;
        d.name = o.value(QStringLiteral("name")).toString();
        for (const QJsonValue &v : o.value(QStringLiteral("members")).toArray())
            d.members.append(v.toInt());
        d.input = o.value(QStringLiteral("input")).toInt(-1);
        d.output = o.value(QStringLiteral("output")).toInt(-1);
        return d;
    }
};
