#include "RecipeManager.h"
#include "FlowScene.h"
#include "NodeBase.h"
#include "AppLog.h"
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDateTime>
#include <algorithm>

RecipeManager *RecipeManager::instance()
{
    static RecipeManager s_instance;
    return &s_instance;
}

RecipeManager::RecipeManager(QObject *parent)
    : QObject(parent)
{
    loadFromStorage();
}

RecipeManager::~RecipeManager()
{
    saveToStorage();
}

namespace {

/// 仅测试用：非空时覆盖存储路径（见头文件说明）。
/// storagePath() 在每次读/写时现算，所以"写之前设置"就能生效。
QString &storageOverride()
{
    static QString path;
    return path;
}

} // namespace

void RecipeManager::setStoragePathOverride(const QString &path)
{
    storageOverride() = path;
}

QString RecipeManager::storagePath() const
{
    if (!storageOverride().isEmpty()) {
        return storageOverride();
    }
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(dataDir);
    if (!dir.exists()) dir.mkpath(QStringLiteral("."));
    return dir.filePath(QStringLiteral("recipes.json"));
}

void RecipeManager::loadFromStorage()
{
    QString path = storagePath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        VFP_DEBUG << "No recipe storage found at:" << path;
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) return;

    QJsonObject root = doc.object();
    QJsonArray recipes = root[QStringLiteral("recipes")].toArray();

    for (const QJsonValue &val : recipes) {
        QJsonObject obj = val.toObject();
        Recipe r;
        r.name = obj[QStringLiteral("name")].toString();
        r.description = obj[QStringLiteral("description")].toString();
        r.createdAt = QDateTime::fromString(obj[QStringLiteral("createdAt")].toString(), Qt::ISODate);
        r.modifiedAt = QDateTime::fromString(obj[QStringLiteral("modifiedAt")].toString(), Qt::ISODate);

        QJsonObject params = obj[QStringLiteral("parameters")].toObject();
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            r.parameters[it.key()] = it.value().toVariant();
        }

        m_recipes[r.name] = r;
    }

    VFP_DEBUG << "Loaded" << m_recipes.size() << "recipes from storage";
}

void RecipeManager::saveToStorage()
{
    QString path = storagePath();
    // 原子写（QSaveFile）：配方库是共享单文件，写坏即全部配方丢失
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        VFP_DEBUG << "Failed to write recipe storage:" << path;
        return;
    }

    QJsonObject root;
    QJsonArray recipes;

    for (auto it = m_recipes.constBegin(); it != m_recipes.constEnd(); ++it) {
        const Recipe &r = it.value();
        QJsonObject obj;
        obj[QStringLiteral("name")] = r.name;
        obj[QStringLiteral("description")] = r.description;
        obj[QStringLiteral("createdAt")] = r.createdAt.toString(Qt::ISODate);
        obj[QStringLiteral("modifiedAt")] = r.modifiedAt.toString(Qt::ISODate);

        QJsonObject params;
        for (auto pit = r.parameters.constBegin(); pit != r.parameters.constEnd(); ++pit) {
            params[pit.key()] = QJsonValue::fromVariant(pit.value());
        }
        obj[QStringLiteral("parameters")] = params;

        recipes.append(obj);
    }

    root[QStringLiteral("recipes")] = recipes;
    const QByteArray payload = QJsonDocument(root).toJson();
    if (file.write(payload) != payload.size() || !file.commit()) {
        file.cancelWriting();
        VFP_DEBUG << "Failed to write recipe storage (incomplete):" << path;
    }
}

QStringList RecipeManager::recipeNames() const
{
    return m_recipes.keys();
}

Recipe RecipeManager::recipe(const QString &name) const
{
    return m_recipes.value(name);
}

