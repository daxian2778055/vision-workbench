#include "RecipeManager.h"
#include "FlowScene.h"
#include "NodeBase.h"
#include "AppLog.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDateTime>

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
    QFile file(path);
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
    file.write(QJsonDocument(root).toJson());
    file.close();
}

QStringList RecipeManager::recipeNames() const
{
    return m_recipes.keys();
}

Recipe RecipeManager::recipe(const QString &name) const
{
    return m_recipes.value(name);
}

bool RecipeManager::saveRecipe(const QString &name, const QString &description, FlowScene *scene)
{
    if (name.isEmpty() || !scene) return false;

    Recipe r;
    r.name = name;
    r.description = description;
    r.createdAt = m_recipes.contains(name) ? m_recipes[name].createdAt : QDateTime::currentDateTime();
    r.modifiedAt = QDateTime::currentDateTime();

    // Collect parameters from all nodes in the scene
    for (NodeBase *node : scene->nodes()) {
        if (!node) continue;
        QString nodeId = QString::number(node->moduleId());
        QJsonObject nodeJson = node->toJson();
        r.parameters[nodeId + QStringLiteral("_type")] = node->fullName();
        r.parameters[nodeId] = QJsonDocument(nodeJson).toJson(QJsonDocument::Compact);
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
    // 按 moduleId 匹配场景节点并恢复其保存的参数（fromJson 覆盖参数与几何）
    int applied = 0;
    for (NodeBase *node : scene->nodes()) {
        QString nodeId = QString::number(node->moduleId());
        if (!r.parameters.contains(nodeId)) continue;
        const QByteArray jsonBytes = r.parameters[nodeId].toString().toUtf8();
        QJsonDocument doc = QJsonDocument::fromJson(jsonBytes);
        if (!doc.isObject()) continue;
        node->fromJson(doc.object());
        ++applied;
    }
    VFP_DEBUG << "Applied recipe" << name << "to" << applied << "nodes";
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

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(obj).toJson());
    file.close();
    return true;
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
