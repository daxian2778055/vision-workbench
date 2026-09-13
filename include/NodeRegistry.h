#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <functional>
#include "NodeBase.h"

class QObject;
class NodeBase;

/// 算子注册描述（声明式：一行注册一个新算子）
struct NodeRegistration {
    QString id;                    /// 稳定 ID（调色板 UserRole，如 DilateNode）
    QString displayName;           /// 中文显示名（工具库/拖放）
    QString englishName;           /// 英文名
    NodeBase::NodeType category;   /// 主分类（拖放 mime 用）
    QString group;                 /// 工具库分组（中文，如「图像处理」）
    std::function<NodeBase *(QObject *)> factory; /// 创建函数
    QString description;           /// 描述
    QString iconPath;              /// 图标路径（资源文件或绝对路径）
    QStringList aliases;           /// 旧版 .vfp / 拖放别名（如「OpenCV二值化」）
};

/// 算子声明式注册表 — 新算子只需在 registerAllNodes() 中登记一行
class NodeRegistry
{
public:
    static NodeRegistry &instance();

    void registerNode(const NodeRegistration &reg);
    void addAliases(const QString &id, const QStringList &aliases);
    void setDescription(const QString &id, const QString &description);

    /// 按调色板 ID 创建（若 name 为空自动用 displayName 命名）
    NodeBase *createById(const QString &id, QObject *parent) const;
    /// 按名称创建（中文或英文均可）
    NodeBase *createByName(const QString &name, QObject *parent) const;

    const NodeRegistration *findById(const QString &id) const;
    const NodeRegistration *findByName(const QString &name) const;

    const QList<NodeRegistration> &all() const { return m_regs; }
    /// 有序工具库分组
    QStringList groups() const;
    QList<const NodeRegistration *> byGroup(const QString &group) const;

private:
    NodeRegistry();
    QList<NodeRegistration> m_regs;
};

/// 集中注册全部算子（含未来新算子），在 NodeFactory 首次使用时调用
void registerAllNodes();
