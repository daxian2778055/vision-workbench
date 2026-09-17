#include "NodeTemplateStore.h"

#include "NodeBase.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>

namespace {

constexpr char kFileName[] = "node_templates.json";

/// 仅测试用：非空时覆盖存储路径
QString &storageOverride()
{
    static QString path;
    return path;
}

/// 模板记录里的字段名（集中定义，读写两侧不会写错）
constexpr char kFieldTemplates[] = "templates";
constexpr char kFieldTypeId[] = "typeId";
constexpr char kFieldType[] = "type";
constexpr char kFieldNodeName[] = "nodeName";
constexpr char kFieldNode[] = "node";

} // namespace

NodeTemplateStore &NodeTemplateStore::instance()
{
    static NodeTemplateStore store;
    return store;
}

QString NodeTemplateStore::storageFilePath()
{
    if (!storageOverride().isEmpty()) {
        return storageOverride();
    }
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }
    return dir + QLatin1Char('/') + QLatin1String(kFileName);
}

void NodeTemplateStore::setStorageFilePathOverride(const QString &path)
{
    storageOverride() = path;
}

void NodeTemplateStore::load()
{
    // 每次读取都重新载入：文件很小，且这样连"用户手工编辑过 JSON"也能生效。
    m_templates = QJsonObject();

    QFile file(storageFilePath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return;   // 首次使用、没有文件属于正常状态
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isObject()) {
        m_templates = doc.object().value(QLatin1String(kFieldTemplates)).toObject();
    }
}

bool NodeTemplateStore::save() const
{
    QJsonObject root;
    root.insert(QLatin1String(kFieldTemplates), m_templates);

    const QString path = storageFilePath();
    QFile file(path);
    // 目标目录可能还不存在（首次使用）——自己建，避免"保存失败但看不出原因"
    const QString dir = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(dir)) {
        return false;
    }
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

QStringList NodeTemplateStore::names() const
{
    const_cast<NodeTemplateStore *>(this)->load();
    QStringList list = m_templates.keys();
    list.sort(Qt::CaseInsensitive);
    return list;
}

bool NodeTemplateStore::contains(const QString &name) const
{
    const_cast<NodeTemplateStore *>(this)->load();
    return m_templates.contains(name);
}

QJsonObject NodeTemplateStore::record(const QString &name) const
{
    const_cast<NodeTemplateStore *>(this)->load();
    return m_templates.value(name).toObject();
}

bool NodeTemplateStore::saveFromNode(const QString &name, NodeBase *node, QString *error)
{
    const QString key = name.trimmed();
    if (key.isEmpty()) {
        if (error) *error = QStringLiteral("模板名不能为空");
        return false;
    }
    if (!node) {
        if (error) *error = QStringLiteral("要保存的算子为空");
        return false;
    }

    // 先读盘再改：避免用陈旧的内存副本覆盖掉别的会话刚保存的模板
    load();

    // 与 FlowScene::duplicateNode 同一套取舍：
    //   moduleId —— 副本/模板实例保持自己的模块 ID，不继承来源；
    //   position —— 位置由插入时决定。
    QJsonObject nodeJson = node->toJson();
    nodeJson.remove(QStringLiteral("moduleId"));
    nodeJson.remove(QStringLiteral("position"));

    QJsonObject rec;
    rec.insert(QLatin1String(kFieldTypeId), node->property("vfpNodeTypeId").toString());
    rec.insert(QLatin1String(kFieldType), static_cast<int>(node->type()));
    rec.insert(QLatin1String(kFieldNodeName), node->name());
    rec.insert(QLatin1String(kFieldNode), nodeJson);
    m_templates.insert(key, rec);

    if (!save()) {
        if (error) {
            *error = QStringLiteral("写入失败：%1（请检查目录权限）").arg(storageFilePath());
        }
        return false;
    }
    return true;
}

bool NodeTemplateStore::remove(const QString &name)
{
    load();
    // 注意：QJsonObject::remove 返回 void（不像 QHash 返回删除个数），
    // 所以"是否存在"必须自己先判断。
    if (!m_templates.contains(name)) {
        return false;
    }
    m_templates.remove(name);
    return save();
}

QJsonObject NodeTemplateStore::nodeJson(const QString &name) const
{
    return record(name).value(QLatin1String(kFieldNode)).toObject();
}

QString NodeTemplateStore::typeIdOf(const QString &name) const
{
    return record(name).value(QLatin1String(kFieldTypeId)).toString();
}

int NodeTemplateStore::typeValueOf(const QString &name) const
{
    return record(name).value(QLatin1String(kFieldType)).toInt();
}

QString NodeTemplateStore::nodeNameOf(const QString &name) const
{
    return record(name).value(QLatin1String(kFieldNodeName)).toString();
}
