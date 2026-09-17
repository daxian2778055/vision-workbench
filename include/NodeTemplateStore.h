#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

class NodeBase;

/// 算子模板库：把配置好的算子（类型 + 全部参数）存成**命名模板**，之后一键插进任意流程。
///
/// 为什么要它：同一台设备上"某产品的定位算子""某工序的检测参数"这类配置会反复出现，
/// 而右键"复制"只在**同一个流程内**可用、还带位置偏移；模板库是跨流程、跨工程、跨会话的
/// （落盘在用户目录下的 JSON）。
///
/// 实现上完全复用既有的序列化能力（与 FlowScene::duplicateNode 同源）：
///   保存 = node->toJson()，去掉 moduleId（副本保持自己的模块 ID）与 position；
///   插入 = NodeRegistry::createById(typeId) + fromJson()（回退：按类型枚举 + 名称建节点）。
/// 因此**新增算子无需为模板库做任何适配**——只要它能 toJson/fromJson。
class NodeTemplateStore
{
public:
    static NodeTemplateStore &instance();

    /// 模板名列表（已排序，供菜单直接使用）
    QStringList names() const;
    bool contains(const QString &name) const;

    /// 从算子抓取一份模板并保存（同名覆盖）。失败时 error 说明原因。
    bool saveFromNode(const QString &name, NodeBase *node, QString *error = nullptr);
    bool remove(const QString &name);

    /// 保存下来的算子完整 JSON（已去 moduleId/position，含 params），直接喂给 fromJson；
    /// 模板不存在返回空对象
    QJsonObject nodeJson(const QString &name) const;
    /// 注册表类型 ID（可能为空 → 插入方回退到按类型枚举建节点）
    QString typeIdOf(const QString &name) const;
    /// 原算子的类型枚举值（回退建节点用）
    int typeValueOf(const QString &name) const;
    /// 原算子的名称（回退建节点用，也是插入后的默认名）
    QString nodeNameOf(const QString &name) const;

    /// 存储文件路径（用户 AppData 下的 JSON）
    static QString storageFilePath();
    /// 仅测试用：把存储文件重定向到临时路径。**必须**在测试里调用，
    /// 否则测试会写进真实用户数据目录（这个坑之前 QSettings 上踩过一次）。
    static void setStorageFilePathOverride(const QString &path);

private:
    NodeTemplateStore() = default;

    void load();
    bool save() const;
    /// 取一条记录；不存在返回空对象
    QJsonObject record(const QString &name) const;

    /// name -> { typeId, type, nodeName, node }。
    /// 不用"载入一次就缓存"：文件很小，每次读取都重新载入，这样连用户手工编辑过的 JSON 也能生效。
    QJsonObject m_templates;
};
