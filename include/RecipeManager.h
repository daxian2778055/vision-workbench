#pragma once

#include <QObject>
#include <QString>
#include <QMap>
#include <QJsonObject>
#include <QJsonArray>

class FlowScene;

/// Represents a single recipe (product configuration)
struct Recipe {
    QString name;
    QString description;
    QMap<QString, QVariant> parameters; // nodeId -> parameter values
    QDateTime createdAt;
    QDateTime modifiedAt;
};

/// Manages recipes (product configurations) for the vision platform
class RecipeManager : public QObject
{
    Q_OBJECT

public:
    static RecipeManager *instance();

    /// Save the current flow scene as a recipe
    bool saveRecipe(const QString &name, const QString &description, FlowScene *scene);
    
    /// Load a recipe into the given flow scene
    bool loadRecipe(const QString &name, FlowScene *scene);
    
    /// Delete a recipe
    bool deleteRecipe(const QString &name);
    
    /// Rename a recipe
    bool renameRecipe(const QString &oldName, const QString &newName);
    
    /// Get all recipe names
    QStringList recipeNames() const;
    
    /// Get a recipe by name
    Recipe recipe(const QString &name) const;
    
    /// Export recipe to JSON file
    bool exportRecipe(const QString &name, const QString &filePath);
    
    /// Import recipe from JSON file
    bool importRecipe(const QString &filePath);

signals:
    void recipeListChanged();

private:
    RecipeManager(QObject *parent = nullptr);
    ~RecipeManager();
    RecipeManager(const RecipeManager &) = delete;
    RecipeManager &operator=(const RecipeManager &) = delete;

    void loadFromStorage();
    void saveToStorage();
    QString storagePath() const;

    QMap<QString, Recipe> m_recipes;
};