namespace {

/// 配方里的算子键：用**名称**而不是 moduleId。
/// moduleId 会随工程/流程重建而变，换个工程就一个都匹配不上（这就是"加载失败"的根因）；
/// 名称是用户在界面上看到、也随时可改的东西，跨工程仍然有意义。
/// 同名算子（例如两个「卡尺测量」）按屏幕顺序编号：第 1 个用原名，之后是 name#2、name#3…
QString nodeKey(const QString &name, int sameNameIndex)
{
    return sameNameIndex <= 0 ? name
                              : QStringLiteral("%1#%2").arg(name).arg(sameNameIndex + 1);
}

/// 屏幕顺序（上→下、左→右）：同名算子的编号必须确定且可复现，
/// 否则同一份配方每次加载可能落到不同的算子上。
void sortByScreenOrder(QList<NodeBase *> &nodes)
{
    std::sort(nodes.begin(), nodes.end(), [](NodeBase *a, NodeBase *b) {
        if (!a || !b) return a != nullptr;   // 空指针排到最后
        const QPointF pa = a->position();
        const QPointF pb = b->position();
        if (qAbs(pa.y() - pb.y()) > 0.5) return pa.y() < pb.y();
        return pa.x() < pb.x();
    });
}

} // namespace

bool RecipeManager::saveRecipe(const QString &name, const QString &description, FlowScene *scene)
{
    if (name.isEmpty() || !scene) return false;

    Recipe r;
    r.name = name;
    r.description = description;
    r.createdAt = m_recipes.contains(name) ? m_recipes[name].createdAt : QDateTime::currentDateTime();
    r.modifiedAt = QDateTime::currentDateTime();
    r.parameters.clear();   // 同名覆盖：清掉上一次的键，避免残留

    QList<NodeBase *> sceneNodes = scene->nodes();
    sortByScreenOrder(sceneNodes);

    QHash<QString, int> sameNameCount;
    for (NodeBase *node : sceneNodes) {
        if (!node) continue;

        const QByteArray blob = QJsonDocument(node->toJson()).toJson(QJsonDocument::Compact);
        const QString typeId = node->property("vfpNodeTypeId").toString();

        // 主键：名称（同名加 #n）。无名算子只写兼容键，不参与名称匹配。
        if (!node->name().isEmpty()) {
            const int idx = sameNameCount.value(node->name(), 0);
            sameNameCount[node->name()] = idx + 1;

            const QString key = nodeKey(node->name(), idx);
            r.parameters[key] = blob;
            r.parameters[key + QStringLiteral("_type")] = node->fullName();
            r.parameters[key + QStringLiteral("_typeid")] = typeId;
        }

        // 兼容键：保留 moduleId 索引，旧版本仍能读这份配方
        const QString legacy = QString::number(node->moduleId());
        r.parameters[legacy] = blob;
        r.parameters[legacy + QStringLiteral("_type")] = node->fullName();
    }

    m_recipes[name] = r;
    saveToStorage();
    emit recipeListChanged();
    return true;
}

bool RecipeManager::loadRecipe(const QString &name, FlowScene *scene)
{
    if (!m_recipes.contains(name) || !scene) return false;

    const Recipe &r = m_recipes[name];

    // 匹配顺序：① 算子名（同名按屏幕顺序编号）→ ② 退回 moduleId（读旧版本存的配方）
    QList<NodeBase *> sceneNodes = scene->nodes();
    sortByScreenOrder(sceneNodes);

    QHash<QString, int> sameNameCount;
    int applied = 0;
    int skippedByType = 0;
    for (NodeBase *node : sceneNodes) {
        if (!node) continue;

        // 类型校验只看**名称匹配**的结果（也就是"同名的配方项"），不受 moduleId 兼容键影响：
        // 因为所有节点的 moduleId 都不同（静态自增），moduleId 兼容键本意是"读旧版本配方"，
        // 若它先命中就会落到别处，使同名的类型校验永远没机会触发——这会让同名不同实现的
        // 危险写回被放行。所以这里只在"名称命中且类型不符"时跳过。
        QString nameKey;
        if (!node->name().isEmpty()) {
            const int idx = sameNameCount.value(node->name(), 0);
            sameNameCount[node->name()] = idx + 1;
            nameKey = nodeKey(node->name(), idx);
        }

        QString key;
        if (!nameKey.isEmpty() && r.parameters.contains(nameKey)) {
            // 优先名称匹配；命中时先过类型校验，不通过就跳过（不再回退到 moduleId 兼容键）
            const QString wantId = r.parameters.value(nameKey + QStringLiteral("_typeid")).toString();
            const QString gotId = node->property("vfpNodeTypeId").toString();
            if (!wantId.isEmpty() && !gotId.isEmpty() && wantId != gotId) {
                ++skippedByType;
                VFP_DEBUG << "配方跳过类型不匹配的算子:" << node->name()
                          << "配方类型:" << wantId << "当前类型:" << gotId;
                continue;
            }
            key = nameKey;
        }
        if (key.isEmpty()) {
            const QString legacy = QString::number(node->moduleId());
            if (r.parameters.contains(legacy)) {
                key = legacy;   // 旧配方（按 moduleId 存的）：名称没匹配上才退回这里
            }
        }
        if (key.isEmpty()) continue;

        const QByteArray jsonBytes = r.parameters[key].toString().toUtf8();
        QJsonDocument doc = QJsonDocument::fromJson(jsonBytes);
        if (!doc.isObject()) continue;
        node->fromJson(doc.object());
        ++applied;
    }
    VFP_DEBUG << "Applied recipe" << name << "to" << applied << "nodes (type mismatch skipped:"
              << skippedByType << ")";
    // 一个算子都没匹配上就是加载失败：以前这里无条件 return true，
    // 于是"提示加载成功"和"实际什么都没发生"可以同时出现。
    // 另外不再发 recipeListChanged——加载并不改动配方列表，发了只会误导监听者。
    return applied > 0;
}

bool RecipeManager::deleteRecipe(const QString &name)
{
    if (!m_recipes.contains(name)) return false;
    m_recipes.remove(name);
    saveToStorage();
    emit recipeListChanged();
    return true;
}

bool RecipeManager::renameRecipe(const QString &oldName, const QString &newName)
{
    if (!m_recipes.contains(oldName) || newName.isEmpty()) return false;
    if (oldName == newName) return true;

    Recipe r = m_recipes.take(oldName);
    r.name = newName;
    m_recipes[newName] = r;
    saveToStorage();
    emit recipeListChanged();
    return true;
}

bool RecipeManager::exportRecipe(const QString &name, const QString &filePath)
{
    if (!m_recipes.contains(name)) return false;

    const Recipe &r = m_recipes[name];
    QJsonObject obj;
    obj[QStringLiteral("name")] = r.name;
    obj[QStringLiteral("description")] = r.description;
    obj[QStringLiteral("createdAt")] = r.createdAt.toString(Qt::ISODate);
    obj[QStringLiteral("modifiedAt")] = r.modifiedAt.toString(Qt::ISODate);

    QJsonObject params;
    for (auto it = r.parameters.constBegin(); it != r.parameters.constEnd(); ++it) {
        params[it.key()] = QJsonValue::fromVariant(it.value());
    }
    obj[QStringLiteral("parameters")] = params;

    QSaveFile file(filePath);   // 原子写：导出文件同样避免半份
    if (!file.open(QIODevice::WriteOnly)) return false;
    const QByteArray payload = QJsonDocument(obj).toJson();
    if (file.write(payload) != payload.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

bool RecipeManager::importRecipe(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) return false;

    QJsonObject obj = doc.object();
    QString name = obj[QStringLiteral("name")].toString();
    if (name.isEmpty()) return false;

    Recipe r;
    r.name = name;
    r.description = obj[QStringLiteral("description")].toString();
    r.createdAt = QDateTime::fromString(obj[QStringLiteral("createdAt")].toString(), Qt::ISODate);
    r.modifiedAt = QDateTime::fromString(obj[QStringLiteral("modifiedAt")].toString(), Qt::ISODate);

    QJsonObject params = obj[QStringLiteral("parameters")].toObject();
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        r.parameters[it.key()] = it.value().toVariant();
    }

    m_recipes[name] = r;
    saveToStorage();
    emit recipeListChanged();
    return true;
}
